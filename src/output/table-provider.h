/* PSPP - a program for statistical analysis.
   Copyright (C) 1997, 1998, 1999, 2000, 2009, 2011, 2013, 2014, 2018 Free Software Foundation, Inc.

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

#ifndef OUTPUT_TABLE_PROVIDER
#define OUTPUT_TABLE_PROVIDER 1

#include <stdint.h>
#include "output/table.h"

struct pool;
struct string;

enum table_halign table_halign_interpret (enum table_halign, bool numeric);

/* A cell in a table. */
struct table_cell
  {
    /* Occupied table region.

       d[TABLE_HORZ][0] is the leftmost column.
       d[TABLE_HORZ][1] is the rightmost column, plus 1.
       d[TABLE_VERT][0] is the top row.
       d[TABLE_VERT][1] is the bottom row, plus 1.

       For an ordinary cell:
           d[TABLE_HORZ][1] == d[TABLE_HORZ][0] + 1
       and d[TABLE_VERT][1] == d[TABLE_VERT][0] + 1

       For a joined cell:
          d[TABLE_HORZ][1] > d[TABLE_HORZ][0] + 1
       or d[TABLE_VERT][1] > d[TABLE_VERT][0] + 1
       or both. */
    int d[TABLE_N_AXES][2];

    unsigned int options;       /* TAB_*. */
    const struct pivot_value *value;
    const struct font_style *font_style;
    const struct cell_style *cell_style;
  };

/* Returns the number of columns that CELL spans.  This is 1 for an ordinary
   cell and greater than one for a cell that joins multiple columns. */
static inline int
table_cell_colspan (const struct table_cell *cell)
{
  return cell->d[TABLE_HORZ][1] - cell->d[TABLE_HORZ][0];
}

/* Returns the number of rows that CELL spans.  This is 1 for an ordinary cell
   and greater than one for a cell that joins multiple rows. */
static inline int
table_cell_rowspan (const struct table_cell *cell)
{
  return cell->d[TABLE_VERT][1] - cell->d[TABLE_VERT][0];
}

/* Returns true if CELL is a joined cell, that is, if it spans multiple rows
   or columns.  Otherwise, returns false. */
static inline bool
table_cell_is_joined (const struct table_cell *cell)
{
  return table_cell_colspan (cell) > 1 || table_cell_rowspan (cell) > 1;
}

/* For use primarily by output drivers. */

void table_get_cell (const struct table *, int x, int y, struct table_cell *);
int table_get_rule (const struct table *, enum table_axis, int x, int y,
                    struct cell_color *);

#endif /* output/table-provider.h */
