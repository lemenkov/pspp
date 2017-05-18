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

struct matrix_material
{
  gsl_matrix *corr ;  /* The correlation matrix */
  gsl_matrix *cov ;   /* The covariance matrix */

  /* Moment matrices */
  const gsl_matrix *n ;           /* MOMENT 0 */
  const gsl_matrix *mean_matrix;  /* MOMENT 1 */
  const gsl_matrix *var_matrix;   /* MOMENT 2 */
};

struct dictionary;
struct variable;
struct casereader;


struct matrix_reader;

struct matrix_reader *create_matrix_reader_from_case_reader (const struct dictionary *dict,
							     struct casereader *in_reader,
							     const struct variable ***vars, size_t *n_vars);

bool destroy_matrix_reader (struct matrix_reader *mr);

bool next_matrix_from_reader (struct matrix_material *mm,
			      struct matrix_reader *mr,
			      const struct variable **vars, int n_vars);


#endif
