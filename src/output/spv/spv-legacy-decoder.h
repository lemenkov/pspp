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

#ifndef OUTPUT_SPV_LEGACY_DECODER_H
#define OUTPUT_SPV_LEGACY_DECODER_H 1

/* SPSS Viewer (SPV) legacy decoder.

   Used by spv.h, not useful directly. */

#include "libpspp/compiler.h"

struct pivot_table;
struct spvdx_visualization;
struct spv_data;
struct pivot_table_look;

char *decode_spvdx_table (const struct spvdx_visualization *,
                          const char *subtype,
                          const struct pivot_table_look *,
                          struct spv_data *,
                          struct pivot_table **outp)
  WARN_UNUSED_RESULT;

#endif /* output/spv/spv-legacy-decoder.h */
