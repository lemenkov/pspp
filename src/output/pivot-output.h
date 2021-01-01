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

#ifndef OUTPUT_PIVOT_OUTPUT_H
#define OUTPUT_PIVOT_OUTPUT_H 1

#include <stdbool.h>
#include <stddef.h>

struct pivot_footnote;
struct pivot_table;
struct table;

#define PIVOT_OUTPUT_FOR_EACH_LAYER(INDEXES, PT, PRINT)                 \
  for ((INDEXES) = NULL;                                                \
       ((INDEXES) = pivot_output_next_layer (PT, INDEXES, PRINT)); )
size_t *pivot_output_next_layer (const struct pivot_table *,
                                 size_t *indexes, bool print);

void pivot_output (const struct pivot_table *,
                   const size_t *layer_indexes,
                   bool printing,
                   struct table **titlep,
                   struct table **layersp,
                   struct table **bodyp,
                   struct table **captionp,
                   struct table **footnotesp,
                   struct pivot_footnote ***fp, size_t *nfp);

#endif /* output/pivot-output.h */
