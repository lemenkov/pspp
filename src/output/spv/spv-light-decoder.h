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

#ifndef OUTPUT_SPV_LIGHT_DECODER_H
#define OUTPUT_SPV_LIGHT_DECODER_H 1

/* SPSS Viewer (SPV) light binary decoder.

   Used by spv.h, not useful directly. */

#include "libpspp/compiler.h"

struct pivot_table;
struct spvlb_table;
struct string_array;

char *decode_spvlb_table (const struct spvlb_table *,
                          struct pivot_table **outp)
  WARN_UNUSED_RESULT;

void collect_spvlb_strings (const struct spvlb_table *, struct string_array *);

const char *spvlb_table_get_encoding (const struct spvlb_table *);

#endif /* output/spv/spv-light-decoder.h */
