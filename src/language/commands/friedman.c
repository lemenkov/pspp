/* PSPP - a program for statistical analysis. -*-c-*-
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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#include "language/commands/friedman.h"

#include <gsl/gsl_cdf.h>
#include <math.h>

#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "output/pivot-table.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)


struct friedman
{
  double *rank_sum;
  double cc;
  double chi_sq;
  double w;
  const struct dictionary *dict;
};

static void show_ranks_box (const struct one_sample_test *ost,
                            const struct friedman *fr);

static void show_sig_box (const struct one_sample_test *ost,
                          const struct friedman *fr);

struct datum
{
  long posn;
  double x;
};

static int
cmp_x (const void *a_, const void *b_)
{
  const struct datum *a = a_;
  const struct datum *b = b_;

  if (a->x < b->x)
    return -1;

  return (a->x > b->x);
}

static int
cmp_posn (const void *a_, const void *b_)
{
  const struct datum *a = a_;
  const struct datum *b = b_;

  if (a->posn < b->posn)
    return -1;

  return (a->posn > b->posn);
}

void
friedman_execute (const struct dataset *ds,
                  struct casereader *input,
                  enum mv_class exclude,
                  const struct npar_test *test,
                  bool exact UNUSED,
                  double timer UNUSED)
{
  double numerator = 0.0;
  double denominator = 0.0;
  int v;
  struct ccase *c;
  const struct dictionary *dict = dataset_dict (ds);
  const struct variable *weight = dict_get_weight (dict);

  struct one_sample_test *ost = UP_CAST (test, struct one_sample_test, parent);
  struct friedman_test *ft = UP_CAST (ost, struct friedman_test, parent);
  bool warn = true;

  double sigma_t = 0.0;
  struct datum *row = XCALLOC (ost->n_vars,  struct datum);
  double rsq;
  struct friedman fr;
  fr.rank_sum = xcalloc (ost->n_vars, sizeof *fr.rank_sum);
  fr.cc = 0.0;
  fr.dict = dict;
  for (v = 0; v < ost->n_vars; ++v)
    {
      row[v].posn = v;
      fr.rank_sum[v] = 0.0;
    }

  input = casereader_create_filter_weight (input, dict, &warn, NULL);
  input = casereader_create_filter_missing (input,
                                            ost->vars, ost->n_vars,
                                            exclude, 0, 0);

  for (; (c = casereader_read (input)); case_unref (c))
    {
      double prev_x = SYSMIS;
      int run_length = 0;

      const double w = weight ? case_num (c, weight) : 1.0;

      fr.cc += w;

      for (v = 0; v < ost->n_vars; ++v)
        {
          const struct variable *var = ost->vars[v];
          const union value *val = case_data (c, var);
          row[v].x = val->f;
        }

      qsort (row, ost->n_vars, sizeof *row, cmp_x);
      for (v = 0; v < ost->n_vars; ++v)
        {
          double x = row[v].x;
          /* Replace value by the Rank */
          if (prev_x == x)
            {
              /* Deal with ties */
              int i;
              run_length++;
              for (i = v - run_length; i < v; ++i)
                {
                  row[i].x *= run_length ;
                  row[i].x += v + 1;
                  row[i].x /= run_length + 1;
                }
              row[v].x = row[v-1].x;
            }
          else
            {
              row[v].x = v + 1;
              if (run_length > 0)
                {
                  double t = run_length + 1;
                  sigma_t += w * (pow3 (t) - t);
                }
              run_length = 0;
            }
          prev_x = x;
        }
      if (run_length > 0)
        {
          double t = run_length + 1;
          sigma_t += w * (pow3 (t) - t);
        }

      qsort (row, ost->n_vars, sizeof *row, cmp_posn);

      for (v = 0; v < ost->n_vars; ++v)
        fr.rank_sum[v] += row[v].x * w;
    }
  casereader_destroy (input);
  free (row);


  for (v = 0; v < ost->n_vars; ++v)
    {
      numerator += pow2 (fr.rank_sum[v]);
    }

  rsq = numerator;

  numerator *= 12.0 / (fr.cc * ost->n_vars * (ost->n_vars + 1));
  numerator -= 3 * fr.cc * (ost->n_vars + 1);

  denominator = 1 - sigma_t / (fr.cc * ost->n_vars * (pow2 (ost->n_vars) - 1));

  fr.chi_sq = numerator / denominator;

  if (ft->kendalls_w)
    {
      fr.w = 12 * rsq ;
      fr.w -= 3 * pow2 (fr.cc) *
        ost->n_vars * pow2 (ost->n_vars + 1);

      fr.w /= pow2 (fr.cc) * (pow3 (ost->n_vars) - ost->n_vars)
        - fr.cc * sigma_t;
    }
  else
    fr.w = SYSMIS;

  show_ranks_box (ost, &fr);
  show_sig_box (ost, &fr);

  free (fr.rank_sum);
}




static void
show_ranks_box (const struct one_sample_test *ost, const struct friedman *fr)
{
  struct pivot_table *table = pivot_table_create (N_("Ranks"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Mean Rank"),
                          N_("Mean Rank"), PIVOT_RC_OTHER);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable"));

  for (size_t i = 0 ; i < ost->n_vars ; ++i)
    {
      int row = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (ost->vars[i]));

      pivot_table_put2 (table, 0, row,
                        pivot_value_new_number (fr->rank_sum[i] / fr->cc));
    }

  pivot_table_submit (table);
}


static void
show_sig_box (const struct one_sample_test *ost, const struct friedman *fr)
{
  const struct friedman_test *ft = UP_CAST (ost, const struct friedman_test, parent);

  struct pivot_table *table = pivot_table_create (N_("Test Statistics"));
  pivot_table_set_weight_var (table, dict_get_weight (fr->dict));

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Statistics"),
    N_("N"), PIVOT_RC_COUNT);
  if (ft->kendalls_w)
    pivot_category_create_leaves (statistics->root, N_("Kendall's W"),
                                  PIVOT_RC_OTHER);
  pivot_category_create_leaves (statistics->root,
                                N_("Chi-Square"), PIVOT_RC_OTHER,
                                N_("df"), PIVOT_RC_INTEGER,
                                N_("Asymp. Sig."), PIVOT_RC_SIGNIFICANCE);

  double entries[5];
  int n = 0;

  entries[n++] = fr->cc;
  if (ft->kendalls_w)
    entries[n++] = fr->w;
  entries[n++] = fr->chi_sq;
  entries[n++] = ost->n_vars - 1;
  entries[n++] = gsl_cdf_chisq_Q (fr->chi_sq, ost->n_vars - 1);
  assert (n <= sizeof entries / sizeof *entries);

  for (size_t i = 0; i < n; i++)
    pivot_table_put1 (table, i, pivot_value_new_number (entries[i]));

  pivot_table_submit (table);
}
