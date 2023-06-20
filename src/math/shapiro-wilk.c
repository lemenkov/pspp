/* PSPP - a program for statistical analysis.
   Copyright (C) 2019 Free Software Foundation, Inc.

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

#include "math/shapiro-wilk.h"

#include <math.h>
#include <assert.h>
#include <gsl/gsl_cdf.h>
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"

#include "gettext.h"
#define N_(msgid) msgid

/* Return the sum of coeff[i] * x^i for all i in the range [0,order).
   It is the caller's responsibility to ensure that coeff points to a
   large enough array containing the desired coefficients.  */
static double
polynomial (const double *coeff, int order, double x)
{
  double result = 0;
  for (int i = 0; i < order; ++i)
    result += coeff[i] * pow (x, i);

  return result;
}

static double
m_i (struct shapiro_wilk *sw, int i)
{
  assert (i > 0);
  assert (i <= sw->n);
  double x = (i - 0.375) / (sw->n + 0.25);

  return gsl_cdf_ugaussian_Pinv (x);
}

static double
a_i (struct shapiro_wilk *sw, int i)
{
  assert (i > 0);
  assert (i <= sw->n);

  if (i <  sw->n / 2.0)
    return -a_i (sw, sw->n - i + 1);
  else if (i == sw->n)
    return sw->a_n1;
  else if (i == sw->n - 1)
    return sw->a_n2;
  else
    return m_i (sw, i) / sqrt (sw->epsilon);
}

struct ccase;

static void
acc (struct statistic *s, const struct ccase *cx UNUSED, double c,
     double cc, double y)
{
  struct shapiro_wilk *sw = UP_CAST (s, struct shapiro_wilk, parent.parent);

  double int_part, frac_part;
  frac_part = modf (c, &int_part);

  if (frac_part != 0 && !sw->warned)
    {
      msg (MW, N_ ("One or more weight values are non-integer."
                   "  Fractional parts will be ignored when calculating the Shapiro-Wilk statistic."));
      sw->warned = true;
    }

  for (int i = 0; i < int_part; ++i)
  {
    double a_ii = a_i (sw, cc - c + i + 1);
    double x_ii = y;

    sw->numerator += a_ii * x_ii;
    sw->denominator += pow (x_ii - sw->mean, 2);
  }
}

static void
destroy (struct statistic *s)
{
  struct shapiro_wilk *sw = UP_CAST (s, struct shapiro_wilk, parent.parent);
  free (sw);
}



double
shapiro_wilk_calculate (const struct shapiro_wilk *sw)
{
  return sw->numerator * sw->numerator / sw->denominator;
}

/* Inititialse a SW ready for calculating the statistic for
   a dataset of size N.  */
struct shapiro_wilk *
shapiro_wilk_create (int n, double mean)
{
  if (n < 3 || n > 5000)
    return NULL;

  struct shapiro_wilk *sw = XZALLOC (struct shapiro_wilk);
  struct order_stats *os = &sw->parent;
  struct statistic *stat = &os->parent;

  const double c1[] = {0, 0.221157,  -0.147981,
                       -2.071190, 4.434685, -2.706056};

  const double c2[] = {0, 0.042981, -0.293762,
                       -1.752461, 5.682633, -3.582633};

  sw->n = n;

  const double u = 1.0 / sqrt (sw->n);

  double m = 0;
  for (int i = 1; i <= sw->n; ++i)
    {
      double m_ii = m_i (sw, i);
      m += m_ii * m_ii;
    }

  double m_n1 = m_i (sw, sw->n);
  double m_n2 = m_i (sw, sw->n - 1);

  sw->a_n1 = polynomial (c1, 6, u);
  sw->a_n1 += m_n1 / sqrt (m);

  sw->a_n2 = polynomial (c2, 6, u);
  sw->a_n2 += m_n2 / sqrt (m);
  sw->mean = mean;

  sw->epsilon =  m - 2 * pow (m_n1, 2) - 2 * pow (m_n2, 2);
  sw->epsilon /= 1 - 2 * pow (sw->a_n1, 2) - 2 * pow (sw->a_n2, 2);

  sw->warned = false;

  os->accumulate = acc;
  stat->destroy = destroy;

  return sw;
}

double
shapiro_wilk_significance (double n, double w)
{
  const double g[] = {-2.273, 0.459};
  const double c3[] = {.544,-.39978,.025054,-6.714e-4};
  const double c4[] = {1.3822,-.77857,.062767,-.0020322};
  const double c5[] = {-1.5861,-.31082,-.083751,.0038915 };
  const double c6[] = {-.4803,-.082676,.0030302};

  double m, s;
  double y = log (1 - w);
  if (n == 3)
    {
      double pi6 = 6.0 / M_PI;
      double stqr = asin(sqrt (3/ 4.0));
      double p = pi6 * (asin (sqrt (w)) - stqr);
      return (p < 0) ? 0 : p;
    }
  else if (n <= 11)
    {
      double gamma = polynomial (g, 2, n);
      y = - log (gamma - y);
      m = polynomial (c3, 4, n);
      s = exp (polynomial (c4, 4, n));
    }
  else
    {
      double xx = log(n);
      m = polynomial (c5, 4, xx);
      s = exp (polynomial (c6, 3, xx));
    }

  double p = gsl_cdf_gaussian_Q (y - m, s);
  return p;
}
