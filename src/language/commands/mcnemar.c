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

#include "mcnemar.h"

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
#include "libpspp/message.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "data/value.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)


struct mcnemar
{
  union value val0;
  union value val1;

  double n00;
  double n01;
  double n10;
  double n11;
};

static void
output_freq_table (variable_pair *vp,
                   const struct mcnemar *param,
                   const struct dictionary *dict);


static void
output_statistics_table (const struct two_sample_test *t2s,
                         const struct mcnemar *param,
                         const struct dictionary *dict);


void
mcnemar_execute (const struct dataset *ds,
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

  struct casereader *r = input;

  struct mcnemar *mc = XCALLOC (t2s->n_pairs,  struct mcnemar);

  for (i = 0 ; i < t2s->n_pairs; ++i)
    {
      mc[i].val0.f = mc[i].val1.f = SYSMIS;
    }

  for (; (c = casereader_read (r)) != NULL; case_unref (c))
    {
      const double weight = dict_get_case_weight (dict, c, &warn);

      for (i = 0 ; i < t2s->n_pairs; ++i)
        {
          variable_pair *vp = &t2s->pairs[i];
          const union value *value0 = case_data (c, (*vp)[0]);
          const union value *value1 = case_data (c, (*vp)[1]);

          if (var_is_value_missing ((*vp)[0], value0) & exclude)
            continue;

          if (var_is_value_missing ((*vp)[1], value1) & exclude)
            continue;


          if (mc[i].val0.f == SYSMIS)
            {
              if (mc[i].val1.f != value0->f)
                mc[i].val0.f = value0->f;
              else if (mc[i].val1.f != value1->f)
                mc[i].val0.f = value1->f;
            }

          if (mc[i].val1.f == SYSMIS)
            {
              if (mc[i].val0.f != value1->f)
                mc[i].val1.f = value1->f;
              else if (mc[i].val0.f != value0->f)
                mc[i].val1.f = value0->f;
            }

          if (mc[i].val0.f == value0->f && mc[i].val0.f == value1->f)
            {
              mc[i].n00 += weight;
            }
          else if (mc[i].val0.f == value0->f && mc[i].val1.f == value1->f)
            {
              mc[i].n10 += weight;
            }
          else if (mc[i].val1.f == value0->f && mc[i].val0.f == value1->f)
            {
              mc[i].n01 += weight;
            }
          else if (mc[i].val1.f == value0->f && mc[i].val1.f == value1->f)
            {
              mc[i].n11 += weight;
            }
          else
            {
              msg (ME, _("The McNemar test is appropriate only for dichotomous variables"));
            }
        }
    }

  casereader_destroy (r);

  for (i = 0 ; i < t2s->n_pairs ; ++i)
    output_freq_table (&t2s->pairs[i], mc + i, dict);

  output_statistics_table (t2s, mc, dict);

  free (mc);
}

static char *
make_pair_name (const variable_pair *pair)
{
  return xasprintf ("%s & %s", var_to_string ((*pair)[0]),
                    var_to_string ((*pair)[1]));
}

static void
output_freq_table (variable_pair *vp,
                   const struct mcnemar *param,
                   const struct dictionary *dict)
{
  struct pivot_table *table = pivot_table_create__ (
    pivot_value_new_user_text_nocopy (make_pair_name (vp)), "Frequencies");
  pivot_table_set_weight_var (table, dict_get_weight (dict));

  struct pivot_dimension *vars[2];
  for (int i = 0; i < 2; i++)
    {
      vars[i] = pivot_dimension_create__ (
        table, i ? PIVOT_AXIS_COLUMN : PIVOT_AXIS_ROW,
        pivot_value_new_variable ((*vp)[i]));
      vars[i]->root->show_label = true;

      for (int j = 0; j < 2; j++)
        {
          const union value *val = j ? &param->val1 : &param->val0;
          pivot_category_create_leaf_rc (
            vars[i]->root, pivot_value_new_var_value ((*vp)[0], val),
            PIVOT_RC_COUNT);
        }
    }

  struct entry
    {
      int idx0;
      int idx1;
      double x;
    }
  entries[] = {
    { 0, 0, param->n00 },
    { 1, 0, param->n01 },
    { 0, 1, param->n10 },
    { 1, 1, param->n11 },
  };
  for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
    {
      const struct entry *e = &entries[i];
      pivot_table_put2 (table, e->idx0, e->idx1,
                        pivot_value_new_number (e->x));
    }

  pivot_table_submit (table);
}

static void
output_statistics_table (const struct two_sample_test *t2s,
                         const struct mcnemar *mc,
                         const struct dictionary *dict)
{
  struct pivot_table *table = pivot_table_create (N_("Test Statistics"));
  pivot_table_set_weight_var (table, dict_get_weight (dict));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_COUNT,
                          N_("Exact Sig. (2-tailed)"), PIVOT_RC_SIGNIFICANCE,
                          N_("Exact Sig. (1-tailed)"), PIVOT_RC_SIGNIFICANCE,
                          N_("Point Probability"), PIVOT_RC_OTHER);

  struct pivot_dimension *pairs = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Pairs"));

  for (size_t i = 0 ; i < t2s->n_pairs; ++i)
    {
      variable_pair *vp = &t2s->pairs[i];
      int pair_idx = pivot_category_create_leaf (
        pairs->root, pivot_value_new_user_text_nocopy (make_pair_name (vp)));

      double n = mc[i].n00 + mc[i].n01 + mc[i].n10 + mc[i].n11;
      double sig = gsl_cdf_binomial_P ((mc[i].n01 > mc[i].n10) ? mc[i].n10: mc[i].n01,
                                       0.5, mc[i].n01 + mc[i].n10);

      double point = gsl_ran_binomial_pdf (mc[i].n01, 0.5,
                                           mc[i].n01 + mc[i].n10);
      double entries[] = { n, 2.0 * sig, sig, point };
      for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
        pivot_table_put2 (table, j, pair_idx,
                          pivot_value_new_number (entries[j]));
    }

  pivot_table_submit (table);
}
