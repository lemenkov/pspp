/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2011 Free Software Foundation, Inc. Written by Jason H. Stover.

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

#ifndef LINREG_H
#define LINREG_H

#include <gsl/gsl_matrix.h>
#include <stdbool.h>

/*
  Find the least-squares estimate of b for the linear model:

  Y = Xb + Z

  where Y is an n-by-1 column vector, X is an n-by-p matrix of
  independent variables, b is a p-by-1 vector of regression coefficients,
  and Z is an n-by-1 normally-distributed random vector with independent
  identically distributed components with mean 0.

  This estimate is found via the sweep operator or singular-value
  decomposition with gsl.


  References:

  1. Matrix Computations, third edition. GH Golub and CF Van Loan.
  The Johns Hopkins University Press. 1996. ISBN 0-8018-5414-8.

  2. Numerical Analysis for Statisticians. K Lange. Springer. 1999.
  ISBN 0-387-94979-8.

  3. Numerical Linear Algebra for Applications in Statistics. JE Gentle.
  Springer. 1998. ISBN 0-387-98542-5.
*/

struct variable;

struct linreg *linreg_alloc (const struct variable *, const struct variable **,
                             double, size_t, bool);

void linreg_unref (struct linreg *);
void linreg_ref (struct linreg *);

int linreg_n_indeps (const struct linreg *c);

/*
  Fit the linear model via least squares.
*/
void linreg_fit (const gsl_matrix *, struct linreg *);

double linreg_predict (const struct linreg *, const double *, size_t);
double linreg_residual (const struct linreg *, double, const double *, size_t);
const struct variable ** linreg_get_vars (const struct linreg *);

/*
  Mean of the independent variable.
 */
double linreg_get_indep_variable_mean (const struct linreg *, size_t);
void linreg_set_indep_variable_mean (struct linreg *, size_t, double);

double linreg_mse (const struct linreg *);

double linreg_intercept (const struct linreg *);

const gsl_matrix * linreg_cov (const struct linreg *);
double linreg_coeff (const struct linreg *, size_t);
const struct variable * linreg_indep_var (const struct linreg *, size_t);
const struct variable * linreg_dep_var (const struct linreg *);
size_t linreg_n_coeffs (const struct linreg *);
double linreg_n_obs (const struct linreg *);
double linreg_sse (const struct linreg *);
double linreg_ssreg (const struct linreg *);
double linreg_dfmodel (const struct linreg *);
double linreg_dferror (const struct linreg *);
double linreg_dftotal (const struct linreg *);
double linreg_sst (const struct linreg *);
void linreg_set_depvar_mean (struct linreg *, double);
double linreg_get_depvar_mean (const struct linreg *);

#endif
