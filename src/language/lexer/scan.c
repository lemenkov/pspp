/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2013 Free Software Foundation, Inc.

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

#include "language/lexer/scan.h"

#include <limits.h>
#include <unistr.h>

#include "data/identifier.h"
#include "language/lexer/token.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/i18n.h"

#include "gl/c-ctype.h"
#include "gl/c-strtod.h"
#include "gl/xmemdup0.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Returns the integer value of (hex) digit C. */
static int
digit_value (int c)
{
  switch (c)
    {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
    default: return INT_MAX;
    }
}

static void
scan_quoted_string (struct substring in, struct token *token)
{
  /* Trim ' or " from front and back. */
  int quote = in.string[0];
  in.string++;
  in.length -= 2;

  struct substring out = { .string = xmalloc (in.length + 1) };

  for (;;)
    {
      size_t pos = ss_find_byte (in, quote);
      if (pos == SIZE_MAX)
        break;

      memcpy (ss_end (out), in.string, pos + 1);
      out.length += pos + 1;
      ss_advance (&in, pos + 2);
    }

  memcpy (ss_end (out), in.string, in.length);
  out.length += in.length;
  out.string[out.length] = '\0';

  *token = (struct token) { .type = T_STRING, .string = out };
}

static char *
scan_hex_string__ (struct substring in, struct substring *out)
{
  if (in.length % 2 != 0)
    return xasprintf (_("String of hex digits has %zu characters, which "
                        "is not a multiple of 2."), in.length);

  ss_realloc (out, in.length / 2 + 1);
  uint8_t *dst = CHAR_CAST (uint8_t *, out->string);
  out->length = in.length / 2;
  for (size_t i = 0; i < in.length; i += 2)
    {
      int hi = digit_value (in.string[i]);
      int lo = digit_value (in.string[i + 1]);

      if (hi >= 16 || lo >= 16)
        return xasprintf (_("`%c' is not a valid hex digit."),
                          in.string[hi >= 16 ? i : i + 1]);

      *dst++ = hi * 16 + lo;
    }

  return NULL;
}

static char *
scan_unicode_string__ (struct substring in, struct substring *out)
{
  if (in.length < 1 || in.length > 8)
    return xasprintf (_("Unicode string contains %zu bytes, which is "
                        "not in the valid range of 1 to 8 bytes."),
                      in.length);

  ucs4_t uc = 0;
  for (size_t i = 0; i < in.length; i++)
    {
      int digit = digit_value (in.string[i]);
      if (digit >= 16)
        return xasprintf (_("`%c' is not a valid hex digit."), in.string[i]);
      uc = uc * 16 + digit;
    }

  if ((uc >= 0xd800 && uc < 0xe000) || uc > 0x10ffff)
    return xasprintf (_("U+%04llX is not a valid Unicode code point."),
                      (long long) uc);

  ss_realloc (out, 4 + 1);
  out->length = u8_uctomb (CHAR_CAST (uint8_t *, ss_end (*out)), uc, 4);

  return NULL;
}

static enum token_type
scan_reserved_word__ (struct substring word)
{
  switch (c_toupper (word.string[0]))
    {
    case 'B':
      return T_BY;

    case 'E':
      return T_EQ;

    case 'G':
      return c_toupper (word.string[1]) == 'E' ? T_GE : T_GT;

    case 'L':
      return c_toupper (word.string[1]) == 'E' ? T_LE : T_LT;

    case 'N':
      return word.length == 2 ? T_NE : T_NOT;

    case 'O':
      return T_OR;

    case 'T':
      return T_TO;

    case 'A':
      return c_toupper (word.string[1]) == 'L' ? T_ALL : T_AND;

    case 'W':
      return T_WITH;
    }

  NOT_REACHED ();
}

static enum token_type
scan_punct1__ (char c0)
{
  switch (c0)
    {
    case '(': return T_LPAREN;
    case ')': return T_RPAREN;
    case ',': return T_COMMA;
    case '=': return T_EQUALS;
    case '-': return T_DASH;
    case '[': return T_LBRACK;
    case ']': return T_RBRACK;
    case '{': return T_LCURLY;
    case '}': return T_RCURLY;
    case '&': return T_AND;
    case '|': return T_OR;
    case '+': return T_PLUS;
    case '/': return T_SLASH;
    case '*': return T_ASTERISK;
    case '<': return T_LT;
    case '>': return T_GT;
    case '~': return T_NOT;
    case ';': return T_SEMICOLON;
    case ':': return T_COLON;
    default: return T_MACRO_PUNCT;
    }

  NOT_REACHED ();
}

static enum token_type
scan_punct2__ (char c0, char c1)
{
  switch (c0)
    {
    case '*':
      return T_EXP;

    case '<':
      return c1 == '=' ? T_LE : T_NE;

    case '>':
      return T_GE;

    case '~':
      return T_NE;

    case '&':
      return T_AND;

    case '|':
      return T_OR;
    }

  NOT_REACHED ();
}

static enum token_type
scan_punct__ (struct substring s)
{
  return (s.length == 1
          ? scan_punct1__ (s.string[0])
          : scan_punct2__ (s.string[0], s.string[1]));
}

static void
scan_number__ (struct substring s, struct token *token)
{
  char buf[128];
  char *p;

  if (s.length < sizeof buf)
    {
      p = buf;
      memcpy (buf, s.string, s.length);
      buf[s.length] = '\0';
    }
  else
    p = xmemdup0 (s.string, s.length);

  bool negative = *p == '-';
  double x = c_strtod (p + negative, NULL);
  *token = (struct token) {
    .type = negative ? T_NEG_NUM : T_POS_NUM,
    .number = negative ? -x : x,
  };

  if (p != buf)
    free (p);
}

static void
tokenize_error__ (struct token *token, char *error)
{
  *token = (struct token) { .type = T_STRING, .string = ss_cstr (error) };
}

static enum tokenize_result
tokenize_string_segment__ (enum segment_type type,
                           struct substring s, struct token *token)
{
  /* Trim X' or U' from front and ' from back. */
  s.string += 2;
  s.length -= 3;

  struct substring out = SS_EMPTY_INITIALIZER;
  char *error = (type == SEG_HEX_STRING
                 ? scan_hex_string__ (s, &out)
                 : scan_unicode_string__ (s, &out));
  if (!error)
    {
      out.string[out.length] = '\0';
      *token = (struct token) { .type = T_STRING, .string = out };
      return TOKENIZE_TOKEN;
    }
  else
    {
      tokenize_error__ (token, error);
      ss_dealloc (&out);
      return TOKENIZE_ERROR;
    }
}

static void
tokenize_unexpected_char (const struct substring *s, struct token *token)
{
  ucs4_t uc;
  u8_mbtouc (&uc, CHAR_CAST (const uint8_t *, s->string), s->length);

  char c_name[16];
  tokenize_error__ (token, xasprintf (_("Bad character %s in input."),
                                      uc_name (uc, c_name)));
}

enum tokenize_result
token_from_segment (enum segment_type type, struct substring s,
                    struct token *token)
{
  switch (type)
    {
    case SEG_NUMBER:
      scan_number__ (s, token);
      return TOKENIZE_TOKEN;

    case SEG_QUOTED_STRING:
      scan_quoted_string (s, token);
      return TOKENIZE_TOKEN;

    case SEG_HEX_STRING:
    case SEG_UNICODE_STRING:
      return tokenize_string_segment__ (type, s, token);

    case SEG_UNQUOTED_STRING:
    case SEG_DO_REPEAT_COMMAND:
    case SEG_INLINE_DATA:
    case SEG_DOCUMENT:
    case SEG_MACRO_BODY:
    case SEG_MACRO_NAME:
      *token = (struct token) { .type = T_STRING, .string = ss_clone (s) };
      return TOKENIZE_TOKEN;

    case SEG_RESERVED_WORD:
      *token = (struct token) { .type = scan_reserved_word__ (s) };
      return TOKENIZE_TOKEN;

    case SEG_IDENTIFIER:
      *token = (struct token) { .type = T_ID, .string = ss_clone (s) };
      return TOKENIZE_TOKEN;

    case SEG_MACRO_ID:
      *token = (struct token) { .type = T_MACRO_ID, .string = ss_clone (s)};
      return TOKENIZE_TOKEN;

    case SEG_PUNCT:
      *token = (struct token) { .type = scan_punct__ (s) };
      if (token->type == T_MACRO_PUNCT)
        token->string = ss_clone (s);
      return TOKENIZE_TOKEN;

    case SEG_SHBANG:
    case SEG_SPACES:
    case SEG_COMMENT:
    case SEG_NEWLINE:
    case SEG_COMMENT_COMMAND:
      return TOKENIZE_EMPTY;

    case SEG_START_DOCUMENT:
      *token = (struct token) {
        .type = T_ID,
        .string = ss_clone (ss_cstr ("DOCUMENT"))
      };
      return TOKENIZE_TOKEN;

    case SEG_START_COMMAND:
    case SEG_SEPARATE_COMMANDS:
    case SEG_END_COMMAND:
    case SEG_INNER_START_COMMAND:
    case SEG_INNER_SEPARATE_COMMANDS:
    case SEG_INNER_END_COMMAND:
      *token = (struct token) { .type = T_ENDCMD };
      return TOKENIZE_TOKEN;

    case SEG_END:
      *token = (struct token) { .type = T_STOP };
      return TOKENIZE_TOKEN;

    case SEG_EXPECTED_QUOTE:
      tokenize_error__ (token, xasprintf (_("Unterminated string constant.")));
      return TOKENIZE_ERROR;

    case SEG_EXPECTED_EXPONENT:
      tokenize_error__ (token,
                        xasprintf (_("Missing exponent following `%.*s'."),
                                   (int) s.length, s.string));
      return TOKENIZE_ERROR;

    case SEG_UNEXPECTED_CHAR:
      tokenize_unexpected_char (&s, token);
      return TOKENIZE_ERROR;
    }

  NOT_REACHED ();
}


/* Initializes SLEX for parsing INPUT, which is LENGTH bytes long, in the
   specified MODE.

   SLEX has no internal state to free, but it retains a reference to INPUT, so
   INPUT must not be modified or freed while SLEX is still in use. */
void
string_lexer_init (struct string_lexer *slex, const char *input, size_t length,
                   enum segmenter_mode mode, bool is_snippet)
{
  *slex = (struct string_lexer) {
    .input = input,
    .length = length,
    .offset = 0,
    .segmenter = segmenter_init (mode, is_snippet),
  };
}

/*  */
enum string_lexer_result
string_lexer_next (struct string_lexer *slex, struct token *token)
{
  for (;;)
    {
      const char *s = slex->input + slex->offset;
      size_t left = slex->length - slex->offset;
      enum segment_type type;
      int n;

      n = segmenter_push (&slex->segmenter, s, left, true, &type);
      assert (n >= 0);

      slex->offset += n;
      switch (token_from_segment (type, ss_buffer (s, n), token))
        {
        case TOKENIZE_TOKEN:
          return token->type == T_STOP ? SLR_END : SLR_TOKEN;

        case TOKENIZE_ERROR:
          return SLR_ERROR;

        case TOKENIZE_EMPTY:
          break;
        }
    }
}

static struct substring
concat (struct substring a, struct substring b)
{
  size_t length = a.length + b.length;
  struct substring out = { .string = xmalloc (length + 1), .length = length };
  memcpy (out.string, a.string, a.length);
  memcpy (out.string + a.length, b.string, b.length);
  out.string[length] = '\0';
  return out;
}

/* Attempts to merge a sequence of tokens together into a single token.  The
   caller feeds tokens in one by one and the merger FSM reports progress.  The
   caller must supply a merger structure M that is set to MERGER_INIT before
   the first call.  The caller must also supply a token OUT for storage, which
   need not be initialized.

   Returns:

   * -1 if more tokens are needed.  Token OUT might be in use for temporary
      storage; to ensure that it is freed, continue calling merger_add() until
      it returns something other than -1.  (T_STOP or T_ENDCMD will make it do
      that.)

   * 0 if the first token submitted to the merger is the output.  This is the
     common case for the first call, and it can be returned for subsequent
     calls as well.

   * A positive number if OUT is initialized to the output token.  The return
     value is the number of tokens being merged to produce this one. */
int
merger_add (struct merger *m, const struct token *in, struct token *out)
{
  /* We perform two different kinds of token merging:

     - String concatenation, where syntax like "a" + "b" is converted into a
       single string token.  This is definitely needed because the parser
       relies on it.

     - Negative number merging, where syntax like -5 is converted from a pair
       of tokens (T_DASH then T_POS_NUM) into a single token (T_NEG_NUM).  This
       might not be needed anymore because the segmenter directly treats a dash
       followed by a number, with optional intervening white space, as a
       negative number.  It's only needed if we want intervening comments to be
       allowed or for part of the negative number token to be produced by macro
       expansion. */
  switch (++m->state)
    {
    case 1:
      if (in->type == T_DASH || in->type == T_STRING)
        {
          *out = *in;
          return -1;
        }
      else
        return 0;

    case 2:
      if (out->type == T_DASH)
        {
          if (in->type == T_POS_NUM)
            {
              *out = (struct token) {
                .type = T_NEG_NUM,
                .number = -in->number
              };
              return 2;
            }
          else
            return 0;
        }
      else
        return in->type == T_PLUS ? -1 : 0;
      NOT_REACHED ();

    case 3:
      if (in->type == T_STRING)
        {
          out->string = concat (out->string, in->string);
          return -1;
        }
      else
        return 0;
      NOT_REACHED ();

    default:
      if (!(m->state % 2))
        return in->type == T_PLUS ? -1 : m->state - 1;
      else
        {
          if (in->type == T_STRING)
            {
              struct substring s = concat (out->string, in->string);
              ss_swap (&s, &out->string);
              ss_dealloc (&s);
              return -1;
            }
          else
            return m->state - 2;
        }
      NOT_REACHED ();
    }
}
