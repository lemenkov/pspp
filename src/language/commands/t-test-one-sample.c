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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>


#include "t-test.h"

#include <math.h>
#include <gsl/gsl_cdf.h>

#include "data/variable.h"
#include "data/format.h"
#include "data/casereader.h"
#include "data/dictionary.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmapx.h"
#include "math/moments.h"

#include "output/pivot-table.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)


struct per_var_stats
{
  const struct variable *var;

  /* N, Mean, Variance */
  struct moments *mom;

  /* Sum of the differences */
  double sum_diff;
};


struct one_samp
{
  struct per_var_stats *stats;
  size_t n_stats;
  double testval;
};


static void
one_sample_test (const struct tt *tt, const struct one_samp *os)
{
  struct pivot_table *table = pivot_table_create (N_("One-Sample Test"));
  pivot_table_set_weight_var (table, tt->wv);

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Statistics"));
  struct pivot_category *group = pivot_category_create_group__ (
    statistics->root, pivot_value_new_user_text_nocopy (
      xasprintf (_("Test Value = %.*g"), DBL_DIG + 1, os->testval)));
  pivot_category_create_leaves (
    group,
    N_("t"), PIVOT_RC_OTHER,
    N_("df"), PIVOT_RC_COUNT,
    N_("Sig. (2-tailed)"), PIVOT_RC_SIGNIFICANCE,
    N_("Mean Difference"), PIVOT_RC_OTHER);
  struct pivot_category *subgroup = pivot_category_create_group__ (
    group, pivot_value_new_user_text_nocopy (
      xasprintf (_("%g%% Confidence Interval of the Difference"),
                 tt->confidence * 100.0)));
  pivot_category_create_leaves (subgroup,
                                N_("Lower"), PIVOT_RC_OTHER,
                                N_("Upper"), PIVOT_RC_OTHER);

  struct pivot_dimension *dep_vars = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Dependent Variables"));

  for (size_t i = 0; i < os->n_stats; i++)
    {
      const struct per_var_stats *per_var_stats = &os->stats[i];
      const struct moments *m = per_var_stats->mom;

      int dep_var_idx = pivot_category_create_leaf (
        dep_vars->root, pivot_value_new_variable (per_var_stats->var));

      double cc, mean, sigma;
      moments_calculate (m, &cc, &mean, &sigma, NULL, NULL);
      double tval = (mean - os->testval) * sqrt (cc / sigma);
      double mean_diff = per_var_stats->sum_diff / cc;
      double se_mean = sqrt (sigma / cc);
      double df = cc - 1.0;
      double p = gsl_cdf_tdist_P (tval, df);
      double q = gsl_cdf_tdist_Q (tval, df);
      double sig = 2.0 * (tval > 0 ? q : p);
      double tval_qinv = gsl_cdf_tdist_Qinv ((1.0 - tt->confidence) / 2.0, df);
      double lower = mean_diff - tval_qinv * se_mean;
      double upper = mean_diff + tval_qinv * se_mean;

      double entries[] = { tval, df, sig, mean_diff, lower, upper };
      for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
        pivot_table_put2 (table, j, dep_var_idx,
                          pivot_value_new_number (entries[j]));
    }

  pivot_table_submit (table);
}

static void
one_sample_summary (const struct tt *tt, const struct one_samp *os)
{
  struct pivot_table *table = pivot_table_create (N_("One-Sample Statistics"));
  pivot_table_set_weight_var (table, tt->wv);

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_COUNT,
                          N_("Mean"), PIVOT_RC_OTHER,
                          N_("Std. Deviation"), PIVOT_RC_OTHER,
                          N_("S.E. Mean"), PIVOT_RC_OTHER);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));

  for (size_t i = 0; i < os->n_stats; i++)
    {
      const struct per_var_stats *per_var_stats = &os->stats[i];
      const struct moments *m = per_var_stats->mom;

      int var_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (per_var_stats->var));

      double cc, mean, sigma;
      moments_calculate (m, &cc, &mean, &sigma, NULL, NULL);

      double entries[] = { cc, mean, sqrt (sigma), sqrt (sigma / cc) };
      for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
        pivot_table_put2 (table, j, var_idx,
                          pivot_value_new_number (entries[j]));
    }

  pivot_table_submit (table);
}

void
one_sample_run (const struct tt *tt, double testval, struct casereader *reader)
{
  struct one_samp os;
  os.testval = testval;
  os.stats = xcalloc (tt->n_vars, sizeof *os.stats);
  os.n_stats = tt->n_vars;
  for (size_t i = 0; i < tt->n_vars; ++i)
    {
      struct per_var_stats *per_var_stats = &os.stats[i];
      per_var_stats->var = tt->vars[i];
      per_var_stats->mom = moments_create (MOMENT_VARIANCE);
    }

  struct casereader *r = casereader_clone (reader);
  struct ccase *c;
  for (; (c = casereader_read (r)); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);
      for (size_t i = 0; i < os.n_stats; i++)
        {
          const struct per_var_stats *per_var_stats = &os.stats[i];
          const struct variable *var = per_var_stats->var;
          const union value *val = case_data (c, var);
          if (var_is_value_missing (var, val) & tt->exclude)
            continue;

          moments_pass_one (per_var_stats->mom, val->f, w);
        }
    }
  casereader_destroy (r);

  r = reader;
  for (; (c = casereader_read (r)); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);
      for (size_t i = 0; i < os.n_stats; i++)
        {
          struct per_var_stats *per_var_stats = &os.stats[i];
          const struct variable *var = per_var_stats->var;
          const union value *val = case_data (c, var);
          if (var_is_value_missing (var, val) & tt->exclude)
            continue;

          moments_pass_two (per_var_stats->mom, val->f, w);
          per_var_stats->sum_diff += w * (val->f - os.testval);
        }
    }
  casereader_destroy (r);

  one_sample_summary (tt, &os);
  one_sample_test (tt, &os);

  for (size_t i = 0; i < os.n_stats; i++)
    {
      const struct per_var_stats *per_var_stats = &os.stats[i];
      moments_destroy (per_var_stats->mom);
    }
  free (os.stats);
}

