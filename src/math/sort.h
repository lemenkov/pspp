/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009 Free Software Foundation, Inc.

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

#ifndef MATH_SORT_H
#define MATH_SORT_H 1

/* Support for sorting cases.

   Use sort_create_writer() to sort cases in the most general way:

   - Create a casewriter with sort_create_writer(), specifying the sort
     criteria.
   - Write all of the cases to be sorted to the casewriter, e.g. with
     casewriter_write().
   - Obtain the sorted results with casewriter_make_reader().

  sort_execute() and sort_execute_1var() are shortcuts for situations where the
  cases are already available from a casereader.

  All of these functions can efficiently sort data bigger than memory. */

struct subcase;
struct caseproto;
struct variable;

extern int min_buffers ;
extern int max_buffers ;

struct casewriter *sort_create_writer (const struct subcase *,
                                       const struct caseproto *);
struct casereader *sort_execute (struct casereader *, const struct subcase *);
struct casereader *sort_execute_1var (struct casereader *,
                                      const struct variable *);

#endif /* math/sort.h */
