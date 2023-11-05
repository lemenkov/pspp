/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011, 2013, 2020 Free Software Foundation, Inc.

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

#include "spreadsheet-reader.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "data/gnumeric-reader.h"
#include "data/ods-reader.h"
#include "libpspp/assertion.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"
#include "gl/c-xvasprintf.h"
#include "gl/intprops.h"

struct spreadsheet *
spreadsheet_ref (struct spreadsheet *s)
{
  s->ref_cnt++;
  return s;
}

void
spreadsheet_unref (struct spreadsheet *s)
{
  if (--s->ref_cnt == 0)
    s->destroy (s);
}


struct casereader *
spreadsheet_make_reader (struct spreadsheet *s,
                         const struct spreadsheet_read_options *opts)
{
  return s->make_reader (s, opts);
}

const char *
spreadsheet_get_sheet_name (struct spreadsheet *s, int n)
{
  return s->get_sheet_name (s, n);
}


char *
spreadsheet_get_sheet_range (struct spreadsheet *s, int n)
{
  return s->get_sheet_range (s, n);
}

int
spreadsheet_get_sheet_n_sheets (struct spreadsheet *s)
{
  return s->get_sheet_n_sheets (s);
}

unsigned int
spreadsheet_get_sheet_n_rows (struct spreadsheet *s, int n)
{
  return s->get_sheet_n_rows (s, n);
}

unsigned int
spreadsheet_get_sheet_n_columns (struct spreadsheet *s, int n)
{
  return s->get_sheet_n_columns (s, n);
}

char *
spreadsheet_get_cell (struct spreadsheet *s, int n, int row, int column)
{
  return s->get_sheet_cell (s, n, row, column);
}


char *
create_cell_ref (int col0, int row0)
{
  if (col0 < 0 || row0 < 0)
    return NULL;

  char s[F26ADIC_STRLEN_MAX + INT_STRLEN_BOUND (row0) + 1];
  str_format_26adic (col0 + 1, true, s, sizeof s);
  size_t len = strlen (s);
  snprintf (s + len, sizeof s - len, "%d", row0 + 1);

  return xstrdup (s);
}

char *
create_cell_range (int col0, int row0, int coli, int rowi)
{
  char *s0 = create_cell_ref (col0, row0);
  char *si = create_cell_ref (coli, rowi);

  char *s =  c_xasprintf ("%s:%s", s0, si);

  free (s0);
  free (si);

  return s;
}


/* Convert a cell reference in the form "A1:B2", to
   integers.  A1 means column zero, row zero.
   B1 means column 1 row 0. AA1 means column 26, row 0.
*/
bool
convert_cell_ref (const char *ref,
                  int *col0, int *row0,
                  int *coli, int *rowi)
{
  char startcol[5];
  char stopcol [5];

  int startrow;
  int stoprow;

  int n = sscanf (ref, "%4[a-zA-Z]%d:%4[a-zA-Z]%d",
              startcol, &startrow,
              stopcol, &stoprow);
  if (n != 4)
    return false;

  *col0 = str_parse_26adic (startcol);
  *coli = str_parse_26adic (stopcol);
  *row0 = startrow - 1;
  *rowi = stoprow - 1 ;

  return true;
}
