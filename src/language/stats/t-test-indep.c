/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2020 Free Software Foundation, Inc.

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

#include "t-test.h"

#include <gsl/gsl_cdf.h>
#include <math.h>
#include "libpspp/misc.h"

#include "libpspp/str.h"
#include "data/casereader.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "math/moments.h"
#include "math/levene.h"
#include "output/pivot-table.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)


struct indep_samples
{
  const struct variable *gvar;
  bool cut;
  const union value *gval0;
  const union value *gval1;
};

struct pair_stats
{
  struct moments *mom[2];
  double lev ;
  struct levene *nl;
};


static void indep_summary (const struct tt *tt, struct indep_samples *is, const struct pair_stats *ps);
static void indep_test (const struct tt *tt, const struct pair_stats *ps);

static int
which_group (const union value *v, const struct indep_samples *is)
{
  int width = var_get_width (is->gvar);
  int cmp = value_compare_3way (v, is->gval0, width);
  if (is->cut)
    return  (cmp < 0);

  if (cmp == 0)
    return 0;

  if (0 == value_compare_3way (v, is->gval1, width))
    return 1;

  return -1;
}

void
indep_run (struct tt *tt, const struct variable *gvar,
	   bool cut,
	   const union value *gval0, const union value *gval1,
	   struct casereader *reader)
{
  struct indep_samples is;
  struct ccase *c;
  struct casereader *r;

  struct pair_stats *ps = XCALLOC (tt->n_vars,  struct pair_stats);

  int v;

  for (v = 0; v < tt->n_vars; ++v)
    {
      ps[v].mom[0] = moments_create (MOMENT_VARIANCE);
      ps[v].mom[1] = moments_create (MOMENT_VARIANCE);
      ps[v].nl = levene_create (var_get_width (gvar), cut ? gval0: NULL);
    }

  is.gvar = gvar;
  is.gval0 = gval0;
  is.gval1 = gval1;
  is.cut = cut;

  r = casereader_clone (reader);
  for (; (c = casereader_read (r)); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);

      const union value *gv = case_data (c, gvar);

      int grp = which_group (gv, &is);
      if (grp < 0)
	continue;

      for (v = 0; v < tt->n_vars; ++v)
	{
	  const union value *val = case_data (c, tt->vars[v]);
	  if (var_is_value_missing (tt->vars[v], val, tt->exclude))
	    continue;

	  moments_pass_one (ps[v].mom[grp], val->f, w);
	  levene_pass_one (ps[v].nl, val->f, w, gv);
	}
    }
  casereader_destroy (r);

  r = casereader_clone (reader);
  for (; (c = casereader_read (r)); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);

      const union value *gv = case_data (c, gvar);

      int grp = which_group (gv, &is);
      if (grp < 0)
	continue;

      for (v = 0; v < tt->n_vars; ++v)
	{
	  const union value *val = case_data (c, tt->vars[v]);
	  if (var_is_value_missing (tt->vars[v], val, tt->exclude))
	    continue;

	  moments_pass_two (ps[v].mom[grp], val->f, w);
	  levene_pass_two (ps[v].nl, val->f, w, gv);
	}
    }
  casereader_destroy (r);

  r = reader;
  for (; (c = casereader_read (r)); case_unref (c))
    {
      double w = dict_get_case_weight (tt->dict, c, NULL);

      const union value *gv = case_data (c, gvar);

      int grp = which_group (gv, &is);
      if (grp < 0)
	continue;

      for (v = 0; v < tt->n_vars; ++v)
	{
	  const union value *val = case_data (c, tt->vars[v]);
	  if (var_is_value_missing (tt->vars[v], val, tt->exclude))
	    continue;

	  levene_pass_three (ps[v].nl, val->f, w, gv);
	}
    }
  casereader_destroy (r);


  for (v = 0; v < tt->n_vars; ++v)
    ps[v].lev = levene_calculate (ps[v].nl);

  indep_summary (tt, &is, ps);
  indep_test (tt, ps);


  for (v = 0; v < tt->n_vars; ++v)
    {
      moments_destroy (ps[v].mom[0]);
      moments_destroy (ps[v].mom[1]);
      levene_destroy (ps[v].nl);
    }
  free (ps);
}


static void
indep_summary (const struct tt *tt, struct indep_samples *is, const struct pair_stats *ps)
{
  struct pivot_table *table = pivot_table_create (N_("Group Statistics"));
  pivot_table_set_weight_var (table, tt->wv);

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_COUNT,
                          N_("Mean"), PIVOT_RC_OTHER,
                          N_("Std. Deviation"), PIVOT_RC_OTHER,
                          N_("S.E. Mean"), PIVOT_RC_OTHER);

  struct pivot_dimension *group = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Group"));
  group->root->show_label = true;
  if (is->cut)
    {
      struct string vallab0 = DS_EMPTY_INITIALIZER;
      ds_put_cstr (&vallab0, "≥");
      var_append_value_name (is->gvar, is->gval0, &vallab0);
      pivot_category_create_leaf (group->root,
                                  pivot_value_new_user_text_nocopy (
                                    ds_steal_cstr (&vallab0)));

      struct string vallab1 = DS_EMPTY_INITIALIZER;
      ds_put_cstr (&vallab1, "<");
      var_append_value_name (is->gvar, is->gval0, &vallab1);
      pivot_category_create_leaf (group->root,
                                  pivot_value_new_user_text_nocopy (
                                    ds_steal_cstr (&vallab1)));
    }
  else
    {
      pivot_category_create_leaf (
        group->root, pivot_value_new_var_value (is->gvar, is->gval0));
      pivot_category_create_leaf (
        group->root, pivot_value_new_var_value (is->gvar, is->gval1));
    }

  struct pivot_dimension *dep_vars = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Dependent Variables"));

  for (size_t v = 0; v < tt->n_vars; ++v)
    {
      const struct variable *var = tt->vars[v];

      int dep_var_idx = pivot_category_create_leaf (
        dep_vars->root, pivot_value_new_variable (var));

      for (int i = 0 ; i < 2; ++i)
	{
	  double cc, mean, sigma;
	  moments_calculate (ps[v].mom[i], &cc, &mean, &sigma, NULL, NULL);

          double entries[] = { cc, mean, sqrt (sigma), sqrt (sigma / cc) };
          for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
            pivot_table_put3 (table, j, i, dep_var_idx,
                              pivot_value_new_number (entries[j]));
	}
    }

  pivot_table_submit (table);
}


static void
indep_test (const struct tt *tt, const struct pair_stats *ps)
{
  struct pivot_table *table = pivot_table_create (
    N_("Independent Samples Test"));

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Statistics"));
  pivot_category_create_group (
    statistics->root, N_("Levene's Test for Equality of Variances"),
    N_("F"), PIVOT_RC_OTHER,
    N_("Sig."), PIVOT_RC_SIGNIFICANCE);
  struct pivot_category *group = pivot_category_create_group (
    statistics->root, N_("T-Test for Equality of Means"),
    N_("t"), PIVOT_RC_OTHER,
    N_("df"), PIVOT_RC_OTHER,
    N_("Sig. (2-tailed)"), PIVOT_RC_SIGNIFICANCE,
    N_("Mean Difference"), PIVOT_RC_OTHER,
    N_("Std. Error Difference"), PIVOT_RC_OTHER);
  pivot_category_create_group (
    /* xgettext:no-c-format */
    group, N_("95% Confidence Interval of the Difference"),
    N_("Lower"), PIVOT_RC_OTHER,
    N_("Upper"), PIVOT_RC_OTHER);

  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Assumptions"),
                          N_("Equal variances assumed"),
                          N_("Equal variances not assumed"));

  struct pivot_dimension *dep_vars = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Dependent Variables"));

  for (size_t v = 0; v < tt->n_vars; ++v)
  {
    int dep_var_idx = pivot_category_create_leaf (
      dep_vars->root, pivot_value_new_variable (tt->vars[v]));

    double cc0, mean0, sigma0;
    double cc1, mean1, sigma1;
    moments_calculate (ps[v].mom[0], &cc0, &mean0, &sigma0, NULL, NULL);
    moments_calculate (ps[v].mom[1], &cc1, &mean1, &sigma1, NULL, NULL);

    double mean_diff = mean0 - mean1;


    /* Equal variances assumed. */
    double e_df = cc0 + cc1 - 2.0;
    double e_pooled_variance = ((cc0 - 1)* sigma0 + (cc1 - 1) * sigma1) / e_df;
    double e_tval = ((mean0 - mean1) / sqrt (e_pooled_variance)
                     / sqrt ((cc0 + cc1) / (cc0 * cc1)));
    double e_p = gsl_cdf_tdist_P (e_tval, e_df);
    double e_q = gsl_cdf_tdist_Q (e_tval, e_df);
    double e_sig = 2.0 * (e_tval > 0 ? e_q : e_p);
    double e_std_err_diff = sqrt (e_pooled_variance * (1.0/cc0 + 1.0/cc1));
    double e_tval_qinv = gsl_cdf_tdist_Qinv ((1 - tt->confidence) / 2.0, e_df);

    /* Equal variances not assumed */
    const double s0 = sigma0 / cc0;
    const double s1 = sigma1 / cc1;
    double d_df = (pow2 (s0 + s1) / (pow2 (s0) / (cc0 - 1)
                                   + pow2 (s1) / (cc1 - 1)));
    double d_tval = mean_diff / sqrt (sigma0 / cc0 + sigma1 / cc1);
    double d_p = gsl_cdf_tdist_P (d_tval, d_df);
    double d_q = gsl_cdf_tdist_Q (d_tval, d_df);
    double d_sig = 2.0 * (d_tval > 0 ? d_q : d_p);
    double d_std_err_diff = sqrt ((sigma0 / cc0) + (sigma1 / cc1));
    double d_tval_qinv = gsl_cdf_tdist_Qinv ((1 - tt->confidence) / 2.0, d_df);

    struct entry
      {
        int assumption_idx;
        int stat_idx;
        double x;
      }
    entries[] =
      {
        { 0, 0, ps[v].lev },
        { 0, 1, gsl_cdf_fdist_Q (ps[v].lev, 1, cc0 + cc1 - 2) },

        { 0, 2, e_tval },
        { 0, 3, e_df },
        { 0, 4, e_sig },
        { 0, 5, mean_diff },
        { 0, 6, e_std_err_diff },
        { 0, 7, mean_diff - e_tval_qinv * e_std_err_diff },
        { 0, 8, mean_diff + e_tval_qinv * e_std_err_diff },

        { 1, 2, d_tval },
        { 1, 3, d_df },
        { 1, 4, d_sig },
        { 1, 5, mean_diff },
        { 1, 6, d_std_err_diff },
        { 1, 7, mean_diff - d_tval_qinv * d_std_err_diff },
        { 1, 8, mean_diff + d_tval_qinv * d_std_err_diff },
      };

    for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
      {
        const struct entry *e = &entries[i];
        pivot_table_put3 (table, e->stat_idx, e->assumption_idx,
                          dep_var_idx, pivot_value_new_number (e->x));
      }
  }

  pivot_table_submit (table);
}
