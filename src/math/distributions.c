/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2010, 2011, 2015, 2016 Free Software Foundation, Inc.

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

#include <config.h>

#include "math/distributions.h"

#include <gsl/gsl_cdf.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_sf.h>
#include <libpspp/misc.h>
#include <math.h>

#include "data/val-type.h"

/* Returns the noncentral beta cumulative distribution function
   value for the given arguments.

   FIXME: The accuracy of this function is not entirely
   satisfactory.  We only match the example values given in AS
   310 to the first 5 significant digits. */
double
ncdf_beta (double x, double a, double b, double lambda)
{
  double c;

  if (x <= 0. || x >= 1. || a <= 0. || b <= 0. || lambda <= 0.)
    return SYSMIS;

  c = lambda / 2.;
  if (lambda < 54.)
    {
      /* Algorithm AS 226. */
      double x0, a0, beta, temp, gx, q, ax, sumq, sum;
      double err_max = 2 * DBL_EPSILON;
      double err_bound;
      int iter_max = 100;
      int iter;

      x0 = floor (c - 5.0 * sqrt (c));
      if (x0 < 0.)
        x0 = 0.;
      a0 = a + x0;
      beta = (gsl_sf_lngamma (a0)
              + gsl_sf_lngamma (b)
              - gsl_sf_lngamma (a0 + b));
      temp = gsl_sf_beta_inc (a0, b, x);
      gx = exp (a0 * log (x) + b * log (1. - x) - beta - log (a0));
      if (a0 >= a)
        q = exp (-c + x0 * log (c)) - gsl_sf_lngamma (x0 + 1.);
      else
        q = exp (-c);
      ax = q * temp;
      sumq = 1. - q;
      sum = ax;

      iter = 0;
      do
        {
          iter++;
          temp -= gx;
          gx = x * (a + b + iter - 1.) * gx / (a + iter);
          q *= c / iter;
          sumq -= q;
          ax = temp * q;
          sum += ax;

          err_bound = (temp - gx) * sumq;
        }
      while (iter < iter_max && err_bound > err_max);

      return sum;
    }
  else
    {
      /* Algorithm AS 310. */
      double m, m_sqrt;
      int iter, iter_lower, iter_upper, iter1, iter2, j;
      double t, q, r, psum, beta, s1, gx, fx, temp, ftemp, t0, s0, sum, s;
      double err_bound;
      double err_max = 2 * DBL_EPSILON;

      iter = 0;

      m = floor (c + .5);
      m_sqrt = sqrt (m);
      iter_lower = m - 5. * m_sqrt;
      iter_upper = m + 5. * m_sqrt;

      t = -c + m * log (c) - gsl_sf_lngamma (m + 1.);
      q = exp (t);
      r = q;
      psum = q;
      beta = (gsl_sf_lngamma (a + m)
              + gsl_sf_lngamma (b)
              - gsl_sf_lngamma (a + m + b));
      s1 = (a + m) * log (x) + b * log (1. - x) - log (a + m) - beta;
      fx = gx = exp (s1);
      ftemp = temp = gsl_sf_beta_inc (a + m, b, x);
      iter++;
      sum = q * temp;
      iter1 = m;

      while (iter1 >= iter_lower && q >= err_max)
        {
          q = q * iter1 / c;
          iter++;
          gx = (a + iter1) / (x * (a + b + iter1 - 1.)) * gx;
          iter1--;
          temp += gx;
          psum += q;
          sum += q * temp;
        }

      t0 = (gsl_sf_lngamma (a + b)
            - gsl_sf_lngamma (a + 1.)
            - gsl_sf_lngamma (b));
      s0 = a * log (x) + b * log (1. - x);

      s = 0.;
      for (j = 0; j < iter1; j++)
        {
          double t1;
          s += exp (t0 + s0 + j * log (x));
          t1 = log (a + b + j) - log (a + 1. + j) + t0;
          t0 = t1;
        }

      err_bound = (1. - gsl_sf_gamma_inc_P (iter1, c)) * (temp + s);
      q = r;
      temp = ftemp;
      gx = fx;
      iter2 = m;
      for (;;)
        {
          double ebd = err_bound + (1. - psum) * temp;
          if (ebd < err_max || iter >= iter_upper)
            break;

          iter2++;
          iter++;
          q = q * c / iter2;
          psum += q;
          temp -= gx;
          gx = x * (a + b + iter2 - 1.) / (a + iter2) * gx;
          sum += q * temp;
        }

      return sum;
    }
}

double
cdf_bvnor (double x0, double x1, double r)
{
  double z = pow2 (x0) - 2. * r * x0 * x1 + pow2 (x1);
  return exp (-z / (2. * (1 - r * r))) * (2. * M_PI * sqrt (1 - r * r));
}

double
idf_fdist (double P, double df1, double df2)
{
  double temp = gsl_cdf_beta_Pinv (P, df1 / 2, df2 / 2);
  return temp * df2 / ((1. - temp) * df1);
}

/*
 *  Mathlib : A C Library of Special Functions
 *  Copyright (C) 1998 Ross Ihaka
 *  Copyright (C) 2000 The R Development Core Team
 *
 *  This program is free software; you can redistribute it and/or
 *  modify
 *  it under the terms of the GNU General Public License as
 *  published by
 *  the Free Software Foundation; either version 2 of the
 *  License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be
 *  useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty
 *  of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301 USA.
 */

/* Returns the density of the noncentral beta distribution with
   noncentrality parameter LAMBDA. */
double
npdf_beta (double x, double a, double b, double lambda)
{
  if (lambda < 0. || a <= 0. || b <= 0.)
    return SYSMIS;
  else if (lambda == 0.)
    return gsl_ran_beta_pdf (x, a, b);
  else
    {
      double max_error = 2 * DBL_EPSILON;
      int max_iter = 200;
      double term = gsl_ran_beta_pdf (x, a, b);
      double lambda2 = 0.5 * lambda;
      double weight = exp (-lambda2);
      double sum = weight * term;
      double psum = weight;
      int k;
      for (k = 1; k <= max_iter && 1 - psum < max_error; k++) {
        weight *= lambda2 / k;
        term *= x * (a + b) / a;
        sum += weight * term;
        psum += weight;
        a += 1;
      }
      return sum;
    }
}

