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
#include <uniwidth.h>

#include "language/command.h"
#include "language/lexer/macro.h"
#include "language/lexer/scan.h"
#include "language/lexer/segment.h"
#include "language/lexer/token.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/deque.h"
#include "libpspp/i18n.h"
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
       call.

       src->tail <= line_pos <= token_pos <= src->head. */
    size_t token_pos;           /* Start of token. */
    size_t token_len;           /* Length of source for token in bytes. */
    size_t line_pos;            /* Start of line containing token_pos. */
    int first_line;             /* Line number at token_pos. */

    /* For a token obtained through macro expansion, this is just this token.

       For a token obtained through the lexer in an ordinary way, these are
       nulls and zeros. */
    char *macro_rep;        /* The whole macro expansion. */
    size_t ofs;             /* Offset of this token in macro_rep. */
    size_t len;             /* Length of this token in macro_rep. */
    size_t *ref_cnt;        /* Number of lex_tokens that refer to macro_rep. */
  };

static void
lex_token_uninit (struct lex_token *t)
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
}

/* A source of tokens, corresponding to a syntax file.

   This is conceptually a lex_reader wrapped with everything needed to convert
   its UTF-8 bytes into tokens. */
struct lex_source
  {
    struct ll ll;               /* In lexer's list of sources. */
    struct lex_reader *reader;
    struct lexer *lexer;
    struct segmenter segmenter;
    bool eof;                   /* True if T_STOP was read from 'reader'. */

    /* Buffer of UTF-8 bytes. */
    char *buffer;
    size_t allocated;           /* Number of bytes allocated. */
    size_t tail;                /* &buffer[0] offset into UTF-8 source. */
    size_t head;                /* &buffer[head - tail] offset into source. */

    /* Positions in source file, tail <= pos <= head for each member here. */
    size_t journal_pos;         /* First byte not yet output to journal. */
    size_t seg_pos;             /* First byte not yet scanned as token. */
    size_t line_pos;            /* First byte of line containing seg_pos. */

    int n_newlines;             /* Number of new-lines up to seg_pos. */
    bool suppress_next_newline;

    /* Tokens.

       This is mostly like a deque, with the invariant that 'back <= middle <=
       front' (modulo SIZE_MAX+1).  The tokens available for parsing are
       between 'back' and 'middle': the token at 'back' is the current token,
       the token at 'back + 1' is the next token, and so on.  There are usually
       no tokens between 'middle' and 'front'; if there are, then they need to
       go through macro expansion and are not yet available for parsing.

       'capacity' is the current number of elements in 'tokens'.  It is always
       a power of 2.  'front', 'middle', and 'back' refer to indexes in
       'tokens' modulo 'capacity'. */
    size_t front;
    size_t middle;
    size_t back;
    size_t capacity;
    size_t mask;                /* capacity - 1 */
    struct lex_token *tokens;
  };

static struct lex_source *lex_source_create (struct lexer *,
                                             struct lex_reader *);
static void lex_source_destroy (struct lex_source *);

/* Lexer. */
struct lexer
  {
    struct ll_list sources;     /* Contains "struct lex_source"s. */
    struct macro_set *macros;
  };

static struct lex_source *lex_source__ (const struct lexer *);
static char *lex_source_get_syntax__ (const struct lex_source *,
                                      int n0, int n1);
static const struct lex_token *lex_next__ (const struct lexer *, int n);
static void lex_source_push_endcmd__ (struct lex_source *);

static void lex_source_pop_back (struct lex_source *);
static bool lex_source_get (const struct lex_source *);
static void lex_source_error_valist (struct lex_source *, int n0, int n1,
                                     const char *format, va_list)
   PRINTF_FORMAT (4, 0);
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

      ll_for_each_safe (source, next, struct lex_source, ll, &lexer->sources)
        lex_source_destroy (source);
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

/* Adds a new token at the front of SRC and returns a pointer to it.  The
   caller should initialize it.  Does not advance the middle pointer, so the
   token isn't immediately available to the parser. */
static struct lex_token *
lex_push_token__ (struct lex_source *src)
{
  if (src->front - src->back >= src->capacity)
    {
      /* Expansion works just like a deque, so we reuse the code. */
      struct deque deque = {
        .capacity = src->capacity,
        .front = src->front,
        .back = src->back,
      };
      src->tokens = deque_expand (&deque, src->tokens, sizeof *src->tokens);
      src->capacity = deque.capacity;
      src->mask = src->capacity - 1;
    }

  struct lex_token *token = &src->tokens[src->front++ & src->mask];
  token->token = (struct token) { .type = T_STOP };
  token->macro_rep = NULL;
  token->ref_cnt = NULL;
  return token;
}

/* Removes the current token from SRC and uninitializes it. */
static void
lex_source_pop_back (struct lex_source *src)
{
  assert (src->middle - src->back > 0);
  lex_token_uninit (&src->tokens[src->back++ & src->mask]);
}

/* Removes the token at the greatest lookahead from SRC and uninitializes
   it. */
static void
lex_source_pop_front (struct lex_source *src)
{
  assert (src->front - src->middle > 0);
  lex_token_uninit (&src->tokens[--src->front & src->mask]);
}

/* Advances LEXER to the next token, consuming the current token. */
void
lex_get (struct lexer *lexer)
{
  struct lex_source *src;

  src = lex_source__ (lexer);
  if (src == NULL)
    return;

  if (src->middle - src->back > 0)
    lex_source_pop_back (src);

  while (src->back == src->middle)
    if (!lex_source_get (src))
      {
        lex_source_destroy (src);
        src = lex_source__ (lexer);
        if (src == NULL)
          return;
      }
}

/* Issuing errors. */

/* Prints a syntax error message containing the current token and
   given message MESSAGE (if non-null). */
void
lex_error (struct lexer *lexer, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  lex_next_error_valist (lexer, 0, 0, format, args);
  va_end (args);
}

/* Prints a syntax error message containing the current token and
   given message MESSAGE (if non-null). */
void
lex_error_valist (struct lexer *lexer, const char *format, va_list args)
{
  lex_next_error_valist (lexer, 0, 0, format, args);
}

/* Prints a syntax error message containing the current token and
   given message MESSAGE (if non-null). */
void
lex_next_error (struct lexer *lexer, int n0, int n1, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  lex_next_error_valist (lexer, n0, n1, format, args);
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
  enum { MAX_OPTIONS = 9 };
  const char *options[MAX_OPTIONS];
  int n = 0;
  while (n < MAX_OPTIONS)
    {
      const char *option = va_arg (args, const char *);
      if (!option)
        break;

      options[n++] = option;
    }
  lex_error_expecting_array (lexer, options, n);
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
      lex_error (lexer, _("expecting %s"), options[0]);
      break;

    case 2:
      lex_error (lexer, _("expecting %s or %s"), options[0], options[1]);
      break;

    case 3:
      lex_error (lexer, _("expecting %s, %s, or %s"), options[0], options[1],
                 options[2]);
      break;

    case 4:
      lex_error (lexer, _("expecting %s, %s, %s, or %s"),
                 options[0], options[1], options[2], options[3]);
      break;

    case 5:
      lex_error (lexer, _("expecting %s, %s, %s, %s, or %s"),
                 options[0], options[1], options[2], options[3], options[4]);
      break;

    case 6:
      lex_error (lexer, _("expecting %s, %s, %s, %s, %s, or %s"),
                 options[0], options[1], options[2], options[3], options[4],
                 options[5]);
      break;

    case 7:
      lex_error (lexer, _("expecting %s, %s, %s, %s, %s, %s, or %s"),
                 options[0], options[1], options[2], options[3], options[4],
                 options[5], options[6]);
      break;

    case 8:
      lex_error (lexer, _("expecting %s, %s, %s, %s, %s, %s, %s, or %s"),
                 options[0], options[1], options[2], options[3], options[4],
                 options[5], options[6], options[7]);
      break;

    default:
      lex_error (lexer, NULL);
    }
}

/* Reports an error to the effect that subcommand SBC may only be specified
   once.

   This function does not take a lexer as an argument or use lex_error(),
   because the result would ordinarily just be redundant: "Syntax error at
   SUBCOMMAND: Subcommand SUBCOMMAND may only be specified once.", which does
   not help the user find the error. */
void
lex_sbc_only_once (const char *sbc)
{
  msg (SE, _("Subcommand %s may only be specified once."), sbc);
}

/* Reports an error to the effect that subcommand SBC is missing.

   This function does not take a lexer as an argument or use lex_error(),
   because a missing subcommand can normally be detected only after the whole
   command has been parsed, and so lex_error() would always report "Syntax
   error at end of command", which does not help the user find the error. */
void
lex_sbc_missing (const char *sbc)
{
  msg (SE, _("Required subcommand %s was not specified."), sbc);
}

/* Reports an error to the effect that specification SPEC may only be specified
   once within subcommand SBC. */
void
lex_spec_only_once (struct lexer *lexer, const char *sbc, const char *spec)
{
  lex_error (lexer, _("%s may only be specified once within subcommand %s"),
             spec, sbc);
}

/* Reports an error to the effect that specification SPEC is missing within
   subcommand SBC. */
void
lex_spec_missing (struct lexer *lexer, const char *sbc, const char *spec)
{
  lex_error (lexer, _("Required %s specification missing from %s subcommand"),
             sbc, spec);
}

/* Prints a syntax error message containing the current token and
   given message MESSAGE (if non-null). */
void
lex_next_error_valist (struct lexer *lexer, int n0, int n1,
                       const char *format, va_list args)
{
  struct lex_source *src = lex_source__ (lexer);

  if (src != NULL)
    lex_source_error_valist (src, n0, n1, format, args);
  else
    {
      struct string s;

      ds_init_empty (&s);
      ds_put_format (&s, _("Syntax error at end of input"));
      if (format != NULL)
        {
          ds_put_cstr (&s, ": ");
          ds_put_vformat (&s, format, args);
        }
      ds_put_byte (&s, '.');
      msg (SE, "%s", ds_cstr (&s));
      ds_destroy (&s);
    }
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
      lex_error (lexer, _("expecting end of command"));
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
      lex_error (lexer, _("expecting string"));
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
      lex_error (lexer, _("expecting integer"));
      return false;
    }
}

/* If the current token is an integer in the range MIN...MAX (inclusive), does
   nothing and returns true.  Otherwise, reports an error and returns false.
   If NAME is nonnull, then it is used in the error message. */
bool
lex_force_int_range (struct lexer *lexer, const char *name, long min, long max)
{
  bool is_integer = lex_is_integer (lexer);
  bool too_small = is_integer && lex_integer (lexer) < min;
  bool too_big = is_integer && lex_integer (lexer) > max;
  if (is_integer && !too_small && !too_big)
    return true;

  if (min > max)
    {
      /* Weird, maybe a bug in the caller.  Just report that we needed an
         integer. */
      if (name)
        lex_error (lexer, _("Integer expected for %s."), name);
      else
        lex_error (lexer, _("Integer expected."));
    }
  else if (min == max)
    {
      if (name)
        lex_error (lexer, _("Expected %ld for %s."), min, name);
      else
        lex_error (lexer, _("Expected %ld."), min);
    }
  else if (min + 1 == max)
    {
      if (name)
        lex_error (lexer, _("Expected %ld or %ld for %s."), min, min + 1, name);
      else
        lex_error (lexer, _("Expected %ld or %ld."), min, min + 1);
    }
  else
    {
      bool report_lower_bound = (min > INT_MIN / 2) || too_small;
      bool report_upper_bound = (max < INT_MAX / 2) || too_big;

      if (report_lower_bound && report_upper_bound)
        {
          if (name)
            lex_error (lexer,
                       _("Expected integer between %ld and %ld for %s."),
                       min, max, name);
          else
            lex_error (lexer, _("Expected integer between %ld and %ld."),
                       min, max);
        }
      else if (report_lower_bound)
        {
          if (min == 0)
            {
              if (name)
                lex_error (lexer, _("Expected non-negative integer for %s."),
                           name);
              else
                lex_error (lexer, _("Expected non-negative integer."));
            }
          else if (min == 1)
            {
              if (name)
                lex_error (lexer, _("Expected positive integer for %s."),
                           name);
              else
                lex_error (lexer, _("Expected positive integer."));
            }
        }
      else if (report_upper_bound)
        {
          if (name)
            lex_error (lexer,
                       _("Expected integer less than or equal to %ld for %s."),
                       max, name);
          else
            lex_error (lexer, _("Expected integer less than or equal to %ld."),
                       max);
        }
      else
        {
          if (name)
            lex_error (lexer, _("Integer expected for %s."), name);
          else
            lex_error (lexer, _("Integer expected."));
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

  lex_error (lexer, _("expecting number"));
  return false;
}

/* If the current token is an identifier, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_id (struct lexer *lexer)
{
  if (lex_token (lexer) == T_ID)
    return true;

  lex_error (lexer, _("expecting identifier"));
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

/* Returns the token in SRC with the greatest lookahead. */
static const struct lex_token *
lex_source_middle (const struct lex_source *src)
{
  assert (src->middle - src->back > 0);
  return &src->tokens[(src->middle - 1) & src->mask];
}

static const struct lex_token *
lex_source_next__ (const struct lex_source *src, int n)
{
  while (src->middle - src->back <= n)
    {
      if (src->middle - src->back > 0)
        {
          const struct lex_token *middle = lex_source_middle (src);
          if (middle->token.type == T_STOP || middle->token.type == T_ENDCMD)
            return middle;
        }

      lex_source_get (src);
    }

  return &src->tokens[(src->back + n) & src->mask];
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

/* Returns the text of the syntax in tokens N0 ahead of the current one,
   through N1 ahead of the current one, inclusive.  (For example, if N0 and N1
   are both zero, this requests the syntax for the current token.)  The caller
   must eventually free the returned string (with free()).  The syntax is
   encoded in UTF-8 and in the original form supplied to the lexer so that, for
   example, it may include comments, spaces, and new-lines if it spans multiple
   tokens.  Macro expansion, however, has already been performed. */
char *
lex_next_representation (const struct lexer *lexer, int n0, int n1)
{
  return lex_source_get_syntax__ (lex_source__ (lexer), n0, n1);
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

/* If LEXER is positioned at the sequence of tokens that may be parsed from S,
   skips it and returns true.  Otherwise, returns false.

   S may consist of an arbitrary sequence of tokens, e.g. "KRUSKAL-WALLIS",
   "2SLS", or "END INPUT PROGRAM".  Identifiers may be abbreviated to their
   first three letters. */
bool
lex_match_phrase (struct lexer *lexer, const char *s)
{
  struct string_lexer slex;
  struct token token;
  int i;

  i = 0;
  string_lexer_init (&slex, s, strlen (s), SEG_MODE_INTERACTIVE, true);
  while (string_lexer_next (&slex, &token))
    if (token.type != SCAN_SKIP)
      {
        bool match = lex_tokens_match (lex_next (lexer, i++), &token);
        token_uninit (&token);
        if (!match)
          return false;
      }

  while (i-- > 0)
    lex_get (lexer);
  return true;
}

static int
lex_source_get_first_line_number (const struct lex_source *src, int n)
{
  return lex_source_next__ (src, n)->first_line;
}

static int
count_newlines (char *s, size_t length)
{
  int n_newlines = 0;
  char *newline;

  while ((newline = memchr (s, '\n', length)) != NULL)
    {
      n_newlines++;
      length -= (newline + 1) - s;
      s = newline + 1;
    }

  return n_newlines;
}

static int
lex_source_get_last_line_number (const struct lex_source *src, int n)
{
  const struct lex_token *token = lex_source_next__ (src, n);

  if (token->first_line == 0)
    return 0;
  else
    {
      char *token_str = &src->buffer[token->token_pos - src->tail];
      return token->first_line + count_newlines (token_str, token->token_len) + 1;
    }
}

static int
count_columns (const char *s_, size_t length)
{
  const uint8_t *s = CHAR_CAST (const uint8_t *, s_);
  int columns;
  size_t ofs;
  int mblen;

  columns = 0;
  for (ofs = 0; ofs < length; ofs += mblen)
    {
      ucs4_t uc;

      mblen = u8_mbtouc (&uc, s + ofs, length - ofs);
      if (uc != '\t')
        {
          int width = uc_width (uc, "UTF-8");
          if (width > 0)
            columns += width;
        }
      else
        columns = ROUND_UP (columns + 1, 8);
    }

  return columns + 1;
}

static int
lex_source_get_first_column (const struct lex_source *src, int n)
{
  const struct lex_token *token = lex_source_next__ (src, n);
  return count_columns (&src->buffer[token->line_pos - src->tail],
                        token->token_pos - token->line_pos);
}

static int
lex_source_get_last_column (const struct lex_source *src, int n)
{
  const struct lex_token *token = lex_source_next__ (src, n);
  char *start, *end, *newline;

  start = &src->buffer[token->line_pos - src->tail];
  end = &src->buffer[(token->token_pos + token->token_len) - src->tail];
  newline = memrchr (start, '\n', end - start);
  if (newline != NULL)
    start = newline + 1;
  return count_columns (start, end - start);
}

/* Returns the 1-based line number of the start of the syntax that represents
   the token N after the current one in LEXER.  Returns 0 for a T_STOP token or
   if the token is drawn from a source that does not have line numbers. */
int
lex_get_first_line_number (const struct lexer *lexer, int n)
{
  const struct lex_source *src = lex_source__ (lexer);
  return src != NULL ? lex_source_get_first_line_number (src, n) : 0;
}

/* Returns the 1-based line number of the end of the syntax that represents the
   token N after the current one in LEXER, plus 1.  Returns 0 for a T_STOP
   token or if the token is drawn from a source that does not have line
   numbers.

   Most of the time, a single token is wholly within a single line of syntax,
   but there are two exceptions: a T_STRING token can be made up of multiple
   segments on adjacent lines connected with "+" punctuators, and a T_NEG_NUM
   token can consist of a "-" on one line followed by the number on the next.
 */
int
lex_get_last_line_number (const struct lexer *lexer, int n)
{
  const struct lex_source *src = lex_source__ (lexer);
  return src != NULL ? lex_source_get_last_line_number (src, n) : 0;
}

/* Returns the 1-based column number of the start of the syntax that represents
   the token N after the current one in LEXER.  Returns 0 for a T_STOP
   token.

   Column numbers are measured according to the width of characters as shown in
   a typical fixed-width font, in which CJK characters have width 2 and
   combining characters have width 0.  */
int
lex_get_first_column (const struct lexer *lexer, int n)
{
  const struct lex_source *src = lex_source__ (lexer);
  return src != NULL ? lex_source_get_first_column (src, n) : 0;
}

/* Returns the 1-based column number of the end of the syntax that represents
   the token N after the current one in LEXER, plus 1.  Returns 0 for a T_STOP
   token.

   Column numbers are measured according to the width of characters as shown in
   a typical fixed-width font, in which CJK characters have width 2 and
   combining characters have width 0.  */
int
lex_get_last_column (const struct lexer *lexer, int n)
{
  const struct lex_source *src = lex_source__ (lexer);
  return src != NULL ? lex_source_get_last_column (src, n) : 0;
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
  struct msg_location *loc = lex_get_lines (lexer, n0, n1);
  loc->first_column = lex_get_first_column (lexer, n0);
  loc->last_column = lex_get_last_column (lexer, n1);
  return loc;
}

/* Returns a newly allocated msg_location for the syntax that represents tokens
   with 0-based offsets N0...N1, inclusive, from the current token.  The
   location only covers the tokens' lines, not the columns.  The caller must
   eventually free the location (with msg_location_destroy()). */
struct msg_location *
lex_get_lines (const struct lexer *lexer, int n0, int n1)
{
  struct msg_location *loc = xmalloc (sizeof *loc);
  *loc = (struct msg_location) {
    .file_name = xstrdup_if_nonnull (lex_get_file_name (lexer)),
    .first_line = lex_get_first_line_number (lexer, n0),
    .last_line = lex_get_last_line_number (lexer, n1),
  };
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
      src->head = src->tail = 0;
      src->journal_pos = src->seg_pos = src->line_pos = 0;
      src->n_newlines = 0;
      src->suppress_next_newline = false;
      src->segmenter = segmenter_init (segmenter_get_mode (&src->segmenter),
                                       false);
      while (src->middle - src->back > 0)
        lex_source_pop_back (src);
      while (src->front - src->middle > 0)
        lex_source_pop_front (src);
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
      while (src->middle - src->back > 0)
        lex_source_pop_back (src);

      for (; src != NULL && src->reader->error != LEX_ERROR_TERMINAL;
           src = lex_source__ (lexer))
        lex_source_destroy (src);
    }
}

static size_t
lex_source_max_tail__ (const struct lex_source *src)
{
  const struct lex_token *token;
  size_t max_tail;

  assert (src->seg_pos >= src->line_pos);
  max_tail = MIN (src->journal_pos, src->line_pos);

  /* Use the oldest token also.  (We know that src->deque cannot be empty
     because we are in the process of adding a new token, which is already
     initialized enough to use here.) */
  token = &src->tokens[src->back & src->mask];
  assert (token->token_pos >= token->line_pos);
  max_tail = MIN (max_tail, token->line_pos);

  return max_tail;
}

static void
lex_source_expand__ (struct lex_source *src)
{
  if (src->head - src->tail >= src->allocated)
    {
      size_t max_tail = lex_source_max_tail__ (src);
      if (max_tail > src->tail)
        {
          /* Advance the tail, freeing up room at the head. */
          memmove (src->buffer, src->buffer + (max_tail - src->tail),
                   src->head - max_tail);
          src->tail = max_tail;
        }
      else
        {
          /* Buffer is completely full.  Expand it. */
          src->buffer = x2realloc (src->buffer, &src->allocated);
        }
    }
  else
    {
      /* There's space available at the head of the buffer.  Nothing to do. */
    }
}

static void
lex_source_read__ (struct lex_source *src)
{
  do
    {
      lex_source_expand__ (src);

      size_t head_ofs = src->head - src->tail;
      size_t space = src->allocated - head_ofs;
      enum prompt_style prompt = segmenter_get_prompt (&src->segmenter);
      size_t n = src->reader->class->read (src->reader, &src->buffer[head_ofs],
                                           space, prompt);
      assert (n <= space);

      if (n == 0)
        {
          /* End of input. */
          src->reader->eof = true;
          lex_source_expand__ (src);
          return;
        }

      src->head += n;
    }
  while (!memchr (&src->buffer[src->seg_pos - src->tail], '\n',
                  src->head - src->seg_pos));
}

static struct lex_source *
lex_source__ (const struct lexer *lexer)
{
  return (ll_is_empty (&lexer->sources) ? NULL
          : ll_data (ll_head (&lexer->sources), struct lex_source, ll));
}

/* Returns the text of the syntax in SRC for tokens N0 ahead of the current
   one, through N1 ahead of the current one, inclusive.  (For example, if N0
   and N1 are both zero, this requests the syntax for the current token.)  The
   caller must eventually free the returned string (with free()).  The syntax
   is encoded in UTF-8 and in the original form supplied to the lexer so that,
   for example, it may include comments, spaces, and new-lines if it spans
   multiple tokens.  Macro expansion, however, has already been performed. */
static char *
lex_source_get_syntax__ (const struct lex_source *src, int n0, int n1)
{
  struct string s = DS_EMPTY_INITIALIZER;
  for (size_t i = n0; i <= n1; )
    {
      /* Find [I,J) as the longest sequence of tokens not produced by macro
         expansion, or otherwise the longest sequence expanded from a single
         macro call. */
      const struct lex_token *first = lex_source_next__ (src, i);
      size_t j;
      for (j = i + 1; j <= n1; j++)
        {
          const struct lex_token *cur = lex_source_next__ (src, j);
          if ((first->macro_rep != NULL) != (cur->macro_rep != NULL)
              || first->macro_rep != cur->macro_rep)
            break;
        }
      const struct lex_token *last = lex_source_next__ (src, j - 1);

      /* Now add the syntax for this sequence of tokens to SRC. */
      if (!ds_is_empty (&s))
        ds_put_byte (&s, ' ');
      if (!first->macro_rep)
        {
          size_t start = first->token_pos;
          size_t end = last->token_pos + last->token_len;
          ds_put_substring (&s, ss_buffer (&src->buffer[start - src->tail],
                                           end - start));
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
lex_source_contains_macro_call (struct lex_source *src, int n0, int n1)
{
  for (size_t i = n0; i <= n1; i++)
    if (lex_source_next__ (src, i)->macro_rep)
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
lex_source_get_macro_call (struct lex_source *src, int n0, int n1)
{
  if (!lex_source_contains_macro_call (src, n0, n1))
    return ss_empty ();

  const struct lex_token *token0 = lex_source_next__ (src, n0);
  const struct lex_token *token1 = lex_source_next__ (src, MAX (n0, n1));
  size_t start = token0->token_pos;
  size_t end = token1->token_pos + token1->token_len;

  return ss_buffer (&src->buffer[start - src->tail], end - start);
}

static void
lex_source_error_valist (struct lex_source *src, int n0, int n1,
                         const char *format, va_list args)
{
  const struct lex_token *token;
  struct string s;

  ds_init_empty (&s);

  token = lex_source_next__ (src, n0);
  if (token->token.type == T_ENDCMD)
    ds_put_cstr (&s, _("Syntax error at end of command"));
  else
    {
      /* Get the syntax that caused the error. */
      char *raw_syntax = lex_source_get_syntax__ (src, n0, n1);
      char syntax[64];
      str_ellipsize (ss_cstr (raw_syntax), syntax, sizeof syntax);
      free (raw_syntax);

      /* Get the macro call(s) that expanded to the syntax that caused the
         error. */
      char call[64];
      str_ellipsize (lex_source_get_macro_call (src, n0, n1),
                     call, sizeof call);

      if (syntax[0])
        {
          if (call[0])
            ds_put_format (&s,
                           _("Syntax error at `%s' (in expansion of `%s')"),
                           syntax, call);
          else
            ds_put_format (&s, _("Syntax error at `%s'"), syntax);
        }
      else
        {
          if (call[0])
            ds_put_format (&s, _("Syntax error in syntax expanded from `%s'"),
                           call);
          else
            ds_put_cstr (&s, _("Syntax error"));
        }
    }

  if (format)
    {
      ds_put_cstr (&s, ": ");
      ds_put_vformat (&s, format, args);
    }
  if (ds_last (&s) != '.')
    ds_put_byte (&s, '.');

  struct msg_location *location = xmalloc (sizeof *location);
  *location = (struct msg_location) {
    .file_name = xstrdup_if_nonnull (src->reader->file_name),
    .first_line = lex_source_get_first_line_number (src, n0),
    .last_line = lex_source_get_last_line_number (src, n1),
    .first_column = lex_source_get_first_column (src, n0),
    .last_column = lex_source_get_last_column (src, n1),
  };
  struct msg *m = xmalloc (sizeof *m);
  *m = (struct msg) {
    .category = MSG_C_SYNTAX,
    .severity = MSG_S_ERROR,
    .location = location,
    .text = ds_steal_cstr (&s),
  };
  msg_emit (m);
}

static void PRINTF_FORMAT (4, 5)
lex_source_error (struct lex_source *src, int n0, int n1,
                  const char *format, ...)
{
  va_list args;
  va_start (args, format);
  lex_source_error_valist (src, n0, n1, format, args);
  va_end (args);
}

static void
lex_get_error (struct lex_source *src, const char *s)
{
  size_t old_middle = src->middle;
  src->middle = src->front;
  size_t n = src->front - src->back - 1;
  lex_source_error (src, n, n, "%s", s);
  src->middle = old_middle;

  lex_source_pop_front (src);
}

/* Attempts to append an additional token at the front of SRC, reading more
   from the underlying lex_reader if necessary.  Returns true if a new token
   was added to SRC's deque, false otherwise.  The caller should retry failures
   unless SRC's 'eof' marker was set to true indicating that there will be no
   more tokens from this source.

   Does not make the new token available for lookahead yet; the caller must
   adjust SRC's 'middle' pointer to do so. */
static bool
lex_source_try_get__ (struct lex_source *src)
{
  /* State maintained while scanning tokens.  Usually we only need a single
     state, but scanner_push() can return SCAN_SAVE to indicate that the state
     needs to be saved and possibly restored later with SCAN_BACK. */
  struct state
    {
      struct segmenter segmenter;
      enum segment_type last_segment;
      int newlines;             /* Number of newlines encountered so far. */
      /* Maintained here so we can update lex_source's similar members when we
         finish. */
      size_t line_pos;
      size_t seg_pos;
    };

  /* Initialize state. */
  struct state state =
    {
      .segmenter = src->segmenter,
      .newlines = 0,
      .seg_pos = src->seg_pos,
      .line_pos = src->line_pos,
    };
  struct state saved = state;

  /* Append a new token to SRC and initialize it. */
  struct lex_token *token = lex_push_token__ (src);
  struct scanner scanner;
  scanner_init (&scanner, &token->token);
  token->line_pos = src->line_pos;
  token->token_pos = src->seg_pos;
  if (src->reader->line_number > 0)
    token->first_line = src->reader->line_number + src->n_newlines;
  else
    token->first_line = 0;

  /* Extract segments and pass them through the scanner until we obtain a
     token. */
  for (;;)
    {
      /* Extract a segment. */
      const char *segment = &src->buffer[state.seg_pos - src->tail];
      size_t seg_maxlen = src->head - state.seg_pos;
      enum segment_type type;
      int seg_len = segmenter_push (&state.segmenter, segment, seg_maxlen,
                                    src->reader->eof, &type);
      if (seg_len < 0)
        {
          /* The segmenter needs more input to produce a segment. */
          assert (!src->reader->eof);
          lex_source_read__ (src);
          continue;
        }

      /* Update state based on the segment. */
      state.last_segment = type;
      state.seg_pos += seg_len;
      if (type == SEG_NEWLINE)
        {
          state.newlines++;
          state.line_pos = state.seg_pos;
        }

      /* Pass the segment into the scanner and try to get a token out. */
      enum scan_result result = scanner_push (&scanner, type,
                                              ss_buffer (segment, seg_len),
                                              &token->token);
      if (result == SCAN_SAVE)
        saved = state;
      else if (result == SCAN_BACK)
        {
          state = saved;
          break;
        }
      else if (result == SCAN_DONE)
        break;
    }

  /* If we've reached the end of a line, or the end of a command, then pass
     the line to the output engine as a syntax text item.  */
  int n_lines = state.newlines;
  if (state.last_segment == SEG_END_COMMAND && !src->suppress_next_newline)
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
      const char *line = &src->buffer[src->journal_pos - src->tail];

      /* Calculate line length, including \n or \r\n end-of-line if present.

         We use src->head even though that may be beyond what we've actually
         converted to tokens (which is only through state.line_pos).  That's
         because, if we're emitting the line due to SEG_END_COMMAND, we want to
         take the whole line through the newline, not just through the '.'. */
      size_t max_len = src->head - src->journal_pos;
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

  token->token_len = state.seg_pos - src->seg_pos;

  src->segmenter = state.segmenter;
  src->seg_pos = state.seg_pos;
  src->line_pos = state.line_pos;
  src->n_newlines += state.newlines;

  switch (token->token.type)
    {
    default:
      return true;

    case T_STOP:
      token->token.type = T_ENDCMD;
      src->eof = true;
      return true;

    case SCAN_BAD_HEX_LENGTH:
    case SCAN_BAD_HEX_DIGIT:
    case SCAN_BAD_UNICODE_DIGIT:
    case SCAN_BAD_UNICODE_LENGTH:
    case SCAN_BAD_UNICODE_CODE_POINT:
    case SCAN_EXPECTED_QUOTE:
    case SCAN_EXPECTED_EXPONENT:
    case SCAN_UNEXPECTED_CHAR:
      char *msg = scan_token_to_error (&token->token);
      lex_get_error (src, msg);
      free (msg);
      return false;

    case SCAN_SKIP:
      lex_source_pop_front (src);
      return false;
    }

  NOT_REACHED ();
}

/* Attempts to add a new token at the front of SRC.  Returns true if
   successful, false on failure.  On failure, the end of SRC has been reached
   and no more tokens will be forthcoming from it.

   Does not make the new token available for lookahead yet; the caller must
   adjust SRC's 'middle' pointer to do so. */
static bool
lex_source_get__ (struct lex_source *src)
{
  while (!src->eof)
    if (lex_source_try_get__ (src))
      return true;
  return false;
}

/* Attempts to obtain a new token for SRC, in particular expanding the number
   of lookahead tokens (the tokens between 'back' and 'middle').

   Returns true if successful, false on failure.  In the latter case, SRC is
   exhausted and 'src->eof' is now true. */
static bool
lex_source_get (const struct lex_source *src_)
{
  struct lex_source *src = CONST_CAST (struct lex_source *, src_);

  /* In the common case, call into the scanner and segmenter to obtain a new
     token between 'middle' and 'front'.  In the uncommon case, there can be one
     or a few tokens there already, leftovers from a macro expansion.

     If we call into the scanner and it fails, then we've hit EOF and we're
     done. */
  if (src->front - src->middle == 0 && !lex_source_get__ (src))
    return false;

  /* We have at least one token available between 'middle' and 'front'.

     The remaining complication is all about macro expansion.  If macro
     expansion is disabled, we're done.  */
  if (!settings_get_mexpand ())
    {
      src->middle++;
      return true;
    }

  /* Now pass tokens one-by-one to the macro expander.

     In the common case where there is no macro to expand, the loop is not
     entered.  */
  struct macro_call *mc;
  int n_call = macro_call_create (
    src->lexer->macros, &src->tokens[src->middle & src->mask].token,
    &mc);
  for (int middle_ofs = 1; !n_call; middle_ofs++)
    {
      if (src->front - src->middle <= middle_ofs && !lex_source_get__ (src))
        {
          /* This should not be reachable because we always get a T_ENDCMD at
             the end of an input file (transformed from T_STOP by
             lex_source_try_get__()) and the macro_expander should always
             terminate expansion on T_ENDCMD. */
          NOT_REACHED ();
        }

      const struct lex_token *t = &src->tokens[(src->middle + middle_ofs)
                                               & src->mask];
      size_t start = t->token_pos;
      size_t end = t->token_pos + t->token_len;
      const struct macro_token mt = {
        .token = t->token,
        .syntax = ss_buffer (&src->buffer[start - src->tail], end - start),
      };

      /* We temporarily add the tokens to the source to avoid re-entry if
         macro_expander_add() reports an error and to give better error
         messages. */
      src->middle += middle_ofs + 1;
      n_call = macro_call_add (mc, &mt);
      src->middle -= middle_ofs + 1;
    }
  if (n_call < 0)
    {
      /* False alarm: no macro expansion after all.  Use first token as
         lookahead.  We'll retry macro expansion from the second token next
         time around. */
      macro_call_destroy (mc);
      src->middle++;
      return true;
    }

  /* Now expand the macro.

     We temporarily add the macro call's tokens to the source in case the macro
     expansion calls msg() to report an error and error processing tries to get
     the location of the error with, e.g. lex_get_first_line_number(), which
     would re-enter this code.  This is a kluge; it might be cleaner to pass
     the line number into macro_expander_get_expansion(). */
  src->middle += n_call;
  struct macro_tokens expansion = { .n = 0 };
  macro_call_expand (mc, src->reader->syntax, &expansion);
  macro_call_destroy (mc);
  src->middle -= n_call;

  /* Convert the macro expansion into syntax for possible error messages later. */
  size_t *ofs = xnmalloc (expansion.n, sizeof *ofs);
  size_t *len = xnmalloc (expansion.n, sizeof *len);
  struct string s = DS_EMPTY_INITIALIZER;
  macro_tokens_to_syntax (&expansion, &s, ofs, len);

  if (settings_get_mprint ())
    output_item_submit (text_item_create (TEXT_ITEM_LOG, ds_cstr (&s),
                                          _("Macro Expansion")));

  /* The first 'n_call' tokens starting at 'middle' will be replaced by the
     macro expansion.  There might be more tokens after that, up to 'front'.

     Figure out the boundary of the macro call in the syntax, to go into the
     lex_tokens for the expansion so that later error messages can report what
     macro was called. */
  const struct lex_token *call_first = &src->tokens[src->middle & src->mask];
  const struct lex_token *call_last
    = &src->tokens[(src->middle + n_call - 1) & src->mask];
  size_t call_pos = call_first->token_pos;
  size_t call_len = (call_last->token_pos + call_last->token_len) - call_pos;
  size_t line_pos = call_first->line_pos;
  int first_line = call_first->first_line;

  /* Destroy the tokens for the call, and save any tokens following the call so
     we can add them back later. */
  for (size_t i = src->middle; i != src->middle + n_call; i++)
    lex_token_uninit (&src->tokens[i & src->mask]);
  size_t n_save = src->front - (src->middle + n_call);
  struct lex_token *save_tokens = xnmalloc (n_save, sizeof *save_tokens);
  for (size_t i = 0; i < n_save; i++)
    save_tokens[i] = src->tokens[(src->middle + n_call + i) & src->mask];
  src->front = src->middle;

  /* Append the macro expansion tokens to the lookahead. */
  char *macro_rep = ds_steal_cstr (&s);
  size_t *ref_cnt = xmalloc (sizeof *ref_cnt);
  *ref_cnt = expansion.n;
  for (size_t i = 0; i < expansion.n; i++)
    {
      *lex_push_token__ (src) = (struct lex_token) {
        .token = expansion.mts[i].token,
        .token_pos = call_pos,
        .token_len = call_len,
        .line_pos = line_pos,
        .first_line = first_line,
        .macro_rep = macro_rep,
        .ofs = ofs[i],
        .len = len[i],
        .ref_cnt = ref_cnt,
      };
      src->middle++;

      ss_dealloc (&expansion.mts[i].syntax);
    }
  free (expansion.mts);
  free (ofs);
  free (len);

  /* Finally, put the saved tokens back. */
  for (size_t i = 0; i < n_save; i++)
    *lex_push_token__ (src) = save_tokens[i];
  free (save_tokens);

  return true;
}

static void
lex_source_push_endcmd__ (struct lex_source *src)
{
  assert (src->back == src->middle && src->middle == src->front);
  *lex_push_token__ (src) = (struct lex_token) {
    .token = { .type = T_ENDCMD } };
  src->middle++;
}

static struct lex_source *
lex_source_create (struct lexer *lexer, struct lex_reader *reader)
{
  struct lex_source *src = xmalloc (sizeof *src);
  *src = (struct lex_source) {
    .reader = reader,
    .segmenter = segmenter_init (reader->syntax, false),
    .lexer = lexer,
  };

  lex_source_push_endcmd__ (src);

  return src;
}

static void
lex_source_destroy (struct lex_source *src)
{
  char *file_name = src->reader->file_name;
  char *encoding = src->reader->encoding;
  if (src->reader->class->destroy != NULL)
    src->reader->class->destroy (src->reader);
  free (file_name);
  free (encoding);
  free (src->buffer);
  while (src->middle - src->back > 0)
    lex_source_pop_back (src);
  while (src->front - src->middle > 0)
    lex_source_pop_front (src);
  free (src->tokens);
  ll_remove (&src->ll);
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
  struct substring ss;
  ss_alloc_substring (&ss, ss_cstr (s));
  return lex_reader_for_substring_nocopy (ss, encoding);
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
