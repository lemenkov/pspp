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

#include "tex-rendering.h"
#include "tex-glyphs.h"

#include "libpspp/hmap.h"
#include "libpspp/hash-functions.h"

#include "gl/mbiter.h"
#include "gl/mbchar.h"
#include "gl/unistr.h"
#include "gl/xalloc.h"

/* Return a string containing TeX code which can  be used to typeset
   Unicode code point CP.  As a side effect, insert any needed macro indeces
   into the hash table MACROS.  */
const char *
code_point_to_tex (ucs4_t cp, struct hmap *macros)
{
  const char *what = 0;

  for (const struct glyph_block *gb = defined_blocks;  gb->start; gb++)
  {
    if (cp < gb->start->code_point)
      break;

    if (cp < gb->start->code_point + gb->n_glyphs)
      {
        what = gb->start[cp - gb->start->code_point].tex_rendering;
        enum tex_ancilliary macro = gb->start[cp - gb->start->code_point].macro;
        if (macro != TEX_NONE)
          {
            struct tex_macro *a_macro = NULL;
            HMAP_FOR_EACH_WITH_HASH (a_macro, struct tex_macro, node, hash_int (0, macro), macros)
              {
                if (a_macro->index == macro)
                  break;
              }

            if (a_macro == NULL)
              {
                a_macro = XMALLOC (struct tex_macro);
                a_macro->index = macro;
                hmap_insert (macros, &a_macro->node, hash_int (0, macro));
              }
          }
        break;
      }
  }

  if (!what)
    fprintf (stderr, "Unsupported code point U+%04X\n", cp);
  return what ? what : unsupported_glyph;
}

/* Convert the first character of the utf8 string S, into a TeX fragment.
   LEN must be the length of S (in bytes).   After this function returns, S
   will have been incremented by the length of the first character in S,
   and LEN will have been decremented by the same amount.   */
const char *
u8_to_tex_fragments (const char **s, size_t *len, struct hmap *macros)
{
  const uint8_t *u = (const uint8_t *) *s;
  size_t clen = u8_mblen (u, *len);

  ucs4_t puc;
  u8_mbtouc (&puc, u, clen);

  *len -= clen;
  *s += clen;

  return code_point_to_tex (puc, macros);
}
