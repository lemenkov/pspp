/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2013, 2016 Free Software Foundation, Inc.

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

#include "language/lexer/segment.h"

#include <limits.h>
#include <unistr.h>

#include "data/identifier.h"
#include "language/lexer/command-name.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"

#include "gl/c-ctype.h"
#include "gl/c-strcase.h"

enum segmenter_state
  {
    S_SHBANG,
    S_GENERAL,
    S_COMMENT_1,
    S_COMMENT_2,
    S_DOCUMENT_1,
    S_DOCUMENT_2,
    S_DOCUMENT_3,
    S_FILE_LABEL_1,
    S_FILE_LABEL_2,
    S_FILE_LABEL_3,
    S_DO_REPEAT_1,
    S_DO_REPEAT_2,
    S_DO_REPEAT_3,
    S_DEFINE_1,
    S_DEFINE_2,
    S_DEFINE_3,
    S_DEFINE_4,
    S_DEFINE_5,
    S_BEGIN_DATA_1,
    S_BEGIN_DATA_2,
    S_BEGIN_DATA_3,
    S_BEGIN_DATA_4,
  };

#define SS_START_OF_LINE (1u << 0)
#define SS_START_OF_COMMAND (1u << 1)

static int segmenter_detect_command_name__ (const char *input,
                                            size_t n, bool eof, int ofs);

static int
segmenter_u8_to_uc__ (ucs4_t *puc, const char *input_, size_t n, bool eof,
                      size_t ofs)
{
  const uint8_t *input = CHAR_CAST (const uint8_t *, input_);
  int mblen;

  assert (n > ofs);

  input += ofs;
  n -= ofs;

  mblen = u8_mbtoucr (puc, input, n);
  if (mblen >= 0)
    return mblen;
  else if (mblen != -2)
    return u8_mbtouc (puc, input, n);
  else if (eof)
    {
      *puc = 0xfffd;
      return n;
    }
  else
    return -1;
}

static int
segmenter_parse_shbang__ (struct segmenter *s, const char *input, size_t n,
                          bool eof, enum segment_type *type)
{
  if (input[0] == '#')
    {
      if (n >= 2)
        {
          if (input[1] == '!')
            {
              for (int ofs = 2; ; ofs++)
                {
                  if (ofs >= n)
                    {
                      if (!eof)
                        return -1;
                    }
                  else if (input[ofs] == '\n')
                    {
                      if (input[ofs - 1] == '\r')
                        ofs--;
                    }
                  else
                    continue;

                  s->state = S_GENERAL;
                  s->substate = SS_START_OF_COMMAND;
                  *type = SEG_SHBANG;
                  return ofs;
                }
            }
        }
      else if (!eof)
        return -1;
    }

  s->state = S_GENERAL;
  s->substate = SS_START_OF_LINE | SS_START_OF_COMMAND;
  return segmenter_push (s, input, n, eof, type);
}

static int
segmenter_parse_digraph__ (const char *seconds, struct segmenter *s,
                           const char *input, size_t n, bool eof,
                           enum segment_type *type)
{
  assert (s->state == S_GENERAL);

  *type = SEG_PUNCT;
  s->substate = 0;
  return (n < 2
          ? (eof ? 1 : -1)
          : (strchr (seconds, input[1]) != NULL ? 2 : 1));
}

static int
skip_comment (const char *input, size_t n, bool eof, size_t ofs)
{
  for (; ofs < n; ofs++)
    {
      if (input[ofs] == '\n')
        return ofs;
      else if (input[ofs] == '*')
        {
          if (ofs + 1 >= n)
            return eof ? ofs + 1 : -1;
          else if (input[ofs + 1] == '/')
            return ofs + 2;
        }
    }
  return eof ? ofs : -1;
}

static int
skip_spaces_and_comments (const char *input, size_t n, bool eof, int ofs)
{
  while (ofs < n)
    {
      ucs4_t uc;
      int mblen;

      mblen = segmenter_u8_to_uc__ (&uc, input, n, eof, ofs);
      if (mblen < 0)
        return -1;

      if (uc == '/')
        {
          if (ofs + 1 >= n)
            return eof ? ofs : -1;
          else if (input[ofs + 1] != '*')
            return ofs;

          ofs = skip_comment (input, n, eof, ofs + 2);
          if (ofs < 0)
            return -1;
        }
      else if (lex_uc_is_space (uc) && uc != '\n')
        ofs += mblen;
      else
        return ofs;
    }

  return eof ? ofs : -1;
}

static int
is_end_of_line (const char *input, size_t n, bool eof, int ofs)
{
  if (ofs >= n)
    return eof ? 1 : -1;
  else if (input[ofs] == '\n')
    return 1;
  else if (input[ofs] == '\r')
    {
      if (ofs + 1 >= n)
        return eof ? 1 : -1;
      return input[ofs + 1] == '\n';
    }
  else
    return 0;
}

static int
at_end_of_line (const char *input, size_t n, bool eof, int ofs)
{
  ofs = skip_spaces_and_comments (input, n, eof, ofs);
  if (ofs < 0)
    return -1;

  return is_end_of_line (input, n, eof, ofs);
}

static bool
is_all_spaces (const char *input_, size_t n)
{
  const uint8_t *input = CHAR_CAST (const uint8_t *, input_);

  int mblen;
  for (int ofs = 0; ofs < n; ofs += mblen)
    {
      ucs4_t uc;
      mblen = u8_mbtouc (&uc, input + ofs, n - ofs);
      if (!lex_uc_is_space (uc))
        return false;
    }
  return true;
}

static int
segmenter_parse_newline__ (const char *input, size_t n, bool eof,
                           enum segment_type *type)
{
  int ofs;

  if (input[0] == '\n')
    ofs = 1;
  else
    {
      if (n < 2)
        {
          assert (!eof);
          return -1;
        }

      assert (input[0] == '\r');
      assert (input[1] == '\n');
      ofs = 2;
    }

  *type = SEG_NEWLINE;
  return ofs;
}

static int
skip_spaces (const char *input, size_t n, bool eof, size_t ofs)
{
  while (ofs < n)
    {
      ucs4_t uc;
      int mblen;

      mblen = segmenter_u8_to_uc__ (&uc, input, n, eof, ofs);
      if (mblen < 0)
        return -1;

      if (!lex_uc_is_space (uc) || uc == '\n')
        return ofs;

      ofs += mblen;
    }

  return eof ? ofs : -1;
}

static int
skip_digits (const char *input, size_t n, bool eof, int ofs)
{
  for (; ofs < n; ofs++)
    if (!c_isdigit (input[ofs]))
      return ofs;
  return eof ? ofs : -1;
}

static int
segmenter_parse_number__ (struct segmenter *s, const char *input, size_t n,
                          bool eof, enum segment_type *type, int ofs)
{
  assert (s->state == S_GENERAL);

  ofs = skip_digits (input, n, eof, ofs);
  if (ofs < 0)
    return -1;

  if (ofs >= n)
    {
      if (!eof)
        return -1;
      goto number;
    }
  if (input[ofs] == '.')
    {
      if (ofs + 1 >= n)
        {
          if (!eof)
            return -1;
          goto number;
        }

      ofs = skip_digits (input, n, eof, ofs + 1);
      if (ofs < 0)
        return -1;
      else if (ofs >= n)
        goto number;
    }

  if (input[ofs] == 'e' || input[ofs] == 'E')
    {
      ofs++;
      if (ofs >= n)
        {
          if (!eof)
            return -1;
          goto expected_exponent;
        }

      if (input[ofs] == '+' || input[ofs] == '-')
        {
          ofs++;
          if (ofs >= n)
            {
              if (!eof)
                return -1;
              goto expected_exponent;
            }
        }

      if (!c_isdigit (input[ofs]))
        goto expected_exponent;

      ofs = skip_digits (input, n, eof, ofs);
      if (ofs < 0)
        return -1;
    }

  if (input[ofs - 1] == '.')
    {
      int eol = at_end_of_line (input, n, eof, ofs);
      if (eol < 0)
        return -1;
      else if (eol)
        ofs--;
    }

number:
  *type = SEG_NUMBER;
  s->substate = 0;
  return ofs;

expected_exponent:
  *type = SEG_EXPECTED_EXPONENT;
  s->substate = 0;
  return ofs;
}

static bool
is_reserved_word (const char *s, int n)
{
  char s0, s1, s2, s3;

  s0 = c_toupper (s[0]);
  switch (n)
    {
    case 2:
      s1 = c_toupper (s[1]);
      return ((s0 == 'B' && s1 == 'Y')
              || (s0 == 'E' && s1 == 'Q')
              || (s0 == 'G' && (s1 == 'E' || s1 == 'T'))
              || (s0 == 'L' && (s1 == 'E' || s1 == 'T'))
              || (s0 == 'N' && s1 == 'E')
              || (s0 == 'O' && s1 == 'R')
              || (s0 == 'T' && s1 == 'O'));

    case 3:
      s1 = c_toupper (s[1]);
      s2 = c_toupper (s[2]);
      return ((s0 == 'A' && ((s1 == 'L' && s2 == 'L')
                             || (s1 == 'N' && s2 == 'D')))
              || (s0 == 'N' && s1 == 'O' && s2 == 'T'));

    case 4:
      s1 = c_toupper (s[1]);
      s2 = c_toupper (s[2]);
      s3 = c_toupper (s[3]);
      return s0 == 'W' && s1 == 'I' && s2 == 'T' && s3 == 'H';

    default:
      return false;
    }
}

static int
segmenter_parse_comment_1__ (struct segmenter *s,
                             const char *input, size_t n, bool eof,
                             enum segment_type *type)
{
  int endcmd;
  int ofs;

  endcmd = -2;
  ofs = 0;
  while (ofs < n)
    {
      ucs4_t uc;
      int mblen;

      mblen = segmenter_u8_to_uc__ (&uc, input, n, eof, ofs);
      if (mblen < 0)
        return -1;

      switch (uc)
        {
        case '.':
          endcmd = ofs;
          break;

        case '\n':
          if (ofs > 1 && input[ofs - 1] == '\r')
            ofs--;
          if (endcmd == -2)
            {
              /* Blank line ends comment command. */
              s->state = S_GENERAL;
              s->substate = SS_START_OF_COMMAND;
              *type = SEG_SEPARATE_COMMANDS;
              return ofs;
            }
          else if (endcmd >= 0)
            {
              /* '.' at end of line ends comment command. */
              s->state = S_GENERAL;
              s->substate = 0;
              *type = SEG_COMMENT_COMMAND;
              return endcmd;
            }
          else
            {
              /* Comment continues onto next line. */
              *type = SEG_COMMENT_COMMAND;
              s->state = S_COMMENT_2;
              return ofs;
            }
          NOT_REACHED ();

        default:
          if (!lex_uc_is_space (uc))
            endcmd = -1;
          break;
        }

      ofs += mblen;
    }

  if (eof)
    {
      /* End of file. */
      s->state = S_GENERAL;
      s->substate = SS_START_OF_COMMAND;
      *type = SEG_SEPARATE_COMMANDS;
      return ofs;
    }

  return -1;
}

static int
segmenter_parse_comment_2__ (struct segmenter *s, const char *input,
                             size_t n, bool eof, enum segment_type *type)
{
  int ofs = segmenter_parse_newline__ (input, n, eof, type);
  if (ofs < 0)
    return -1;

  int new_cmd;
  if (ofs >= n)
    {
      if (!eof)
        return -1;
      new_cmd = false;
    }
  else
    {
      ucs4_t uc;
      int mblen = segmenter_u8_to_uc__ (&uc, input, n, eof, ofs);
      if (mblen < 0)
        return -1;

      if (uc == '+' || uc == '-' || uc == '.')
        new_cmd = true;
      else if (!lex_uc_is_space (uc))
        switch (s->mode)
          {
          case SEG_MODE_INTERACTIVE:
            new_cmd = false;
            break;

          case SEG_MODE_BATCH:
            new_cmd = true;
            break;

          case SEG_MODE_AUTO:
            new_cmd = segmenter_detect_command_name__ (input, n, eof, ofs);
            if (new_cmd < 0)
              return -1;
            break;

          default:
            NOT_REACHED ();
          }
      else
        new_cmd = false;
    }

  if (new_cmd)
    {
      s->state = S_GENERAL;
      s->substate = SS_START_OF_LINE | SS_START_OF_COMMAND;
    }
  else
    s->state = S_COMMENT_1;
  return ofs;
}

static int
segmenter_parse_document_1__ (struct segmenter *s, const char *input, size_t n,
                              bool eof, enum segment_type *type)
{
  bool end_cmd;
  int ofs;

  end_cmd = false;
  ofs = 0;
  while (ofs < n)
    {
      ucs4_t uc;
      int mblen;

      mblen = segmenter_u8_to_uc__ (&uc, input, n, eof, ofs);
      if (mblen < 0)
        return -1;

      switch (uc)
        {
        case '.':
          end_cmd = true;
          break;

        case '\n':
          if (ofs > 1 && input[ofs - 1] == '\r')
            ofs--;

          *type = SEG_DOCUMENT;
          s->state = end_cmd ? S_DOCUMENT_3 : S_DOCUMENT_2;
          return ofs;

        default:
          if (!lex_uc_is_space (uc))
            end_cmd = false;
          break;
        }

      ofs += mblen;
    }
  if (eof)
    {
      *type = SEG_DOCUMENT;
      s->state = S_DOCUMENT_3;
      return ofs;
    }
  return -1;
}

static int
segmenter_parse_document_2__ (struct segmenter *s, const char *input, size_t n,
                              bool eof, enum segment_type *type)
{
  int ofs;

  ofs = segmenter_parse_newline__ (input, n, eof, type);
  if (ofs < 0)
    return -1;

  s->state = S_DOCUMENT_1;
  return ofs;
}

static int
segmenter_parse_document_3__ (struct segmenter *s, enum segment_type *type)
{
  *type = SEG_END_COMMAND;
  s->state = S_GENERAL;
  s->substate = SS_START_OF_COMMAND | SS_START_OF_LINE;
  return 0;
}

static int
segmenter_unquoted (const char *input, size_t n, bool eof, int ofs)

{
  ofs = skip_spaces_and_comments (input, n, eof, ofs);
  if (ofs < 0)
    return -1;
  else if (ofs < n)
    {
      char c = input[ofs];
      return c != '\'' && c != '"' && c != '\n';
    }
  else
    {
      assert (eof);
      return 0;
    }
}

static int
next_id_in_command (const struct segmenter *s, const char *input, size_t n,
                    bool eof, int ofs, char id[], size_t id_size)
{
  struct segmenter sub;

  assert (id_size > 0);

  sub.mode = s->mode;
  sub.state = S_GENERAL;
  sub.substate = 0;
  for (;;)
    {
      enum segment_type type;
      int retval;

      retval = segmenter_push (&sub, input + ofs, n - ofs, eof, &type);
      if (retval < 0)
        {
          id[0] = '\0';
          return -1;
        }

      switch (type)
        {
        case SEG_SHBANG:
        case SEG_SPACES:
        case SEG_COMMENT:
        case SEG_NEWLINE:
          break;

        case SEG_IDENTIFIER:
          if (retval < id_size)
            {
              memcpy (id, input + ofs, retval);
              id[retval] = '\0';
              return ofs + retval;
            }
          /* fall through */

        case SEG_NUMBER:
        case SEG_QUOTED_STRING:
        case SEG_HEX_STRING:
        case SEG_UNICODE_STRING:
        case SEG_UNQUOTED_STRING:
        case SEG_RESERVED_WORD:
        case SEG_PUNCT:
        case SEG_COMMENT_COMMAND:
        case SEG_DO_REPEAT_COMMAND:
        case SEG_INLINE_DATA:
        case SEG_MACRO_ID:
        case SEG_MACRO_NAME:
        case SEG_MACRO_BODY:
        case SEG_START_DOCUMENT:
        case SEG_DOCUMENT:
        case SEG_START_COMMAND:
        case SEG_SEPARATE_COMMANDS:
        case SEG_END_COMMAND:
        case SEG_INNER_START_COMMAND:
        case SEG_INNER_SEPARATE_COMMANDS:
        case SEG_INNER_END_COMMAND:
        case SEG_END:
        case SEG_EXPECTED_QUOTE:
        case SEG_EXPECTED_EXPONENT:
        case SEG_UNEXPECTED_CHAR:
          id[0] = '\0';
          return ofs + retval;
        }
      ofs += retval;
    }
}

/* Called when INPUT begins with a character that can start off an ID token. */
static int
segmenter_parse_id__ (struct segmenter *s, const char *input, size_t n,
                      bool eof, enum segment_type *type)
{
  ucs4_t uc;
  int ofs;

  assert (n > 0);
  assert (s->state == S_GENERAL);

  ofs = u8_mbtouc (&uc, CHAR_CAST (const uint8_t *, input), n);
  for (;;)
    {
      int mblen;

      if (ofs >= n)
        {
          if (eof)
            break;
          return -1;
        }

      mblen = segmenter_u8_to_uc__ (&uc, input, n, eof, ofs);
      if (mblen < 0)
        return -1;
      else if (!lex_uc_is_idn (uc))
        break;

      ofs += mblen;
    }

  if (input[ofs - 1] == '.')
    {
      int eol = at_end_of_line (input, n, eof, ofs);
      if (eol < 0)
        return -1;
      else if (eol)
        ofs--;
    }

  *type = (is_reserved_word (input, ofs) ? SEG_RESERVED_WORD
           : input[0] == '!' ? SEG_MACRO_ID
           : SEG_IDENTIFIER);

  if (s->substate & SS_START_OF_COMMAND)
    {
      struct substring word = ss_buffer (input, ofs);

      if (lex_id_match_n (ss_cstr ("COMMENT"), word, 4))
        {
          s->state = S_COMMENT_1;
          return segmenter_parse_comment_1__ (s, input, n, eof, type);
        }
      else if (lex_id_match (ss_cstr ("DOCUMENT"), word))
        {
          s->state = S_DOCUMENT_1;
          *type = SEG_START_DOCUMENT;
          return 0;
        }
      else if (lex_id_match_n (ss_cstr ("DEFINE"), word, 6))
        {
          s->state = S_DEFINE_1;
          return ofs;
        }
      else if (lex_id_match (ss_cstr ("FILE"), word))
        {
          char id[16];

          if (next_id_in_command (s, input, n, eof, ofs, id, sizeof id) < 0)
            return -1;
          else if (lex_id_match (ss_cstr ("LABEL"), ss_cstr (id)))
            {
              s->state = S_FILE_LABEL_1;
              s->substate = 0;
              return ofs;
            }
        }
      else if (lex_id_match (ss_cstr ("DO"), word))
        {
          char id[16];

          if (next_id_in_command (s, input, n, eof, ofs, id, sizeof id) < 0)
            return -1;
          else if (lex_id_match (ss_cstr ("REPEAT"), ss_cstr (id)))
            {
              s->state = S_DO_REPEAT_1;
              s->substate = 0;
              return ofs;
            }
        }
      else if (lex_id_match (ss_cstr ("BEGIN"), word))
        {
          char id[16];
          int ofs2;

          ofs2 = next_id_in_command (s, input, n, eof, ofs, id, sizeof id);
          if (ofs2 < 0)
            return -1;
          else if (lex_id_match (ss_cstr ("DATA"), ss_cstr (id)))
            {
              int eol = -1;
              /* We've found BEGIN DATA.  Check whether that's the entire
                 command (either followed by a new-line or by '.' then a
                 new-line). */
              ofs2 = skip_spaces_and_comments (input, n, eof, ofs2);
              if (ofs2 < 0)
                return -1;

              if (ofs2 >= n)
                assert (eof);
              else if (input[ofs2] == '.')
                {
                  ofs2 = skip_spaces_and_comments (input, n, eof, ofs2 + 1);
                  if (ofs2 < 0)
                    return -1;
                }

              eol = is_end_of_line (input, n, eof, ofs2);
              if (eol < 0)
                return -1;
              else if (eol)
                {
                  /* BEGIN DATA is indeed the entire command.  We choose a next
                     state depending on whether it's one line long or two lines
                     long. */
                  if (memchr (input, '\n', ofs2))
                    s->state = S_BEGIN_DATA_1;
                  else
                    s->state = S_BEGIN_DATA_2;
                  s->substate = 0;
                  return ofs;
                }
            }
        }
    }

  s->substate = 0;
  return ofs;
}

static int
segmenter_parse_string__ (enum segment_type string_type,
                          int ofs, struct segmenter *s,
                          const char *input, size_t n, bool eof,
                          enum segment_type *type)
{
  int quote = input[ofs];

  ofs++;
  while (ofs < n)
    if (input[ofs] == quote)
      {
        ofs++;
        if (ofs < n)
          {
            if (input[ofs] == quote)
              {
                ofs++;
                continue;
              }
          }
        else if (!eof)
          return -1;

        *type = string_type;
        s->substate = 0;
        return ofs;
      }
    else if (input[ofs] == '\n')
      goto expected_quote;
    else
      ofs++;

  if (eof)
    goto expected_quote;

  return -1;

expected_quote:
  *type = SEG_EXPECTED_QUOTE;
  s->substate = 0;
  return ofs;
}

static int
segmenter_maybe_parse_string__ (enum segment_type string_type,
                                struct segmenter *s,
                                const char *input, size_t n, bool eof,
                                enum segment_type *type)
{
  if (n < 2)
    {
      if (!eof)
        return -1;
    }
  else if (input[1] == '\'' || input[1] == '"')
    return segmenter_parse_string__ (string_type, 1, s, input, n, eof, type);

  return segmenter_parse_id__ (s, input, n, eof, type);
}

static int
segmenter_parse_mid_command__ (struct segmenter *s,
                               const char *input, size_t n, bool eof,
                               enum segment_type *type)
{
  ucs4_t uc;
  int mblen;
  int ofs;

  assert (s->state == S_GENERAL);
  assert (!(s->substate & SS_START_OF_LINE));

  mblen = segmenter_u8_to_uc__ (&uc, input, n, eof, 0);
  if (mblen < 0)
    return -1;

  switch (uc)
    {
    case '\n':
      s->substate |= SS_START_OF_LINE;
      *type = SEG_NEWLINE;
      return 1;

    case '/':
      if (n < 2)
        {
          if (!eof)
            return -1;
        }
      else if (input[1] == '*')
        {
          ofs = skip_comment (input, n, eof, 2);
          if (ofs < 0)
            return -1;

          *type = SEG_COMMENT;
          return ofs;
        }

      s->substate = 0;
      *type = SEG_PUNCT;
      return 1;

    case '-':
      ofs = skip_spaces (input, n, eof, 1);
      if (ofs < 0)
        return -1;
      else if (ofs < n && c_isdigit (input[ofs]))
        return segmenter_parse_number__ (s, input, n, eof, type, ofs);
      else if (ofs < n && input[ofs] == '.')
        {
          if (ofs + 1 >= n)
            {
              if (!eof)
                return -1;
            }
          else if (c_isdigit (input[ofs + 1]))
            return segmenter_parse_number__ (s, input, n, eof, type, ofs);
        }
      /* Fall through. */
    case '(': case ')': case '{': case ',': case '=': case ';': case ':':
    case '[': case ']': case '}': case '&': case '|': case '+':
      *type = SEG_PUNCT;
      s->substate = 0;
      return 1;

    case '*':
      if (s->substate & SS_START_OF_COMMAND)
        {
          /* '*' at the beginning of a command begins a comment. */
          s->state = S_COMMENT_1;
          return segmenter_parse_comment_1__ (s, input, n, eof, type);
        }
      else
        return segmenter_parse_digraph__ ("*", s, input, n, eof, type);

    case '<':
      return segmenter_parse_digraph__ ("=>", s, input, n, eof, type);

    case '>':
      return segmenter_parse_digraph__ ("=", s, input, n, eof, type);

    case '~':
      return segmenter_parse_digraph__ ("=", s, input, n, eof, type);

    case '.':
      if (n < 2)
        {
          if (!eof)
            return -1;
        }
      else if (c_isdigit (input[1]))
        return segmenter_parse_number__ (s, input, n, eof, type, 0);
      {
        int eol = at_end_of_line (input, n, eof, 1);
        if (eol < 0)
          return -1;

        if (eol)
          {
            *type = SEG_END_COMMAND;
            s->substate = SS_START_OF_COMMAND;
          }
        else
          *type = SEG_PUNCT;
        return 1;
      }

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      return segmenter_parse_number__ (s, input, n, eof, type, 0);

    case 'u': case 'U':
      return segmenter_maybe_parse_string__ (SEG_UNICODE_STRING,
                                             s, input, n, eof, type);

    case 'x': case 'X':
      return segmenter_maybe_parse_string__ (SEG_HEX_STRING,
                                             s, input, n, eof, type);

    case '\'': case '"':
      return segmenter_parse_string__ (SEG_QUOTED_STRING, 0,
                                       s, input, n, eof, type);

    case '!':
      if (n < 2)
        {
          if (!eof)
            return -1;
          *type = SEG_PUNCT;
          return 1;
        }
      else if (input[1] == '*')
        {
          *type = SEG_MACRO_ID;
          return 2;
        }
      else
        return segmenter_parse_id__ (s, input, n, eof, type);

    default:
      if (lex_uc_is_space (uc))
        {
          ofs = skip_spaces (input, n, eof, mblen);
          if (ofs < 0)
            return -1;

          if (ofs < n && input[ofs - 1] == '\r' && input[ofs] == '\n')
            {
              if (ofs == 1)
                {
                  s->substate |= SS_START_OF_LINE;
                  *type = SEG_NEWLINE;
                  return 2;
                }
              else
                ofs--;
            }
          *type = SEG_SPACES;
          return ofs;
        }
      else if (lex_uc_is_id1 (uc))
        return segmenter_parse_id__ (s, input, n, eof, type);
      else if (uc > 32 && uc < 127 && uc != '\\' && uc != '^')
        {
          *type = SEG_PUNCT;
          s->substate = 0;
          return 1;
        }
      else
        {
          *type = SEG_UNEXPECTED_CHAR;
          s->substate = 0;
          return mblen;
        }
    }
}

static int
compare_commands (const void *a_, const void *b_)
{
  const char *const *ap = a_;
  const char *const *bp = b_;
  const char *a = *ap;
  const char *b = *bp;

  return c_strcasecmp (a, b);
}

static const char **
segmenter_get_command_name_candidates (unsigned char first)
{
#define DEF_CMD(STATES, FLAGS, NAME, LABEL, FUNCTION) NAME,
#define UNIMPL_CMD(NAME, DESCRIPTION) NAME,
  static const char *commands[] =
    {
#include "language/command.def"
      ""
    };
  static size_t n_commands = (sizeof commands / sizeof *commands) - 1;
#undef DEF_CMD
#undef UNIMPL_CMD

  static bool inited;

  static const char **cindex[UCHAR_MAX + 1];

  if (!inited)
    {
      size_t i;

      inited = true;

      qsort (commands, n_commands, sizeof *commands, compare_commands);
      for (i = 0; i < n_commands; i++)
        {
          unsigned char c = c_toupper (commands[i][0]);
          if (cindex[c] == NULL)
            cindex[c] = &commands[i];
        }
      for (i = 0; i <= UCHAR_MAX; i++)
        if (cindex[i] == NULL)
          cindex[i] = &commands[n_commands];
    }

  return cindex[c_toupper (first)];
}

static int
segmenter_detect_command_name__ (const char *input, size_t n, bool eof,
                                 int ofs)
{
  const char **commands;

  input += ofs;
  n -= ofs;
  ofs = 0;
  for (;;)
    {
      ucs4_t uc;
      int mblen;

      if (ofs >= n)
        {
          if (eof)
            break;
          return -1;
        }

      mblen = segmenter_u8_to_uc__ (&uc, input, n, eof, ofs);
      if (mblen < 0)
        return -1;

      if (uc == '\n'
          || !(lex_uc_is_space (uc) || lex_uc_is_idn (uc) || uc == '-'))
        break;

      ofs += mblen;
    }
  if (!ofs)
    return 0;

  if (input[ofs - 1] == '.')
    ofs--;

  for (commands = segmenter_get_command_name_candidates (input[0]);
       c_toupper (input[0]) == c_toupper ((*commands)[0]);
       commands++)
    {
      int missing_words;
      bool exact;

      if (command_match (ss_cstr (*commands), ss_buffer (input, ofs),
                         &exact, &missing_words)
          && missing_words <= 0)
        return 1;
    }

  return 0;
}

static int
is_start_of_string__ (const char *input, size_t n, bool eof, int ofs)
{
  if (ofs >= n)
    return eof ? 0 : -1;

  int c = input[ofs];
  if (c == 'x' || c == 'X' || c == 'u' || c == 'U')
    {
      if (ofs + 1 >= n)
        return eof ? 0 : -1;

      return input[ofs + 1] == '\'' || input[ofs + 1] == '"';
    }
  else
    return c == '\'' || c == '"' || c == '\n';
}

static int
segmenter_parse_start_of_line__ (struct segmenter *s,
                                 const char *input, size_t n, bool eof,
                                 enum segment_type *type)
{
  ucs4_t uc;
  int mblen;
  int ofs;

  assert (s->state == S_GENERAL);
  assert (s->substate & SS_START_OF_LINE);

  mblen = segmenter_u8_to_uc__ (&uc, input, n, eof, 0);
  if (mblen < 0)
    return -1;

  switch (uc)
    {
    case '+':
      ofs = skip_spaces_and_comments (input, n, eof, 1);
      if (ofs < 0)
        return -1;
      else
        {
          int is_string = is_start_of_string__ (input, n, eof, ofs);
          if (is_string < 0)
            return -1;
          else if (is_string)
            {
              /* This is punctuation that may separate pieces of a string. */
              *type = SEG_PUNCT;
              s->substate = 0;
              return 1;
            }
        }
      *type = SEG_START_COMMAND;
      s->substate = SS_START_OF_COMMAND;
      return 1;

    case '-':
      *type = SEG_START_COMMAND;
      s->substate = SS_START_OF_COMMAND;
      return 1;

    case '.':
      /* We've found '.' at the beginning of a line.  If there's more text on
         the line, then it starts a new command, because '+' or '-' or '.' in
         the leftmost column does that.  If the command is otherwise blank,
         then it ends the previous command.  The difference only matters for
         deciding whether the line is part of the previous command in
         command_segmenter. */
      {
        int eol = at_end_of_line (input, n, eof, 1);
        if (eol < 0)
          return -1;

        *type = eol ? SEG_END_COMMAND : SEG_START_COMMAND;
        s->substate = SS_START_OF_COMMAND;
        return 1;
      }

    default:
      if (lex_uc_is_space (uc))
        {
          int eol = at_end_of_line (input, n, eof, 0);
          if (eol < 0)
            return -1;
          else if (eol)
            {
              s->substate = SS_START_OF_COMMAND;
              *type = SEG_SEPARATE_COMMANDS;
              return 0;
            }
          break;
        }

      if (s->mode == SEG_MODE_INTERACTIVE || s->substate & SS_START_OF_COMMAND)
        break;
      else if (s->mode == SEG_MODE_AUTO)
        {
          int cmd = segmenter_detect_command_name__ (input, n, eof, 0);
          if (cmd < 0)
            return -1;
          else if (cmd == 0)
            break;
        }
      else
        assert (s->mode == SEG_MODE_BATCH);

      s->substate = SS_START_OF_COMMAND;
      *type = SEG_START_COMMAND;
      return 0;
    }

  s->substate = SS_START_OF_COMMAND;
  return segmenter_parse_mid_command__ (s, input, n, eof, type);
}

static int
segmenter_parse_file_label_1__ (struct segmenter *s,
                                const char *input, size_t n, bool eof,
                                enum segment_type *type)
{
  struct segmenter sub;
  int ofs;

  sub = *s;
  sub.state = S_GENERAL;
  ofs = segmenter_push (&sub, input, n, eof, type);

  if (ofs < 0)
    return -1;
  else if (*type == SEG_IDENTIFIER)
    {
      int result;

      assert (lex_id_match (ss_cstr ("LABEL"),
                            ss_buffer ((char *) input, ofs)));
      result = segmenter_unquoted (input, n, eof, ofs);
      if (result < 0)
        return -1;
      else
        {
          if (result)
            s->state = S_FILE_LABEL_2;
          else
            *s = sub;
          return ofs;
        }
    }
  else
    {
      s->substate = sub.substate;
      return ofs;
    }
}

static int
segmenter_parse_file_label_2__ (struct segmenter *s,
                                const char *input, size_t n, bool eof,
                                enum segment_type *type)
{
  int ofs;

  ofs = skip_spaces (input, n, eof, 0);
  if (ofs < 0)
    return -1;
  s->state = S_FILE_LABEL_3;
  *type = SEG_SPACES;
  return ofs;
}

static int
segmenter_parse_file_label_3__ (struct segmenter *s,
                                const char *input, size_t n, bool eof,
                                enum segment_type *type)
{
  int endcmd;
  int ofs;

  endcmd = -1;
  ofs = 0;
  while (ofs < n)
    {
      ucs4_t uc;
      int mblen;

      mblen = segmenter_u8_to_uc__ (&uc, input, n, eof, ofs);
      if (mblen < 0)
        return -1;

      switch (uc)
        {
        case '\n':
          goto end_of_line;

        case '.':
          endcmd = ofs;
          break;

        default:
          if (!lex_uc_is_space (uc))
            endcmd = -1;
          break;
        }

      ofs += mblen;
    }

  if (eof)
    {
    end_of_line:
      s->state = S_GENERAL;
      s->substate = 0;
      *type = SEG_UNQUOTED_STRING;
      return endcmd >= 0 ? endcmd : ofs;
    }

  return -1;
}

static int
segmenter_subparse (struct segmenter *s,
                    const char *input, size_t n, bool eof,
                    enum segment_type *type)
{
  struct segmenter sub;
  int ofs;

  sub.mode = s->mode;
  sub.state = S_GENERAL;
  sub.substate = s->substate;
  ofs = segmenter_push (&sub, input, n, eof, type);
  s->substate = sub.substate;
  return ofs;
}

/* We are segmenting a DO REPEAT command, currently reading the syntax that
   defines the stand-in variables (the head) before the lines of syntax to be
   repeated (the body). */
static int
segmenter_parse_do_repeat_1__ (struct segmenter *s,
                               const char *input, size_t n, bool eof,
                               enum segment_type *type)
{
  int ofs = segmenter_subparse (s, input, n, eof, type);
  if (ofs < 0)
    return -1;

  if (*type == SEG_SEPARATE_COMMANDS)
    {
      /* We reached a blank line that separates the head from the body. */
      s->state = S_DO_REPEAT_2;
      *type = SEG_INNER_SEPARATE_COMMANDS;
    }
  else if (*type == SEG_END_COMMAND || *type == SEG_START_COMMAND)
    {
      /* We reached the body. */
      s->state = S_DO_REPEAT_3;
      s->substate = 1;
      *type = (*type == SEG_END_COMMAND
               ? SEG_INNER_END_COMMAND
               : SEG_INNER_START_COMMAND);
    }

  return ofs;
}

/* We are segmenting a DO REPEAT command, currently reading a blank line that
   separates the head from the body. */
static int
segmenter_parse_do_repeat_2__ (struct segmenter *s,
                               const char *input, size_t n, bool eof,
                               enum segment_type *type)
{
  int ofs = segmenter_subparse (s, input, n, eof, type);
  if (ofs < 0)
    return -1;

  if (*type == SEG_NEWLINE)
    {
      /* We reached the body. */
      s->state = S_DO_REPEAT_3;
      s->substate = 1;
    }

  return ofs;
}

static bool
check_repeat_command (struct segmenter *s,
                      const char *input, size_t n, bool eof)
{
  int direction;
  char id[16];
  int ofs;

  ofs = 0;
  if (input[ofs] == '+' || input[ofs] == '-')
    ofs++;

  ofs = next_id_in_command (s, input, n, eof, ofs, id, sizeof id);
  if (ofs < 0)
    return false;
  else if (lex_id_match (ss_cstr ("DO"), ss_cstr (id)))
    direction = 1;
  else if (lex_id_match (ss_cstr ("END"), ss_cstr (id)))
    direction = -1;
  else
    return true;

  ofs = next_id_in_command (s, input, n, eof, ofs, id, sizeof id);
  if (ofs < 0)
    return false;

  if (lex_id_match (ss_cstr ("REPEAT"), ss_cstr (id)))
    s->substate += direction;
  return true;
}

static int
segmenter_parse_full_line__ (const char *input, size_t n, bool eof,
                             enum segment_type *type)
{
  const char *newline = memchr (input, '\n', n);
  if (!newline)
    return eof ? n : -1;

  ptrdiff_t ofs = newline - input;
  if (ofs == 0 || (ofs == 1 && input[0] == '\r'))
    {
      *type = SEG_NEWLINE;
      return ofs + 1;
    }
  else
    return ofs - (input[ofs - 1] == '\r');
}

/* We are in the body of DO REPEAT, segmenting the lines of syntax that are to
   be repeated.  Report each line of syntax as a single SEG_DO_REPEAT_COMMAND.

   DO REPEAT can be nested, so we look for DO REPEAT...END REPEAT blocks inside
   the lines we're segmenting.  s->substate counts the nesting level, starting
   at 1. */
static int
segmenter_parse_do_repeat_3__ (struct segmenter *s,
                               const char *input, size_t n, bool eof,
                               enum segment_type *type)
{
  int ofs;

  ofs = segmenter_parse_full_line__ (input, n, eof, type);
  if (ofs < 0 || (ofs > 0 && input[ofs - 1] == '\n'))
    return ofs;
  else if (!check_repeat_command (s, input, n, eof) && !eof)
    return -1;
  else if (s->substate == 0)
    {
      /* Nesting level dropped to 0, so we've finished reading the DO REPEAT
         body. */
      s->state = S_GENERAL;
      s->substate = SS_START_OF_COMMAND | SS_START_OF_LINE;
      return segmenter_push (s, input, n, eof, type);
    }
  else
    {
      *type = SEG_DO_REPEAT_COMMAND;
      return ofs;
    }
}

/* We are segmenting a DEFINE command, which consists of:

  - The DEFINE keyword.

  - An identifier.  We transform this into SEG_MACRO_NAME instead of
    SEG_IDENTIFIER or SEG_MACRO_NAME because this identifier must never be
    macro-expanded.

  - Anything but "(".

  - "(" followed by a sequence of tokens possibly including balanced parentheses
    up to a final ")".

  - A sequence of any number of lines, one string per line, ending with
    "!ENDDEFINE".  The first line is usually blank (that is, a newline follows
    the "(").  The last line usually just has "!ENDDEFINE." on it, but it can
    start with other tokens.  The whole DEFINE...!ENDDEFINE can be on a single
    line, even.
   */
static int
segmenter_parse_define_1_2__ (struct segmenter *s,
                              const char *input, size_t n, bool eof,
                              enum segment_type *type)
{
  int ofs = segmenter_subparse (s, input, n, eof, type);
  if (ofs < 0)
    return -1;

  if (s->state == S_DEFINE_1
      && (*type == SEG_IDENTIFIER || *type == SEG_MACRO_ID))
    {
      *type = SEG_MACRO_NAME;
      s->state = S_DEFINE_2;
    }
  else if (*type == SEG_SEPARATE_COMMANDS
      || *type == SEG_END_COMMAND
      || *type == SEG_START_COMMAND)
    {
      /* The DEFINE command is malformed because we reached its end without
         ever hitting a "(" token.  Transition back to general parsing. */
      s->state = S_GENERAL;
      return ofs;
    }
  else if (*type == SEG_PUNCT && input[0] == '(')
    {
      s->state = S_DEFINE_3;
      s->nest = 1;
      return ofs;
    }

  return ofs;
}

static int
segmenter_parse_define_3__ (struct segmenter *s,
                            const char *input, size_t n, bool eof,
                            enum segment_type *type)
{
  int ofs = segmenter_subparse (s, input, n, eof, type);
  if (ofs < 0)
    return -1;

  if (*type == SEG_SEPARATE_COMMANDS
      || *type == SEG_END_COMMAND
      || *type == SEG_START_COMMAND)
    {
      /* The DEFINE command is malformed because we reached its end before
         closing the set of parentheses.  Transition back to general
         parsing. */
      s->state = S_GENERAL;
      return ofs;
    }
  else if (*type == SEG_PUNCT && input[0] == '(')
    {
      s->nest++;
      return ofs;
    }
  else if (*type == SEG_PUNCT && input[0] == ')')
    {
      s->nest--;
      if (!s->nest)
        {
          s->state = S_DEFINE_4;
          s->substate = 0;
        }
      return ofs;
    }

  return ofs;
}

static size_t
find_enddefine (struct substring input)
{
  size_t n = input.length;
  const struct substring enddefine = ss_cstr ("!ENDDEFINE");
  for (int ofs = 0;;)
    {
      /* Skip !ENDDEFINE in comment. */
      ofs = skip_spaces_and_comments (input.string, n, true, ofs);
      if (ofs + enddefine.length > n)
        return SIZE_MAX;

      char c = input.string[ofs];
      if (c == '!'
               && ss_equals_case (ss_substr (input, ofs, enddefine.length),
                                  enddefine))
        return ofs;
      else if (c == '\'' || c == '"')
        {
          /* Skip quoted !ENDDEFINE. */
          ofs++;
          for (;;)
            {
              if (ofs >= n)
                return SIZE_MAX;
              else if (input.string[ofs++] == c)
                break;
            }
        }
      else
        ofs++;
    }
}

/* We are in the body of a macro definition, looking for additional lines of
   the body or !ENDDEFINE. */
static int
segmenter_parse_define_4__ (struct segmenter *s,
                            const char *input, size_t n, bool eof,
                            enum segment_type *type)
{
  /* Gather a whole line. */
  const char *newline = memchr (input, '\n', n);
  int ofs = (newline ? newline - input - (newline > input && newline[-1] == '\r')
             : eof ? n
             : -1);
  if (ofs < 0)
    return -1;

  /* Does the line contain !ENDDEFINE? */
  size_t end = find_enddefine (ss_buffer (input, ofs));
  if (end == SIZE_MAX)
    {
      /* No !ENDDEFINE.  We have a full line of macro body.

         The line might be blank, whether completely empty or just spaces and
         comments.  That's OK: we need to report blank lines because they can
         have significance.

         However, if the first line of the macro body (the same line as the
         closing parenthesis in the argument definition) is blank, we just
         report it as spaces because it's not significant. */
      *type = (s->substate == 0 && is_all_spaces (input, ofs)
               ? SEG_SPACES : SEG_MACRO_BODY);
      s->state = S_DEFINE_5;
      s->substate = 1;
      return ofs;
    }
  else
    {
      /* Macro ends at the !ENDDEFINE on this line. */
      s->state = S_GENERAL;
      s->substate = 0;
      if (!end)
        {
          /* Line starts with !ENDDEFINE. */
          return segmenter_push (s, input, n, eof, type);
        }
      else
        {
          if (is_all_spaces (input, end))
            {
              /* Line starts with spaces followed by !ENDDEFINE. */
              *type = SEG_SPACES;
            }
          else
            {
              /* Line starts with some content followed by !ENDDEFINE. */
              *type = SEG_MACRO_BODY;
            }
          return end;
        }
    }
}

static int
segmenter_parse_define_5__ (struct segmenter *s,
                            const char *input, size_t n, bool eof,
                            enum segment_type *type)
{
  int ofs = segmenter_parse_newline__ (input, n, eof, type);
  if (ofs < 0)
    return -1;

  s->state = S_DEFINE_4;
  return ofs;
}

/* We're segmenting the first line of a two-line BEGIN DATA command.  Segment
   up to the first new-line.

   This BEGIN DATA is expressed something like this (weird, but legal):

      BEGIN
       DATA. */
static int
segmenter_parse_begin_data_1__ (struct segmenter *s,
                                const char *input, size_t n, bool eof,
                                enum segment_type *type)
{
  int ofs = segmenter_subparse (s, input, n, eof, type);
  if (ofs < 0)
    return -1;

  if (*type == SEG_NEWLINE)
    s->state = S_BEGIN_DATA_2;

  return ofs;
}

/* We're segmenting a one-line BEGIN DATA command, or the second line of a
   two-line BEGIN DATA command.  Segment up to the new-line. */
static int
segmenter_parse_begin_data_2__ (struct segmenter *s,
                                const char *input, size_t n, bool eof,
                                enum segment_type *type)
{
  int ofs = segmenter_subparse (s, input, n, eof, type);
  if (ofs < 0)
    return -1;

  if (*type == SEG_NEWLINE)
    s->state = S_BEGIN_DATA_3;
  else if (*type == SEG_END_COMMAND)
    *type = SEG_INNER_END_COMMAND;

  return ofs;
}

static bool
is_end_data (const char *input, size_t n)
{
  const uint8_t *u_input = CHAR_CAST (const uint8_t *, input);
  bool endcmd;
  ucs4_t uc;
  int mblen;
  int ofs;

  if (n < 4 || c_strncasecmp (input, "END", 3))
    return false;

  ofs = 3;
  mblen = u8_mbtouc (&uc, u_input + ofs, n - ofs);
  if (!lex_uc_is_space (uc))
    return false;
  ofs += mblen;

  if (n - ofs < 4 || c_strncasecmp (input + ofs, "DATA", 4))
    return false;
  ofs += 4;

  endcmd = false;
  while (ofs < n)
    {
      mblen = u8_mbtouc (&uc, u_input + ofs, n - ofs);
      if (uc == '.')
        {
          if (endcmd)
            return false;
          endcmd = true;
        }
      else if (!lex_uc_is_space (uc))
        return false;
      ofs += mblen;
    }

  return true;
}

static int
segmenter_parse_begin_data_3__ (struct segmenter *s,
                                const char *input, size_t n, bool eof,
                                enum segment_type *type)
{
  int ofs;

  ofs = segmenter_parse_full_line__ (input, n, eof, type);
  if (ofs < 0)
    return -1;
  else if (is_end_data (input, ofs))
    {
      s->state = S_GENERAL;
      s->substate = SS_START_OF_COMMAND | SS_START_OF_LINE;
      return segmenter_push (s, input, n, eof, type);
    }
  else
    {
      *type = SEG_INLINE_DATA;
      s->state = S_BEGIN_DATA_4;
      return input[ofs - 1] == '\n' ? 0 : ofs;
    }
}

static int
segmenter_parse_begin_data_4__ (struct segmenter *s,
                                const char *input, size_t n, bool eof,
                                enum segment_type *type)
{
  int ofs;

  ofs = segmenter_parse_newline__ (input, n, eof, type);
  if (ofs < 0)
    return -1;

  s->state = S_BEGIN_DATA_3;
  return ofs;
}

/* Returns the name of segment TYPE as a string.  The caller must not modify
   or free the returned string.

   This is useful only for debugging and testing. */
const char *
segment_type_to_string (enum segment_type type)
{
  switch (type)
    {
#define SEG_TYPE(NAME) case SEG_##NAME: return #NAME;
      SEG_TYPES
#undef SEG_TYPE
    default:
      return "unknown segment type";
    }
}

/* Returns a segmenter with the given syntax MODE.

   If IS_SNIPPET is false, then the segmenter will parse as if it's being given
   a whole file.  This means, for example, that it will interpret - or + at the
   beginning of the syntax as a separator between commands (since - or + at the
   beginning of a line has this meaning).

   If IS_SNIPPET is true, then the segmenter will parse as if it's being given
   an isolated piece of syntax.  This means that, for example, that it will
   interpret - or + at the beginning of the syntax as an operator token or (if
   followed by a digit) as part of a number.

   A segmenter does not contain any external references, so nothing needs to be
   done to destroy one.  For the same reason, segmenters may be copied with
   plain struct assignment (or memcpy). */
struct segmenter
segmenter_init (enum segmenter_mode mode, bool is_snippet)
{
  return (struct segmenter) {
    .state = is_snippet ? S_GENERAL : S_SHBANG,
    .mode = mode,
  };
}

/* Returns the mode passed to segmenter_init() for S. */
enum segmenter_mode
segmenter_get_mode (const struct segmenter *s)
{
  return s->mode;
}

/* Attempts to label a prefix of S's remaining input with a segment type.  The
   caller supplies the first N bytes of the remaining input as INPUT, which
   must be a UTF-8 encoded string.  If EOF is true, then the N bytes supplied
   are the entire (remainder) of the input; if EOF is false, then further input
   is potentially available.

   The input may contain '\n' or '\r\n' line ends in any combination.

   If successful, returns the number of bytes in the segment at the beginning
   of INPUT (between 0 and N, inclusive) and stores the type of that segment
   into *TYPE.  The next call to segmenter_push() should not include those
   bytes as part of INPUT, because they have (figuratively) been consumed by
   the segmenter.

   Segments can have zero length, including segment types SEG_END,
   SEG_SEPARATE_COMMANDS, SEG_START_DOCUMENT, SEG_INLINE_DATA, and SEG_SPACES.

   Failure occurs only if the segment type of the N bytes in INPUT cannot yet
   be determined.  In this case segmenter_push() returns -1.  If more input is
   available, the caller should obtain some more, then call again with a larger
   N.  If this is not enough, the process might need to repeat again and agin.
   If input is exhausted, then the caller may call again setting EOF to true.
   segmenter_push() will never return -1 when EOF is true.

   The caller must not, in a sequence of calls, supply contradictory input.
   That is, bytes provided as part of INPUT in one call, but not consumed, must
   not be provided with *different* values on subsequent calls.  This is
   because segmenter_push() must often make decisions based on looking ahead
   beyond the bytes that it consumes. */
int
segmenter_push (struct segmenter *s, const char *input, size_t n, bool eof,
                enum segment_type *type)
{
  if (!n)
    {
      if (eof)
        {
          *type = SEG_END;
          return 0;
        }
      else
        return -1;
    }

  switch (s->state)
    {
    case S_SHBANG:
      return segmenter_parse_shbang__ (s, input, n, eof, type);

    case S_GENERAL:
      return (s->substate & SS_START_OF_LINE
              ? segmenter_parse_start_of_line__ (s, input, n, eof, type)
              : segmenter_parse_mid_command__ (s, input, n, eof, type));

    case S_COMMENT_1:
      return segmenter_parse_comment_1__ (s, input, n, eof, type);
    case S_COMMENT_2:
      return segmenter_parse_comment_2__ (s, input, n, eof, type);

    case S_DOCUMENT_1:
      return segmenter_parse_document_1__ (s, input, n, eof, type);
    case S_DOCUMENT_2:
      return segmenter_parse_document_2__ (s, input, n, eof, type);
    case S_DOCUMENT_3:
      return segmenter_parse_document_3__ (s, type);

    case S_FILE_LABEL_1:
      return segmenter_parse_file_label_1__ (s, input, n, eof, type);
    case S_FILE_LABEL_2:
      return segmenter_parse_file_label_2__ (s, input, n, eof, type);
    case S_FILE_LABEL_3:
      return segmenter_parse_file_label_3__ (s, input, n, eof, type);

    case S_DO_REPEAT_1:
      return segmenter_parse_do_repeat_1__ (s, input, n, eof, type);
    case S_DO_REPEAT_2:
      return segmenter_parse_do_repeat_2__ (s, input, n, eof, type);
    case S_DO_REPEAT_3:
      return segmenter_parse_do_repeat_3__ (s, input, n, eof, type);

    case S_DEFINE_1:
    case S_DEFINE_2:
      return segmenter_parse_define_1_2__ (s, input, n, eof, type);
    case S_DEFINE_3:
      return segmenter_parse_define_3__ (s, input, n, eof, type);
    case S_DEFINE_4:
      return segmenter_parse_define_4__ (s, input, n, eof, type);
    case S_DEFINE_5:
      return segmenter_parse_define_5__ (s, input, n, eof, type);

    case S_BEGIN_DATA_1:
      return segmenter_parse_begin_data_1__ (s, input, n, eof, type);
    case S_BEGIN_DATA_2:
      return segmenter_parse_begin_data_2__ (s, input, n, eof, type);
    case S_BEGIN_DATA_3:
      return segmenter_parse_begin_data_3__ (s, input, n, eof, type);
    case S_BEGIN_DATA_4:
      return segmenter_parse_begin_data_4__ (s, input, n, eof, type);
    }

  NOT_REACHED ();
}

/* Returns the style of command prompt to display to an interactive user for
   input in S.  The return value is most accurate in mode SEG_MODE_INTERACTIVE
   and at the beginning of a line (that is, if segmenter_push() consumed as
   much as possible of the input up to a new-line).  */
enum prompt_style
segmenter_get_prompt (const struct segmenter *s)
{
  switch (s->state)
    {
    case S_SHBANG:
      return PROMPT_FIRST;

    case S_GENERAL:
      return s->substate & SS_START_OF_COMMAND ? PROMPT_FIRST : PROMPT_LATER;

    case S_COMMENT_1:
    case S_COMMENT_2:
      return PROMPT_COMMENT;

    case S_DOCUMENT_1:
    case S_DOCUMENT_2:
      return PROMPT_DOCUMENT;
    case S_DOCUMENT_3:
      return PROMPT_FIRST;

    case S_FILE_LABEL_1:
      return PROMPT_LATER;
    case S_FILE_LABEL_2:
    case S_FILE_LABEL_3:
      return PROMPT_FIRST;

    case S_DO_REPEAT_1:
    case S_DO_REPEAT_2:
      return s->substate & SS_START_OF_COMMAND ? PROMPT_FIRST : PROMPT_LATER;
    case S_DO_REPEAT_3:
      return PROMPT_DO_REPEAT;

    case S_DEFINE_1:
    case S_DEFINE_2:
    case S_DEFINE_3:
      return s->substate & SS_START_OF_COMMAND ? PROMPT_FIRST : PROMPT_LATER;
    case S_DEFINE_4:
    case S_DEFINE_5:
      return PROMPT_DEFINE;

    case S_BEGIN_DATA_1:
      return PROMPT_FIRST;
    case S_BEGIN_DATA_2:
      return PROMPT_LATER;
    case S_BEGIN_DATA_3:
    case S_BEGIN_DATA_4:
      return PROMPT_DATA;

    }

  NOT_REACHED ();
}
