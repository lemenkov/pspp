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

#include <math.h>
#include <gsl/gsl_cdf.h>

#include "t-test.h"

#include "math/moments.h"
#include "math/correlation.h"
#include "data/casereader.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"

#include "output/pivot-table.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)


struct pair_stats
{
  double sum_of_prod;
  struct moments *mom0;
  const struct variable *var0;

  struct moments *mom1;
  const struct variable *var1;

  struct moments *mom_diff;
};

struct paired_samp
{
  struct pair_stats *ps;
  size_t n_ps;
};

static void paired_summary (const struct tt *tt, struct paired_samp *os);
static void paired_correlations (const struct tt *tt, struct paired_samp *os);
static void paired_test (const struct tt *tt, const struct paired_samp *os);

void
paired_run (const struct tt *tt, size_t n_pairs, vp *pairs, struct casereader *reader)
{
  struct ccase *c;
  struct paired_samp ps;
  struct casereader *r;

  ps.ps = xcalloc (n_pairs, sizeof *ps.ps);
  ps.n_ps = n_pairs;
  for (size_t i = 0; i < n_pairs; ++i)
    {
      vp *pair = &pairs[i];
      struct pair_stats *pp = &ps.ps[i];
      pp->var0 = (*pair)[0];
      pp->var1 = (*pair)[1];
      pp->mom0 = moments_create (MOMENT_VARIANCE);
      pp->mom1 = moments_create (MOMENT_VARIANCE);
      pp->mom_diff = moments_create (MOMENT_VARIANCE);
    }

  r = casereader_clone (reader);
  for (; (c = casereader_read (r)); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);

      for (int i = 0; i < ps.n_ps; i++)
        {
          struct pair_stats *pp = &ps.ps[i];
          const union value *val0 = case_data (c, pp->var0);
          const union value *val1 = case_data (c, pp->var1);
          if (var_is_value_missing (pp->var0, val0) & tt->exclude)
            continue;

          if (var_is_value_missing (pp->var1, val1) & tt->exclude)
            continue;

          moments_pass_one (pp->mom0, val0->f, w);
          moments_pass_one (pp->mom1, val1->f, w);
          moments_pass_one (pp->mom_diff, val0->f - val1->f, w);
        }
    }
  casereader_destroy (r);

  r = reader;
  for (; (c = casereader_read (r)); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);

      for (int i = 0; i < ps.n_ps; i++)
        {
          struct pair_stats *pp = &ps.ps[i];
          const union value *val0 = case_data (c, pp->var0);
          const union value *val1 = case_data (c, pp->var1);
          if (var_is_value_missing (pp->var0, val0) & tt->exclude)
            continue;

          if (var_is_value_missing (pp->var1, val1) & tt->exclude)
            continue;

          moments_pass_two (pp->mom0, val0->f, w);
          moments_pass_two (pp->mom1, val1->f, w);
          moments_pass_two (pp->mom_diff, val0->f - val1->f, w);
          pp->sum_of_prod += val0->f * val1->f * w;
        }
    }
  casereader_destroy (r);

  paired_summary (tt, &ps);
  paired_correlations (tt, &ps);
  paired_test (tt, &ps);

  /* Clean up */

  for (int i = 0; i < ps.n_ps; i++)
    {
      struct pair_stats *pp = &ps.ps[i];
      moments_destroy (pp->mom0);
      moments_destroy (pp->mom1);
      moments_destroy (pp->mom_diff);
    }
  free (ps.ps);
}

static void
paired_summary (const struct tt *tt, struct paired_samp *os)
{
  struct pivot_table *table = pivot_table_create (
    N_("Paired Sample Statistics"));
  pivot_table_set_weight_var (table, tt->wv);

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_COUNT,
                          N_("Mean"), PIVOT_RC_OTHER,
                          N_("Std. Deviation"), PIVOT_RC_OTHER,
                          N_("S.E. Mean"), PIVOT_RC_OTHER);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));

  for (size_t i = 0; i < os->n_ps; i++)
    {
      struct pair_stats *pp = &os->ps[i];
      struct pivot_category *pair = pivot_category_create_group__ (
        variables->root, pivot_value_new_text_format (N_("Pair %zu"), i + 1));

      for (int j = 0; j < 2; j++)
        {
          const struct variable *var = j ? pp->var1 : pp->var0;
          const struct moments *mom = j ? pp->mom1 : pp->mom0;
          double cc, mean, sigma;
          moments_calculate (mom, &cc, &mean, &sigma, NULL, NULL);

          int var_idx = pivot_category_create_leaf (
            pair, pivot_value_new_variable (var));

          double entries[] = { cc, mean, sqrt (sigma), sqrt (sigma / cc) };
          for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
            pivot_table_put2 (table, j, var_idx,
                              pivot_value_new_number (entries[j]));
        }
    }

  pivot_table_submit (table);
}


static void
paired_correlations (const struct tt *tt, struct paired_samp *os)
{
  struct pivot_table *table = pivot_table_create (
    N_("Paired Samples Correlations"));
  pivot_table_set_weight_var (table, tt->wv);

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_COUNT,
                          N_("Correlation"), PIVOT_RC_CORRELATION,
                          N_("Sig."), PIVOT_RC_SIGNIFICANCE);

  struct pivot_dimension *pairs = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Pairs"));

  for (size_t i = 0; i < os->n_ps; i++)
    {
      struct pair_stats *pp = &os->ps[i];
      struct pivot_category *group = pivot_category_create_group__ (
        pairs->root, pivot_value_new_text_format (N_("Pair %zu"), i + 1));

      int row = pivot_category_create_leaf (
        group, pivot_value_new_text_format (N_("%s & %s"),
                                            var_to_string (pp->var0),
                                            var_to_string (pp->var1)));

      double cc0, mean0, sigma0;
      double cc1, mean1, sigma1;
      moments_calculate (pp->mom0, &cc0, &mean0, &sigma0, NULL, NULL);
      moments_calculate (pp->mom1, &cc1, &mean1, &sigma1, NULL, NULL);
      /* If this fails, then we're not dealing with missing values properly */
      assert (cc0 == cc1);

      double corr = ((pp->sum_of_prod / cc0 - mean0 * mean1)
                     / sqrt (sigma0 * sigma1) * cc0 / (cc0 - 1));
      double sig = 2.0 * significance_of_correlation (corr, cc0);
      double entries[] = { cc0, corr, sig };
      for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
        pivot_table_put2 (table, i, row, pivot_value_new_number (entries[i]));
    }

  pivot_table_submit (table);
}


static void
paired_test (const struct tt *tt, const struct paired_samp *os)
{
  struct pivot_table *table = pivot_table_create (N_("Paired Samples Test"));
  pivot_table_set_weight_var (table, tt->wv);

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Statistics"));
  struct pivot_category *group = pivot_category_create_group (
    statistics->root, N_("Paired Differences"),
    N_("Mean"), PIVOT_RC_OTHER,
    N_("Std. Deviation"), PIVOT_RC_OTHER,
    N_("S.E. Mean"), PIVOT_RC_OTHER);
  struct pivot_category *interval = pivot_category_create_group__ (
    group, pivot_value_new_text_format (
      N_("%g%% Confidence Interval of the Difference"),
      tt->confidence * 100.0));
  pivot_category_create_leaves (interval,
                                N_("Lower"), PIVOT_RC_OTHER,
                                N_("Upper"), PIVOT_RC_OTHER);
  pivot_category_create_leaves (statistics->root,
                                N_("t"), PIVOT_RC_OTHER,
                                N_("df"), PIVOT_RC_COUNT,
                                N_("Sig. (2-tailed)"), PIVOT_RC_SIGNIFICANCE);

  struct pivot_dimension *pairs = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Pairs"));

  for (size_t i = 0; i < os->n_ps; i++)
    {
      struct pair_stats *pp = &os->ps[i];
      struct pivot_category *group = pivot_category_create_group__ (
        pairs->root, pivot_value_new_text_format (N_("Pair %zu"), i + 1));

      int row = pivot_category_create_leaf (
        group, pivot_value_new_text_format (N_("%s - %s"),
                                            var_to_string (pp->var0),
                                            var_to_string (pp->var1)));

      double cc, mean, sigma;
      moments_calculate (pp->mom_diff, &cc, &mean, &sigma, NULL, NULL);

      double df = cc - 1.0;

      double t = mean * sqrt (cc / sigma);
      double se_mean = sqrt (sigma / cc);

      double p = gsl_cdf_tdist_P (t, df);
      double q = gsl_cdf_tdist_Q (t, df);
      double sig = 2.0 * (t > 0 ? q : p);

      double t_qinv = gsl_cdf_tdist_Qinv ((1.0 - tt->confidence) / 2.0, df);

      double entries[] = {
        mean,
        sqrt (sigma),
        se_mean,
        mean - t_qinv * se_mean,
        mean + t_qinv * se_mean,
        t,
        df,
        sig,
      };
      for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
        pivot_table_put2 (table, i, row, pivot_value_new_number (entries[i]));

    }

  pivot_table_submit (table);
}
