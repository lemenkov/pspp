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

/* TableLook file decoder.

   A TableLook specifies styles for tables and other aspects of output.  They
   exist standalone as .stt files as well as embedded in structure XML, in
   either case as a tableProperties element.
*/

#include <stdbool.h>
#include "libpspp/compiler.h"

struct pivot_table_look;
struct spvsx_table_properties;

char *spv_table_look_decode (const struct spvsx_table_properties *,
                             struct pivot_table_look **)
  WARN_UNUSED_RESULT;
char *spv_table_look_read (const char *, struct pivot_table_look **)
  WARN_UNUSED_RESULT;
char *spv_table_look_write (const char *, const struct pivot_table_look *)
  WARN_UNUSED_RESULT;

#endif /* output/spv/spv-table-look.h */
