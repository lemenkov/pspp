/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2013, 2016 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include "language/lexer/lexer.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unictype.h>
#include <unistd.h>
#include <unistr.h>

#include "language/command.h"
#include "language/lexer/macro.h"
#include "language/lexer/scan.h"
#include "language/lexer/segment.h"
#include "language/lexer/token.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/deque.h"
#include "libpspp/float-range.h"
#include "libpspp/i18n.h"
#include "libpspp/intern.h"
#include "libpspp/ll.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"
#include "libpspp/u8-istream.h"
#include "output/journal.h"
#include "output/output-item.h"

#include "gl/c-ctype.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* A token within a lex_source. */
struct lex_token
  {
    /* The regular token information. */
    struct token token;

    /* For a token obtained through the lexer in an ordinary way, this is the
       location of the token in terms of the lex_source's buffer.

       For a token produced through macro expansion, this is the entire macro
       call. */
    size_t token_pos;           /* Offset into src->buffer of token start. */
    size_t token_len;           /* Length of source for token in bytes. */

    /* For a token obtained through macro expansion, this is just this token.

       For a token obtained through the lexer in an ordinary way, these are
       nulls and zeros. */
    char *macro_rep;        /* The whole macro expansion. */
    size_t ofs;             /* Offset of this token in macro_rep. */
    size_t len;             /* Length of this token in macro_rep. */
    size_t *ref_cnt;        /* Number of lex_tokens that refer to macro_rep. */
  };

static struct msg_point lex_token_start_point (const struct lex_source *,
                                               const struct lex_token *);
static struct msg_point lex_token_end_point (const struct lex_source *,
                                             const struct lex_token *);

static bool lex_ofs_at_phrase__ (struct lexer *, int ofs, const char *s,
                                 size_t *n_matchedp);

/* Source offset of the last byte in TOKEN. */
static size_t
lex_token_end (const struct lex_token *token)
{
  return token->token_pos + MAX (token->token_len, 1) - 1;
}

static void
lex_token_destroy (struct lex_token *t)
{
  token_uninit (&t->token);
  if (t->ref_cnt)
    {
      assert (*t->ref_cnt > 0);
      if (!--*t->ref_cnt)
        {
          free (t->macro_rep);
          free (t->ref_cnt);
        }
    }
  free (t);
}

/* A deque of lex_tokens that comprises one stage in the token pipeline in a
   lex_source. */
struct lex_stage
  {
    struct deque deque;
    struct lex_token **tokens;
  };

static void lex_stage_clear (struct lex_stage *);
static void lex_stage_uninit (struct lex_stage *);

static size_t lex_stage_count (const struct lex_stage *);
static bool lex_stage_is_empty (const struct lex_stage *);

static struct lex_token *lex_stage_first (struct lex_stage *);
static struct lex_token *lex_stage_nth (struct lex_stage *, size_t ofs);

static void lex_stage_push_last (struct lex_stage *, struct lex_token *);
static void lex_stage_pop_first (struct lex_stage *);

static void lex_stage_shift (struct lex_stage *dst, struct lex_stage *src,
                             size_t n);

/* Deletes all the tokens from STAGE. */
static void
lex_stage_clear (struct lex_stage *stage)
{
  while (!deque_is_empty (&stage->deque))
    lex_stage_pop_first (stage);
}

/* Deletes all the tokens from STAGE and frees storage for the deque. */
static void
lex_stage_uninit (struct lex_stage *stage)
{
  lex_stage_clear (stage);
  free (stage->tokens);
}

/* Returns true if STAGE contains no tokens, otherwise false. */
static bool
lex_stage_is_empty (const struct lex_stage *stage)
{
  return deque_is_empty (&stage->deque);
}

/* Returns the number of tokens in STAGE. */
static size_t
lex_stage_count (const struct lex_stage *stage)
{
  return deque_count (&stage->deque);
}

/* Returns the first token in STAGE, which must be nonempty.
   The first token is the one accessed with the least lookahead. */
static struct lex_token *
lex_stage_first (struct lex_stage *stage)
{
  return lex_stage_nth (stage, 0);
}

/* Returns the token the given INDEX in STAGE.  The first token (with the least
   lookahead) is 0, the second token is 1, and so on.  There must be at least
   INDEX + 1 tokens in STAGE. */
static struct lex_token *
lex_stage_nth (struct lex_stage *stage, size_t index)
{
  return stage->tokens[deque_back (&stage->deque, index)];
}

/* Adds TOKEN so that it becomes the last token in STAGE. */
static void
lex_stage_push_last (struct lex_stage *stage, struct lex_token *token)
{
  if (deque_is_full (&stage->deque))
    stage->tokens = deque_expand (&stage->deque, stage->tokens,
                                  sizeof *stage->tokens);
  stage->tokens[deque_push_front (&stage->deque)] = token;
}

/* Removes and returns the first token from STAGE. */
static struct lex_token *
lex_stage_take_first (struct lex_stage *stage)
{
  return stage->tokens[deque_pop_back (&stage->deque)];
}

/* Removes the first token from STAGE and uninitializes it. */
static void
lex_stage_pop_first (struct lex_stage *stage)
{
  lex_token_destroy (lex_stage_take_first (stage));
}

/* Removes the first N tokens from SRC, appending them to DST as the last
   tokens. */
static void
lex_stage_shift (struct lex_stage *dst, struct lex_stage *src, size_t n)
{
  for (size_t i = 0; i < n; i++)
    lex_stage_push_last (dst, lex_stage_take_first (src));
}

/* A source of tokens, corresponding to a syntax file.

   This is conceptually a lex_reader wrapped with everything needed to convert
   its UTF-8 bytes into tokens. */
struct lex_source
  {
    struct ll ll;               /* In lexer's list of sources. */

    /* Reference count:

       - One for struct lexer.

       - One for each struct msg_location that references this source. */
    size_t n_refs;

    struct lex_reader *reader;
    struct lexer *lexer;
    struct segmenter segmenter;
    bool eof;                   /* True if T_STOP was read from 'reader'. */

    /* Buffer of UTF-8 bytes. */
    char *buffer;               /* Source file contents. */
    size_t length;              /* Number of bytes filled. */
    size_t allocated;           /* Number of bytes allocated. */

    /* Offsets into 'buffer'. */
    size_t journal_pos;         /* First byte not yet output to journal. */
    size_t seg_pos;             /* First byte not yet scanned as token. */

    /* Offset into 'buffer' of starts of lines. */
    size_t *lines;
    size_t n_lines, allocated_lines;

    bool suppress_next_newline;

    /* Tokens.

       This is a pipeline with the following stages.  Each token eventually
       made available to the parser passes through of these stages.  The stages
       are named after the processing that happens in each one.

       Initially, tokens come from the segmenter and scanner to 'pp':

       - pp: Tokens that need to pass through the macro preprocessor to end up
         in 'merge'.

       - merge: Tokens that need to pass through scan_merge() to end up in
         'parse'.

       - parse: Tokens available to the client for parsing.

      'pp' and 'merge' store tokens only temporarily until they pass into
      'parse'.  Tokens then live in 'parse' until the command is fully
      consumed, at which time they are freed together. */
    struct lex_stage pp;
    struct lex_stage merge;
    struct lex_token **parse;
    size_t n_parse, allocated_parse, parse_ofs;
  };

static struct lex_source *lex_source_create (struct lexer *,
                                             struct lex_reader *);

/* Lexer. */
struct lexer
  {
    struct ll_list sources;     /* Contains "struct lex_source"s. */
    struct macro_set *macros;

    /* Temporarily stores errors and warnings to be emitted by the lexer while
       lexing is going on, to avoid reentrancy. */
    struct msg **messages;
    size_t n_messages, allocated_messages;
  };

static struct lex_source *lex_source__ (const struct lexer *);
static char *lex_source_syntax__ (const struct lex_source *,
                                  int ofs0, int ofs1);
static const struct lex_token *lex_next__ (const struct lexer *, int n);
static void lex_source_push_endcmd__ (struct lex_source *);
static void lex_source_push_parse (struct lex_source *, struct lex_token *);
static void lex_source_clear_parse (struct lex_source *);

static bool lex_source_get_parse (struct lex_source *);
static void lex_source_msg_valist (struct lex_source *, enum msg_class,
                                   int ofs0, int ofs1,
                                   const char *format, va_list)
   PRINTF_FORMAT (5, 0);
static const struct lex_token *lex_source_next__ (const struct lex_source *,
                                                  int n);

/* Initializes READER with the specified CLASS and otherwise some reasonable
   defaults.  The caller should fill in the others members as desired. */
void
lex_reader_init (struct lex_reader *reader,
                 const struct lex_reader_class *class)
{
  reader->class = class;
  reader->syntax = SEG_MODE_AUTO;
  reader->error = LEX_ERROR_CONTINUE;
  reader->file_name = NULL;
  reader->encoding = NULL;
  reader->line_number = 0;
  reader->eof = false;
}

/* Frees any file name already in READER and replaces it by a copy of
   FILE_NAME, or if FILE_NAME is null then clears any existing name. */
void
lex_reader_set_file_name (struct lex_reader *reader, const char *file_name)
{
  free (reader->file_name);
  reader->file_name = xstrdup_if_nonnull (file_name);
}

/* Creates and returns a new lexer. */
struct lexer *
lex_create (void)
{
  struct lexer *lexer = xmalloc (sizeof *lexer);
  *lexer = (struct lexer) {
    .sources = LL_INITIALIZER (lexer->sources),
    .macros = macro_set_create (),
  };
  return lexer;
}

/* Destroys LEXER. */
void
lex_destroy (struct lexer *lexer)
{
  if (lexer != NULL)
    {
      struct lex_source *source, *next;

      assert (!lexer->messages);

      ll_for_each_safe (source, next, struct lex_source, ll, &lexer->sources)
        {
          ll_remove (&source->ll);
          lex_source_unref (source);
        }
      macro_set_destroy (lexer->macros);
      free (lexer);
    }
}

/* Adds M to LEXER's set of macros.  M replaces any existing macro with the
   same name.  Takes ownership of M. */
void
lex_define_macro (struct lexer *lexer, struct macro *m)
{
  macro_set_add (lexer->macros, m);
}

/* Returns LEXER's macro set.  The caller should not modify it. */
const struct macro_set *
lex_get_macros (const struct lexer *lexer)
{
  return lexer->macros;
}

/* Inserts READER into LEXER so that the next token read by LEXER comes from
   READER.  Before the caller, LEXER must either be empty or at a T_ENDCMD
   token. */
void
lex_include (struct lexer *lexer, struct lex_reader *reader)
{
  assert (ll_is_empty (&lexer->sources) || lex_token (lexer) == T_ENDCMD);
  ll_push_head (&lexer->sources, &lex_source_create (lexer, reader)->ll);
}

/* Appends READER to LEXER, so that it will be read after all other current
   readers have already been read. */
void
lex_append (struct lexer *lexer, struct lex_reader *reader)
{
  ll_push_tail (&lexer->sources, &lex_source_create (lexer, reader)->ll);
}

/* Advancing. */

/* Advances LEXER to the next token, consuming the current token. */
void
lex_get (struct lexer *lexer)
{
  struct lex_source *src;

  src = lex_source__ (lexer);
  if (src == NULL)
    return;

  if (src->parse_ofs < src->n_parse)
    {
      if (src->parse[src->parse_ofs]->token.type == T_ENDCMD)
        lex_source_clear_parse (src);
      else
        src->parse_ofs++;
    }

  while (src->parse_ofs == src->n_parse)
    if (!lex_source_get_parse (src))
      {
        ll_remove (&src->ll);
        lex_source_unref (src);
        src = lex_source__ (lexer);
        if (src == NULL)
          return;
      }
}

/* Advances LEXER by N tokens. */
void
lex_get_n (struct lexer *lexer, size_t n)
{
  while (n-- > 0)
    lex_get (lexer);
}

/* Issuing errors. */

/* Prints a syntax error message containing the current token and
   given message MESSAGE (if non-null). */
void
lex_error (struct lexer *lexer, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  lex_ofs_msg_valist (lexer, SE, lex_ofs (lexer), lex_ofs (lexer),
                      format, args);
  va_end (args);
}

/* Prints a syntax error message for the span of tokens N0 through N1,
   inclusive, from the current token in LEXER, adding message MESSAGE (if
   non-null). */
void
lex_next_error (struct lexer *lexer, int n0, int n1, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  int ofs = lex_ofs (lexer);
  lex_ofs_msg_valist (lexer, SE, n0 + ofs, n1 + ofs, format, args);
  va_end (args);
}

/* Prints a syntax error message for the span of tokens with offsets OFS0
   through OFS1, inclusive, within the current command in LEXER, adding message
   MESSAGE (if non-null). */
void
lex_ofs_error (struct lexer *lexer, int ofs0, int ofs1, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  lex_ofs_msg_valist (lexer, SE, ofs0, ofs1, format, args);
  va_end (args);
}

/* Prints a message of the given CLASS containing the current token and given
   message MESSAGE (if non-null). */
void
lex_msg (struct lexer *lexer, enum msg_class class, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  lex_ofs_msg_valist (lexer, class, lex_ofs (lexer), lex_ofs (lexer),
                      format, args);
  va_end (args);
}

/* Prints a syntax error message for the span of tokens N0 through N1,
   inclusive, from the current token in LEXER, adding message MESSAGE (if
   non-null). */
void
lex_next_msg (struct lexer *lexer, enum msg_class class, int n0, int n1,
              const char *format, ...)
{
  va_list args;

  va_start (args, format);
  int ofs = lex_ofs (lexer);
  lex_ofs_msg_valist (lexer, class, n0 + ofs, n1 + ofs, format, args);
  va_end (args);
}

/* Prints a message of the given CLASS for the span of tokens with offsets OFS0
   through OFS1, inclusive, within the current command in LEXER, adding message
   MESSAGE (if non-null). */
void
lex_ofs_msg (struct lexer *lexer, enum msg_class class, int ofs0, int ofs1,
             const char *format, ...)
{
  va_list args;

  va_start (args, format);
  lex_ofs_msg_valist (lexer, class, ofs0, ofs1, format, args);
  va_end (args);
}

/* Prints a syntax error message saying that one of the strings provided as
   varargs, up to the first NULL, is expected. */
void
(lex_error_expecting) (struct lexer *lexer, ...)
{
  va_list args;

  va_start (args, lexer);
  lex_error_expecting_valist (lexer, args);
  va_end (args);
}

/* Prints a syntax error message saying that one of the options provided in
   ARGS, up to the first NULL, is expected. */
void
lex_error_expecting_valist (struct lexer *lexer, va_list args)
{
  const char **options = NULL;
  size_t allocated = 0;
  size_t n = 0;

  for (;;)
    {
      const char *option = va_arg (args, const char *);
      if (!option)
        break;

      if (n >= allocated)
        options = x2nrealloc (options, &allocated, sizeof *options);
      options[n++] = option;
    }
  lex_error_expecting_array (lexer, options, n);
  free (options);
}

void
lex_error_expecting_array (struct lexer *lexer, const char **options, size_t n)
{
  switch (n)
    {
    case 0:
      lex_error (lexer, NULL);
      break;

    case 1:
      lex_error (lexer, _("Syntax error expecting %s."), options[0]);
      break;

    case 2:
      lex_error (lexer, _("Syntax error expecting %s or %s."),
                 options[0], options[1]);
      break;

    case 3:
      lex_error (lexer, _("Syntax error expecting %s, %s, or %s."),
                 options[0], options[1], options[2]);
      break;

    case 4:
      lex_error (lexer, _("Syntax error expecting %s, %s, %s, or %s."),
                 options[0], options[1], options[2], options[3]);
      break;

    case 5:
      lex_error (lexer, _("Syntax error expecting %s, %s, %s, %s, or %s."),
                 options[0], options[1], options[2], options[3], options[4]);
      break;

    case 6:
      lex_error (lexer, _("Syntax error expecting %s, %s, %s, %s, %s, or %s."),
                 options[0], options[1], options[2], options[3], options[4],
                 options[5]);
      break;

    case 7:
      lex_error (lexer, _("Syntax error expecting %s, %s, %s, %s, %s, %s, "
                          "or %s."),
                 options[0], options[1], options[2], options[3], options[4],
                 options[5], options[6]);
      break;

    case 8:
      lex_error (lexer, _("Syntax error expecting %s, %s, %s, %s, %s, %s, %s, "
                          "or %s."),
                 options[0], options[1], options[2], options[3], options[4],
                 options[5], options[6], options[7]);
      break;

    default:
      {
        struct string s = DS_EMPTY_INITIALIZER;
        for (size_t i = 0; i < n; i++)
          {
            if (i > 0)
              ds_put_cstr (&s, ", ");
            ds_put_cstr (&s, options[i]);
          }
        lex_error (lexer, _("Syntax error expecting one of the following: %s."),
                   ds_cstr (&s));
        ds_destroy (&s);
      }
      break;
    }
}

/* Reports an error to the effect that subcommand SBC may only be specified
   once. */
void
lex_sbc_only_once (struct lexer *lexer, const char *sbc)
{
  int ofs = lex_ofs (lexer) - 1;
  if (lex_ofs_token (lexer, ofs)->type == T_EQUALS)
    ofs--;

  /* lex_ofs_at_phrase__() handles subcommand names that are keywords, such as
     BY. */
  if (lex_ofs_at_phrase__ (lexer, ofs, sbc, NULL))
    lex_ofs_error (lexer, ofs, ofs,
                   _("Subcommand %s may only be specified once."), sbc);
  else
    msg (SE, _("Subcommand %s may only be specified once."), sbc);
}

/* Reports an error to the effect that subcommand SBC is missing.

   This function does not take a lexer as an argument or use lex_error(),
   because a missing subcommand can normally be detected only after the whole
   command has been parsed, and so lex_error() would always report "Syntax
   error at end of command", which does not help the user find the error. */
void
lex_sbc_missing (struct lexer *lexer, const char *sbc)
{
  lex_ofs_error (lexer, 0, lex_max_ofs (lexer),
                 _("Required subcommand %s was not specified."), sbc);
}

/* Reports an error to the effect that specification SPEC may only be specified
   once within subcommand SBC. */
void
lex_spec_only_once (struct lexer *lexer, const char *sbc, const char *spec)
{
  lex_error (lexer, _("%s may only be specified once within subcommand %s."),
             spec, sbc);
}

/* Reports an error to the effect that specification SPEC is missing within
   subcommand SBC. */
void
lex_spec_missing (struct lexer *lexer, const char *sbc, const char *spec)
{
  lex_error (lexer, _("Required %s specification missing from %s subcommand."),
             spec, sbc);
}

/* Prints a syntax error message for the span of tokens with offsets OFS0
   through OFS1, inclusive, within the current command in LEXER, adding message
   MESSAGE (if non-null) with the given ARGS. */
void
lex_ofs_msg_valist (struct lexer *lexer, enum msg_class class,
                    int ofs0, int ofs1, const char *format, va_list args)
{
  lex_source_msg_valist (lex_source__ (lexer), class, ofs0, ofs1, format, args);
}

/* Checks that we're at end of command.
   If so, returns a successful command completion code.
   If not, flags a syntax error and returns an error command
   completion code. */
int
lex_end_of_command (struct lexer *lexer)
{
  if (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_STOP)
    {
      lex_error (lexer, _("Syntax error expecting end of command."));
      return CMD_FAILURE;
    }
  else
    return CMD_SUCCESS;
}

/* Token testing functions. */

/* Returns true if the current token is a number. */
bool
lex_is_number (const struct lexer *lexer)
{
  return lex_next_is_number (lexer, 0);
}

/* Returns true if the current token is a string. */
bool
lex_is_string (const struct lexer *lexer)
{
  return lex_next_is_string (lexer, 0);
}

/* Returns the value of the current token, which must be a
   floating point number. */
double
lex_number (const struct lexer *lexer)
{
  return lex_next_number (lexer, 0);
}

/* Returns true iff the current token is an integer. */
bool
lex_is_integer (const struct lexer *lexer)
{
  return lex_next_is_integer (lexer, 0);
}

/* Returns the value of the current token, which must be an
   integer. */
long
lex_integer (const struct lexer *lexer)
{
  return lex_next_integer (lexer, 0);
}

/* Token testing functions with lookahead.

   A value of 0 for N as an argument to any of these functions refers to the
   current token.  Lookahead is limited to the current command.  Any N greater
   than the number of tokens remaining in the current command will be treated
   as referring to a T_ENDCMD token. */

/* Returns true if the token N ahead of the current token is a number. */
bool
lex_next_is_number (const struct lexer *lexer, int n)
{
  return token_is_number (lex_next (lexer, n));
}

/* Returns true if the token N ahead of the current token is a string. */
bool
lex_next_is_string (const struct lexer *lexer, int n)
{
  return token_is_string (lex_next (lexer, n));
}

/* Returns the value of the token N ahead of the current token, which must be a
   floating point number. */
double
lex_next_number (const struct lexer *lexer, int n)
{
  return token_number (lex_next (lexer, n));
}

/* Returns true if the token N ahead of the current token is an integer. */
bool
lex_next_is_integer (const struct lexer *lexer, int n)
{
  return token_is_integer (lex_next (lexer, n));
}

/* Returns the value of the token N ahead of the current token, which must be
   an integer. */
long
lex_next_integer (const struct lexer *lexer, int n)
{
  return token_integer (lex_next (lexer, n));
}

/* Token matching functions. */

/* If the current token has the specified TYPE, skips it and returns true.
   Otherwise, returns false. */
bool
lex_match (struct lexer *lexer, enum token_type type)
{
  if (lex_token (lexer) == type)
    {
      lex_get (lexer);
      return true;
    }
  else
    return false;
}

/* If the current token matches IDENTIFIER, skips it and returns true.
   IDENTIFIER may be abbreviated to its first three letters.  Otherwise,
   returns false.

   IDENTIFIER must be an ASCII string. */
bool
lex_match_id (struct lexer *lexer, const char *identifier)
{
  return lex_match_id_n (lexer, identifier, 3);
}

/* If the current token is IDENTIFIER, skips it and returns true.  IDENTIFIER
   may be abbreviated to its first N letters.  Otherwise, returns false.

   IDENTIFIER must be an ASCII string. */
bool
lex_match_id_n (struct lexer *lexer, const char *identifier, size_t n)
{
  if (lex_token (lexer) == T_ID
      && lex_id_match_n (ss_cstr (identifier), lex_tokss (lexer), n))
    {
      lex_get (lexer);
      return true;
    }
  else
    return false;
}

/* If the current token is integer X, skips it and returns true.  Otherwise,
   returns false. */
bool
lex_match_int (struct lexer *lexer, int x)
{
  if (lex_is_integer (lexer) && lex_integer (lexer) == x)
    {
      lex_get (lexer);
      return true;
    }
  else
    return false;
}

/* Forced matches. */

/* If this token is IDENTIFIER, skips it and returns true.  IDENTIFIER may be
   abbreviated to its first 3 letters.  Otherwise, reports an error and returns
   false.

   IDENTIFIER must be an ASCII string. */
bool
lex_force_match_id (struct lexer *lexer, const char *identifier)
{
  if (lex_match_id (lexer, identifier))
    return true;
  else
    {
      lex_error_expecting (lexer, identifier);
      return false;
    }
}

/* If the current token has the specified TYPE, skips it and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_match (struct lexer *lexer, enum token_type type)
{
  if (lex_token (lexer) == type)
    {
      lex_get (lexer);
      return true;
    }
  else
    {
      const char *type_string = token_type_to_string (type);
      if (type_string)
        {
          char *s = xasprintf ("`%s'", type_string);
          lex_error_expecting (lexer, s);
          free (s);
        }
      else
        lex_error_expecting (lexer, token_type_to_name (type));

      return false;
    }
}

/* If the current token is a string, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_string (struct lexer *lexer)
{
  if (lex_is_string (lexer))
    return true;
  else
    {
      lex_error (lexer, _("Syntax error expecting string."));
      return false;
    }
}

/* If the current token is a string or an identifier, does nothing and returns
   true.  Otherwise, reports an error and returns false.

   This is meant for use in syntactic situations where we want to encourage the
   user to supply a quoted string, but for compatibility we also accept
   identifiers.  (One example of such a situation is file names.)  Therefore,
   the error message issued when the current token is wrong only says that a
   string is expected and doesn't mention that an identifier would also be
   accepted. */
bool
lex_force_string_or_id (struct lexer *lexer)
{
  return lex_token (lexer) == T_ID || lex_force_string (lexer);
}

/* If the current token is an integer, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_int (struct lexer *lexer)
{
  if (lex_is_integer (lexer))
    return true;
  else
    {
      lex_error (lexer, _("Syntax error expecting integer."));
      return false;
    }
}

/* If the current token is an integer in the range MIN...MAX (inclusive), does
   nothing and returns true.  Otherwise, reports an error and returns false.
   If NAME is nonnull, then it is used in the error message. */
bool
lex_force_int_range (struct lexer *lexer, const char *name, long min, long max)
{
  min = MAX (min, DBL_UNIT_LONG_MIN);
  max = MIN (max, DBL_UNIT_LONG_MAX);

  bool is_number = lex_is_number (lexer);
  bool is_integer = lex_is_integer (lexer);
  bool too_small = (is_integer ? lex_integer (lexer) < min
                    : is_number ? lex_number (lexer) < min
                    : false);
  bool too_big = (is_integer ? lex_integer (lexer) > max
                  : is_number ? lex_number (lexer) > max
                  : false);
  if (is_integer && !too_small && !too_big)
    return true;

  if (min > max)
    {
      /* Weird, maybe a bug in the caller.  Just report that we needed an
         integer. */
      if (name)
        lex_error (lexer, _("Syntax error expecting integer for %s."), name);
      else
        lex_error (lexer, _("Syntax error expecting integer."));
    }
  else if (min == max)
    {
      if (name)
        lex_error (lexer, _("Syntax error expecting %ld for %s."), min, name);
      else
        lex_error (lexer, _("Syntax error expecting %ld."), min);
    }
  else if (min + 1 == max)
    {
      if (name)
        lex_error (lexer, _("Syntax error expecting %ld or %ld for %s."),
                   min, min + 1, name);
      else
        lex_error (lexer, _("Syntax error expecting %ld or %ld."),
                   min, min + 1);
    }
  else
    {
      bool report_lower_bound = (min > INT_MIN / 2) || too_small;
      bool report_upper_bound = (max < INT_MAX / 2) || too_big;

      if (report_lower_bound && report_upper_bound)
        {
          if (name)
            lex_error (lexer,
                       _("Syntax error expecting integer "
                         "between %ld and %ld for %s."),
                       min, max, name);
          else
            lex_error (lexer, _("Syntax error expecting integer "
                                "between %ld and %ld."),
                       min, max);
        }
      else if (report_lower_bound)
        {
          if (min == 0)
            {
              if (name)
                lex_error (lexer, _("Syntax error expecting "
                                    "non-negative integer for %s."),
                           name);
              else
                lex_error (lexer, _("Syntax error expecting "
                                    "non-negative integer."));
            }
          else if (min == 1)
            {
              if (name)
                lex_error (lexer, _("Syntax error expecting "
                                    "positive integer for %s."),
                           name);
              else
                lex_error (lexer, _("Syntax error expecting "
                                    "positive integer."));
            }
          else
            {
              if (name)
                lex_error (lexer, _("Syntax error expecting "
                                    "integer %ld or greater for %s."),
                           min, name);
              else
                lex_error (lexer, _("Syntax error expecting "
                                    "integer %ld or greater."), min);
            }
        }
      else if (report_upper_bound)
        {
          if (name)
            lex_error (lexer,
                       _("Syntax error expecting integer less than or equal "
                         "to %ld for %s."),
                       max, name);
          else
            lex_error (lexer, _("Syntax error expecting integer less than or "
                                "equal to %ld."),
                       max);
        }
      else
        {
          if (name)
            lex_error (lexer, _("Syntax error expecting integer for %s."),
                       name);
          else
            lex_error (lexer, _("Syntax error expecting integer."));
        }
    }
  return false;
}

/* If the current token is a number, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_num (struct lexer *lexer)
{
  if (lex_is_number (lexer))
    return true;

  lex_error (lexer, _("Syntax error expecting number."));
  return false;
}

/* If the current token is a number in the closed range [MIN,MAX], does
   nothing and returns true.  Otherwise, reports an error and returns false.
   If NAME is nonnull, then it is used in the error message. */
bool
lex_force_num_range_closed (struct lexer *lexer, const char *name,
                            double min, double max)
{
  bool is_number = lex_is_number (lexer);
  bool too_small = is_number && lex_number (lexer) < min;
  bool too_big = is_number && lex_number (lexer) > max;
  if (is_number && !too_small && !too_big)
    return true;

  if (min > max)
    {
      /* Weird, maybe a bug in the caller.  Just report that we needed a
         number. */
      if (name)
        lex_error (lexer, _("Syntax error expecting number for %s."), name);
      else
        lex_error (lexer, _("Syntax error expecting number."));
    }
  else if (min == max)
    {
      if (name)
        lex_error (lexer, _("Syntax error expecting number %g for %s."),
                   min, name);
      else
        lex_error (lexer, _("Syntax error expecting number %g."), min);
    }
  else
    {
      bool report_lower_bound = min > -DBL_MAX || too_small;
      bool report_upper_bound = max < DBL_MAX || too_big;

      if (report_lower_bound && report_upper_bound)
        {
          if (name)
            lex_error (lexer,
                       _("Syntax error expecting number "
                         "between %g and %g for %s."),
                       min, max, name);
          else
            lex_error (lexer, _("Syntax error expecting number "
                                "between %g and %g."),
                       min, max);
        }
      else if (report_lower_bound)
        {
          if (min == 0)
            {
              if (name)
                lex_error (lexer, _("Syntax error expecting "
                                    "non-negative number for %s."),
                           name);
              else
                lex_error (lexer, _("Syntax error expecting "
                                    "non-negative number."));
            }
          else
            {
              if (name)
                lex_error (lexer, _("Syntax error expecting number "
                                    "%g or greater for %s."),
                           min, name);
              else
                lex_error (lexer, _("Syntax error expecting number "
                                    "%g or greater."), min);
            }
        }
      else if (report_upper_bound)
        {
          if (name)
            lex_error (lexer,
                       _("Syntax error expecting number "
                         "less than or equal to %g for %s."),
                       max, name);
          else
            lex_error (lexer, _("Syntax error expecting number "
                                "less than or equal to %g."),
                       max);
        }
      else
        {
          if (name)
            lex_error (lexer, _("Syntax error expecting number for %s."), name);
          else
            lex_error (lexer, _("Syntax error expecting number."));
        }
    }
  return false;
}

/* If the current token is a number in the half-open range [MIN,MAX), does
   nothing and returns true.  Otherwise, reports an error and returns false.
   If NAME is nonnull, then it is used in the error message. */
bool
lex_force_num_range_co (struct lexer *lexer, const char *name,
                        double min, double max)
{
  bool is_number = lex_is_number (lexer);
  bool too_small = is_number && lex_number (lexer) < min;
  bool too_big = is_number && lex_number (lexer) >= max;
  if (is_number && !too_small && !too_big)
    return true;

  if (min >= max)
    {
      /* Weird, maybe a bug in the caller.  Just report that we needed a
         number. */
      if (name)
        lex_error (lexer, _("Syntax error expecting number for %s."), name);
      else
        lex_error (lexer, _("Syntax error expecting number."));
    }
  else
    {
      bool report_lower_bound = min > -DBL_MAX || too_small;
      bool report_upper_bound = max < DBL_MAX || too_big;

      if (report_lower_bound && report_upper_bound)
        {
          if (name)
            lex_error (lexer, _("Syntax error expecting number "
                                "in [%g,%g) for %s."),
                       min, max, name);
          else
            lex_error (lexer, _("Syntax error expecting number in [%g,%g)."),
                       min, max);
        }
      else if (report_lower_bound)
        {
          if (min == 0)
            {
              if (name)
                lex_error (lexer, _("Syntax error expecting "
                                    "non-negative number for %s."),
                           name);
              else
                lex_error (lexer, _("Syntax error expecting "
                                    "non-negative number."));
            }
          else
            {
              if (name)
                lex_error (lexer, _("Syntax error expecting "
                                    "number %g or greater for %s."),
                           min, name);
              else
                lex_error (lexer, _("Syntax error expecting "
                                    "number %g or greater."), min);
            }
        }
      else if (report_upper_bound)
        {
          if (name)
            lex_error (lexer,
                       _("Syntax error expecting "
                         "number less than %g for %s."), max, name);
          else
            lex_error (lexer, _("Syntax error expecting "
                                "number less than %g."), max);
        }
      else
        {
          if (name)
            lex_error (lexer, _("Syntax error expecting number for %s."), name);
          else
            lex_error (lexer, _("Syntax error expecting number."));
        }
    }
  return false;
}

/* If the current token is a number in the half-open range (MIN,MAX], does
   nothing and returns true.  Otherwise, reports an error and returns false.
   If NAME is nonnull, then it is used in the error message. */
bool
lex_force_num_range_oc (struct lexer *lexer, const char *name,
                        double min, double max)
{
  bool is_number = lex_is_number (lexer);
  bool too_small = is_number && lex_number (lexer) <= min;
  bool too_big = is_number && lex_number (lexer) > max;
  if (is_number && !too_small && !too_big)
    return true;

  if (min >= max)
    {
      /* Weird, maybe a bug in the caller.  Just report that we needed a
         number. */
      if (name)
        lex_error (lexer, _("Syntax error expecting number for %s."), name);
      else
        lex_error (lexer, _("Syntax error expecting number."));
    }
  else
    {
      bool report_lower_bound = min > -DBL_MAX || too_small;
      bool report_upper_bound = max < DBL_MAX || too_big;

      if (report_lower_bound && report_upper_bound)
        {
          if (name)
            lex_error (lexer, _("Syntax error expecting number "
                                "in (%g,%g] for %s."),
                       min, max, name);
          else
            lex_error (lexer, _("Syntax error expecting number in (%g,%g]."),
                       min, max);
        }
      else if (report_lower_bound)
        {
          if (min == 0)
            {
              if (name)
                lex_error (lexer, _("Syntax error expecting "
                                    "positive number for %s."),
                           name);
              else
                lex_error (lexer, _("Syntax error expecting positive number."));
            }
          else
            {
              if (name)
                lex_error (lexer, _("Syntax error expecting "
                                    "number greater than %g for %s."),
                           min, name);
              else
                lex_error (lexer, _("Syntax error expecting "
                                    "number greater than %g."), min);
            }
        }
      else if (report_upper_bound)
        {
          if (name)
            lex_error (lexer,
                       _("Syntax error expecting number %g or less for %s."),
                       max, name);
          else
            lex_error (lexer, _("Syntax error expecting number %g or less."),
                       max);
        }
      else
        {
          if (name)
            lex_error (lexer, _("Syntax error expecting number for %s."), name);
          else
            lex_error (lexer, _("Syntax error expecting number."));
        }
    }
  return false;
}

/* If the current token is a number in the open range (MIN,MAX), does
   nothing and returns true.  Otherwise, reports an error and returns false.
   If NAME is nonnull, then it is used in the error message. */
bool
lex_force_num_range_open (struct lexer *lexer, const char *name,
                          double min, double max)
{
  bool is_number = lex_is_number (lexer);
  bool too_small = is_number && lex_number (lexer) <= min;
  bool too_big = is_number && lex_number (lexer) >= max;
  if (is_number && !too_small && !too_big)
    return true;

  if (min >= max)
    {
      /* Weird, maybe a bug in the caller.  Just report that we needed a
         number. */
      if (name)
        lex_error (lexer, _("Syntax error expecting number for %s."), name);
      else
        lex_error (lexer, _("Syntax error expecting number."));
    }
  else
    {
      bool report_lower_bound = min > -DBL_MAX || too_small;
      bool report_upper_bound = max < DBL_MAX || too_big;

      if (report_lower_bound && report_upper_bound)
        {
          if (name)
            lex_error (lexer, _("Syntax error expecting number "
                                "in (%g,%g) for %s."),
                       min, max, name);
          else
            lex_error (lexer, _("Syntax error expecting number "
                                "in (%g,%g)."), min, max);
        }
      else if (report_lower_bound)
        {
          if (min == 0)
            {
              if (name)
                lex_error (lexer, _("Syntax error expecting "
                                    "positive number for %s."), name);
              else
                lex_error (lexer, _("Syntax error expecting "
                                    "positive number."));
            }
          else
            {
              if (name)
                lex_error (lexer, _("Syntax error expecting number "
                                    "greater than %g for %s."),
                           min, name);
              else
                lex_error (lexer, _("Syntax error expecting number "
                                    "greater than %g."), min);
            }
        }
      else if (report_upper_bound)
        {
          if (name)
            lex_error (lexer, _("Syntax error expecting number "
                                "less than %g for %s."),
                       max, name);
          else
            lex_error (lexer, _("Syntax error expecting number "
                                "less than %g."), max);
        }
      else
        {
          if (name)
            lex_error (lexer, _("Syntax error expecting number "
                                "for %s."), name);
          else
            lex_error (lexer, _("Syntax error expecting number."));
        }
    }
  return false;
}

/* If the current token is an identifier, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_id (struct lexer *lexer)
{
  if (lex_token (lexer) == T_ID)
    return true;

  lex_error (lexer, _("Syntax error expecting identifier."));
  return false;
}

/* Token accessors. */

/* Returns the type of LEXER's current token. */
enum token_type
lex_token (const struct lexer *lexer)
{
  return lex_next_token (lexer, 0);
}

/* Returns the number in LEXER's current token.

   Only T_NEG_NUM and T_POS_NUM tokens have meaningful values.  For other
   tokens this function will always return zero. */
double
lex_tokval (const struct lexer *lexer)
{
  return lex_next_tokval (lexer, 0);
}

/* Returns the null-terminated string in LEXER's current token, UTF-8 encoded.

   Only T_ID and T_STRING tokens have meaningful strings.  For other tokens
   this functions this function will always return NULL.

   The UTF-8 encoding of the returned string is correct for variable names and
   other identifiers.  Use filename_to_utf8() to use it as a filename.  Use
   data_in() to use it in a "union value".  */
const char *
lex_tokcstr (const struct lexer *lexer)
{
  return lex_next_tokcstr (lexer, 0);
}

/* Returns the string in LEXER's current token, UTF-8 encoded.  The string is
   null-terminated (but the null terminator is not included in the returned
   substring's 'length').

   Only T_ID and T_STRING tokens have meaningful strings.  For other tokens
   this functions this function will always return NULL.

   The UTF-8 encoding of the returned string is correct for variable names and
   other identifiers.  Use filename_to_utf8() to use it as a filename.  Use
   data_in() to use it in a "union value".  */
struct substring
lex_tokss (const struct lexer *lexer)
{
  return lex_next_tokss (lexer, 0);
}

/* Looking ahead.

   A value of 0 for N as an argument to any of these functions refers to the
   current token.  Lookahead is limited to the current command.  Any N greater
   than the number of tokens remaining in the current command will be treated
   as referring to a T_ENDCMD token. */

static const struct lex_token *
lex_next__ (const struct lexer *lexer_, int n)
{
  struct lexer *lexer = CONST_CAST (struct lexer *, lexer_);
  struct lex_source *src = lex_source__ (lexer);

  if (src != NULL)
    return lex_source_next__ (src, n);
  else
    {
      static const struct lex_token stop_token = { .token = { .type = T_STOP } };
      return &stop_token;
    }
}

static const struct lex_token *
lex_source_ofs__ (const struct lex_source *src_, int ofs)
{
  struct lex_source *src = CONST_CAST (struct lex_source *, src_);

  if (ofs < 0)
    {
      static const struct lex_token endcmd_token
        = { .token = { .type = T_ENDCMD } };
      return &endcmd_token;
    }

  while (ofs >= src->n_parse)
    {
      if (src->n_parse > 0)
        {
          const struct lex_token *t = src->parse[src->n_parse - 1];
          if (t->token.type == T_STOP || t->token.type == T_ENDCMD)
            return t;
        }

      lex_source_get_parse (src);
    }

  return src->parse[ofs];
}

static const struct lex_token *
lex_source_next__ (const struct lex_source *src, int n)
{
  return lex_source_ofs__ (src, n + src->parse_ofs);
}

/* Returns the "struct token" of the token N after the current one in LEXER.
   The returned pointer can be invalidated by pretty much any succeeding call
   into the lexer, although the string pointer within the returned token is
   only invalidated by consuming the token (e.g. with lex_get()). */
const struct token *
lex_next (const struct lexer *lexer, int n)
{
  return &lex_next__ (lexer, n)->token;
}

/* Returns the type of the token N after the current one in LEXER. */
enum token_type
lex_next_token (const struct lexer *lexer, int n)
{
  return lex_next (lexer, n)->type;
}

/* Returns the number in the tokn N after the current one in LEXER.

   Only T_NEG_NUM and T_POS_NUM tokens have meaningful values.  For other
   tokens this function will always return zero. */
double
lex_next_tokval (const struct lexer *lexer, int n)
{
  return token_number (lex_next (lexer, n));
}

/* Returns the null-terminated string in the token N after the current one, in
   UTF-8 encoding.

   Only T_ID and T_STRING tokens have meaningful strings.  For other tokens
   this functions this function will always return NULL.

   The UTF-8 encoding of the returned string is correct for variable names and
   other identifiers.  Use filename_to_utf8() to use it as a filename.  Use
   data_in() to use it in a "union value".  */
const char *
lex_next_tokcstr (const struct lexer *lexer, int n)
{
  return lex_next_tokss (lexer, n).string;
}

/* Returns the string in the token N after the current one, in UTF-8 encoding.
   The string is null-terminated (but the null terminator is not included in
   the returned substring's 'length').

   Only T_ID, T_MACRO_ID, T_STRING tokens have meaningful strings.  For other
   tokens this functions this function will always return NULL.

   The UTF-8 encoding of the returned string is correct for variable names and
   other identifiers.  Use filename_to_utf8() to use it as a filename.  Use
   data_in() to use it in a "union value".  */
struct substring
lex_next_tokss (const struct lexer *lexer, int n)
{
  return lex_next (lexer, n)->string;
}

/* Returns the offset of the current token within the command being parsed in
   LEXER.  This is 0 for the first token in a command, 1 for the second, and so
   on.  The return value is useful later for referring to this token in calls
   to lex_ofs_*(). */
int
lex_ofs (const struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);
  return src ? src->parse_ofs : 0;
}

/* Returns the offset of the last token in the current command. */
int
lex_max_ofs (const struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);
  if (!src)
    return 0;

  int ofs = MAX (1, src->n_parse) - 1;
  for (;;)
    {
      enum token_type type = lex_source_ofs__ (src, ofs)->token.type;
      if (type == T_ENDCMD || type == T_STOP)
        return ofs;

      ofs++;
    }
}

/* Returns the token within LEXER's current command with offset OFS.  Use
   lex_ofs() to find out the offset of the current token. */
const struct token *
lex_ofs_token (const struct lexer *lexer_, int ofs)
{
  struct lexer *lexer = CONST_CAST (struct lexer *, lexer_);
  struct lex_source *src = lex_source__ (lexer);

  if (src != NULL)
    return &lex_source_next__ (src, ofs - src->parse_ofs)->token;
  else
    {
      static const struct token stop_token = { .type = T_STOP };
      return &stop_token;
    }
}

/* Allocates and returns a new struct msg_location that spans tokens with
   offsets OFS0 through OFS1, inclusive, within the current command in
   LEXER.  See lex_ofs() for an explanation of token offsets.

   The caller owns and must eventually free the returned object. */
struct msg_location *
lex_ofs_location (const struct lexer *lexer, int ofs0, int ofs1)
{
  int ofs = lex_ofs (lexer);
  return lex_get_location (lexer, ofs0 - ofs, ofs1 - ofs);
}

/* Returns a msg_point for the first character in the token with offset OFS,
   where offset 0 is the first token in the command currently being parsed, 1
   the second token, and so on.  These are absolute offsets, not relative to
   the token currently being parsed within the command.

   Returns zeros for a T_STOP token.
 */
struct msg_point
lex_ofs_start_point (const struct lexer *lexer, int ofs)
{
  const struct lex_source *src = lex_source__ (lexer);
  return (src
          ? lex_token_start_point (src, lex_source_ofs__ (src, ofs))
          : (struct msg_point) { 0, 0 });
}

/* Returns a msg_point for the last character, inclusive, in the token with
   offset OFS, where offset 0 is the first token in the command currently being
   parsed, 1 the second token, and so on.  These are absolute offsets, not
   relative to the token currently being parsed within the command.

   Returns zeros for a T_STOP token.

   Most of the time, a single token is wholly within a single line of syntax,
   so that the start and end point for a given offset have the same line
   number.  There are two exceptions: a T_STRING token can be made up of
   multiple segments on adjacent lines connected with "+" punctuators, and a
   T_NEG_NUM token can consist of a "-" on one line followed by the number on
   the next.
 */
struct msg_point
lex_ofs_end_point (const struct lexer *lexer, int ofs)
{
  const struct lex_source *src = lex_source__ (lexer);
  return (src
          ? lex_token_end_point (src, lex_source_ofs__ (src, ofs))
          : (struct msg_point) { 0, 0 });
}

/* Returns the text of the syntax in tokens N0 ahead of the current one,
   through N1 ahead of the current one, inclusive.  (For example, if N0 and N1
   are both zero, this requests the syntax for the current token.)

   The caller must eventually free the returned string (with free()).  The
   syntax is encoded in UTF-8 and in the original form supplied to the lexer so
   that, for example, it may include comments, spaces, and new-lines if it
   spans multiple tokens.  Macro expansion, however, has already been
   performed. */
char *
lex_next_representation (const struct lexer *lexer, int n0, int n1)
{
  const struct lex_source *src = lex_source__ (lexer);
  return (src
          ? lex_source_syntax__ (src, n0 + src->parse_ofs, n1 + src->parse_ofs)
          : xstrdup (""));
}


/* Returns the text of the syntax in tokens with offsets OFS0 to OFS1,
   inclusive.  (For example, if OFS0 and OFS1 are both zero, this requests the
   syntax for the first token in the current command.)

   The caller must eventually free the returned string (with free()).  The
   syntax is encoded in UTF-8 and in the original form supplied to the lexer so
   that, for example, it may include comments, spaces, and new-lines if it
   spans multiple tokens.  Macro expansion, however, has already been
   performed. */
char *
lex_ofs_representation (const struct lexer *lexer, int ofs0, int ofs1)
{
  const struct lex_source *src = lex_source__ (lexer);
  return src ? lex_source_syntax__ (src, ofs0, ofs1) : xstrdup ("");
}

/* Returns true if the token N ahead of the current one was produced by macro
   expansion, false otherwise. */
bool
lex_next_is_from_macro (const struct lexer *lexer, int n)
{
  return lex_next__ (lexer, n)->macro_rep != NULL;
}

static bool
lex_tokens_match (const struct token *actual, const struct token *expected)
{
  if (actual->type != expected->type)
    return false;

  switch (actual->type)
    {
    case T_POS_NUM:
    case T_NEG_NUM:
      return actual->number == expected->number;

    case T_ID:
      return lex_id_match (expected->string, actual->string);

    case T_STRING:
      return (actual->string.length == expected->string.length
              && !memcmp (actual->string.string, expected->string.string,
                          actual->string.length));

    default:
      return true;
    }
}

static bool
lex_ofs_at_phrase__ (struct lexer *lexer, int ofs, const char *s,
                     size_t *n_matchedp)
{
  struct string_lexer slex;
  struct token token;

  size_t n_matched = 0;
  bool all_matched = true;
  string_lexer_init (&slex, s, strlen (s), SEG_MODE_INTERACTIVE, true);
  while (string_lexer_next (&slex, &token))
    {
      bool match = lex_tokens_match (lex_ofs_token (lexer, ofs + n_matched),
                                     &token);
      token_uninit (&token);
      if (!match)
        {
          all_matched = false;
          break;
        }
      n_matched++;
    }
  if (n_matchedp)
    *n_matchedp = n_matched;
  return all_matched;
}

/* If LEXER is positioned at the sequence of tokens that may be parsed from S,
   returns true.  Otherwise, returns false.

   S may consist of an arbitrary sequence of tokens, e.g. "KRUSKAL-WALLIS",
   "2SLS", or "END INPUT PROGRAM".  Identifiers may be abbreviated to their
   first three letters. */
bool
lex_at_phrase (struct lexer *lexer, const char *s)
{
  return lex_ofs_at_phrase__ (lexer, lex_ofs (lexer), s, NULL);
}

/* If LEXER is positioned at the sequence of tokens that may be parsed from S,
   skips it and returns true.  Otherwise, returns false.

   S may consist of an arbitrary sequence of tokens, e.g. "KRUSKAL-WALLIS",
   "2SLS", or "END INPUT PROGRAM".  Identifiers may be abbreviated to their
   first three letters. */
bool
lex_match_phrase (struct lexer *lexer, const char *s)
{
  size_t n_matched;
  if (!lex_ofs_at_phrase__ (lexer, lex_ofs (lexer), s, &n_matched))
    return false;
  lex_get_n (lexer, n_matched);
  return true;
}

/* If LEXER is positioned at the sequence of tokens that may be parsed from S,
   skips it and returns true.  Otherwise, issues an error and returns false.

   S may consist of an arbitrary sequence of tokens, e.g. "KRUSKAL-WALLIS",
   "2SLS", or "END INPUT PROGRAM".  Identifiers may be abbreviated to their
   first three letters. */
bool
lex_force_match_phrase (struct lexer *lexer, const char *s)
{
  size_t n_matched;
  bool ok = lex_ofs_at_phrase__ (lexer, lex_ofs (lexer), s, &n_matched);
  if (ok)
    lex_get_n (lexer, n_matched);
  else
    lex_next_error (lexer, 0, n_matched, _("Syntax error expecting `%s'."), s);
  return ok;
}

/* Returns the 1-based line number of the source text at the byte OFFSET in
   SRC. */
static int
lex_source_ofs_to_line_number (const struct lex_source *src, size_t offset)
{
  size_t lo = 0;
  size_t hi = src->n_lines;
  for (;;)
    {
      size_t mid = (lo + hi) / 2;
      if (mid + 1 >= src->n_lines)
        return src->n_lines;
      else if (offset >= src->lines[mid + 1])
        lo = mid;
      else if (offset < src->lines[mid])
        hi = mid;
      else
        return mid + 1;
    }
}

/* Returns the 1-based column number of the source text at the byte OFFSET in
   SRC. */
static int
lex_source_ofs_to_column_number (const struct lex_source *src, size_t offset)
{
  const char *newline = memrchr (src->buffer, '\n', offset);
  size_t line_ofs = newline ? newline - src->buffer + 1 : 0;
  return utf8_count_columns (&src->buffer[line_ofs], offset - line_ofs) + 1;
}

static struct msg_point
lex_source_ofs_to_point__ (const struct lex_source *src, size_t offset)
{
  return (struct msg_point) {
    .line = lex_source_ofs_to_line_number (src, offset),
    .column = lex_source_ofs_to_column_number (src, offset),
  };
}

static struct msg_point
lex_token_start_point (const struct lex_source *src,
                       const struct lex_token *token)
{
  return lex_source_ofs_to_point__ (src, token->token_pos);
}

static struct msg_point
lex_token_end_point (const struct lex_source *src,
                     const struct lex_token *token)
{
  return lex_source_ofs_to_point__ (src, lex_token_end (token));
}

static struct msg_location
lex_token_location (const struct lex_source *src,
                    const struct lex_token *t0,
                    const struct lex_token *t1)
{
  return (struct msg_location) {
    .file_name = intern_new_if_nonnull (src->reader->file_name),
    .start = lex_token_start_point (src, t0),
    .end = lex_token_end_point (src, t1),
    .src = CONST_CAST (struct lex_source *, src),
  };
}

static struct msg_location *
lex_token_location_rw (const struct lex_source *src,
                       const struct lex_token *t0,
                       const struct lex_token *t1)
{
  struct msg_location location = lex_token_location (src, t0, t1);
  return msg_location_dup (&location);
}

static struct msg_location *
lex_source_get_location (const struct lex_source *src, int ofs0, int ofs1)
{
  return lex_token_location_rw (src,
                                lex_source_ofs__ (src, ofs0),
                                lex_source_ofs__ (src, ofs1));
}

/* Returns the name of the syntax file from which the current command is drawn.
   Returns NULL for a T_STOP token or if the command's source does not have
   line numbers.

   There is no version of this function that takes an N argument because
   lookahead only works to the end of a command and any given command is always
   within a single syntax file. */
const char *
lex_get_file_name (const struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);
  return src == NULL ? NULL : src->reader->file_name;
}

/* Returns a newly allocated msg_location for the syntax that represents tokens
   with 0-based offsets N0...N1, inclusive, from the current token.  The caller
   must eventually free the location (with msg_location_destroy()). */
struct msg_location *
lex_get_location (const struct lexer *lexer, int n0, int n1)
{
  struct msg_location *loc = xmalloc (sizeof *loc);
  *loc = (struct msg_location) {
    .file_name = intern_new_if_nonnull (lex_get_file_name (lexer)),
    .start = lex_ofs_start_point (lexer, n0 + lex_ofs (lexer)),
    .end = lex_ofs_end_point (lexer, n1 + lex_ofs (lexer)),
    .src = lex_source__ (lexer),
  };
  lex_source_ref (loc->src);
  return loc;
}

const char *
lex_get_encoding (const struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);
  return src == NULL ? NULL : src->reader->encoding;
}

/* Returns the syntax mode for the syntax file from which the current drawn is
   drawn.  Returns SEG_MODE_AUTO for a T_STOP token or if the command's source
   does not have line numbers.

   There is no version of this function that takes an N argument because
   lookahead only works to the end of a command and any given command is always
   within a single syntax file. */
enum segmenter_mode
lex_get_syntax_mode (const struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);
  return src == NULL ? SEG_MODE_AUTO : src->reader->syntax;
}

/* Returns the error mode for the syntax file from which the current drawn is
   drawn.  Returns LEX_ERROR_TERMINAL for a T_STOP token or if the command's
   source does not have line numbers.

   There is no version of this function that takes an N argument because
   lookahead only works to the end of a command and any given command is always
   within a single syntax file. */
enum lex_error_mode
lex_get_error_mode (const struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);
  return src == NULL ? LEX_ERROR_TERMINAL : src->reader->error;
}

/* If the source that LEXER is currently reading has error mode
   LEX_ERROR_TERMINAL, discards all buffered input and tokens, so that the next
   token to be read comes directly from whatever is next read from the stream.

   It makes sense to call this function after encountering an error in a
   command entered on the console, because usually the user would prefer not to
   have cascading errors. */
void
lex_interactive_reset (struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);
  if (src != NULL && src->reader->error == LEX_ERROR_TERMINAL)
    {
      src->length = 0;
      src->journal_pos = src->seg_pos = 0;
      src->n_lines = 0;
      src->suppress_next_newline = false;
      src->segmenter = segmenter_init (segmenter_get_mode (&src->segmenter),
                                       false);
      lex_stage_clear (&src->pp);
      lex_stage_clear (&src->merge);
      lex_source_clear_parse (src);
      lex_source_push_endcmd__ (src);
    }
}

/* Advances past any tokens in LEXER up to a T_ENDCMD or T_STOP. */
void
lex_discard_rest_of_command (struct lexer *lexer)
{
  while (lex_token (lexer) != T_STOP && lex_token (lexer) != T_ENDCMD)
    lex_get (lexer);
}

/* Discards all lookahead tokens in LEXER, then discards all input sources
   until it encounters one with error mode LEX_ERROR_TERMINAL or until it
   runs out of input sources. */
void
lex_discard_noninteractive (struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);
  if (src != NULL)
    {
      if (src->reader->error == LEX_ERROR_IGNORE)
        return;

      lex_stage_clear (&src->pp);
      lex_stage_clear (&src->merge);
      lex_source_clear_parse (src);

      for (; src != NULL && src->reader->error != LEX_ERROR_TERMINAL;
           src = lex_source__ (lexer))
        {
          ll_remove (&src->ll);
          lex_source_unref (src);
        }
    }
}

static void
lex_source_expand__ (struct lex_source *src)
{
  if (src->length >= src->allocated)
    src->buffer = x2realloc (src->buffer, &src->allocated);
}

static void
lex_source_read__ (struct lex_source *src)
{
  do
    {
      lex_source_expand__ (src);

      size_t space = src->allocated - src->length;
      enum prompt_style prompt = segmenter_get_prompt (&src->segmenter);
      size_t n = src->reader->class->read (src->reader,
                                           &src->buffer[src->length],
                                           space, prompt);
      assert (n <= space);

      if (n == 0)
        {
          /* End of input. */
          src->reader->eof = true;
          return;
        }

      src->length += n;
    }
  while (!memchr (&src->buffer[src->seg_pos], '\n',
                  src->length - src->seg_pos));
}

static struct lex_source *
lex_source__ (const struct lexer *lexer)
{
  return (ll_is_empty (&lexer->sources) ? NULL
          : ll_data (ll_head (&lexer->sources), struct lex_source, ll));
}

const struct lex_source *
lex_source (const struct lexer *lexer)
{
  return lex_source__ (lexer);
}

/* Returns the text of the syntax in SRC for tokens with offsets OFS0 through
   OFS1 in the current command, inclusive.  (For example, if OFS0 and OFS1 are
   both zero, this requests the syntax for the first token in the current
   command.)  The caller must eventually free the returned string (with
   free()).  The syntax is encoded in UTF-8 and in the original form supplied
   to the lexer so that, for example, it may include comments, spaces, and
   new-lines if it spans multiple tokens.  Macro expansion, however, has
   already been performed. */
static char *
lex_source_syntax__ (const struct lex_source *src, int ofs0, int ofs1)
{
  struct string s = DS_EMPTY_INITIALIZER;
  for (size_t i = ofs0; i <= ofs1; )
    {
      /* Find [I,J) as the longest sequence of tokens not produced by macro
         expansion, or otherwise the longest sequence expanded from a single
         macro call. */
      const struct lex_token *first = lex_source_ofs__ (src, i);
      size_t j;
      for (j = i + 1; j <= ofs1; j++)
        {
          const struct lex_token *cur = lex_source_ofs__ (src, j);
          if ((first->macro_rep != NULL) != (cur->macro_rep != NULL)
              || first->macro_rep != cur->macro_rep)
            break;
        }
      const struct lex_token *last = lex_source_ofs__ (src, j - 1);

      /* Now add the syntax for this sequence of tokens to SRC. */
      if (!ds_is_empty (&s))
        ds_put_byte (&s, ' ');
      if (!first->macro_rep)
        {
          size_t start = first->token_pos;
          size_t end = last->token_pos + last->token_len;
          ds_put_substring (&s, ss_buffer (&src->buffer[start], end - start));
        }
      else
        {
          size_t start = first->ofs;
          size_t end = last->ofs + last->len;
          ds_put_substring (&s, ss_buffer (first->macro_rep + start,
                                           end - start));
        }

      i = j;
    }
  return ds_steal_cstr (&s);
}

static bool
lex_source_contains_macro_call (struct lex_source *src, int ofs0, int ofs1)
{
  for (int i = ofs0; i <= ofs1; i++)
    if (lex_source_ofs__ (src, i)->macro_rep)
      return true;
  return false;
}

/* If tokens N0...N1 (inclusive) in SRC contains a macro call, this returns the
   raw UTF-8 syntax for the macro call (not for the expansion) and for any
   other tokens included in that range.  The syntax is encoded in UTF-8 and in
   the original form supplied to the lexer so that, for example, it may include
   comments, spaces, and new-lines if it spans multiple tokens.

   Returns an empty string if the token range doesn't include a macro call.

   The caller must not modify or free the returned string. */
static struct substring
lex_source_get_macro_call (struct lex_source *src, int ofs0, int ofs1)
{
  if (!lex_source_contains_macro_call (src, ofs0, ofs1))
    return ss_empty ();

  const struct lex_token *token0 = lex_source_ofs__ (src, ofs0);
  const struct lex_token *token1 = lex_source_ofs__ (src, MAX (ofs0, ofs1));
  size_t start = token0->token_pos;
  size_t end = token1->token_pos + token1->token_len;

  return ss_buffer (&src->buffer[start], end - start);
}

static void
lex_source_msg_valist (struct lex_source *src, enum msg_class class,
                       int ofs0, int ofs1, const char *format, va_list args)
{
  struct string s = DS_EMPTY_INITIALIZER;

  if (src)
    {
      /* Get the macro call(s) that expanded to the syntax that caused the
         error. */
      char call[64];
      str_ellipsize (lex_source_get_macro_call (src, ofs0, ofs1),
                     call, sizeof call);
      if (call[0])
        ds_put_format (&s, _("In syntax expanded from `%s'"), call);
    }
  else
    ds_put_cstr (&s, _("At end of input"));

  if (!ds_is_empty (&s))
    ds_put_cstr (&s, ": ");
  if (format)
    ds_put_vformat (&s, format, args);
  else
    ds_put_cstr (&s, _("Syntax error."));

  if (ds_last (&s) != '.')
    ds_put_byte (&s, '.');

  struct msg *m = xmalloc (sizeof *m);
  *m = (struct msg) {
    .category = msg_class_to_category (class),
    .severity = msg_class_to_severity (class),
    .location = src ? lex_source_get_location (src, ofs0, ofs1) : NULL,
    .text = ds_steal_cstr (&s),
  };
  msg_emit (m);
}

static void
lex_get_error (struct lex_source *src, const struct lex_token *token)
{
  struct msg *m = xmalloc (sizeof *m);
  *m = (struct msg) {
    .category = MSG_C_SYNTAX,
    .severity = MSG_S_ERROR,
    .location = lex_token_location_rw (src, token, token),
    .text = ss_xstrdup (token->token.string),
  };

  struct lexer *lexer = src->lexer;
  if (lexer->n_messages >= lexer->allocated_messages)
    lexer->messages = x2nrealloc (lexer->messages, &lexer->allocated_messages,
                                  sizeof *lexer->messages);
  lexer->messages[lexer->n_messages++] = m;
}

/* Attempts to append an additional token to 'pp' in SRC, reading more from the
   underlying lex_reader if necessary.  Returns true if a new token was added
   to SRC's deque, false otherwise.  The caller should retry failures unless
   SRC's 'eof' marker was set to true indicating that there will be no more
   tokens from this source. */
static bool
lex_source_try_get_pp (struct lex_source *src)
{
  /* Append a new token to SRC and initialize it. */
  struct lex_token *token = xmalloc (sizeof *token);
  token->token = (struct token) { .type = T_STOP };
  token->macro_rep = NULL;
  token->ref_cnt = NULL;
  token->token_pos = src->seg_pos;

  /* Extract a segment. */
  const char *segment;
  enum segment_type seg_type;
  int seg_len;
  for (;;)
    {
      segment = &src->buffer[src->seg_pos];
      seg_len = segmenter_push (&src->segmenter, segment,
                                src->length - src->seg_pos,
                                src->reader->eof, &seg_type);
      if (seg_len >= 0)
        break;

      /* The segmenter needs more input to produce a segment. */
      assert (!src->reader->eof);
      lex_source_read__ (src);
    }

  /* Update state based on the segment. */
  token->token_len = seg_len;
  src->seg_pos += seg_len;
  if (seg_type == SEG_NEWLINE)
    {
      if (src->n_lines >= src->allocated_lines)
        src->lines = x2nrealloc (src->lines, &src->allocated_lines,
                                 sizeof *src->lines);
      src->lines[src->n_lines++] = src->seg_pos;
    }

  /* Get a token from the segment. */
  enum tokenize_result result = token_from_segment (
    seg_type, ss_buffer (segment, seg_len), &token->token);

  /* If we've reached the end of a line, or the end of a command, then pass
     the line to the output engine as a syntax text item.  */
  int n_lines = seg_type == SEG_NEWLINE;
  if (seg_type == SEG_END_COMMAND && !src->suppress_next_newline)
    {
      n_lines++;
      src->suppress_next_newline = true;
    }
  else if (n_lines > 0 && src->suppress_next_newline)
    {
      n_lines--;
      src->suppress_next_newline = false;
    }
  for (int i = 0; i < n_lines; i++)
    {
      /* Beginning of line. */
      const char *line = &src->buffer[src->journal_pos];

      /* Calculate line length, including \n or \r\n end-of-line if present.

         We use src->length even though that may be beyond what we've actually
         converted to tokens.  That's because, if we're emitting the line due
         to SEG_END_COMMAND, we want to take the whole line through the
         newline, not just through the '.'. */
      size_t max_len = src->length - src->journal_pos;
      const char *newline = memchr (line, '\n', max_len);
      size_t line_len = newline ? newline - line + 1 : max_len;

      /* Calculate line length excluding end-of-line. */
      size_t copy_len = line_len;
      if (copy_len > 0 && line[copy_len - 1] == '\n')
        copy_len--;
      if (copy_len > 0 && line[copy_len - 1] == '\r')
        copy_len--;

      /* Submit the line as syntax. */
      output_item_submit (text_item_create_nocopy (TEXT_ITEM_SYNTAX,
                                                   xmemdup0 (line, copy_len),
                                                   NULL));

      src->journal_pos += line_len;
    }

  switch (result)
    {
    case TOKENIZE_ERROR:
      lex_get_error (src, token);
      /* Fall through. */
    case TOKENIZE_EMPTY:
      lex_token_destroy (token);
      return false;

    case TOKENIZE_TOKEN:
      if (token->token.type == T_STOP)
        {
          token->token.type = T_ENDCMD;
          src->eof = true;
        }
      lex_stage_push_last (&src->pp, token);
      return true;
    }
  NOT_REACHED ();
}

/* Attempts to append a new token to SRC.  Returns true if successful, false on
   failure.  On failure, the end of SRC has been reached and no more tokens
   will be forthcoming from it.

   Does not make the new token available for lookahead yet; the caller must
   adjust SRC's 'middle' pointer to do so. */
static bool
lex_source_get_pp (struct lex_source *src)
{
  while (!src->eof)
    if (lex_source_try_get_pp (src))
      return true;
  return false;
}

static bool
lex_source_try_get_merge (const struct lex_source *src_)
{
  struct lex_source *src = CONST_CAST (struct lex_source *, src_);

  if (lex_stage_is_empty (&src->pp) && !lex_source_get_pp (src))
    return false;

  if (!settings_get_mexpand ())
    {
      lex_stage_shift (&src->merge, &src->pp, lex_stage_count (&src->pp));
      return true;
    }

  /* Now pass tokens one-by-one to the macro expander.

     In the common case where there is no macro to expand, the loop is not
     entered.  */
  struct macro_call *mc;
  int n_call = macro_call_create (src->lexer->macros,
                                  &lex_stage_first (&src->pp)->token, &mc);
  for (int ofs = 1; !n_call; ofs++)
    {
      if (lex_stage_count (&src->pp) <= ofs && !lex_source_get_pp (src))
        {
          /* This should not be reachable because we always get a T_ENDCMD at
             the end of an input file (transformed from T_STOP by
             lex_source_try_get_pp()) and the macro_expander should always
             terminate expansion on T_ENDCMD. */
          NOT_REACHED ();
        }

      const struct lex_token *t = lex_stage_nth (&src->pp, ofs);
      const struct macro_token mt = {
        .token = t->token,
        .syntax = ss_buffer (&src->buffer[t->token_pos], t->token_len),
      };
      const struct msg_location loc = lex_token_location (src, t, t);
      n_call = macro_call_add (mc, &mt, &loc);
    }
  if (n_call < 0)
    {
      /* False alarm: no macro expansion after all.  Use first token as
         lookahead.  We'll retry macro expansion from the second token next
         time around. */
      macro_call_destroy (mc);
      lex_stage_shift (&src->merge, &src->pp, 1);
      return true;
    }

  /* The first 'n_call' tokens in 'pp', which we bracket as C0...C1, inclusive,
     are a macro call.  (These are likely to be the only tokens in 'pp'.)
     Expand them.  */
  const struct lex_token *c0 = lex_stage_first (&src->pp);
  const struct lex_token *c1 = lex_stage_nth (&src->pp, n_call - 1);
  struct macro_tokens expansion = { .n = 0 };
  struct msg_location loc = lex_token_location (src, c0, c1);
  macro_call_expand (mc, src->reader->syntax, &loc, &expansion);
  macro_call_destroy (mc);

  /* Convert the macro expansion into syntax for possible error messages
     later. */
  size_t *ofs = xnmalloc (expansion.n, sizeof *ofs);
  size_t *len = xnmalloc (expansion.n, sizeof *len);
  struct string s = DS_EMPTY_INITIALIZER;
  macro_tokens_to_syntax (&expansion, &s, ofs, len);

  if (settings_get_mprint ())
    output_item_submit (text_item_create (TEXT_ITEM_LOG, ds_cstr (&s),
                                          _("Macro Expansion")));

  /* Append the macro expansion tokens to the lookahead. */
  if (expansion.n > 0)
    {
      char *macro_rep = ds_steal_cstr (&s);
      size_t *ref_cnt = xmalloc (sizeof *ref_cnt);
      *ref_cnt = expansion.n;
      for (size_t i = 0; i < expansion.n; i++)
        {
          struct lex_token *token = xmalloc (sizeof *token);
          *token = (struct lex_token) {
            .token = expansion.mts[i].token,
            .token_pos = c0->token_pos,
            .token_len = (c1->token_pos + c1->token_len) - c0->token_pos,
            .macro_rep = macro_rep,
            .ofs = ofs[i],
            .len = len[i],
            .ref_cnt = ref_cnt,
          };
          lex_stage_push_last (&src->merge, token);

          ss_dealloc (&expansion.mts[i].syntax);
        }
    }
  else
    ds_destroy (&s);
  free (expansion.mts);
  free (ofs);
  free (len);

  /* Destroy the tokens for the call. */
  for (size_t i = 0; i < n_call; i++)
    lex_stage_pop_first (&src->pp);

  return expansion.n > 0;
}

/* Attempts to obtain at least one new token into 'merge' in SRC.

   Returns true if successful, false on failure.  In the latter case, SRC is
   exhausted and 'src->eof' is now true. */
static bool
lex_source_get_merge (struct lex_source *src)
{
  while (!src->eof)
    if (lex_source_try_get_merge (src))
      return true;
  return false;
}

static bool
lex_source_get_parse__ (struct lex_source *src)
{
  struct merger m = MERGER_INIT;
  struct token out;
  for (size_t i = 0; ; i++)
    {
      while (lex_stage_count (&src->merge) <= i && !lex_source_get_merge (src))
        {
          /* We always get a T_ENDCMD at the end of an input file
             (transformed from T_STOP by lex_source_try_get_pp()) and
             merger_add() should never return -1 on T_ENDCMD. */
          assert (lex_stage_is_empty (&src->merge));
          return false;
        }

      int retval = merger_add (&m, &lex_stage_nth (&src->merge, i)->token,
                               &out);
      if (!retval)
        {
          lex_source_push_parse (src, lex_stage_take_first (&src->merge));
          return true;
        }
      else if (retval > 0)
        {
          /* Add a token that merges all the tokens together. */
          const struct lex_token *first = lex_stage_first (&src->merge);
          const struct lex_token *last = lex_stage_nth (&src->merge,
                                                        retval - 1);
          bool macro = first->macro_rep && first->macro_rep == last->macro_rep;
          struct lex_token *t = xmalloc (sizeof *t);
          *t = (struct lex_token) {
            .token = out,
            .token_pos = first->token_pos,
            .token_len = (last->token_pos - first->token_pos) + last->token_len,

            /* This works well if all the tokens were not expanded from macros,
               or if they came from the same macro expansion.  It just gives up
               in the other (corner) cases. */
            .macro_rep = macro ? first->macro_rep : NULL,
            .ofs = macro ? first->ofs : 0,
            .len = macro ? (last->ofs - first->ofs) + last->len : 0,
            .ref_cnt = macro ? first->ref_cnt : NULL,
          };
          if (t->ref_cnt)
            ++*t->ref_cnt;
          lex_source_push_parse (src, t);

          for (int i = 0; i < retval; i++)
            lex_stage_pop_first (&src->merge);
          return true;
        }
    }
}

/* Attempts to obtain at least one new token into 'lookahead' in SRC.

   Returns true if successful, false on failure.  In the latter case, SRC is
   exhausted and 'src->eof' is now true. */
static bool
lex_source_get_parse (struct lex_source *src)
{
  bool ok = lex_source_get_parse__ (src);
  struct lexer *lexer = src->lexer;
  if (lexer->n_messages)
    {
      struct msg **messages = lexer->messages;
      size_t n = lexer->n_messages;

      lexer->messages = NULL;
      lexer->n_messages = lexer->allocated_messages = 0;

      for (size_t i = 0; i < n; i++)
        msg_emit (messages[i]);
      free (messages);
    }
  return ok;
}

static void
lex_source_push_endcmd__ (struct lex_source *src)
{
  assert (src->n_parse == 0);

  struct lex_token *token = xmalloc (sizeof *token);
  *token = (struct lex_token) { .token = { .type = T_ENDCMD } };
  lex_source_push_parse (src, token);
}

static void
lex_source_push_parse (struct lex_source *src, struct lex_token *token)
{
  if (src->n_parse >= src->allocated_parse)
    src->parse = x2nrealloc (src->parse, &src->allocated_parse,
                             sizeof *src->parse);
  src->parse[src->n_parse++] = token;
}

static void
lex_source_clear_parse (struct lex_source *src)
{
  for (size_t i = 0; i < src->n_parse; i++)
    lex_token_destroy (src->parse[i]);
  src->n_parse = src->parse_ofs = 0;
}

static struct lex_source *
lex_source_create (struct lexer *lexer, struct lex_reader *reader)
{
  size_t allocated_lines = 4;
  size_t *lines = xmalloc (allocated_lines * sizeof *lines);
  *lines = 0;

  struct lex_source *src = xmalloc (sizeof *src);
  *src = (struct lex_source) {
    .n_refs = 1,
    .reader = reader,
    .segmenter = segmenter_init (reader->syntax, false),
    .lexer = lexer,
    .lines = lines,
    .n_lines = 1,
    .allocated_lines = allocated_lines,
  };

  lex_source_push_endcmd__ (src);

  return src;
}

void
lex_set_message_handler (struct lexer *lexer,
                         void (*output_msg) (const struct msg *,
                                             struct lexer *))
{
  struct msg_handler msg_handler = {
    .output_msg = (void (*)(const struct msg *, void *)) output_msg,
    .aux = lexer,
    .lex_source_ref = lex_source_ref,
    .lex_source_unref = lex_source_unref,
    .lex_source_get_line = lex_source_get_line,
  };
  msg_set_handler (&msg_handler);
}

struct lex_source *
lex_source_ref (const struct lex_source *src_)
{
  struct lex_source *src = CONST_CAST (struct lex_source *, src_);
  if (src)
    {
      assert (src->n_refs > 0);
      src->n_refs++;
    }
  return src;
}

void
lex_source_unref (struct lex_source *src)
{
  if (!src)
    return;

  assert (src->n_refs > 0);
  if (--src->n_refs > 0)
    return;

  char *file_name = src->reader->file_name;
  char *encoding = src->reader->encoding;
  if (src->reader->class->destroy != NULL)
    src->reader->class->destroy (src->reader);
  free (file_name);
  free (encoding);
  free (src->buffer);
  free (src->lines);
  lex_stage_uninit (&src->pp);
  lex_stage_uninit (&src->merge);
  lex_source_clear_parse (src);
  free (src->parse);
  free (src);
}

struct lex_file_reader
  {
    struct lex_reader reader;
    struct u8_istream *istream;
  };

static struct lex_reader_class lex_file_reader_class;

/* Creates and returns a new lex_reader that will read from file FILE_NAME (or
   from stdin if FILE_NAME is "-").  The file is expected to be encoded with
   ENCODING, which should take one of the forms accepted by
   u8_istream_for_file().  SYNTAX and ERROR become the syntax mode and error
   mode of the new reader, respectively.

   Returns a null pointer if FILE_NAME cannot be opened. */
struct lex_reader *
lex_reader_for_file (const char *file_name, const char *encoding,
                     enum segmenter_mode syntax,
                     enum lex_error_mode error)
{
  struct lex_file_reader *r;
  struct u8_istream *istream;

  istream = (!strcmp(file_name, "-")
             ? u8_istream_for_fd (encoding, STDIN_FILENO)
             : u8_istream_for_file (encoding, file_name, O_RDONLY));
  if (istream == NULL)
    {
      msg (ME, _("Opening `%s': %s."), file_name, strerror (errno));
      return NULL;
    }

  r = xmalloc (sizeof *r);
  lex_reader_init (&r->reader, &lex_file_reader_class);
  r->reader.syntax = syntax;
  r->reader.error = error;
  r->reader.file_name = xstrdup (file_name);
  r->reader.encoding = xstrdup_if_nonnull (encoding);
  r->reader.line_number = 1;
  r->istream = istream;

  return &r->reader;
}

static struct lex_file_reader *
lex_file_reader_cast (struct lex_reader *r)
{
  return UP_CAST (r, struct lex_file_reader, reader);
}

static size_t
lex_file_read (struct lex_reader *r_, char *buf, size_t n,
               enum prompt_style prompt_style UNUSED)
{
  struct lex_file_reader *r = lex_file_reader_cast (r_);
  ssize_t n_read = u8_istream_read (r->istream, buf, n);
  if (n_read < 0)
    {
      msg (ME, _("Error reading `%s': %s."), r_->file_name, strerror (errno));
      return 0;
    }
  return n_read;
}

static void
lex_file_close (struct lex_reader *r_)
{
  struct lex_file_reader *r = lex_file_reader_cast (r_);

  if (u8_istream_fileno (r->istream) != STDIN_FILENO)
    {
      if (u8_istream_close (r->istream) != 0)
        msg (ME, _("Error closing `%s': %s."), r_->file_name, strerror (errno));
    }
  else
    u8_istream_free (r->istream);

  free (r);
}

static struct lex_reader_class lex_file_reader_class =
  {
    lex_file_read,
    lex_file_close
  };

struct lex_string_reader
  {
    struct lex_reader reader;
    struct substring s;
    size_t offset;
  };

static struct lex_reader_class lex_string_reader_class;

/* Creates and returns a new lex_reader for the contents of S, which must be
   encoded in the given ENCODING.  The new reader takes ownership of S and will free it
   with ss_dealloc() when it is closed. */
struct lex_reader *
lex_reader_for_substring_nocopy (struct substring s, const char *encoding)
{
  struct lex_string_reader *r;

  r = xmalloc (sizeof *r);
  lex_reader_init (&r->reader, &lex_string_reader_class);
  r->reader.syntax = SEG_MODE_AUTO;
  r->reader.encoding = xstrdup_if_nonnull (encoding);
  r->s = s;
  r->offset = 0;

  return &r->reader;
}

/* Creates and returns a new lex_reader for a copy of null-terminated string S,
   which must be encoded in ENCODING.  The caller retains ownership of S. */
struct lex_reader *
lex_reader_for_string (const char *s, const char *encoding)
{
  return lex_reader_for_substring_nocopy (ss_clone (ss_cstr (s)), encoding);
}

/* Formats FORMAT as a printf()-like format string and creates and returns a
   new lex_reader for the formatted result.  */
struct lex_reader *
lex_reader_for_format (const char *format, const char *encoding, ...)
{
  struct lex_reader *r;
  va_list args;

  va_start (args, encoding);
  r = lex_reader_for_substring_nocopy (ss_cstr (xvasprintf (format, args)), encoding);
  va_end (args);

  return r;
}

static struct lex_string_reader *
lex_string_reader_cast (struct lex_reader *r)
{
  return UP_CAST (r, struct lex_string_reader, reader);
}

static size_t
lex_string_read (struct lex_reader *r_, char *buf, size_t n,
                 enum prompt_style prompt_style UNUSED)
{
  struct lex_string_reader *r = lex_string_reader_cast (r_);
  size_t chunk;

  chunk = MIN (n, r->s.length - r->offset);
  memcpy (buf, r->s.string + r->offset, chunk);
  r->offset += chunk;

  return chunk;
}

static void
lex_string_close (struct lex_reader *r_)
{
  struct lex_string_reader *r = lex_string_reader_cast (r_);

  ss_dealloc (&r->s);
  free (r);
}

static struct lex_reader_class lex_string_reader_class =
  {
    lex_string_read,
    lex_string_close
  };

struct substring
lex_source_get_line (const struct lex_source *src, int line)
{
  if (line < 1 || line > src->n_lines)
    return ss_empty ();

  size_t ofs = src->lines[line - 1];
  size_t end;
  if (line < src->n_lines)
    end = src->lines[line];
  else
    {
      const char *newline = memchr (src->buffer + ofs, '\n', src->length - ofs);
      end = newline ? newline - src->buffer : src->length;
    }
  return ss_buffer (&src->buffer[ofs], end - ofs);
}
