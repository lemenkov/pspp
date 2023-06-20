/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/commands/npar-summary.h"

#include <math.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "math/moments.h"
#include "output/pivot-table.h"

#include "gl/minmax.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)


void
npar_summary_calc_descriptives (struct descriptives *desc,
                                struct casereader *input,
                                const struct dictionary *dict,
                                const struct variable *const *vv,
                                int n_vars,
                                enum mv_class filter)
{
  int i = 0;
  for (i = 0 ; i < n_vars; ++i)
    {
      double minimum = DBL_MAX;
      double maximum = -DBL_MAX;
      double var;
      struct moments1 *moments = moments1_create (MOMENT_VARIANCE);
      struct ccase *c;
      const struct variable *v = vv[i];
      struct casereader *pass;

      pass = casereader_clone (input);
      pass = casereader_create_filter_missing (pass,
                                               &v, 1,
                                               filter, NULL, NULL);
      pass = casereader_create_filter_weight (pass, dict, NULL, NULL);
      while ((c = casereader_read (pass)) != NULL)
        {
          double val = case_num (c, v);
          double w = dict_get_case_weight (dict, c, NULL);
          minimum = MIN (minimum, val);
          maximum = MAX (maximum, val);
          moments1_add (moments, val, w);
          case_unref (c);
        }
      casereader_destroy (pass);

      moments1_calculate (moments,
                          &desc[i].n,
                          &desc[i].mean,
                          &var,
                          NULL, NULL);

      desc[i].std_dev = sqrt (var);

      moments1_destroy (moments);

      desc[i].min = minimum;
      desc[i].max = maximum;
    }

  casereader_destroy (input);
}



void
do_summary_box (const struct descriptives *desc,
                const struct variable *const *vv,
                int n_vars,
                const struct fmt_spec wfmt)
{
  if (!desc)
    return;

  struct pivot_table *table = pivot_table_create (
    N_("Descriptive Statistics"));
  pivot_table_set_weight_format (table, wfmt);

  pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Statistics"),
    N_("N"), PIVOT_RC_COUNT,
    N_("Mean"), PIVOT_RC_OTHER,
    N_("Std. Deviation"), PIVOT_RC_OTHER,
    N_("Minimum"), N_("Maximum"));

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable"));

  for (int v = 0; v < n_vars; ++v)
    {
      const struct variable *var = vv[v];

      int row = pivot_category_create_leaf (variables->root,
                                            pivot_value_new_variable (var));

      double entries[] = { desc[v].n, desc[v].mean, desc[v].std_dev };
      for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
        pivot_table_put2 (table, j, row, pivot_value_new_number (entries[j]));

      union value extrema[2] = { { .f = desc[v].min }, { .f = desc[v].max } };
      for (size_t j = 0; j < 2; j++)
        pivot_table_put2 (table, 3 + j, row,
                          pivot_value_new_var_value (var, &extrema[j]));
    }

  pivot_table_submit (table);
}
