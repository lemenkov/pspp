/* PSPP - a program for statistical analysis.
   Copyright (C) 2020 Free Software Foundation, Inc.

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

#include "gl/xalloc.h"

#include "tex-parsing.h"
#include "libpspp/ll.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum state
  {
   STATE_INITIAL,
   STATE_CS,
   STATE_COMMENT
  };


/* Return the category of C.
   These are the default TeX categories as defined in Chapter 7 of
   The TeXbook  */
static enum tex_cat
category (const char c)
{
  if (c >= 'A' && c <= 'Z')
    return CAT_LETTER;

  if (c >= 'a' && c <= 'z')
    return CAT_LETTER;

  switch (c)
    {
    case '\\':
      return CAT_ESCAPE;
    case '{':
      return CAT_BEGIN_GROUP;
    case '}':
      return CAT_END_GROUP;
    case '$':
      return CAT_MATH_MODE;
    case '&':
      return CAT_ALIGNMENT;
    case '#':
      return CAT_PARAMETER;
    case '^':
      return CAT_SUPERSCRIPT;
    case '_':
      return CAT_SUBSCRIPT;
    case '~':
      return CAT_ACTIVE;
    case ' ':
    case '\t':
      return CAT_SPACE;
    case '\n':
    case '\r':
      return CAT_EOL;
    case '%':
      return CAT_COMMENT;
    case 127:
      return CAT_INVALID;
    case 0:
      return CAT_IGNORED;
    }

  return CAT_OTHER;
}


/* Parse the TeX fragment STR into TeX tokens and push them
   on to LIST. */
void
tex_parse (const char *str, struct ll_list *list)
{
  enum state state = STATE_INITIAL;
  struct tex_token *token = NULL;
  int c;
  while ((c = *str++) != '\0')
    {
      enum tex_cat cat = category (c);

      if (state == STATE_COMMENT)
        {
          ds_put_byte (&token->str, c);
          if (cat == CAT_EOL)
            {
              token->cat = CAT_COMMENT;
              ll_push_tail (list, &token->ll);
              state = STATE_INITIAL;
            }
        }
      else if (state == STATE_INITIAL)
        {
          token = XZALLOC (struct tex_token);
          ds_init_empty (&token->str);
          if (cat == CAT_COMMENT)
            {
              ds_put_byte (&token->str, c);
              state = STATE_COMMENT;
            }
          else if (cat == CAT_ESCAPE)
            {
              ds_put_byte (&token->str, c);
              state = STATE_CS;
            }
          else
            {
              ds_put_byte (&token->str, c);
              token->cat = category (c);
              ll_push_tail (list, &token->ll);
            }
        }
      else if (state == STATE_CS)
        {
          ds_put_byte (&token->str, c);
          if (cat != CAT_LETTER)
            {
              if (ds_length (&token->str) > 2)
                {
                  ds_truncate (&token->str, ds_length (&token->str) - 1);
                  str--;
                }
              token->cat = CAT_CONTROL_SEQ;
              ll_push_tail (list, &token->ll);
              state = STATE_INITIAL;
            }
        }
    }
  if (state == STATE_CS)
    {
      /* The end of the string was encountered whilst processing
         a control sequence.  */

      /* A \ at the end of the string must be erroneous.  */
      assert (ds_length (&token->str) > 1);
      token->cat = CAT_CONTROL_SEQ;
      ll_push_tail (list, &token->ll);
      state = STATE_INITIAL;
    }

  assert (state == STATE_INITIAL);
}
