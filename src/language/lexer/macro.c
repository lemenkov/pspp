/* PSPP - a program for statistical analysis.
   Copyright (C) 2021 Free Software Foundation, Inc.

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

#include "language/lexer/macro.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "data/settings.h"
#include "language/lexer/lexer.h"
#include "language/lexer/segment.h"
#include "language/lexer/scan.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"
#include "libpspp/stringi-map.h"
#include "libpspp/stringi-set.h"

#include "gl/c-ctype.h"
#include "gl/ftoastr.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* An entry in the stack of macros and macro directives being expanded.  The
   stack is maintained as a linked list.  Entries are not dynamically allocated
   but on the program stack.

   The outermost entry, where 'next' is NULL, represents the source location of
   the call to the macro. */
struct macro_expansion_stack
  {
    const struct macro_expansion_stack *next; /* Next outer stack entry. */
    const char *name;                    /* A macro name or !IF, !DO, etc. */
    const struct msg_location *location; /* Source location if available. */
  };

/* Reports an error during macro expansion.  STACK is the stack for reporting
   the location of the error, MT is the optional token at which the error was
   detected, and FORMAT along with the varargs is the message to report. */
static void PRINTF_FORMAT (3, 0)
macro_error_valist (const struct macro_expansion_stack *stack,
                    const struct macro_token *mt, const char *format,
                    va_list args)
{
  struct msg_stack **ms = NULL;
  size_t allocated_ms = 0;
  size_t n_ms = 0;

  const struct macro_expansion_stack *p;
  for (p = stack; p && p->next; p = p->next)
    {
      if (n_ms >= allocated_ms)
        ms = x2nrealloc (ms, &allocated_ms, sizeof *ms);

      /* TRANSLATORS: These strings are used for explaining the context of an
         error.  The "While expanding" message appears first, followed by zero
         or more of the "inside expansion" messages.  `innermost',
         `next_inner`, etc., are names of macros, and `foobar' is a piece of
         PSPP syntax:

         foo.sps:12: At `foobar' in the expansion of 'innermost',
         foo.sps:23: inside the expansion of 'next_inner',
         foo.sps:34: inside the expansion of 'next_inner2',
         foo.sps:45: inside the expansion of 'outermost',
         foo.sps:76: This is the actual error message. */
      char *description;
      if (p == stack)
        {
          if (mt && mt->syntax.length)
            {
              char syntax[64];
              str_ellipsize (mt->syntax, syntax, sizeof syntax);
              description = xasprintf (_("At `%s' in the expansion of `%s',"),
                                       syntax, p->name);
            }
          else
            description = xasprintf (_("In the expansion of `%s',"), p->name);
        }
      else
        description = xasprintf (_("inside the expansion of `%s',"), p->name);

      ms[n_ms] = xmalloc (sizeof *ms[n_ms]);
      *ms[n_ms] = (struct msg_stack) {
        .location = msg_location_dup (p->location),
        .description = description,
      };
      n_ms++;
    }

  struct msg *m = xmalloc (sizeof *m);
  *m = (struct msg) {
    .category = MSG_C_SYNTAX,
    .severity = MSG_S_ERROR,
    .stack = ms,
    .n_stack = n_ms,
    .location = msg_location_dup (p ? p->location : NULL),
    .text = xvasprintf (format, args),
  };
  msg_emit (m);
}

/* Reports an error during macro expansion.  STACK is the stack for reporting
   the location of the error, MT is the optional token at which the error was
   detected, and FORMAT along with the varargs is the message to report. */
static void PRINTF_FORMAT (3, 4)
macro_error (const struct macro_expansion_stack *stack,
             const struct macro_token *mt, const char *format, ...)
{
  va_list args;
  va_start (args, format);
  macro_error_valist (stack, mt, format, args);
  va_end (args);
}

void
macro_token_copy (struct macro_token *dst, const struct macro_token *src)
{
  token_copy (&dst->token, &src->token);
  dst->syntax = ss_clone (src->syntax);
}

void
macro_token_uninit (struct macro_token *mt)
{
  token_uninit (&mt->token);
  ss_dealloc (&mt->syntax);
}

void
macro_token_to_syntax (struct macro_token *mt, struct string *s)
{
  ds_put_substring (s, mt->syntax);
}
bool
is_macro_keyword (struct substring s)
{
  static struct stringi_set keywords = STRINGI_SET_INITIALIZER (keywords);
  if (stringi_set_is_empty (&keywords))
    {
      static const char *kws[] = {
        "BREAK",
        "CHAREND",
        "CMDEND",
        "DEFAULT",
        "DO",
        "DOEND",
        "ELSE",
        "ENCLOSE",
        "ENDDEFINE",
        "IF",
        "IFEND",
        "IN",
        "LET",
        "NOEXPAND",
        "OFFEXPAND",
        "ONEXPAND",
        "POSITIONAL",
        "THEN",
        "TOKENS",
      };
      for (size_t i = 0; i < sizeof kws / sizeof *kws; i++)
        stringi_set_insert (&keywords, kws[i]);
    }

  ss_ltrim (&s, ss_cstr ("!"));
  return stringi_set_contains_len (&keywords, s.string, s.length);
}

void
macro_tokens_copy (struct macro_tokens *dst, const struct macro_tokens *src)
{
  *dst = (struct macro_tokens) {
    .mts = xmalloc (src->n * sizeof *dst->mts),
    .n = src->n,
    .allocated = src->n,
  };
  for (size_t i = 0; i < src->n; i++)
    macro_token_copy (&dst->mts[i], &src->mts[i]);
}

void
macro_tokens_uninit (struct macro_tokens *mts)
{
  for (size_t i = 0; i < mts->n; i++)
    macro_token_uninit (&mts->mts[i]);
  free (mts->mts);
}

struct macro_token *
macro_tokens_add_uninit (struct macro_tokens *mts)
{
  if (mts->n >= mts->allocated)
    mts->mts = x2nrealloc (mts->mts, &mts->allocated, sizeof *mts->mts);
  return &mts->mts[mts->n++];
}

void
macro_tokens_add (struct macro_tokens *mts, const struct macro_token *mt)
{
  macro_token_copy (macro_tokens_add_uninit (mts), mt);
}

/* Tokenizes SRC according to MODE and appends the tokens to MTS, using STACK
   for error reporting. */
static void
macro_tokens_from_string (struct macro_tokens *mts, const struct substring src,
                          enum segmenter_mode mode,
                          const struct macro_expansion_stack *stack)
{
  struct segmenter segmenter = segmenter_init (mode, true);
  struct substring body = src;

  while (body.length > 0)
    {
      enum segment_type type;
      int seg_len = segmenter_push (&segmenter, body.string,
                                    body.length, true, &type);
      assert (seg_len >= 0);

      struct macro_token mt = {
        .token = { .type = T_STOP },
        .syntax = ss_head (body, seg_len),
      };
      enum tokenize_result result
        = token_from_segment (type, mt.syntax, &mt.token);
      ss_advance (&body, seg_len);

      switch (result)
        {
        case TOKENIZE_EMPTY:
          break;

        case TOKENIZE_TOKEN:
          macro_tokens_add (mts, &mt);
          break;

        case TOKENIZE_ERROR:
          macro_error (stack, &mt, "%s", mt.token.string.string);
          break;
        }

      token_uninit (&mt.token);
    }
}

void
macro_tokens_print (const struct macro_tokens *mts, FILE *stream)
{
  for (size_t i = 0; i < mts->n; i++)
    token_print (&mts->mts[i].token, stream);
}

enum token_class
  {
    TC_ENDCMD,                  /* No space before or after (new-line after). */
    TC_BINOP,                   /* Space on both sides. */
    TC_COMMA,                   /* Space afterward. */
    TC_ID,                      /* Don't need spaces except sequentially. */
    TC_PUNCT,                   /* Don't need spaces except sequentially. */
  };

static bool
needs_space (enum token_class prev, enum token_class next)
{
  /* Don't need a space before or after the end of a command.
     (A new-line is needed afterward as a special case.) */
  if (prev == TC_ENDCMD || next == TC_ENDCMD)
    return false;

  /* Binary operators always have a space on both sides. */
  if (prev == TC_BINOP || next == TC_BINOP)
    return true;

  /* A comma always has a space afterward. */
  if (prev == TC_COMMA)
    return true;

  /* Otherwise, PREV is TC_ID or TC_PUNCT, which only need a space if there are
     two or them in a row. */
  return prev == next;
}

static enum token_class
classify_token (enum token_type type)
{
  switch (type)
    {
    case T_ID:
    case T_MACRO_ID:
    case T_POS_NUM:
    case T_NEG_NUM:
    case T_STRING:
      return TC_ID;

    case T_STOP:
      return TC_PUNCT;

    case T_ENDCMD:
      return TC_ENDCMD;

    case T_LPAREN:
    case T_RPAREN:
    case T_LBRACK:
    case T_RBRACK:
    case T_LCURLY:
    case T_RCURLY:
      return TC_PUNCT;

    case T_PLUS:
    case T_DASH:
    case T_ASTERISK:
    case T_SLASH:
    case T_EQUALS:
    case T_COLON:
    case T_AND:
    case T_OR:
    case T_NOT:
    case T_EQ:
    case T_GE:
    case T_GT:
    case T_LE:
    case T_LT:
    case T_NE:
    case T_ALL:
    case T_BY:
    case T_TO:
    case T_WITH:
    case T_EXP:
    case T_MACRO_PUNCT:
      return TC_BINOP;

    case T_COMMA:
    case T_SEMICOLON:
      return TC_COMMA;
    }

  NOT_REACHED ();
}

/* Appends syntax for the tokens in MTS to S.  If OFS and LEN are nonnull, sets
   OFS[i] to the offset within S of the start of token 'i' in MTS and LEN[i] to
   its length.  OFS[i] + LEN[i] is not necessarily OFS[i + 1] because some
   tokens are separated by white space.  */
void
macro_tokens_to_syntax (struct macro_tokens *mts, struct string *s,
                        size_t *ofs, size_t *len)
{
  assert ((ofs != NULL) == (len != NULL));

  if (!mts->n)
    return;

  for (size_t i = 0; i < mts->n; i++)
    {
      if (i > 0)
        {
          enum token_type prev = mts->mts[i - 1].token.type;
          enum token_type next = mts->mts[i].token.type;

          if (prev == T_ENDCMD)
            ds_put_byte (s, '\n');
          else
            {
              enum token_class pc = classify_token (prev);
              enum token_class nc = classify_token (next);
              if (needs_space (pc, nc))
                ds_put_byte (s, ' ');
            }
        }

      if (ofs)
        ofs[i] = s->ss.length;
      macro_token_to_syntax (&mts->mts[i], s);
      if (len)
        len[i] = s->ss.length - ofs[i];
    }
}

void
macro_destroy (struct macro *m)
{
  if (!m)
    return;

  free (m->name);
  msg_location_destroy (m->location);
  for (size_t i = 0; i < m->n_params; i++)
    {
      struct macro_param *p = &m->params[i];
      free (p->name);

      macro_tokens_uninit (&p->def);
      token_uninit (&p->start);
      token_uninit (&p->end);
    }
  free (m->params);
  macro_tokens_uninit (&m->body);
  free (m);
}

struct macro_set *
macro_set_create (void)
{
  struct macro_set *set = xmalloc (sizeof *set);
  *set = (struct macro_set) {
    .macros = HMAP_INITIALIZER (set->macros),
  };
  return set;
}

void
macro_set_destroy (struct macro_set *set)
{
  if (!set)
    return;

  struct macro *macro, *next;
  HMAP_FOR_EACH_SAFE (macro, next, struct macro, hmap_node, &set->macros)
    {
      hmap_delete (&set->macros, &macro->hmap_node);
      macro_destroy (macro);
    }
  hmap_destroy (&set->macros);
  free (set);
}

static unsigned int
hash_macro_name (const char *name)
{
  return utf8_hash_case_string (name, 0);
}

static struct macro *
macro_set_find__ (struct macro_set *set, const char *name)
{
  if (macro_set_is_empty (set))
    return NULL;

  struct macro *macro;
  HMAP_FOR_EACH_WITH_HASH (macro, struct macro, hmap_node,
                           hash_macro_name (name), &set->macros)
    if (!utf8_strcasecmp (macro->name, name))
      return macro;

  return NULL;
}

const struct macro *
macro_set_find (const struct macro_set *set, const char *name)
{
  return macro_set_find__ (CONST_CAST (struct macro_set *, set), name);
}

/* Adds M to SET.  M replaces any existing macro with the same name.  Takes
   ownership of M. */
void
macro_set_add (struct macro_set *set, struct macro *m)
{
  struct macro *victim = macro_set_find__ (set, m->name);
  if (victim)
    {
      hmap_delete (&set->macros, &victim->hmap_node);
      macro_destroy (victim);
    }

  hmap_insert (&set->macros, &m->hmap_node, hash_macro_name (m->name));
}

/* Macro call parsing. */

enum mc_state
  {
    /* Accumulating tokens in mc->params toward the end of any type of
       argument. */
    MC_ARG,

    /* Expecting the opening delimiter of an ARG_ENCLOSE argument. */
    MC_ENCLOSE,

    /* Expecting a keyword for a keyword argument. */
    MC_KEYWORD,

    /* Expecting an equal sign for a keyword argument. */
    MC_EQUALS,

    /* Macro fully parsed and ready for expansion. */
    MC_FINISHED,
  };

/* Parsing macro calls.  This is a FSM driven by macro_call_create() and
   macro_call_add() to identify the macro being called and obtain its
   arguments.  'state' identifies the FSM state. */
struct macro_call
  {
    const struct macro_set *macros;
    const struct macro *macro;
    struct macro_tokens **args;
    const struct macro_expansion_stack *stack;
    const struct macro_expander *me;

    enum mc_state state;
    size_t n_tokens;
    const struct macro_param *param; /* Parameter currently being parsed. */
  };

static bool macro_expand_arg (const struct token *,
                              const struct macro_expander *,
                              struct macro_tokens *exp);

/* Completes macro expansion by initializing arguments that weren't supplied to
   their defaults. */
static int
mc_finished (struct macro_call *mc)
{
  mc->state = MC_FINISHED;
  for (size_t i = 0; i < mc->macro->n_params; i++)
    if (!mc->args[i])
      mc->args[i] = &mc->macro->params[i].def;
  return mc->n_tokens;
}

static int
mc_next_arg (struct macro_call *mc)
{
  if (!mc->param)
    {
      assert (!mc->macro->n_params);
      return mc_finished (mc);
    }
  else if (mc->param->positional)
    {
      mc->param++;
      if (mc->param >= &mc->macro->params[mc->macro->n_params])
        return mc_finished (mc);
      else
        {
          mc->state = (!mc->param->positional ? MC_KEYWORD
                       : mc->param->arg_type == ARG_ENCLOSE ? MC_ENCLOSE
                       : MC_ARG);
          return 0;
        }
    }
  else
    {
      for (size_t i = 0; i < mc->macro->n_params; i++)
        if (!mc->args[i])
          {
            mc->state = MC_KEYWORD;
            return 0;
          }
      return mc_finished (mc);
    }
}

static void PRINTF_FORMAT (3, 4)
mc_error (const struct macro_call *mc, const struct msg_location *loc,
          const char *format, ...)
{
  va_list args;
  va_start (args, format);
  if (!mc->stack)
    {
      const struct macro_expansion_stack stack = { .location = loc };
      macro_error_valist (&stack, NULL, format, args);
    }
  else
    macro_error_valist (mc->stack, NULL, format, args);
  va_end (args);
}

static int
mc_add_arg (struct macro_call *mc, const struct macro_token *mt,
            const struct msg_location *loc)
{
  const struct macro_param *p = mc->param;
  struct macro_tokens **argp = &mc->args[p - mc->macro->params];

  const struct token *token = &mt->token;
  if (token->type == T_ENDCMD || token->type == T_STOP)
    {
      if (*argp)
        {
          switch (p->arg_type)
            {
            case ARG_CMDEND:
              /* This is OK, it's the expected way to end the argument. */
              break;

            case ARG_N_TOKENS:
              mc_error (mc, loc,
                        ngettext (_("Reached end of command expecting %zu "
                                    "more token in argument %s to macro %s."),
                                  _("Reached end of command expecting %zu "
                                    "more tokens in argument %s to macro %s."),
                                  p->n_tokens - (*argp)->n),
                        p->n_tokens - (*argp)->n, p->name, mc->macro->name);
              break;

            case ARG_CHAREND:
            case ARG_ENCLOSE:
              {
                char *end = token_to_string (&p->end);
                mc_error (mc, loc, _("Reached end of command expecting \"%s\" "
                                     "in argument %s to macro %s."),
                          end, p->name, mc->macro->name);
                free (end);
              }
              break;
            }
        }

      /* The end of a command ends the current argument, precludes any further
         arguments, and is not itself part of the argument. */
      return mc_finished (mc);
    }

  mc->n_tokens++;

  if (!*argp)
    *argp = xzalloc (sizeof **argp);

  bool add_token;               /* Should we add 'mt' to the current arg? */
  bool next_arg;                /* Should we advance to the next arg? */
  switch (p->arg_type)
    {
    case ARG_N_TOKENS:
      next_arg = (*argp)->n + 1 >= p->n_tokens;
      add_token = true;
      break;

    case ARG_CHAREND:
    case ARG_ENCLOSE:
      next_arg = token_equal (token, &p->end);
      add_token = !next_arg;
      break;

    case ARG_CMDEND:
      next_arg = false;
      add_token = true;
      break;

    default:
      NOT_REACHED ();
    }

  if (add_token)
    {
      if (!macro_expand_arg (&mt->token, mc->me, *argp))
        macro_tokens_add (*argp, mt);
    }
  return next_arg ? mc_next_arg (mc) : 0;
}

static int
mc_expected (struct macro_call *mc, const struct macro_token *actual,
             const struct msg_location *loc, const struct token *expected)
{
  const struct substring actual_s = (actual->syntax.length ? actual->syntax
                                     : ss_cstr (_("<end of input>")));
  char *expected_s = token_to_string (expected);
  mc_error (mc, loc,
            _("Found `%.*s' while expecting `%s' reading argument %s "
              "to macro %s."),
            (int) actual_s.length, actual_s.string, expected_s,
            mc->param->name, mc->macro->name);
  free (expected_s);

  return mc_finished (mc);
}

static int
mc_enclose (struct macro_call *mc, const struct macro_token *mt,
            const struct msg_location *loc)
{
  const struct token *token = &mt->token;
  const struct macro_param *p = mc->param;
  if (token_equal (&p->start, token))
    {
      mc->n_tokens++;

      struct macro_tokens **argp = &mc->args[p - mc->macro->params];
      if (!*argp)
        *argp = xzalloc (sizeof **argp);
      mc->state = MC_ARG;
      return 0;
    }
  else if (p->positional && (token->type == T_ENDCMD || token->type == T_STOP))
    return mc_finished (mc);
  else
    return mc_expected (mc, mt, loc, &p->start);
}

static const struct macro_param *
macro_find_parameter_by_name (const struct macro *m, struct substring name)
{
  if (!m)
    return NULL;

  ss_ltrim (&name, ss_cstr ("!"));

  for (size_t i = 0; i < m->n_params; i++)
    {
      const struct macro_param *p = &m->params[i];
      struct substring p_name = ss_cstr (p->name + 1);
      if (!utf8_strncasecmp (p_name.string, p_name.length,
                             name.string, name.length))
        return p;
    }
  return NULL;
}

static int
mc_keyword (struct macro_call *mc, const struct macro_token *mt,
            const struct msg_location *loc)
{
  const struct token *token = &mt->token;
  if (token->type != T_ID)
    return mc_finished (mc);

  const struct macro_param *p = macro_find_parameter_by_name (mc->macro,
                                                              token->string);
  if (p)
    {
      struct macro_tokens **argp = &mc->args[p - mc->macro->params];
      if (*argp)
        mc_error (mc, loc,
                  _("Argument %s multiply specified in call to macro %s."),
                  p->name, mc->macro->name);

      *argp = xzalloc (sizeof **argp);
      mc->param = p;
      mc->n_tokens++;
      mc->state = MC_EQUALS;
      return 0;
    }

  return mc_finished (mc);
}

static int
mc_equals (struct macro_call *mc, const struct macro_token *mt,
           const struct msg_location *loc)
{
  if (mt->token.type == T_EQUALS)
    {
      mc->n_tokens++;
      mc->state = mc->param->arg_type == ARG_ENCLOSE ? MC_ENCLOSE : MC_ARG;
      return 0;
    }

  return mc_expected (mc, mt, loc, &(struct token) { .type = T_EQUALS });
}

static int
macro_call_create__ (const struct macro_set *macros,
                     const struct macro_expansion_stack *stack,
                     const struct macro_expander *me,
                     const struct token *token,
                     struct macro_call **mcp)
{
  const struct macro *macro = (token->type == T_ID || token->type == T_MACRO_ID
                               ? macro_set_find (macros, token->string.string)
                               : NULL);
  if (!macro)
    {
      *mcp = NULL;
      return -1;
    }

  struct macro_call *mc = xmalloc (sizeof *mc);
  *mc = (struct macro_call) {
    .macros = macros,
    .macro = macro,
    .n_tokens = 1,
    .state = (!macro->n_params ? MC_FINISHED
              : !macro->params[0].positional ? MC_KEYWORD
              : macro->params[0].arg_type == ARG_ENCLOSE ? MC_ENCLOSE
              : MC_ARG),
    .args = macro->n_params ? xcalloc (macro->n_params, sizeof *mc->args) : NULL,
    .param = macro->params,
    .stack = stack,
    .me = me,
  };
  *mcp = mc;

  return mc->state == MC_FINISHED ? 1 : 0;
}

/* If TOKEN is the first token of a call to a macro in MACROS, create a new
   macro expander, initializes *MCP to it.  Returns 0 if more tokens are needed
   and should be added via macro_call_add() or 1 if the caller should next call
   macro_call_expand().

   If TOKEN is not the first token of a macro call, returns -1 and sets *MCP to
   NULL. */
int
macro_call_create (const struct macro_set *macros,
                   const struct token *token,
                   struct macro_call **mcp)
{
  return macro_call_create__ (macros, NULL, NULL, token, mcp);
}

void
macro_call_destroy (struct macro_call *mc)
{
  if (!mc)
    return;

  for (size_t i = 0; i < mc->macro->n_params; i++)
    {
      struct macro_tokens *a = mc->args[i];
      if (a && a != &mc->macro->params[i].def)
        {
          macro_tokens_uninit (a);
          free (a);
        }
    }
  free (mc->args);
  free (mc);
}

/* Adds TOKEN to the collection of tokens in MC that potentially need to be
   macro expanded.

   Returns -1 if the tokens added do not actually invoke a macro.  The caller
   should consume the first token without expanding it.  (Later tokens might
   invoke a macro so it's best to feed the second token into a new expander.)

   Returns 0 if the macro expander needs more tokens, for macro arguments or to
   decide whether this is actually a macro invocation.  The caller should call
   macro_call_add() again with the next token.

   Returns a positive number to indicate that the returned number of tokens
   invoke a macro.  The number returned might be less than the number of tokens
   added because it can take a few tokens of lookahead to determine whether the
   macro invocation is finished.  The caller should call macro_call_expand() to
   obtain the expansion. */
int
macro_call_add (struct macro_call *mc, const struct macro_token *mt,
                const struct msg_location *loc)
{
  switch (mc->state)
    {
    case MC_ARG:
      return mc_add_arg (mc, mt, loc);

    case MC_ENCLOSE:
      return mc_enclose (mc, mt, loc);

    case MC_KEYWORD:
      return mc_keyword (mc, mt, loc);

    case MC_EQUALS:
      return mc_equals (mc, mt, loc);

    default:
      NOT_REACHED ();
    }
}

/* Macro expansion. */

struct macro_expander
  {
    /* Always available. */
    const struct macro_set *macros;     /* Macros to expand recursively. */
    enum segmenter_mode segmenter_mode; /* Mode for tokenization. */
    int nesting_countdown;              /* Remaining nesting levels. */
    const struct macro_expansion_stack *stack; /* Stack for error reporting. */
    bool *expand;                       /* May macro calls be expanded? */
    struct stringi_map *vars;           /* Variables from !do and !let. */

    /* Only nonnull if inside a !DO loop. */
    bool *break_;                       /* Set to true to break out of loop. */

    /* Only nonnull if expanding a macro (and not, say, a macro argument). */
    const struct macro *macro;
    struct macro_tokens **args;
  };

static void
macro_expand (const struct macro_token *mts, size_t n_mts,
              const struct macro_expander *, struct macro_tokens *);

static size_t
expand_macro_function (const struct macro_expander *me,
                       const struct macro_token *input, size_t n_input,
                       struct string *output);

/* Parses one function argument from the N_INPUT tokens in INPUT
   Each argument to a macro function is one of:

       - A quoted string or other single literal token.

       - An argument to the macro being expanded, e.g. !1 or a named argument.

       - !*.

       - A function invocation.

   Each function invocation yields a character sequence to be turned into a
   sequence of tokens.  The case where that character sequence is a single
   quoted string is an important special case.
*/
static size_t
parse_function_arg (const struct macro_expander *me,
                    const struct macro_token *input, size_t n_input,
                    struct string *farg)
{
  assert (n_input > 0);

  const struct token *token = &input[0].token;
  if (token->type == T_MACRO_ID && me->macro)
    {
      const struct macro_param *param = macro_find_parameter_by_name (
        me->macro, token->string);
      if (param)
        {
          size_t param_idx = param - me->macro->params;
          macro_tokens_to_syntax (me->args[param_idx], farg, NULL, NULL);
          return 1;
        }

      if (ss_equals (token->string, ss_cstr ("!*")))
        {
          for (size_t i = 0; i < me->macro->n_params; i++)
            {
              if (!me->macro->params[i].positional)
                break;
              if (i)
                ds_put_byte (farg, ' ');
              macro_tokens_to_syntax (me->args[i], farg, NULL, NULL);
            }
          return 1;
        }

      const char *var = stringi_map_find__ (me->vars,
                                            token->string.string,
                                            token->string.length);
      if (var)
        {
          ds_put_cstr (farg, var);
          return 1;
        }

      size_t n_function = expand_macro_function (me, input, n_input, farg);
      if (n_function)
        return n_function;
    }

  ds_put_substring (farg, input[0].syntax);
  return 1;
}

static size_t
parse_function_args (const struct macro_expander *me,
                     const struct macro_token *mts, size_t n,
                     const char *function,
                     struct string_array *args)
{
  assert (n >= 2 && mts[1].token.type == T_LPAREN);

  for (size_t i = 2; i < n; )
    {
      if (mts[i].token.type == T_RPAREN)
        return i + 1;

      struct string s = DS_EMPTY_INITIALIZER;
      i += parse_function_arg (me, mts + i, n - i, &s);
      string_array_append_nocopy (args, ds_steal_cstr (&s));

      if (i >= n)
        break;
      else if (mts[i].token.type == T_COMMA)
        i++;
      else if (mts[i].token.type != T_RPAREN)
        {
          macro_error (me->stack, &mts[i],
                       _("`,' or `)' expected in call to macro function %s."),
                       function);
          return 0;
        }
    }

  macro_error (me->stack, NULL, _("Missing `)' in call to macro function %s."),
               function);
  return 0;
}

static bool
unquote_string (const char *s, enum segmenter_mode segmenter_mode,
                struct string *content)
{
  struct string_lexer slex;
  string_lexer_init (&slex, s, strlen (s), segmenter_mode, true);

  struct token token1;
  if (string_lexer_next (&slex, &token1) != SLR_TOKEN
      || token1.type != T_STRING)
    {
      token_uninit (&token1);
      return false;
    }

  struct token token2;
  if (string_lexer_next (&slex, &token2) != SLR_END)
    {
      token_uninit (&token1);
      token_uninit (&token2);
      return false;
    }

  if (content)
    ds_put_substring (content, token1.string);
  token_uninit (&token1);
  return true;
}

static const char *
unquote_string_in_place (const char *s, enum segmenter_mode segmenter_mode,
                         struct string *tmp)
{
  ds_init_empty (tmp);
  return unquote_string (s, segmenter_mode, tmp) ? ds_cstr (tmp) : s;
}

static bool
parse_integer (const char *s, int *np)
{
  errno = 0;

  char *tail;
  long int n = strtol (s, &tail, 10);
  *np = n < INT_MIN ? INT_MIN : n > INT_MAX ? INT_MAX : n;
  tail += strspn (tail, CC_SPACES);
  return *tail == '\0' && errno != ERANGE && n == *np;
}

static size_t
expand_macro_function (const struct macro_expander *me,
                       const struct macro_token *input, size_t n_input,
                       struct string *output)
{
  if (!n_input || input[0].token.type != T_MACRO_ID)
    return 0;

  struct macro_function
    {
      const char *name;
      int min_args;
      int max_args;
    };
  enum macro_function_id
    {
      MF_BLANKS,
      MF_CONCAT,
      MF_EVAL,
      MF_HEAD,
      MF_INDEX,
      MF_LENGTH,
      MF_QUOTE,
      MF_SUBSTR,
      MF_TAIL,
      MF_UNQUOTE,
      MF_UPCASE,
    };
  static const struct macro_function mfs[] = {
    [MF_BLANKS]  = { "!BLANKS",  1, 1 },
    [MF_CONCAT]  = { "!CONCAT",  1, INT_MAX },
    [MF_EVAL]    = { "!EVAL",    1, 1 },
    [MF_HEAD]    = { "!HEAD",    1, 1 },
    [MF_INDEX]   = { "!INDEX",   2, 2 },
    [MF_LENGTH]  = { "!LENGTH",  1, 1 },
    [MF_QUOTE]   = { "!QUOTE",   1, 1 },
    [MF_SUBSTR]  = { "!SUBSTR",  2, 3 },
    [MF_TAIL]    = { "!TAIL",    1, 1 },
    [MF_UNQUOTE] = { "!UNQUOTE", 1, 1 },
    [MF_UPCASE]  = { "!UPCASE",  1, 1 },
  };

  if (lex_id_match_n (ss_cstr ("!NULL"), input[0].token.string, 4))
    return 1;

  if (n_input < 2 || input[1].token.type != T_LPAREN)
    {
      /* Only consider macro functions when the name is followed by '('. */
      return 0;
    }

  /* Is this a macro function name? */
  const struct macro_function *mf;
  for (mf = mfs; ; mf++)
    {
      if (mf >= mfs + sizeof mfs / sizeof *mfs)
        {
          /* Not a macro function. */
          return 0;
        }

      if (lex_id_match_n (ss_cstr (mf->name), input[0].token.string, 4))
        break;
    }

  enum macro_function_id id = mf - mfs;

  struct string_array args = STRING_ARRAY_INITIALIZER;
  size_t n_consumed = parse_function_args (me, input, n_input, mf->name, &args);
  if (!n_consumed)
    {
      string_array_destroy (&args);
      return 0;
    }

  if (args.n < mf->min_args || args.n > mf->max_args)
    {
      if (mf->min_args == 1 && mf->max_args == 1)
        macro_error (me->stack, NULL,
                     _("Macro function %s takes one argument (not %zu)."),
                     mf->name, args.n);
      else if (mf->min_args == 2 && mf->max_args == 2)
        macro_error (me->stack, NULL,
                     _("Macro function %s takes two arguments (not %zu)."),
                     mf->name, args.n);
      else if (mf->min_args == 2 && mf->max_args == 3)
        macro_error (me->stack, NULL,
                     _("Macro function %s takes two or three arguments "
                       "(not %zu)."),
                     mf->name, args.n);
      else if (mf->min_args == 1 && mf->max_args == INT_MAX)
        macro_error (me->stack, NULL,
                     _("Macro function %s needs at least one argument."),
                     mf->name);
      else
        NOT_REACHED ();
      string_array_destroy (&args);
      return 0;
    }

  switch (id)
    {
    case MF_LENGTH:
      ds_put_format (output, "%zu", strlen (args.strings[0]));
      break;

    case MF_BLANKS:
      {
        int n;
        if (!parse_integer (args.strings[0], &n))
          {
            macro_error (me->stack, NULL,
                         _("Argument to !BLANKS must be non-negative integer "
                           "(not \"%s\")."), args.strings[0]);
            string_array_destroy (&args);
            return 0;
          }

        ds_put_byte_multiple (output, ' ', n);
      }
      break;

    case MF_CONCAT:
      for (size_t i = 0; i < args.n; i++)
        if (!unquote_string (args.strings[i], me->segmenter_mode, output))
          ds_put_cstr (output, args.strings[i]);
      break;

    case MF_HEAD:
      {
        struct string tmp;
        const char *s = unquote_string_in_place (args.strings[0],
                                                 me->segmenter_mode, &tmp);

        struct macro_tokens mts = { .n = 0 };
        macro_tokens_from_string (&mts, ss_cstr (s), me->segmenter_mode,
                                  me->stack);
        if (mts.n > 0)
          ds_put_substring (output, mts.mts[0].syntax);
        macro_tokens_uninit (&mts);
        ds_destroy (&tmp);
      }
      break;

    case MF_INDEX:
      {
        const char *haystack = args.strings[0];
        const char *needle = strstr (haystack, args.strings[1]);
        ds_put_format (output, "%zu", needle ? needle - haystack + 1 : 0);
      }
      break;

    case MF_QUOTE:
      if (unquote_string (args.strings[0], me->segmenter_mode, NULL))
        ds_put_cstr (output, args.strings[0]);
      else
        {
          ds_extend (output, strlen (args.strings[0]) + 2);
          ds_put_byte (output, '\'');
          for (const char *p = args.strings[0]; *p; p++)
            {
              if (*p == '\'')
                ds_put_byte (output, '\'');
              ds_put_byte (output, *p);
            }
          ds_put_byte (output, '\'');
        }
      break;

    case MF_SUBSTR:
      {
        int start;
        if (!parse_integer (args.strings[1], &start) || start < 1)
          {
            macro_error (me->stack, NULL,
                         _("Second argument of !SUBSTR must be "
                           "positive integer (not \"%s\")."),
                         args.strings[1]);
            string_array_destroy (&args);
            return 0;
          }

        int count = INT_MAX;
        if (args.n > 2 && (!parse_integer (args.strings[2], &count) || count < 0))
          {
            macro_error (me->stack, NULL,
                         _("Third argument of !SUBSTR must be "
                           "non-negative integer (not \"%s\")."),
                         args.strings[2]);
            string_array_destroy (&args);
            return 0;
          }

        struct substring s = ss_cstr (args.strings[0]);
        ds_put_substring (output, ss_substr (s, start - 1, count));
      }
      break;

    case MF_TAIL:
      {
        struct string tmp;
        const char *s = unquote_string_in_place (args.strings[0],
                                                 me->segmenter_mode, &tmp);

        struct macro_tokens mts = { .n = 0 };
        macro_tokens_from_string (&mts, ss_cstr (s), me->segmenter_mode,
                                  me->stack);
        if (mts.n > 1)
          {
            struct macro_tokens tail = { .mts = mts.mts + 1, .n = mts.n - 1 };
            macro_tokens_to_syntax (&tail, output, NULL, NULL);
          }
        macro_tokens_uninit (&mts);
        ds_destroy (&tmp);
      }
      break;

    case MF_UNQUOTE:
      if (!unquote_string (args.strings[0], me->segmenter_mode, output))
        ds_put_cstr (output, args.strings[0]);
      break;

    case MF_UPCASE:
      {
        struct string tmp;
        const char *s = unquote_string_in_place (args.strings[0],
                                                 me->segmenter_mode, &tmp);
        char *upper = utf8_to_upper (s);
        ds_put_cstr (output, upper);
        free (upper);
        ds_destroy (&tmp);
      }
      break;

    case MF_EVAL:
      {
        struct macro_tokens mts = { .n = 0 };
        macro_tokens_from_string (&mts, ss_cstr (args.strings[0]),
                                  me->segmenter_mode, me->stack);
        struct macro_tokens exp = { .n = 0 };
        struct macro_expansion_stack stack = {
          .name = "!EVAL",
          .next = me->stack
        };
        struct macro_expander subme = *me;
        subme.break_ = NULL;
        subme.stack = &stack;

        macro_expand (mts.mts, mts.n, &subme, &exp);
        macro_tokens_to_syntax (&exp, output, NULL, NULL);
        macro_tokens_uninit (&exp);
        macro_tokens_uninit (&mts);
      }
      break;

    default:
      NOT_REACHED ();
    }

  string_array_destroy (&args);
  return n_consumed;
}

static char *macro_evaluate_or (const struct macro_expander *me,
                                const struct macro_token **tokens,
                                const struct macro_token *end);

static char *
macro_evaluate_literal (const struct macro_expander *me,
                        const struct macro_token **tokens,
                        const struct macro_token *end)
{
  const struct macro_token *p = *tokens;
  if (p >= end)
    return NULL;
  if (p->token.type == T_LPAREN)
    {
      p++;
      char *value = macro_evaluate_or (me, &p, end);
      if (!value)
        return NULL;
      if (p >= end || p->token.type != T_RPAREN)
        {
          free (value);
          macro_error (me->stack, p < end ? p : NULL,
                       _("Expecting ')' in macro expression."));
          return NULL;
        }
      p++;
      *tokens = p;
      return value;
    }
  else if (p->token.type == T_RPAREN)
    {
      macro_error (me->stack, p, _("Expecting literal or function invocation "
                                   "in macro expression."));
      return NULL;
    }

  struct string function_output = DS_EMPTY_INITIALIZER;
  size_t function_consumed = parse_function_arg (me, p, end - p,
                                                 &function_output);
  struct string unquoted = DS_EMPTY_INITIALIZER;
  if (unquote_string (ds_cstr (&function_output), me->segmenter_mode,
                      &unquoted))
    {
      ds_swap (&function_output, &unquoted);
      ds_destroy (&unquoted);
    }
  *tokens = p + function_consumed;
  return ds_steal_cstr (&function_output);
}

/* Returns true if MT is valid as a macro operator.  Only operators written as
   symbols (e.g. <>) are usable in macro expressions, not operator written as
   letters (e.g. EQ). */
static bool
is_macro_operator (const struct macro_token *mt)
{
  return mt->syntax.length > 0 && !c_isalpha (mt->syntax.string[0]);
}

static enum token_type
parse_relational_op (const struct macro_token *mt)
{
  switch (mt->token.type)
    {
    case T_EQUALS:
      return T_EQ;

    case T_NE:
    case T_LT:
    case T_GT:
    case T_LE:
    case T_GE:
      return is_macro_operator (mt) ? mt->token.type : T_STOP;

    case T_MACRO_ID:
      return (ss_equals_case (mt->token.string, ss_cstr ("!EQ")) ? T_EQ
              : ss_equals_case (mt->token.string, ss_cstr ("!NE")) ? T_NE
              : ss_equals_case (mt->token.string, ss_cstr ("!LT")) ? T_LT
              : ss_equals_case (mt->token.string, ss_cstr ("!GT")) ? T_GT
              : ss_equals_case (mt->token.string, ss_cstr ("!LE")) ? T_LE
              : ss_equals_case (mt->token.string, ss_cstr ("!GE")) ? T_GE
              : T_STOP);

    default:
      return T_STOP;
    }
}

static char *
macro_evaluate_relational (const struct macro_expander *me,
                           const struct macro_token **tokens,
                           const struct macro_token *end)
{
  const struct macro_token *p = *tokens;
  char *lhs = macro_evaluate_literal (me, &p, end);
  if (!lhs)
    return NULL;

  enum token_type op = p >= end ? T_STOP : parse_relational_op (p);
  if (op == T_STOP)
    {
      *tokens = p;
      return lhs;
    }
  p++;

  char *rhs = macro_evaluate_literal (me, &p, end);
  if (!rhs)
    {
      free (lhs);
      return NULL;
    }

  struct string lhs_tmp, rhs_tmp;
  int cmp = strcmp (unquote_string_in_place (lhs, me->segmenter_mode,
                                             &lhs_tmp),
                    unquote_string_in_place (rhs, me->segmenter_mode,
                                             &rhs_tmp));
  ds_destroy (&lhs_tmp);
  ds_destroy (&rhs_tmp);

  free (lhs);
  free (rhs);

  bool b = (op == T_EQUALS || op == T_EQ ? !cmp
            : op == T_NE ? cmp
            : op == T_LT ? cmp < 0
            : op == T_GT ? cmp > 0
            : op == T_LE ? cmp <= 0
            : /* T_GE */ cmp >= 0);

  *tokens = p;
  return xstrdup (b ? "1" : "0");
}

static char *
macro_evaluate_not (const struct macro_expander *me,
                    const struct macro_token **tokens,
                    const struct macro_token *end)
{
  const struct macro_token *p = *tokens;

  unsigned int negations = 0;
  while (p < end
         && (ss_equals_case (p->syntax, ss_cstr ("!NOT"))
             || ss_equals (p->syntax, ss_cstr ("~"))))
    {
      p++;
      negations++;
    }

  char *operand = macro_evaluate_relational (me, &p, end);
  if (!operand || !negations)
    {
      *tokens = p;
      return operand;
    }

  bool b = strcmp (operand, "0") ^ (negations & 1);
  free (operand);
  *tokens = p;
  return xstrdup (b ? "1" : "0");
}

static char *
macro_evaluate_and (const struct macro_expander *me,
                    const struct macro_token **tokens,
                    const struct macro_token *end)
{
  const struct macro_token *p = *tokens;
  char *lhs = macro_evaluate_not (me, &p, end);
  if (!lhs)
    return NULL;

  while (p < end
         && (ss_equals_case (p->syntax, ss_cstr ("!AND"))
             || ss_equals (p->syntax, ss_cstr ("&"))))
    {
      p++;
      char *rhs = macro_evaluate_not (me, &p, end);
      if (!rhs)
        {
          free (lhs);
          return NULL;
        }

      bool b = strcmp (lhs, "0") && strcmp (rhs, "0");
      free (lhs);
      free (rhs);
      lhs = xstrdup (b ? "1" : "0");
    }
  *tokens = p;
  return lhs;
}

static char *
macro_evaluate_or (const struct macro_expander *me,
                   const struct macro_token **tokens,
                   const struct macro_token *end)
{
  const struct macro_token *p = *tokens;
  char *lhs = macro_evaluate_and (me, &p, end);
  if (!lhs)
    return NULL;

  while (p < end
         && (ss_equals_case (p->syntax, ss_cstr ("!OR"))
             || ss_equals (p->syntax, ss_cstr ("|"))))
    {
      p++;
      char *rhs = macro_evaluate_and (me, &p, end);
      if (!rhs)
        {
          free (lhs);
          return NULL;
        }

      bool b = strcmp (lhs, "0") || strcmp (rhs, "0");
      free (lhs);
      free (rhs);
      lhs = xstrdup (b ? "1" : "0");
    }
  *tokens = p;
  return lhs;
}

static char *
macro_evaluate_expression (const struct macro_token **tokens, size_t n_tokens,
                           const struct macro_expander *me)
{
  return macro_evaluate_or (me, tokens, *tokens + n_tokens);
}

static bool
macro_evaluate_number (const struct macro_token **tokens, size_t n_tokens,
                       const struct macro_expander *me,
                       double *number)
{
  char *s = macro_evaluate_expression (tokens, n_tokens, me);
  if (!s)
    return false;

  struct macro_tokens mts = { .n = 0 };
  macro_tokens_from_string (&mts, ss_cstr (s), me->segmenter_mode, me->stack);
  if (mts.n != 1 || !token_is_number (&mts.mts[0].token))
    {
      macro_error (me->stack, mts.n > 0 ? &mts.mts[0] : NULL,
                   _("Macro expression must evaluate to "
                     "a number (not \"%s\")."), s);
      free (s);
      macro_tokens_uninit (&mts);
      return false;
    }

  *number = token_number (&mts.mts[0].token);
  free (s);
  macro_tokens_uninit (&mts);
  return true;
}

static const struct macro_token *
find_ifend_clause (const struct macro_token *p, const struct macro_token *end)
{
  size_t nesting = 0;
  for (; p < end; p++)
    {
      if (p->token.type != T_MACRO_ID)
        continue;

      if (ss_equals_case (p->token.string, ss_cstr ("!IF")))
        nesting++;
      else if (lex_id_match_n (p->token.string, ss_cstr ("!IFEND"), 4))
        {
          if (!nesting)
            return p;
          nesting--;
        }
      else if (lex_id_match_n (p->token.string, ss_cstr ("!ELSE"), 4)
               && !nesting)
        return p;
    }
  return NULL;
}

static size_t
macro_expand_if (const struct macro_token *tokens, size_t n_tokens,
                 const struct macro_expander *me,
                 struct macro_tokens *exp)
{
  const struct macro_token *p = tokens;
  const struct macro_token *end = tokens + n_tokens;

  if (p >= end || !ss_equals_case (p->token.string, ss_cstr ("!IF")))
    return 0;

  p++;
  char *result = macro_evaluate_expression (&p, end - p, me);
  if (!result)
    return 0;
  bool b = strcmp (result, "0");
  free (result);

  if (p >= end
      || p->token.type != T_MACRO_ID
      || !lex_id_match_n (p->token.string, ss_cstr ("!THEN"), 4))
    {
      macro_error (me->stack, p < end ? p : NULL,
                   _("!THEN expected in macro !IF construct."));
      return 0;
    }

  const struct macro_token *start_then = p + 1;
  const struct macro_token *end_then = find_ifend_clause (start_then, end);
  if (!end_then)
    {
      macro_error (me->stack, NULL,
                   _("!ELSE or !IFEND expected in macro !IF construct."));
      return 0;
    }

  const struct macro_token *start_else, *end_if;
  if (lex_id_match_n (end_then->token.string, ss_cstr ("!ELSE"), 4))
    {
      start_else = end_then + 1;
      end_if = find_ifend_clause (start_else, end);
      if (!end_if
          || !lex_id_match_n (end_if->token.string, ss_cstr ("!IFEND"), 4))
        {
          macro_error (me->stack, end_if ? end_if : NULL,
                       _("!IFEND expected in macro !IF construct."));
          return 0;
        }
    }
  else
    {
      start_else = NULL;
      end_if = end_then;
    }

  const struct macro_token *start;
  size_t n;
  if (b)
    {
      start = start_then;
      n = end_then - start_then;
    }
  else if (start_else)
    {
      start = start_else;
      n = end_if - start_else;
    }
  else
    {
      start = NULL;
      n = 0;
    }

  if (n)
    {
      struct macro_expansion_stack stack = {
        .name = "!IF",
        .next = me->stack,
      };
      struct macro_expander subme = *me;
      subme.stack = &stack;
      macro_expand (start, n, &subme, exp);
    }
  return (end_if + 1) - tokens;
}

static size_t
macro_parse_let (const struct macro_token *tokens, size_t n_tokens,
                 const struct macro_expander *me)
{
  const struct macro_token *p = tokens;
  const struct macro_token *end = tokens + n_tokens;

  if (p >= end || !ss_equals_case (p->token.string, ss_cstr ("!LET")))
    return 0;
  p++;

  if (p >= end || p->token.type != T_MACRO_ID)
    {
      macro_error (me->stack, p < end ? p : NULL,
                   _("Expected macro variable name following !LET."));
      return 0;
    }
  const struct substring var_name = p->token.string;
  if (is_macro_keyword (var_name)
      || macro_find_parameter_by_name (me->macro, var_name))
    {
      macro_error (me->stack, p < end ? p : NULL,
                   _("Cannot use argument name or macro keyword "
                     "\"%.*s\" as !LET variable."),
                   (int) var_name.length, var_name.string);
      return 0;
    }
  p++;

  if (p >= end || p->token.type != T_EQUALS)
    {
      macro_error (me->stack, p < end ? p : NULL,
                   _("Expected `=' following !LET."));
      return 0;
    }
  p++;

  char *value = macro_evaluate_expression (&p, end - p, me);
  if (!value)
    return 0;

  stringi_map_replace_nocopy (me->vars, ss_xstrdup (var_name), value);
  return p - tokens;
}

static const struct macro_token *
find_doend (const struct macro_expansion_stack *stack,
            const struct macro_token *p, const struct macro_token *end)
{
  size_t nesting = 0;
  for (; p < end; p++)
    {
      if (p->token.type != T_MACRO_ID)
        continue;

      if (ss_equals_case (p->token.string, ss_cstr ("!DO")))
        nesting++;
      else if (lex_id_match_n (p->token.string, ss_cstr ("!DOEND"), 4))
        {
          if (!nesting)
            return p;
          nesting--;
        }
    }
  macro_error (stack, NULL, _("Missing !DOEND."));
  return NULL;
}

static size_t
macro_expand_do (const struct macro_token *tokens, size_t n_tokens,
                 const struct macro_expander *me,
                 struct macro_tokens *exp)
{
  const struct macro_token *p = tokens;
  const struct macro_token *end = tokens + n_tokens;

  if (p >= end || !ss_equals_case (p->token.string, ss_cstr ("!DO")))
    return 0;
  p++;

  if (p >= end || p->token.type != T_MACRO_ID)
    {
      macro_error (me->stack, p < end ? p : NULL,
                   _("Expected macro variable name following !DO."));
      return 0;
    }
  const struct substring var_name = p->token.string;
  if (is_macro_keyword (var_name)
      || macro_find_parameter_by_name (me->macro, var_name))
    {
      macro_error (me->stack, p, _("Cannot use argument name or macro "
                                   "keyword as !DO variable."));
      return 0;
    }
  p++;

  struct macro_expansion_stack substack = {
    .name = "!DO",
    .next = me->stack,
  };
  bool break_ = false;
  struct macro_expander subme = *me;
  subme.break_ = &break_;
  subme.stack = &substack;

  int miterate = settings_get_miterate ();
  if (p < end && p->token.type == T_MACRO_ID
      && ss_equals_case (p->token.string, ss_cstr ("!IN")))
    {
      p++;
      char *list = macro_evaluate_expression (&p, end - p, &subme);
      if (!list)
        return 0;

      struct macro_tokens items = { .n = 0 };
      macro_tokens_from_string (&items, ss_cstr (list), me->segmenter_mode,
                                me->stack);
      free (list);

      const struct macro_token *do_end = find_doend (subme.stack, p, end);
      if (!do_end)
        {
          macro_tokens_uninit (&items);
          return 0;
        }

      for (size_t i = 0; i < items.n && !break_; i++)
        {
          if (i >= miterate)
            {
              macro_error (&substack, NULL,
                           _("!DO loop over list exceeded "
                             "maximum number of iterations %d.  "
                             "(Use SET MITERATE to change the limit.)"),
                           miterate);
              break;
            }
          stringi_map_replace_nocopy (me->vars, ss_xstrdup (var_name),
                                      ss_xstrdup (items.mts[i].syntax));

          macro_expand (p, do_end - p, &subme, exp);
        }
      macro_tokens_uninit (&items);
      return do_end - tokens + 1;
    }
  else if (p < end && p->token.type == T_EQUALS)
    {
      p++;
      double first;
      if (!macro_evaluate_number (&p, end - p, &subme, &first))
        return 0;

      if (p >= end || p->token.type != T_MACRO_ID
          || !ss_equals_case (p->token.string, ss_cstr ("!TO")))
        {
          macro_error (subme.stack, p < end ? p : NULL,
                       _("Expected !TO in numerical !DO loop."));
          return 0;
        }
      p++;

      double last;
      if (!macro_evaluate_number (&p, end - p, &subme, &last))
        return 0;

      double by = 1.0;
      if (p < end && p->token.type == T_MACRO_ID
          && ss_equals_case (p->token.string, ss_cstr ("!BY")))
        {
          p++;
          if (!macro_evaluate_number (&p, end - p, &subme, &by))
            return 0;

          if (by == 0.0)
            {
              macro_error (subme.stack, NULL, _("!BY value cannot be zero."));
              return 0;
            }
        }

      const struct macro_token *do_end = find_doend (subme.stack, p, end);
      if (!do_end)
        return 0;
      if ((by > 0 && first <= last) || (by < 0 && first >= last))
        {
          int i = 0;
          for (double index = first;
               by > 0 ? (index <= last) : (index >= last) && !break_;
               index += by)
            {
              if (i++ > miterate)
                {
                  macro_error (subme.stack, NULL,
                               _("Numerical !DO loop exceeded "
                                 "maximum number of iterations %d.  "
                                 "(Use SET MITERATE to change the limit.)"),
                               miterate);
                  break;
                }

              char index_s[DBL_BUFSIZE_BOUND];
              c_dtoastr (index_s, sizeof index_s, 0, 0, index);
              stringi_map_replace_nocopy (me->vars, ss_xstrdup (var_name),
                                          xstrdup (index_s));

              macro_expand (p, do_end - p, &subme, exp);
            }
        }

      return do_end - tokens + 1;
    }
  else
    {
      macro_error (me->stack, p < end ? p : NULL,
                   _("Expected `=' or !IN in !DO loop."));
      return 0;
    }
}

static void
macro_expand_arg__ (const struct macro_expander *me, size_t idx,
                  struct macro_tokens *exp)
{
  const struct macro_param *param = &me->macro->params[idx];
  const struct macro_tokens *arg = me->args[idx];

  if (*me->expand && param->expand_arg)
    {
      struct stringi_map vars = STRINGI_MAP_INITIALIZER (vars);
      struct macro_expansion_stack stack = {
        .name = param->name,
        .next = me->stack,
      };
      struct macro_expander subme = {
        .macros = me->macros,
        .macro = NULL,
        .args = NULL,
        .segmenter_mode = me->segmenter_mode,
        .expand = me->expand,
        .break_ = NULL,
        .vars = &vars,
        .nesting_countdown = me->nesting_countdown,
        .stack = &stack,
      };
      macro_expand (arg->mts, arg->n, &subme, exp);
      stringi_map_destroy (&vars);
    }
  else
    for (size_t i = 0; i < arg->n; i++)
      macro_tokens_add (exp, &arg->mts[i]);
}

static bool
macro_expand_arg (const struct token *token, const struct macro_expander *me,
                  struct macro_tokens *exp)
{
  if (!me || token->type != T_MACRO_ID)
    return false;

  /* Macro arguments. */
  if (me->macro)
    {
      const struct macro_param *param = macro_find_parameter_by_name (
        me->macro, token->string);
      if (param)
        {
          macro_expand_arg__ (me, param - me->macro->params, exp);
          return true;
        }
      else if (ss_equals (token->string, ss_cstr ("!*")))
        {
          for (size_t j = 0; j < me->macro->n_params; j++)
            macro_expand_arg__ (me, j, exp);
          return true;
        }
    }

  /* Variables set by !DO or !LET. */
  const char *var = stringi_map_find__ (me->vars, token->string.string,
                                        token->string.length);
  if (var)
    {
      macro_tokens_from_string (exp, ss_cstr (var),
                                me->segmenter_mode, me->stack);
      return true;
    }

  return false;
}

static size_t
macro_expand__ (const struct macro_token *mts, size_t n,
                const struct macro_expander *me,
                struct macro_tokens *exp)
{
  const struct token *token = &mts[0].token;

  /* Recursive macro calls. */
  if (*me->expand)
    {
      struct macro_call *submc;
      int n_call = macro_call_create__ (me->macros, me->stack, me,
                                        token, &submc);
      for (size_t j = 1; !n_call; j++)
        {
          const struct macro_token endcmd
            = { .token = { .type = T_ENDCMD } };
          n_call = macro_call_add (submc, j < n ? &mts[j] : &endcmd, NULL);
        }
      if (n_call > 0)
        {
          struct stringi_map vars = STRINGI_MAP_INITIALIZER (vars);
          struct macro_expansion_stack stack = {
            .name = submc->macro->name,
            .location = submc->macro->location,
            .next = me->stack,
          };
          struct macro_expander subme = {
            .macros = submc->macros,
            .macro = submc->macro,
            .args = submc->args,
            .segmenter_mode = me->segmenter_mode,
            .expand = me->expand,
            .break_ = NULL,
            .vars = &vars,
            .nesting_countdown = me->nesting_countdown - 1,
            .stack = &stack,
          };
          const struct macro_tokens *body = &submc->macro->body;
          macro_expand (body->mts, body->n, &subme, exp);
          macro_call_destroy (submc);
          stringi_map_destroy (&vars);
          return n_call;
        }

      macro_call_destroy (submc);
    }

  if (token->type != T_MACRO_ID)
    {
      macro_tokens_add (exp, &mts[0]);
      return 1;
    }

  /* Parameters and macro variables. */
  if (macro_expand_arg (token, me, exp))
    return 1;

  /* Macro functions. */
  struct string function_output = DS_EMPTY_INITIALIZER;
  size_t n_function = expand_macro_function (me, mts, n, &function_output);
  if (n_function)
    {
      macro_tokens_from_string (exp, function_output.ss,
                                me->segmenter_mode, me->stack);
      ds_destroy (&function_output);

      return n_function;
    }

  size_t n_if = macro_expand_if (mts, n, me, exp);
  if (n_if > 0)
    return n_if;

  size_t n_let = macro_parse_let (mts, n, me);
  if (n_let > 0)
    return n_let;

  size_t n_do = macro_expand_do (mts, n, me, exp);
  if (n_do > 0)
    return n_do;

  if (lex_id_match_n (token->string, ss_cstr ("!break"), 4))
    {
      if (me->break_)
        *me->break_ = true;
      else
        macro_error (me->stack, &mts[0], _("!BREAK outside !DO."));
    }
  else if (lex_id_match_n (token->string, ss_cstr ("!onexpand"), 4))
    *me->expand = true;
  else if (lex_id_match_n (token->string, ss_cstr ("!offexpand"), 4))
    *me->expand = false;
  else
    macro_tokens_add (exp, &mts[0]);
  return 1;
}

static void
macro_expand (const struct macro_token *mts, size_t n,
              const struct macro_expander *me,
              struct macro_tokens *exp)
{
  if (me->nesting_countdown <= 0)
    {
      macro_error (me->stack, NULL, _("Maximum nesting level %d exceeded.  "
                                      "(Use SET MNEST to change the limit.)"),
                   settings_get_mnest ());
      for (size_t i = 0; i < n; i++)
        macro_tokens_add (exp, &mts[i]);
      return;
    }

  for (size_t i = 0; i < n; )
    {
      if (me->break_ && *me->break_)
        break;

      size_t consumed = macro_expand__ (&mts[i], n - i, me, exp);
      assert (consumed > 0 && i + consumed <= n);
      i += consumed;
    }
}

void
macro_call_expand (struct macro_call *mc, enum segmenter_mode segmenter_mode,
                   const struct msg_location *call_loc,
                   struct macro_tokens *exp)
{
  assert (mc->state == MC_FINISHED);

  bool expand = true;
  struct stringi_map vars = STRINGI_MAP_INITIALIZER (vars);
  struct macro_expansion_stack stack0 = {
    .location = call_loc,
  };
  struct macro_expansion_stack stack1 = {
    .next = &stack0,
    .name = mc->macro->name,
    .location = mc->macro->location,
  };
  struct macro_expander me = {
    .macros = mc->macros,
    .macro = mc->macro,
    .args = mc->args,
    .segmenter_mode = segmenter_mode,
    .expand = &expand,
    .break_ = NULL,
    .vars = &vars,
    .nesting_countdown = settings_get_mnest (),
    .stack = &stack1,
  };

  const struct macro_tokens *body = &mc->macro->body;
  macro_expand (body->mts, body->n, &me, exp);

  stringi_map_destroy (&vars);
}

