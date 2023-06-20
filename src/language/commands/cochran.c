/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2014 Free Software Foundation, Inc.

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

#include "language/commands/cochran.h"

#include <float.h>
#include <gsl/gsl_cdf.h>
#include <stdbool.h>

#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/val-type.h"
#include "data/variable.h"
#include "language/commands/npar.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "output/pivot-table.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

struct cochran
{
  double success;
  double failure;

  double *hits;
  double *misses;

  const struct dictionary *dict;
  double cc;
  double df;
  double q;
};

static void show_freqs_box (const struct one_sample_test *ost, const struct cochran *ch);
static void show_sig_box (const struct cochran *ch);

void
cochran_execute (const struct dataset *ds,
              struct casereader *input,
              enum mv_class exclude,
              const struct npar_test *test,
              bool exact UNUSED, double timer UNUSED)
{
  struct one_sample_test *ct = UP_CAST (test, struct one_sample_test, parent);
  int v;
  struct cochran ch;
  const struct dictionary *dict = dataset_dict (ds);
  const struct variable *weight = dict_get_weight (dict);

  struct ccase *c;
  double rowsq = 0;
  ch.cc = 0.0;
  ch.dict = dict;
  ch.success = SYSMIS;
  ch.failure = SYSMIS;
  ch.hits = xcalloc (ct->n_vars, sizeof *ch.hits);
  ch.misses = xcalloc (ct->n_vars, sizeof *ch.misses);

  for (; (c = casereader_read (input)); case_unref (c))
    {
      double case_hits = 0.0;
      const double w = weight ? case_num (c, weight) : 1.0;
      for (v = 0; v < ct->n_vars; ++v)
        {
          const struct variable *var = ct->vars[v];
          const union value *val = case_data (c, var);

          if (var_is_value_missing (var, val) & exclude)
            continue;

          if (ch.success == SYSMIS)
            {
              ch.success = val->f;
            }
          else if (ch.failure == SYSMIS && val->f != ch.success)
            {
              ch.failure = val->f;
            }
          if (ch.success == val->f)
            {
              ch.hits[v] += w;
              case_hits += w;
            }
          else if (ch.failure == val->f)
            {
              ch.misses[v] += w;
            }
          else
            {
              msg (MW, _("More than two values encountered.  Cochran Q test will not be run."));
              goto finish;
            }
        }
      ch.cc += w;
      rowsq += pow2 (case_hits);
    }
  casereader_destroy (input);

  {
    double c_l = 0;
    double c_l2 = 0;
    for (v = 0; v < ct->n_vars; ++v)
      {
        c_l += ch.hits[v];
        c_l2 += pow2 (ch.hits[v]);
      }

    ch.q = ct->n_vars * c_l2;
    ch.q -= pow2 (c_l);
    ch.q *= ct->n_vars - 1;

    ch.q /= ct->n_vars * c_l - rowsq;

    ch.df = ct->n_vars - 1;
  }

  show_freqs_box (ct, &ch);
  show_sig_box (&ch);

 finish:

  free (ch.hits);
  free (ch.misses);
}

static void
show_freqs_box (const struct one_sample_test *ost, const struct cochran *ct)
{
  struct pivot_table *table = pivot_table_create (N_("Frequencies"));
  pivot_table_set_weight_var (table, dict_get_weight (ct->dict));

  char *success = xasprintf (_("Success (%.*g)"), DBL_DIG + 1, ct->success);
  char *failure = xasprintf (_("Failure (%.*g)"), DBL_DIG + 1, ct->failure);
  struct pivot_dimension *values = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Value"),
    success, PIVOT_RC_COUNT,
    failure, PIVOT_RC_COUNT);
  values->root->show_label = true;
  free (failure);
  free (success);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable"));

  for (size_t i = 0 ; i < ost->n_vars ; ++i)
    {
      int row = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (ost->vars[i]));

      pivot_table_put2 (table, 0, row, pivot_value_new_number (ct->hits[i]));
      pivot_table_put2 (table, 1, row, pivot_value_new_number (ct->misses[i]));
    }

  pivot_table_submit (table);
}

static void
show_sig_box (const struct cochran *ch)
{
  struct pivot_table *table = pivot_table_create (N_("Test Statistics"));

  pivot_table_set_weight_format (table, dict_get_weight_format (ch->dict));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Value"), N_("Value"));

  pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Statistics"),
    N_("N"), PIVOT_RC_COUNT,
    N_("Cochran's Q"), PIVOT_RC_SIGNIFICANCE,
    N_("df"), PIVOT_RC_INTEGER,
    N_("Asymp. Sig."), PIVOT_RC_SIGNIFICANCE);

  double sig = gsl_cdf_chisq_Q (ch->q, ch->df);
  double entries[] = { ch->cc, ch->q, ch->df, sig };
  for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
    pivot_table_put2 (table, 0, i, pivot_value_new_number (entries[i]));
  pivot_table_submit (table);
}
