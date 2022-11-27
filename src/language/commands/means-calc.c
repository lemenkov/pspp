/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012, 2013, 2019 Free Software Foundation, Inc.

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

#include "data/case.h"
#include "data/format.h"
#include "data/variable.h"

#include "libpspp/bt.h"
#include "libpspp/hmap.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"

#include "math/moments.h"
#include "output/pivot-table.h"

#include <math.h>

#include "means.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

/* A base struct for all statistics.  */
struct statistic
{
};

/* Statistics which accumulate a single value.  */
struct statistic_simple
{
  struct statistic parent;
  double acc;
};

/* Statistics based on moments.  */
struct statistic_moment
{
  struct statistic parent;
  struct moments1 *mom;
};


static struct statistic *
default_create (struct pool *pool)
{
  struct statistic_moment *pvd = pool_alloc (pool, sizeof *pvd);

  pvd->mom = moments1_create (MOMENT_KURTOSIS);

  return (struct statistic *) pvd;
}

static void
default_update (struct statistic *stat, double w, double x)
{
  struct statistic_moment *pvd = (struct statistic_moment *) stat;

  moments1_add (pvd->mom, x, w);
}

static void
default_destroy (struct statistic *stat)
{
  struct statistic_moment *pvd = (struct statistic_moment *) stat;
  moments1_destroy (pvd->mom);
}


/* Simple statistics have nothing to destroy.  */
static void
simple_destroy (struct statistic *stat UNUSED)
{
}



/* HARMONIC MEAN: The reciprocal of the sum of the reciprocals:
   1 / (1/(x_0) + 1/(x_1) + ... + 1/(x_{n-1})) */

struct harmonic_mean
{
  struct statistic parent;
  double rsum;
  double n;
};

static struct statistic *
harmonic_create (struct pool *pool)
{
  struct harmonic_mean *hm = pool_alloc (pool, sizeof *hm);

  hm->rsum = 0;
  hm->n = 0;

  return (struct statistic *) hm;
}


static void
harmonic_update (struct statistic *stat, double w, double x)
{
  struct harmonic_mean *hm = (struct harmonic_mean *) stat;
  hm->rsum  += w / x;
  hm->n += w;
}


static double
harmonic_get (const struct statistic *pvd)
{
  const struct harmonic_mean *hm = (const struct harmonic_mean *) pvd;

  return hm->n / hm->rsum;
}



/* GEOMETRIC MEAN:  The nth root of the product of all n observations
   pow ((x_0 * x_1 * ... x_{n - 1}), 1/n)  */
struct geometric_mean
{
  struct statistic parent;
  double prod;
  double n;
};

static struct statistic *
geometric_create (struct pool *pool)
{
  struct geometric_mean *gm = pool_alloc (pool, sizeof *gm);

  gm->prod = 1.0;
  gm->n = 0;

  return (struct statistic *) gm;
}

static void
geometric_update (struct statistic  *pvd, double w, double x)
{
  struct geometric_mean *gm = (struct geometric_mean *)pvd;
  gm->prod  *=  pow (x, w);
  gm->n += w;
}


static double
geometric_get (const struct statistic *pvd)
{
  const struct geometric_mean *gm = (const struct geometric_mean *)pvd;
  return pow (gm->prod, 1.0 / gm->n);
}



/* The getters for moment based statistics simply calculate the
   moment.    The only exception is Std Dev. which needs to call
   sqrt as well.  */

static double
sum_get (const struct statistic *pvd)
{
  double n, mean;

  moments1_calculate (((struct statistic_moment *)pvd)->mom, &n, &mean, 0, 0, 0);

  return mean * n;
}


static double
n_get (const struct statistic *pvd)
{
  double n;

  moments1_calculate (((struct statistic_moment *)pvd)->mom, &n, 0, 0, 0, 0);

  return n;
}

static double
arithmean_get (const struct statistic *pvd)
{
  double n, mean;

  moments1_calculate (((struct statistic_moment *)pvd)->mom, &n, &mean, 0, 0, 0);

  return mean;
}

static double
variance_get (const struct statistic *pvd)
{
  double n, mean, variance;

  moments1_calculate (((struct statistic_moment *)pvd)->mom, &n, &mean, &variance, 0, 0);

  return variance;
}


static double
stddev_get (const struct statistic *pvd)
{
  return sqrt (variance_get (pvd));
}

static double
skew_get (const struct statistic *pvd)
{
  double skew;

  moments1_calculate (((struct statistic_moment *)pvd)->mom, NULL, NULL, NULL, &skew, 0);

  return skew;
}

static double
sekurt_get (const struct statistic *pvd)
{
  double n;

  moments1_calculate (((struct statistic_moment *)pvd)->mom, &n, NULL, NULL, NULL, NULL);

  return calc_sekurt (n);
}

static double
seskew_get (const struct statistic *pvd)
{
  double n;

  moments1_calculate (((struct statistic_moment *)pvd)->mom, &n, NULL, NULL, NULL, NULL);

  return calc_seskew (n);
}

static double
kurt_get (const struct statistic *pvd)
{
  double kurt;

  moments1_calculate (((struct statistic_moment *)pvd)->mom, NULL, NULL, NULL, NULL, &kurt);

  return kurt;
}

static double
semean_get (const struct statistic *pvd)
{
  double n, var;

  moments1_calculate (((struct statistic_moment *)pvd)->mom, &n, NULL, &var, NULL, NULL);

  return sqrt (var / n);
}



/* MIN: The smallest (closest to minus infinity) value. */

static struct statistic *
min_create (struct pool *pool)
{
  struct statistic_simple *pvd = pool_alloc (pool, sizeof *pvd);

  pvd->acc = DBL_MAX;

  return (struct statistic *) pvd;
}

static void
min_update (struct statistic *pvd, double w UNUSED, double x)
{
  double *r = &((struct statistic_simple *)pvd)->acc;

  if (x < *r)
    *r = x;
}

static double
min_get (const struct statistic *pvd)
{
  double *r = &((struct statistic_simple *)pvd)->acc;

  return *r;
}

/* MAX: The largest (closest to plus infinity) value. */

static struct statistic *
max_create (struct pool *pool)
{
  struct statistic_simple *pvd = pool_alloc (pool, sizeof *pvd);

  pvd->acc = -DBL_MAX;

  return (struct statistic *) pvd;
}

static void
max_update (struct statistic *pvd, double w UNUSED, double x)
{
  double *r = &((struct statistic_simple *)pvd)->acc;

  if (x > *r)
    *r = x;
}

static double
max_get (const struct statistic *pvd)
{
  double *r = &((struct statistic_simple *)pvd)->acc;

  return *r;
}



struct range
{
  struct statistic parent;
  double min;
  double max;
};

/* Initially min and max are set to their most (inverted) extreme possible
   values.  */
static struct statistic *
range_create (struct pool *pool)
{
  struct range *r = pool_alloc (pool, sizeof *r);

  r->min = DBL_MAX;
  r->max = -DBL_MAX;

  return (struct statistic *) r;
}

/* On each update, set min and max to X or leave unchanged,
   as appropriate.  */
static void
range_update (struct statistic *pvd, double w UNUSED, double x)
{
  struct range *r = (struct range *) pvd;

  if (x > r->max)
    r->max = x;

  if (x < r->min)
    r->min = x;
}

/*  Get the difference between min and max.  */
static double
range_get (const struct statistic *pvd)
{
  const struct range *r = (struct range *) pvd;

  return r->max - r->min;
}



/* LAST: The last value (the one closest to the end of the file).  */

static struct statistic *
last_create (struct pool *pool)
{
  struct statistic_simple *pvd = pool_alloc (pool, sizeof *pvd);

  return (struct statistic *) pvd;
}

static void
last_update (struct statistic *pvd, double w UNUSED, double x)
{
  struct statistic_simple *stat = (struct statistic_simple *) pvd;

  stat->acc = x;
}

static double
last_get (const struct statistic *pvd)
{
  const struct statistic_simple *stat = (struct statistic_simple *) pvd;

  return stat->acc;
}

/* FIRST: The first value (the one closest to the start of the file).  */

static struct statistic *
first_create (struct pool *pool)
{
  struct statistic_simple *pvd = pool_alloc (pool, sizeof *pvd);

  pvd->acc = SYSMIS;

  return (struct statistic *) pvd;
}

static void
first_update (struct statistic *pvd, double w UNUSED, double x)
{
  struct statistic_simple *stat = (struct statistic_simple *) pvd;

  if (stat->acc == SYSMIS)
    stat->acc = x;
}

static double
first_get (const struct statistic *pvd)
{
  const struct statistic_simple *stat = (struct statistic_simple *) pvd;

  return stat->acc;
}

/* Table of cell_specs */
const struct cell_spec cell_spec[n_MEANS_STATISTICS] = {
  {N_("Mean"),           "MEAN",      NULL          ,   default_create,   default_update,   arithmean_get, default_destroy},
  {N_("N"),              "COUNT",     PIVOT_RC_COUNT,   default_create,   default_update,   n_get,         default_destroy},
  {N_("Std. Deviation"), "STDDEV",    NULL          ,   default_create,   default_update,   stddev_get,    default_destroy},
#if 0
  {N_("Median"),         "MEDIAN",    NULL          ,   default_create,   default_update,   NULL,          default_destroy},
  {N_("Group Median"),   "GMEDIAN",   NULL          ,   default_create,   default_update,   NULL,          default_destroy},
#endif
  {N_("S.E. Mean"),      "SEMEAN",    NULL          ,   default_create,   default_update,   semean_get,    default_destroy},
  {N_("Sum"),            "SUM",       NULL          ,   default_create,   default_update,   sum_get,       default_destroy},
  {N_("Minimum"),        "MIN",       NULL          ,   min_create,       min_update,       min_get,       simple_destroy},
  {N_("Maximum"),        "MAX",       NULL          ,   max_create,       max_update,       max_get,       simple_destroy},
  {N_("Range"),          "RANGE",     NULL          ,   range_create,     range_update,     range_get,     simple_destroy},
  {N_("Variance"),       "VARIANCE",  PIVOT_RC_OTHER,   default_create,   default_update,   variance_get,  default_destroy},
  {N_("Kurtosis"),       "KURT",      PIVOT_RC_OTHER,   default_create,   default_update,   kurt_get,      default_destroy},
  {N_("S.E. Kurt"),      "SEKURT",    PIVOT_RC_OTHER,   default_create,   default_update,   sekurt_get,    default_destroy},
  {N_("Skewness"),       "SKEW",      PIVOT_RC_OTHER,   default_create,   default_update,   skew_get,      default_destroy},
  {N_("S.E. Skew"),      "SESKEW",    PIVOT_RC_OTHER,   default_create,   default_update,   seskew_get,    default_destroy},
  {N_("First"),          "FIRST",     NULL          ,   first_create,     first_update,     first_get,     simple_destroy},
  {N_("Last"),           "LAST",      NULL          ,   last_create,      last_update,      last_get,      simple_destroy},
#if 0
  {N_("Percent N"),      "NPCT",      PIVOT_RC_PERCENT, default_create,   default_update,   NULL,          default_destroy},
  {N_("Percent Sum"),    "SPCT",      PIVOT_RC_PERCENT, default_create,   default_update,   NULL,          default_destroy},
#endif
  {N_("Harmonic Mean"),  "HARMONIC",  NULL          ,   harmonic_create,  harmonic_update,  harmonic_get,  simple_destroy},
  {N_("Geom. Mean"),     "GEOMETRIC", NULL          ,   geometric_create, geometric_update, geometric_get, simple_destroy}
};
