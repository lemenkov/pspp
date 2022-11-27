/* PSPP - a program for statistical analysis.
   Copyright (C) 2017 Free Software Foundation, Inc.

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

#ifndef MATRIX_READER_H
#define MATRIX_READER_H

#include <gsl/gsl_matrix.h>
#include <stdbool.h>

struct casereader;
struct ccase;
struct dictionary;
struct matrix_reader;
struct variable;

struct matrix_reader
  {
    const struct dictionary *dict;
    struct casegrouper *grouper;

    /* Variables in 'dict'. */
    const struct variable **svars;  /* Split variables. */
    size_t n_svars;
    const struct variable *rowtype; /* ROWTYPE_. */
    const struct variable **fvars;  /* Factor variables. */
    size_t n_fvars;
    const struct variable *varname; /* VARNAME_. */
    const struct variable **cvars;  /* Continuous variables. */
    size_t n_cvars;
  };

struct matrix_material
{
  gsl_matrix *corr;             /* The correlation matrix */
  gsl_matrix *cov;              /* The covariance matrix */

  /* Moment matrices */
  gsl_matrix *n;                /* MOMENT 0 */
  gsl_matrix *mean_matrix;      /* MOMENT 1 */
  gsl_matrix *var_matrix;       /* MOMENT 2 */
};

#define MATRIX_MATERIAL_INIT { .corr = NULL }
void matrix_material_uninit (struct matrix_material *);

struct matrix_reader *matrix_reader_create (const struct dictionary *,
                                            struct casereader *);

bool matrix_reader_destroy (struct matrix_reader *mr);

bool matrix_reader_next (struct matrix_material *mm, struct matrix_reader *mr,
                         struct casereader **groupp);

struct substring matrix_reader_get_string (const struct ccase *,
                                           const struct variable *);
void matrix_reader_set_string (struct ccase *, const struct variable *,
                               struct substring);


#endif
