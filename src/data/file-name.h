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

struct file_handle ;

char *fn_search_path (const char *base_name, char **path);
char *fn_extension (const struct file_handle *);

bool fn_exists (const struct file_handle *);


FILE *fn_open (const struct file_handle *fn, const char *mode);
int fn_close (const struct file_handle *fn, FILE *file);

const char * default_output_path (void);

#if defined _WIN32 || defined __WIN32__
#define WIN32_LEAN_AND_MEAN  /* avoid including junk */
#define UNICODE 1
#include <windows.h>
#else
typedef char TCHAR;
#endif

TCHAR * convert_to_filename_encoding (const char *s, size_t len, const char *current_encoding);


#endif /* file-name.h */
