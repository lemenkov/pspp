/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010 Free Software Foundation, Inc.

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

#ifndef FILE_NAME_H
#define FILE_NAME_H 1

#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>

char *fn_search_path (const char *base_name, char **path);
char *fn_extension (const char *fn);

bool fn_is_absolute (const char *fn);
bool fn_exists (const char *fn);

FILE *fn_open (const char *fn, const char *mode);
int fn_close (const char *fn, FILE *file);

const char * default_output_path (void);

#endif /* file-name.h */
