/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2013 Free Software Foundation, Inc.

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


#ifndef ZIP_READER_H
#define ZIP_READER_H 1

struct zip_member;
struct zip_reader;
struct string;

/* Create zip reader to read the file called FILENAME.
   If ERRS is non-null if will be used to contain any error messages
   which the reader wishes to report.
 */
struct zip_reader *zip_reader_create (const char *filename, struct string *errs);

/* Destroy the zip reader */
void zip_reader_destroy (struct zip_reader *zr);

/* Returns the name of ZR's member IDX, IDX >= 0.  Returns NULL if ZR has fewer
   than (IDX + 1) members. */
const char *zip_reader_get_member_name(const struct zip_reader *zr,
                                       size_t idx);

/* Returns true if ZR contains a member named MEMBER, false otherwise. */
bool zip_reader_contains_member (const struct zip_reader *zr,
                                 const char *member);

/* Return the zip member in the reader ZR, called MEMBER */
struct zip_member *zip_member_open (struct zip_reader *zr, const char *member);

/* Read up to N bytes from ZM, storing them in BUF.
   Returns the number of bytes read, or -1 on error */
int zip_member_read (struct zip_member *zm, void *buf, size_t n);

/* Read all of ZM into memory, storing the data in *DATAP and its size in *NP.
   Returns NULL if successful, otherwise an error string that the caller
   must eventually free(). */
char *zip_member_read_all (struct zip_reader *, const char *member_name,
                           void **datap, size_t *np) WARN_UNUSED_RESULT;

void zip_member_finish (struct zip_member *zm);



#endif
