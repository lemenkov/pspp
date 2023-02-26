/* PSPP - a program for statistical analysis.
   Copyright (C) 2023 Free Software Foundation, Inc.

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

#ifndef DATA_VARSET_H
#define DATA_VARSET_H 1

/* Variable set.

   A variable set is a named set of variables.  The SPSS GUI allows users to
   pick which variable sets are displayed in the data editor and dialog boxes.
 */

#include <stdbool.h>
#include <stddef.h>

struct dictionary;

/* A variable set. */
struct varset
  {
    char *name;                 /* UTF-8 encoded name. */
    struct variable **vars;     /* Constituent variables. */
    size_t n_vars;              /* Number of constituent variables. */
  };

struct varset *varset_clone (const struct varset *);
void varset_destroy (struct varset *);

#endif /* data/varset.h */
