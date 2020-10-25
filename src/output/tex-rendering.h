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

#ifndef TEX_RENDERING_H
#define TEX_RENDERING_H

#include "gl/unitypes.h"
#include <stddef.h>

struct hmap;

/* Return a string containing TeX code which can  be used to typeset
   Unicode code point CP.  */
const char * code_point_to_tex (ucs4_t cp, struct hmap *macros);


/* Convert the first character of the utf8 string S, into a TeX fragment.
   LEN must be the length of S (in bytes).   After this function returns, S
   will have been incremented by the length of the first character in S,
   and LEN will have been decremented by the same amount.   */
const char * u8_to_tex_fragments (const char **s, size_t *len, struct hmap *macros);


#endif
