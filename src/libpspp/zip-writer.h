/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_ZIP_WRITER_H
#define LIBPSPP_ZIP_WRITER_H 1

#include <stdbool.h>
#include <stdio.h>

struct zip_writer *zip_writer_create (const char *file_name);

void zip_writer_add_start (struct zip_writer *, const char *member_name);
void zip_writer_add_write (struct zip_writer *, const void *, size_t);
void zip_writer_add_finish (struct zip_writer *);

void zip_writer_add (struct zip_writer *, FILE *, const char *member_name);
void zip_writer_add_string (struct zip_writer *, const char *member_name,
                            const char *content);
void zip_writer_add_memory (struct zip_writer *, const char *member_name,
                            const void *content, size_t size);

bool zip_writer_close (struct zip_writer *);

#endif /* libpspp/zip-writer.h */
