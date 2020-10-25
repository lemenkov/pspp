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

#ifndef TEX_GLYPHS_H
#define TEX_GLYPHS_H 1

#include "gl/unitypes.h"

#include "libpspp/hmap.h"


enum tex_ancilliary
  {
   TEX_NONE,
   TEX_VULGAR_FRAC,
   TEX_OGONEK,
   TEX_THORN_UC,
   TEX_THORN_LC,
   TEX_GUILLEMET_LEFT,
   TEX_GUILLEMET_RIGHT,
   TEX_ETH,
   TEX_DOT,
   TEX_DOUBLE_ACUTE
  };

extern const char *tex_macro[];

struct tex_macro
{
  struct hmap_node node;
  enum tex_ancilliary index;
};


struct glyph
{
  ucs4_t code_point;
  const char *name;
  enum tex_ancilliary macro;
  const char *tex_rendering;
};

struct glyph_block
{
  const struct glyph *start;
  int n_glyphs;
};


extern const char *unsupported_glyph;

extern const struct glyph_block defined_blocks[] ;

#endif
