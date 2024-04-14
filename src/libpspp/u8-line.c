/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012, 2020 Free Software Foundation, Inc.

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

#include "libpspp/u8-line.h"
#include <unistr.h>
#include <uniwidth.h>
#include "libpspp/cast.h"
#include "libpspp/str.h"

/* Initializes LINE as an empty u8_line. */
void
u8_line_init (struct u8_line *line)
{
  ds_init_empty (&line->s);
  line->width = 0;
}

/* Frees data owned by LINE. */
void
u8_line_destroy (struct u8_line *line)
{
  ds_destroy (&line->s);
}

/* Clears LINE to zero length. */
void
u8_line_clear (struct u8_line *line)
{
  ds_clear (&line->s);
  line->width = 0;
}

static int
u8_mb_to_display (int *wp, const uint8_t *s, size_t n)
{
  size_t ofs;
  ucs4_t uc;
  int w;

  ofs = u8_mbtouc (&uc, s, n);
  if (ofs < n && s[ofs] == '\b')
    {
      ofs++;
      ofs += u8_mbtouc (&uc, s + ofs, n - ofs);
    }

  w = uc_width (uc, "UTF-8");
  if (w <= 0)
    {
      *wp = 0;
      return ofs;
    }

  while (ofs < n)
    {
      int mblen = u8_mbtouc (&uc, s + ofs, n - ofs);
      if (uc_width (uc, "UTF-8") > 0)
        break;
      ofs += mblen;
    }

  *wp = w;
  return ofs;
}

/* Position of one or more characters within a u8_line. */
struct u8_pos
  {
    /* 0-based display columns.

       For one single-width character, x1 == x0 + 1.
       For one double-width character, x1 == x0 + 2. */
    int x0;
    int x1;

    /* Byte offsets.

       For one ordinary ASCII character, ofs1 == ofs0 + 1.
       For one Unicode code point 0x80 or higher, 2 <= ofs1 - ofs0 <= 4. */
    size_t ofs0;
    size_t ofs1;
  };

static bool
u8_line_find_pos (const struct u8_line *line, int target_x, struct u8_pos *c)
{
  const uint8_t *s = CHAR_CAST (const uint8_t *, ds_cstr (&line->s));
  size_t length = ds_length (&line->s);
  size_t ofs;
  int mblen;
  int x;

  x = 0;
  c->x0 = 0;
  c->ofs0 = 0;
  for (ofs = 0; ofs < length; ofs += mblen)
    {
      int w;
      c->x0 = x;
      c->ofs0 = ofs;
      mblen = u8_mb_to_display (&w, s + ofs, length - ofs);
      if (x + w > target_x)
        {
          c->x1 = x + w;
          c->ofs1 = ofs + mblen;
          return true;
        }
      x += w;
    }

  /* This can happen if there are non-printable characters
     in the line.  */
  c->x1 = x;
  c->ofs1 = ofs;
  return false;
}

static struct u8_pos
u8_line_find_span (const struct u8_line *line, int x0, int x1)
{
  struct u8_pos p0, p1;
  u8_line_find_pos (line, x0, &p0);
  u8_line_find_pos (line, x1, &p1);
  if (p1.x0 >= x1)
    return (struct u8_pos)
      {
        .x0 = p0.x0,
        .ofs0 = p0.ofs0,
        .x1 = p1.x0,
        .ofs1 = p1.ofs0,
      };
  else
    return (struct u8_pos)
      {
        .x0 = p0.x0,
        .ofs0 = p0.ofs0,
        .x1 = p1.x1,
        .ofs1 = p1.ofs1,
      };
}

/* Prepares LINE to write N bytes of characters that comprise X1-X0 column
   widths starting at 0-based column X0.  Returns the first byte of the N for
   the caller to fill in. */
char *
u8_line_reserve (struct u8_line *line, int x0, int x1, int n)
{
  assert (x1 >= x0);
  if (x0 >= line->width)
    {
      /* The common case: adding new characters at the end of a line. */
      ds_put_byte_multiple (&line->s, ' ', x0 - line->width);
      line->width = x1;
      return ds_put_uninit (&line->s, n);
    }
  else if (x0 == x1)
    return NULL;
  else if (x1 >= line->width)
    {
      struct u8_pos p0;
      u8_line_find_pos (line, x0, &p0);

      /* If a double-width character occupies both x0 - 1 and x0, then replace
         its first character width by '?'. */
      char *s = ds_data (&line->s);
      while (p0.x0 < x0)
        {
          s[p0.ofs0++] = '?';
          p0.x0++;
        }

      ds_truncate (&line->s, p0.ofs0);
      line->width = x1;
      return ds_put_uninit (&line->s, n);
    }
  else
    {
      struct u8_pos span = u8_line_find_span (line, x0, x1);

      char *s = ds_data (&line->s);
      while (span.x0 < x0)
        {
          s[span.ofs0++] = '?';
          span.x0++;
        }
      while (span.x1 > x1)
        {
          s[--span.ofs1] = '?';
          span.x1--;
        }
      assert (span.ofs1 >= span.ofs0);
      return ds_splice_uninit (&line->s, span.ofs0, span.ofs1 - span.ofs0, n);
    }
}

/* Writes the N bytes of characters that comprise X1-X0 column widths into LINE
   starting at 0-based column X0. */
void
u8_line_put (struct u8_line *line, int x0, int x1, const char *s, int n)
{
  memcpy (u8_line_reserve (line, x0, x1, n), s, n);
}

/* Changes the width of LINE to X column widths.  If X is longer than LINE's
   previous width, LINE is extended by appending spaces.  If X is shorter than
   LINE's previous width, LINE is shortened by removing trailing characters. */
void
u8_line_set_length (struct u8_line *line, int x)
{
  if (x > line->width)
    {
      ds_put_byte_multiple (&line->s, ' ', x - line->width);
      line->width = x;
    }
  else if (x < line->width)
    {
      struct u8_pos pos;

      u8_line_find_pos (line, x, &pos);
      ds_truncate (&line->s, pos.ofs0);
      line->width = pos.x0;
      if (x > line->width)
        {
          ds_put_byte_multiple (&line->s, '?', x - line->width);
          line->width = x;
        }
    }
}
