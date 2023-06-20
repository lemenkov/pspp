/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

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

#include "language/commands/mann-whitney.h"

#include <gsl/gsl_cdf.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "libpspp/cast.h"
#include "libpspp/misc.h"
#include "math/sort.h"
#include "output/pivot-table.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

/* Calculates the adjustment necessary for tie compensation */
static void
distinct_callback (double v UNUSED, casenumber t, double w UNUSED, void *aux)
{
  double *tiebreaker = aux;

  *tiebreaker += (pow3 (t) - t) / 12.0;
}

struct mw
{
  double rank_sum[2];
  double n[2];

  double u;  /* The Mann-Whitney U statistic */
  double w;  /* The Wilcoxon Rank Sum W statistic */
  double z;
};

static void show_ranks_box (const struct n_sample_test *, const struct mw *);
static void show_statistics_box (const struct n_sample_test *,
                                 const struct mw *);



static bool
belongs_to_test (const struct ccase *c, void *aux)
{
  const struct n_sample_test *nst = aux;

  const union value *group = case_data (c, nst->indep_var);
  const size_t group_var_width = var_get_width (nst->indep_var);

  if (value_equal (group, &nst->val1, group_var_width))
    return true;

  if (value_equal (group, &nst->val2, group_var_width))
    return true;

  return false;
}



void
mann_whitney_execute (const struct dataset *ds,
                      struct casereader *input,
                      enum mv_class exclude,
                      const struct npar_test *test,
                      bool exact UNUSED,
                      double timer UNUSED)
{
  int i;
  const struct dictionary *dict = dataset_dict (ds);
  const struct n_sample_test *nst = UP_CAST (test, const struct n_sample_test, parent);

  const struct caseproto *proto = casereader_get_proto (input);
  size_t rank_idx = caseproto_get_n_widths (proto);

  struct mw *mw = XCALLOC (nst->n_vars,  struct mw);

  for (i = 0; i < nst->n_vars; ++i)
    {
      double tiebreaker = 0.0;
      bool warn = true;
      enum rank_error rerr = 0;
      struct casereader *rr;
      struct ccase *c;
      const struct variable *var = nst->vars[i];

      struct casereader *reader =
        casereader_create_filter_func (casereader_clone (input),
                                       belongs_to_test,
                                       NULL,
                                       CONST_CAST (struct n_sample_test *, nst),
                                       NULL);

      reader = casereader_create_filter_missing (reader, &var, 1,
                                                 exclude,
                                                 NULL, NULL);

      reader = sort_execute_1var (reader, var);

      rr = casereader_create_append_rank (reader, var,
                                          dict_get_weight (dict),
                                          &rerr,
                                          distinct_callback, &tiebreaker);

      for (; (c = casereader_read (rr)); case_unref (c))
        {
          const union value *group = case_data (c, nst->indep_var);
          const size_t group_var_width = var_get_width (nst->indep_var);
          const double rank = case_num_idx (c, rank_idx);

          if (value_equal (group, &nst->val1, group_var_width))
            {
              mw[i].rank_sum[0] += rank;
              mw[i].n[0] += dict_get_case_weight (dict, c, &warn);
            }
          else if (value_equal (group, &nst->val2, group_var_width))
            {
              mw[i].rank_sum[1] += rank;
              mw[i].n[1] += dict_get_case_weight (dict, c, &warn);
            }
        }
      casereader_destroy (rr);

      {
        double n;
        double denominator;
        struct mw *mwv = &mw[i];

        mwv->u = mwv->n[0] * mwv->n[1] ;
        mwv->u += mwv->n[0] * (mwv->n[0] + 1) / 2.0;
        mwv->u -= mwv->rank_sum[0];

        mwv->w = mwv->rank_sum[1];
        if (mwv->u > mwv->n[0] * mwv->n[1] / 2.0)
          {
            mwv->u =  mwv->n[0] * mwv->n[1] - mwv->u;
            mwv->w = mwv->rank_sum[0];
          }
        mwv->z = mwv->u - mwv->n[0] * mwv->n[1] / 2.0;
        n = mwv->n[0] + mwv->n[1];
        denominator = pow3(n) - n;
        denominator /= 12;
        denominator -= tiebreaker;
        denominator *= mwv->n[0] * mwv->n[1];
        denominator /= n * (n - 1);

        mwv->z /= sqrt (denominator);
      }
    }
  casereader_destroy (input);

  show_ranks_box (nst, mw);
  show_statistics_box (nst, mw);

  free (mw);
}

static void
show_ranks_box (const struct n_sample_test *nst, const struct mw *mwv)
{
  struct pivot_table *table = pivot_table_create (N_("Ranks"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_COUNT,
                          N_("Mean Rank"), PIVOT_RC_OTHER,
                          N_("Sum of Ranks"), PIVOT_RC_OTHER);

  struct pivot_dimension *indep = pivot_dimension_create__ (
    table, PIVOT_AXIS_ROW, pivot_value_new_variable (nst->indep_var));
  pivot_category_create_leaf (indep->root,
                              pivot_value_new_var_value (nst->indep_var,
                                                         &nst->val1));
  pivot_category_create_leaf (indep->root,
                              pivot_value_new_var_value (nst->indep_var,
                                                         &nst->val2));
  pivot_category_create_leaves (indep->root, N_("Total"));

  struct pivot_dimension *dep = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Dependent Variables"));

  for (size_t i = 0 ; i < nst->n_vars ; ++i)
    {
      const struct mw *mw = &mwv[i];

      int dep_idx = pivot_category_create_leaf (
        dep->root, pivot_value_new_variable (nst->vars[i]));

      struct entry
        {
          int stat_idx;
          int indep_idx;
          double x;
        }
      entries[] = {
        /* N. */
        { 0, 0, mw->n[0] },
        { 0, 1, mw->n[1] },
        { 0, 2, mw->n[0] + mw->n[1] },

        /* Mean Rank. */
        { 1, 0, mw->rank_sum[0] / mw->n[0] },
        { 1, 1, mw->rank_sum[1] / mw->n[1] },

        /* Sum of Ranks. */
        { 2, 0, mw->rank_sum[0] },
        { 2, 1, mw->rank_sum[1] },
      };

      for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
        {
          const struct entry *e = &entries[j];
          pivot_table_put3 (table, e->stat_idx, e->indep_idx, dep_idx,
                            pivot_value_new_number (e->x));
        }
    }

  pivot_table_submit (table);
}

static void
show_statistics_box (const struct n_sample_test *nst, const struct mw *mwv)
{
  struct pivot_table *table = pivot_table_create (N_("Test Statistics"));

  pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Statistics"),
    _("Mann-Whitney U"), PIVOT_RC_OTHER,
    _("Wilcoxon W"), PIVOT_RC_OTHER,
    _("Z"), PIVOT_RC_OTHER,
    _("Asymp. Sig. (2-tailed)"), PIVOT_RC_SIGNIFICANCE);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));

  for (size_t i = 0 ; i < nst->n_vars ; ++i)
    {
      const struct mw *mw = &mwv[i];

      int row = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (nst->vars[i]));

      double entries[] = {
        mw->u,
        mw->w,
        mw->z,
        2.0 * gsl_cdf_ugaussian_P (mw->z),
      };
      for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
        pivot_table_put2 (table, i, row, pivot_value_new_number (entries[i]));
    }

  pivot_table_submit (table);
}
