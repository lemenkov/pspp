/* PSPP - a program for statistical analysis.
   Copyright (C) 2011 Free Software Foundation, Inc.

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

#include "language/commands/ks-one-sample.h"

#include <gsl/gsl_cdf.h>
#include <math.h>
#include <stdlib.h>


#include "math/sort.h"
#include "data/case.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "language/commands/freq.h"
#include "language/commands/npar.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)


/* The per test variable statistics */
struct ks
{
  double obs_cc;

  double test_min ;
  double test_max;
  double mu;
  double sigma;

  double diff_pos;
  double diff_neg;

  double ssq;
  double sum;
};

typedef double theoretical (const struct ks *ks, double x);
typedef theoretical *theoreticalfp;

static double
theoretical_uniform (const struct ks *ks, double x)
{
  return gsl_cdf_flat_P (x, ks->test_min, ks->test_max);
}

static double
theoretical_normal (const struct ks *ks, double x)
{
  return gsl_cdf_gaussian_P (x - ks->mu, ks->sigma);
}

static double
theoretical_poisson (const struct ks *ks, double x)
{
  return gsl_cdf_poisson_P (x, ks->mu);
}

static double
theoretical_exponential (const struct ks *ks, double x)
{
  return gsl_cdf_exponential_P (x, 1/ks->mu);
}


static const  theoreticalfp theoreticalf[4] =
{
  theoretical_normal,
  theoretical_uniform,
  theoretical_poisson,
  theoretical_exponential
};

/*
   Return the assymptotic approximation to the significance of Z
 */
static double
ks_asymp_sig (double z)
{
  if (z < 0.27)
    return 1;

  if (z >= 3.1)
    return 0;

  if (z < 1)
    {
      double q = exp (-1.233701 * pow (z, -2));
      return 1 - 2.506628 * (q + pow (q, 9) + pow (q, 25))/ z ;
    }
  else
    {
      double q = exp (-2 * z * z);
      return 2 * (q - pow (q, 4) + pow (q, 9) - pow (q, 16))/ z ;
    }
}

static void show_results (const struct ks *, const struct ks_one_sample_test *,  const struct fmt_spec);


void
ks_one_sample_execute (const struct dataset *ds,
                       struct casereader *input,
                       enum mv_class exclude,
                       const struct npar_test *test,
                       bool x UNUSED, double y UNUSED)
{
  const struct dictionary *dict = dataset_dict (ds);
  const struct ks_one_sample_test *kst = UP_CAST (test, const struct ks_one_sample_test, parent.parent);
  const struct one_sample_test *ost = &kst->parent;
  struct ccase *c;
  const struct fmt_spec wfmt = dict_get_weight_format (dict);
  bool warn = true;
  int v;
  struct casereader *r = casereader_clone (input);

  struct ks *ks = XCALLOC (ost->n_vars,  struct ks);

  for (v = 0; v < ost->n_vars; ++v)
    {
      ks[v].obs_cc = 0;
      ks[v].test_min = DBL_MAX;
      ks[v].test_max = -DBL_MAX;
      ks[v].diff_pos = -DBL_MAX;
      ks[v].diff_neg = DBL_MAX;
      ks[v].sum = 0;
      ks[v].ssq = 0;
    }

  for (; (c = casereader_read (r)) != NULL; case_unref (c))
    {
      const double weight = dict_get_case_weight (dict, c, &warn);

      for (v = 0; v < ost->n_vars; ++v)
        {
          const struct variable *var = ost->vars[v];
          const union value *val = case_data (c, var);

          if (var_is_value_missing (var, val) & exclude)
            continue;

          minimize (&ks[v].test_min, val->f);
          maximize (&ks[v].test_max, val->f);

          ks[v].obs_cc += weight;
          ks[v].sum += val->f;
          ks[v].ssq += pow2 (val->f);
        }
    }
  casereader_destroy (r);

  for (v = 0; v < ost->n_vars; ++v)
    {
      const struct variable *var = ost->vars[v];
      double cc = 0;
      double prev_empirical = 0;

      switch (kst->dist)
        {
        case KS_UNIFORM:
          if (kst->p[0] != SYSMIS)
            ks[v].test_min = kst->p[0];

          if (kst->p[1] != SYSMIS)
            ks[v].test_max = kst->p[1];
          break;
        case KS_NORMAL:
          if (kst->p[0] != SYSMIS)
            ks[v].mu = kst->p[0];
          else
            ks[v].mu = ks[v].sum / ks[v].obs_cc;

          if (kst->p[1] != SYSMIS)
            ks[v].sigma = kst->p[1];
          else
            {
              ks[v].sigma = ks[v].ssq - pow2 (ks[v].sum) / ks[v].obs_cc;
              ks[v].sigma /= ks[v].obs_cc - 1;
              ks[v].sigma = sqrt (ks[v].sigma);
            }

          break;
        case KS_POISSON:
        case KS_EXPONENTIAL:
          if (kst->p[0] != SYSMIS)
            ks[v].mu = ks[v].sigma = kst->p[0];
          else
            ks[v].mu = ks[v].sigma = ks[v].sum / ks[v].obs_cc;
          break;
        default:
          NOT_REACHED ();
        }

      r = sort_execute_1var (casereader_clone (input), var);
      for (; (c = casereader_read (r)) != NULL; case_unref (c))
        {
          double theoretical, empirical;
          double d, dp;
          const double weight = dict_get_case_weight (dict, c, &warn);
          const union value *val = case_data (c, var);

          if (var_is_value_missing (var, val) & exclude)
            continue;

          cc += weight;

          empirical = cc / ks[v].obs_cc;

          theoretical = theoreticalf[kst->dist] (&ks[v], val->f);

          d = empirical - theoretical;
          dp = prev_empirical - theoretical;

          if (d > 0)
            maximize (&ks[v].diff_pos, d);
          else
            minimize (&ks[v].diff_neg, d);

          if (dp > 0)
            maximize (&ks[v].diff_pos, dp);
          else
            minimize (&ks[v].diff_neg, dp);

          prev_empirical = empirical;
        }

      casereader_destroy (r);
    }

  show_results (ks, kst, wfmt);

  free (ks);
  casereader_destroy (input);
}


static void
show_results (const struct ks *ks,
              const struct ks_one_sample_test *kst,
              const struct fmt_spec wfmt)
{
  struct pivot_table *table = pivot_table_create (
    N_("One-Sample Kolmogorov-Smirnov Test"));
  pivot_table_set_weight_format (table, wfmt);

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Statistics"),
    N_("N"), PIVOT_RC_COUNT);

  switch (kst->dist)
    {
    case KS_UNIFORM:
      pivot_category_create_group (statistics->root, N_("Uniform Parameters"),
                                   N_("Minimum"), N_("Maximum"));
      break;

    case KS_NORMAL:
      pivot_category_create_group (statistics->root, N_("Normal Parameters"),
                                   N_("Mean"), N_("Std. Deviation"));
      break;

    case KS_POISSON:
      pivot_category_create_group (statistics->root, N_("Poisson Parameters"),
                                   N_("Lambda"));
      break;

    case KS_EXPONENTIAL:
      pivot_category_create_group (statistics->root,
                                   N_("Exponential Parameters"), N_("Scale"));
      break;

    default:
      NOT_REACHED ();
    }

  pivot_category_create_group (
    statistics->root, N_("Most Extreme Differences"),
    N_("Absolute"), N_("Positive"), N_("Negative"));

  pivot_category_create_leaves (
    statistics->root, N_("Kolmogorov-Smirnov Z"),
    _("Asymp. Sig. (2-tailed)"), PIVOT_RC_SIGNIFICANCE);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Variables"));

  for (size_t i = 0; i < kst->parent.n_vars; ++i)
    {
      int col = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (kst->parent.vars[i]));

      double values[10];
      size_t n = 0;

      values[n++] = ks[i].obs_cc;

      switch (kst->dist)
        {
        case KS_UNIFORM:
          values[n++] = ks[i].test_min;
          values[n++] = ks[i].test_max;
          break;

        case KS_NORMAL:
          values[n++] = ks[i].mu;
          values[n++] = ks[i].sigma;
          break;

        case KS_POISSON:
        case KS_EXPONENTIAL:
          values[n++] = ks[i].mu;
          break;

        default:
          NOT_REACHED ();
        }

      double abs = ks[i].diff_pos;
      maximize (&abs, -ks[i].diff_neg);

      double z = sqrt (ks[i].obs_cc) * abs;

      values[n++] = abs;
      values[n++] = ks[i].diff_pos;
      values[n++] = ks[i].diff_neg;
      values[n++] = z;
      values[n++] = ks_asymp_sig (z);

      for (size_t j = 0; j < n; j++)
        pivot_table_put2 (table, j, col, pivot_value_new_number (values[j]));
    }

  pivot_table_submit (table);
}
