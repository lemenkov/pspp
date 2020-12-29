/* PSPP - a program for statistical analysis.
   Copyright (C) 2018 Free Software Foundation, Inc.

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

#include "spv-css-parser.h"

#include <stdlib.h>
#include <string.h>

#include "libpspp/str.h"
#include "output/options.h"
#include "output/pivot-table.h"
#include "spv.h"

#include "gl/c-ctype.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"

enum css_token_type
  {
    T_EOF,
    T_ID,
    T_LCURLY,
    T_RCURLY,
    T_COLON,
    T_SEMICOLON,
    T_ERROR
  };

struct css_token
  {
    enum css_token_type type;
    char *s;
  };

static char *
css_skip_spaces (char *p)
{
  for (;;)
    {
      if (c_isspace (*p))
        p++;
      else if (!strncmp (p, "<!--", 4))
        p += 4;
      else if (!strncmp (p, "-->", 3))
        p += 3;
      else
        return p;
    }
}

static bool
css_is_separator (unsigned char c)
{
  return c_isspace (c) || strchr ("{}:;", c);
}

static void
css_token_get (char **p_, struct css_token *token)
{
  char *p = *p_;

  free (token->s);
  token->s = NULL;

  p = css_skip_spaces (p);
  if (*p == '\0')
    token->type = T_EOF;
  else if (*p == '{')
    {
      token->type = T_LCURLY;
      p++;
    }
  else if (*p == '}')
    {
      token->type = T_RCURLY;
      p++;
    }
  else if (*p == ':')
    {
      token->type = T_COLON;
      p++;
    }
  else if (*p == ';')
    {
      token->type = T_SEMICOLON;
      p++;
    }
  else
    {
      token->type = T_ID;
      char *start = p;
      while (!css_is_separator (*p))
        p++;
      token->s = xmemdup0 (start, p - start);
    }
  *p_ = p;
}

static void
css_decode_key_value (const char *key, const char *value,
                      struct font_style *font)
{
  if (!strcmp (key, "color"))
    {
      struct cell_color color;
      if (parse_color__ (value, &color))
        font->fg[0] = font->bg[0] = color;
    }
  else if (!strcmp (key, "font-weight"))
    font->bold = !strcmp (value, "bold");
  else if (!strcmp (key, "font-style"))
    font->italic = !strcmp (value, "italic");
  else if (!strcmp (key, "font-decoration"))
    font->underline = !strcmp (value, "underline");
  else if (!strcmp (key, "font-family"))
    {
      free (font->typeface);
      font->typeface = xstrdup (value);
    }
  else if (!strcmp (key, "font-size"))
    font->size = atoi (value) * 3 / 4;

  /* bg_color */

}

char *
spv_parse_css_style (char *style, struct font_style *font)
{
  *font = (struct font_style) FONT_STYLE_INITIALIZER;

  char *p = style;
  struct css_token token = { .s = NULL };
  css_token_get (&p, &token);
  while (token.type != T_EOF)
    {
      if (token.type != T_ID || !strcmp (token.s, "p"))
        {
          css_token_get (&p, &token);
          continue;
        }

      char *key = token.s;
      token.s = NULL;
      css_token_get (&p, &token);

      if (token.type == T_COLON)
        {
          struct string value = DS_EMPTY_INITIALIZER;
          for (;;)
            {
              css_token_get (&p, &token);
              if (token.type != T_ID)
                break;
              if (!ds_is_empty (&value))
                ds_put_byte (&value, ' ');
              ds_put_cstr (&value, token.s);
            }

          css_decode_key_value (key, ds_cstr (&value), font);

          ds_destroy (&value);
        }
      free (key);
    }
  return NULL;
}
