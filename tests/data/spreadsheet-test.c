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
#include "progname.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include <data/spreadsheet-reader.h>
#include <data/gnumeric-reader.h>
#include <data/ods-reader.h>

enum OPT
  {
   OPT_REFCHECK = 0x100,
   OPT_REVERSE,
   OPT_SHEET,
   OPT_METADATA
  };

static const struct option long_opts[] =
  {
   {"refcheck", no_argument, NULL, OPT_REFCHECK},
   {"reverse", no_argument, NULL, OPT_REVERSE},
   {"sheet", required_argument, NULL, OPT_SHEET},
   {"metadata", no_argument, NULL, OPT_METADATA},
   {0, 0, 0, 0}
  };

int
main (int argc, char **argv)
{
  set_program_name (argv[0]);

  bool refcheck = false;
  bool reverse = false;
  int sheet = 0;
  bool get_n_sheets = false;
  int opt;
  while ((opt = getopt_long (argc, argv, "", long_opts, NULL)) != -1)
    {
      switch (opt)
        {
	case OPT_METADATA:
	  get_n_sheets = true;
	  break;
        case OPT_REFCHECK:
          refcheck = true;
          break;
        case OPT_REVERSE:
          reverse = true;
          break;
        case OPT_SHEET:
          sheet = atoi (optarg);
          break;
        default: /* '?' */
          fprintf (stderr, "Usage: spreadsheet-test [opts] file\n");
          exit (EXIT_FAILURE);
        }
    }

  if (argc < optind)
    {
      fprintf (stderr, "Usage: spreadsheet-test [-s n] file\n");
      exit (EXIT_FAILURE);
    }

  struct spreadsheet *ss = NULL;

  char *ext = strrchr (argv[optind], '.');
  if (ext == NULL)
    return 1;
  if (0 == strcmp (ext, ".ods"))
    ss = ods_probe (argv[optind], true);
  else if (0 == strcmp (ext, ".gnumeric"))
    ss = gnumeric_probe (argv[optind], true);

  if (ss == NULL)
    return 1;

  if (get_n_sheets)
    {
      int n_sheets = spreadsheet_get_sheet_n_sheets (ss);
      printf ("Number of sheets: %d\n", n_sheets);
      goto end;
    }
  int rows = spreadsheet_get_sheet_n_rows (ss, sheet);
  int columns = spreadsheet_get_sheet_n_columns (ss, sheet);

  printf ("Rows %d; Columns %d\n", rows, columns);
  for (int r_ = 0; r_ < rows; r_++)
    {
      int r = reverse ? (rows - r_ - 1) : r_;
      for (int c_ = 0; c_ < columns; c_++)
        {
	  int c = reverse ? (columns - c_ - 1) : c_ ;
          char *s = spreadsheet_get_cell (ss, sheet, r, c);
          if (refcheck)
            {
              int row, col;
              sscanf (s, "%d:%d", &row, &col);
              assert (row == r);
              assert (col == c);
            }
	  else
            {
              fputs (s ? s : "", stdout);
	      if (c_ < columns - 1)
		putchar ('\t');
            }


          free (s);
        }
      if (!refcheck)
        {
	  putchar ('\n');
        }
    }

  rows = spreadsheet_get_sheet_n_rows (ss, sheet);
  columns = spreadsheet_get_sheet_n_columns (ss, sheet);

 end:
  spreadsheet_unref (ss);

  return 0;
}
