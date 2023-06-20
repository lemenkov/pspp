/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/commands/sign.h"

#include <gsl/gsl_cdf.h>
#include <gsl/gsl_randist.h>

#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "data/variable.h"
#include "language/commands/npar.h"
#include "libpspp/str.h"
#include "output/pivot-table.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

struct sign_test_params
{
  double pos;
  double ties;
  double neg;

  double one_tailed_sig;
  double point_prob;
};

static int
add_pair_leaf (struct pivot_dimension *dimension, variable_pair *pair)
{
  char *label = xasprintf ("%s - %s", var_to_string ((*pair)[0]),
                           var_to_string ((*pair)[1]));
  return pivot_category_create_leaf (
    dimension->root,
    pivot_value_new_user_text_nocopy (label));
}

static void
output_frequency_table (const struct two_sample_test *t2s,
                        const struct sign_test_params *param,
                        const struct dictionary *dict)
{
  struct pivot_table *table = pivot_table_create (N_("Frequencies"));
  pivot_table_set_weight_var (table, dict_get_weight (dict));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("N"),
                          N_("N"), PIVOT_RC_COUNT);

  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Differences"),
                          N_("Negative Differences"),
                          N_("Positive Differences"),
                          N_("Ties"), N_("Total"));

  struct pivot_dimension *pairs = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Pairs"));

  for (size_t i = 0 ; i < t2s->n_pairs; ++i)
    {
      variable_pair *vp = &t2s->pairs[i];

      int pair_idx = add_pair_leaf (pairs, vp);

      const struct sign_test_params *p = &param[i];
      double values[] = { p->neg, p->pos, p->ties, p->ties + p->neg + p->pos };
      for (size_t j = 0; j < sizeof values / sizeof *values; j++)
        pivot_table_put3 (table, 0, j, pair_idx,
                          pivot_value_new_number (values[j]));
    }

  pivot_table_submit (table);
}

static void
output_statistics_table (const struct two_sample_test *t2s,
                         const struct sign_test_params *param)
{
  struct pivot_table *table = pivot_table_create (N_("Test Statistics"));

  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Statistics"),
                          N_("Exact Sig. (2-tailed)"), PIVOT_RC_SIGNIFICANCE,
                          N_("Exact Sig. (1-tailed)"), PIVOT_RC_SIGNIFICANCE,
                          N_("Point Probability"), PIVOT_RC_SIGNIFICANCE);

  struct pivot_dimension *pairs = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Pairs"));

  for (size_t i = 0 ; i < t2s->n_pairs; ++i)
    {
      variable_pair *vp = &t2s->pairs[i];
      int pair_idx = add_pair_leaf (pairs, vp);

      const struct sign_test_params *p = &param[i];
      double values[] = { p->one_tailed_sig * 2,
                          p->one_tailed_sig,
                          p->point_prob };
      for (size_t j = 0; j < sizeof values / sizeof *values; j++)
        pivot_table_put2 (table, j, pair_idx,
                          pivot_value_new_number (values[j]));
    }

  pivot_table_submit (table);
}

void
sign_execute (const struct dataset *ds,
                  struct casereader *input,
                  enum mv_class exclude,
                  const struct npar_test *test,
                  bool exact UNUSED,
                  double timer UNUSED)
{
  int i;
  bool warn = true;
  const struct dictionary *dict = dataset_dict (ds);
  const struct two_sample_test *t2s = UP_CAST (test, const struct two_sample_test, parent);
  struct ccase *c;

  struct sign_test_params *stp = XCALLOC (t2s->n_pairs,  struct sign_test_params);

  struct casereader *r = input;

  for (; (c = casereader_read (r)) != NULL; case_unref (c))
    {
      const double weight = dict_get_case_weight (dict, c, &warn);

      for (i = 0 ; i < t2s->n_pairs; ++i)
        {
          variable_pair *vp = &t2s->pairs[i];
          const union value *value0 = case_data (c, (*vp)[0]);
          const union value *value1 = case_data (c, (*vp)[1]);
          const double diff = value0->f - value1->f;

          if (var_is_value_missing ((*vp)[0], value0) & exclude)
            continue;

          if (var_is_value_missing ((*vp)[1], value1) & exclude)
            continue;

          if (diff > 0)
            stp[i].pos += weight;
          else if (diff < 0)
            stp[i].neg += weight;
          else
            stp[i].ties += weight;
        }
    }

  casereader_destroy (r);

  for (i = 0 ; i < t2s->n_pairs; ++i)
    {
      int r = MIN (stp[i].pos, stp[i].neg);
      stp[i].one_tailed_sig = gsl_cdf_binomial_P (r,
                                                  0.5,
                                                  stp[i].pos + stp[i].neg);

      stp[i].point_prob = gsl_ran_binomial_pdf (r, 0.5,
                                                stp[i].pos + stp[i].neg);
    }

  output_frequency_table (t2s, stp, dict);

  output_statistics_table (t2s, stp);

  free (stp);
}
