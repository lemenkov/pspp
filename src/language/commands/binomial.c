/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009, 2010, 2011, 2014 Free Software Foundation, Inc.

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

#include "language/commands/binomial.h"

#include <float.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_randist.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/value-labels.h"
#include "data/value.h"
#include "data/variable.h"
#include "language/commands/freq.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"
#include "gl/minmax.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

static double calculate_binomial_internal (double n1, double n2,
                                           double p);


static void
swap (double *i1, double *i2)
{
  double temp = *i1;
  *i1 = *i2;
  *i2 = temp;
}

static double
calculate_binomial (double n1, double n2, double p)
{
  const double n = n1 + n2;
  const bool test_reversed = (n1 / n > p) ;
  if (test_reversed)
    {
      p = 1 - p ;
      swap (&n1, &n2);
    }

  return calculate_binomial_internal (n1, n2, p);
}

static double
calculate_binomial_internal (double n1, double n2, double p)
{
  /* SPSS Statistical Algorithms has completely different and WRONG
     advice here. */

  double sig1tailed = gsl_cdf_binomial_P (n1, p, n1 + n2);

  if (p == 0.5)
    return sig1tailed > 0.5 ? 1.0 :sig1tailed * 2.0;

  return sig1tailed ;
}

static bool
do_binomial (const struct dictionary *dict,
             struct casereader *input,
             const struct one_sample_test *ost,
             struct freq *cat1,
             struct freq *cat2,
             enum mv_class exclude
        )
{
  const struct binomial_test *bst = UP_CAST (ost, const struct binomial_test, parent);
  bool warn = true;

  struct ccase *c;

  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    {
      int v;
      double w = dict_get_case_weight (dict, c, &warn);

      for (v = 0 ; v < ost->n_vars ; ++v)
        {
          const struct variable *var = ost->vars[v];
          double value = case_num (c, var);

          if (var_is_num_missing (var, value) & exclude)
            continue;

          if (bst->cutpoint != SYSMIS)
            {
              if (cat1[v].values[0].f >= value)
                  cat1[v].count  += w;
              else
                  cat2[v].count += w;
            }
          else
            {
              if (SYSMIS == cat1[v].values[0].f)
                {
                  cat1[v].values[0].f = value;
                  cat1[v].count = w;
                }
              else if (cat1[v].values[0].f == value)
                cat1[v].count += w;
              else if (SYSMIS == cat2[v].values[0].f)
                {
                  cat2[v].values[0].f = value;
                  cat2[v].count = w;
                }
              else if (cat2[v].values[0].f == value)
                cat2[v].count += w;
              else if (bst->category1 == SYSMIS)
                msg (ME, _("Variable %s is not dichotomous"), var_get_name (var));
            }
        }
    }
  return casereader_destroy (input);
}



void
binomial_execute (const struct dataset *ds,
                  struct casereader *input,
                  enum mv_class exclude,
                  const struct npar_test *test,
                  bool exact UNUSED,
                  double timer UNUSED)
{
  const struct dictionary *dict = dataset_dict (ds);
  const struct one_sample_test *ost = UP_CAST (test, const struct one_sample_test, parent);
  const struct binomial_test *bst = UP_CAST (ost, const struct binomial_test, parent);

  struct freq *cat[2];
  int i;

  assert ((bst->category1 == SYSMIS) == (bst->category2 == SYSMIS) || bst->cutpoint != SYSMIS);

  for (i = 0; i < 2; i++)
    {
      double value;
      if (i == 0)
        value = bst->cutpoint != SYSMIS ? bst->cutpoint : bst->category1;
      else
        value = bst->category2;

      cat[i] = xnmalloc (ost->n_vars, sizeof *cat[i]);
      for (size_t v = 0; v < ost->n_vars; v++)
        {
          cat[i][v].values[0].f = value;
          cat[i][v].count = 0;
        }
    }

  if (do_binomial (dataset_dict (ds), input, ost, cat[0], cat[1], exclude))
    {
      struct pivot_table *table = pivot_table_create (N_("Binomial Test"));
      pivot_table_set_weight_var (table, dict_get_weight (dict));

      pivot_dimension_create (
        table, PIVOT_AXIS_COLUMN, N_("Statistics"),
        N_("Category"),
        N_("N"), PIVOT_RC_COUNT,
        N_("Observed Prop."), PIVOT_RC_OTHER,
        N_("Test Prop."), PIVOT_RC_OTHER,
        (bst->p == 0.5
         ? N_("Exact Sig. (2-tailed)")
         : N_("Exact Sig. (1-tailed)")), PIVOT_RC_SIGNIFICANCE);

      pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Groups"),
                              N_("Group 1"), N_("Group 2"), N_("Total"));

      struct pivot_dimension *variables = pivot_dimension_create (
        table, PIVOT_AXIS_ROW, N_("Variables"));

      for (size_t v = 0; v < ost->n_vars; ++v)
        {
          const struct variable *var = ost->vars[v];

          int var_idx = pivot_category_create_leaf (
            variables->root, pivot_value_new_variable (var));

          /* Category. */
          if (bst->cutpoint != SYSMIS)
            pivot_table_put3 (
              table, 0, 0, var_idx,
              pivot_value_new_user_text_nocopy (
                xasprintf ("<= %.*g", DBL_DIG + 1, bst->cutpoint)));
          else
            for (int i = 0; i < 2; i++)
              pivot_table_put3 (
                table, 0, i, var_idx,
                pivot_value_new_var_value (var, cat[i][v].values));

          double n_total = cat[0][v].count + cat[1][v].count;
          double sig = calculate_binomial (cat[0][v].count, cat[1][v].count,
                                           bst->p);
          struct entry
            {
              int stat_idx;
              int group_idx;
              double x;
            }
          entries[] = {
            /* N. */
            { 1, 0, cat[0][v].count },
            { 1, 1, cat[1][v].count },
            { 1, 2, n_total },
            /* Observed Prop. */
            { 2, 0, cat[0][v].count / n_total },
            { 2, 1, cat[1][v].count / n_total },
            { 2, 2, 1.0 },
            /* Test Prop. */
            { 3, 0, bst->p },
            /* Significance. */
            { 4, 0, sig }
          };
          for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
            {
              const struct entry *e = &entries[i];
              pivot_table_put3 (table, e->stat_idx, e->group_idx,
                                var_idx, pivot_value_new_number (e->x));
            }
        }

      pivot_table_submit (table);
    }

  for (i = 0; i < 2; i++)
    free (cat[i]);
}
