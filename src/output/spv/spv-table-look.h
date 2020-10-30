/* PSPP - a program for statistical analysis.
   Copyright (C) 2018, 2020 Free Software Foundation, Inc.

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

#ifndef OUTPUT_SPV_TABLE_LOOK_H
#define OUTPUT_SPV_TABLE_LOOK_H 1

/* TableLook decoder.

   A TableLook specifies styles for tables and other aspects of output.  They
   exist standalone as .stt files as well as embedded in structure XML, in
   either case as a tableProperties element.
*/

#include <stdbool.h>
#include "libpspp/compiler.h"
#include "output/pivot-table.h"
#include "output/table.h"

struct spvsx_table_properties;

struct spv_table_look
  {
    char *name;                 /* May be null. */

    /* General properties. */
    bool omit_empty;
    int width_ranges[TABLE_N_AXES][2];      /* In 1/96" units. */
    bool row_labels_in_corner;

    /* Footnote display settings. */
    bool show_numeric_markers;
    bool footnote_marker_superscripts;

    /* Styles. */
    struct table_area_style areas[PIVOT_N_AREAS];
    struct table_border_style borders[PIVOT_N_BORDERS];

    /* Print settings. */
    bool print_all_layers;
    bool paginate_layers;
    bool shrink_to_width;
    bool shrink_to_length;
    bool top_continuation, bottom_continuation;
    char *continuation;
    size_t n_orphan_lines;
  };

void spv_table_look_destroy (struct spv_table_look *);

char *spv_table_look_decode (const struct spvsx_table_properties *,
                             struct spv_table_look **)
  WARN_UNUSED_RESULT;
char *spv_table_look_read (const char *, struct spv_table_look **)
  WARN_UNUSED_RESULT;
char *spv_table_look_write (const char *, const struct spv_table_look *)
  WARN_UNUSED_RESULT;

void spv_table_look_install (const struct spv_table_look *,
                             struct pivot_table *);
struct spv_table_look *spv_table_look_get (const struct pivot_table *);

#endif /* output/spv/spv-table-look.h */
