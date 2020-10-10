/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2010, 2016 Free Software Foundation, Inc.

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

#ifndef SPREADSHEET_READ_H
#define SPREADSHEET_READ_H 1

#include <stdbool.h>
#include <libpspp/compiler.h>

struct casereeader;

/* Default width of string variables. */
#define SPREADSHEET_DEFAULT_WIDTH 8

/* These elements are read/write.
   They may be passed in NULL (for pointers) or negative for integers, in which
   case they will be filled in be the function.
*/
struct spreadsheet_read_options
{
  char *sheet_name ;       /* The name of the sheet to open (in UTF-8) */
  int sheet_index ;        /* The index of the sheet to open (only used if sheet_name is NULL).
			      The first index is 1 NOT 0 */
  char *cell_range ;       /* The cell range (in UTF-8) */
  bool read_names ;        /* True if the first row is to be used as the names of the variables */
  int asw ;                /* The width of string variables in the created dictionary */
};

int ps26_to_int (const char *str);
char * int_to_ps26 (int);

bool convert_cell_ref (const char *ref,
		       int *col0, int *row0,
		       int *coli, int *rowi);


#define _xml(X) (CHAR_CAST (const xmlChar *, (X)))

#define _xmlchar_to_int(X) ((X) ? atoi (CHAR_CAST (const char *, (X))) : -1)

struct sheet_detail
{
  /* The name of the sheet (utf8 encoding) */
  char *name;

  /* The extents of the sheet.  */
  int first_col;
  int last_col;
  int first_row;
  int last_row;
};

struct spreadsheet
{
  /** General spreadsheet object related things.  */
  int ref_cnt;

  /* A 3 letter string (null terminated) which identifies the type of
     spreadsheet (eg: "ODS" for opendocument; "GNM" for gnumeric etc).  */
  char type[4];

  void (*destroy) (struct spreadsheet *);
  struct casereader* (*make_reader) (struct spreadsheet *,
				    const struct spreadsheet_read_options *);
  const char * (*get_sheet_name) (struct spreadsheet *, int);
  char * (*get_sheet_range) (struct spreadsheet *, int);
  int (*get_sheet_n_sheets) (struct spreadsheet *);
  unsigned int (*get_sheet_n_rows) (struct spreadsheet *, int);
  unsigned int (*get_sheet_n_columns) (struct spreadsheet *, int);
  char * (*get_sheet_cell) (struct spreadsheet *, int , int , int);

  char *file_name;

  struct sheet_detail *sheets;


  /** Things specific to casereaders.  */

  /* The dictionary for client's reference.
     Client must ref or clone it if it needs a permanent or modifiable copy. */
  struct dictionary *dict;
  struct caseproto *proto;
  struct ccase *first_case;
  bool used_first_case;

  /* Where the reader should start and stop.  */
  int start_row;
  int start_col;
  int stop_row;
  int stop_col;
};


struct casereader * spreadsheet_make_reader (struct spreadsheet *, const struct spreadsheet_read_options *);

const char * spreadsheet_get_sheet_name (struct spreadsheet *s, int n) OPTIMIZE(2);
char * spreadsheet_get_sheet_range (struct spreadsheet *s, int n) OPTIMIZE(2);
int  spreadsheet_get_sheet_n_sheets (struct spreadsheet *s) OPTIMIZE(2);
unsigned int  spreadsheet_get_sheet_n_rows (struct spreadsheet *s, int n) OPTIMIZE(2);
unsigned int  spreadsheet_get_sheet_n_columns (struct spreadsheet *s, int n) OPTIMIZE(2);

char * spreadsheet_get_cell (struct spreadsheet *s, int n, int row, int column);

char * create_cell_ref (int col0, int row0);
char *create_cell_range (int col0, int row0, int coli, int rowi);

struct spreadsheet * spreadsheet_ref (struct spreadsheet *s) WARN_UNUSED_RESULT;
void spreadsheet_unref (struct spreadsheet *);


#define SPREADSHEET_CAST(X) ((struct spreadsheet *)(X))

#endif
