/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2007, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/commands/chisquare.h"

#include <gsl/gsl_cdf.h>
#include <math.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "language/commands/freq.h"
#include "language/commands/npar.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/message.h"
#include "libpspp/taint.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

/* Adds frequency counts of each value of VAR in INPUT between LO and HI to
   FREQ_HASH.  LO and HI and each input value is truncated to an integer.
   Returns true if successful, false on input error.  It is the caller's
   responsibility to initialize FREQ_HASH and to free it when no longer
   required, even on failure. */
static bool
create_freq_hash_with_range (const struct dictionary *dict,
                             struct casereader *input,
                             const struct variable *var,
                             double lo_, double hi_,
                             struct hmap *freq_hash)
{
  struct freq **entries;
  bool warn = true;
  struct ccase *c;
  double lo, hi;
  double i_d;

  assert (var_is_numeric (var));
  lo = trunc (lo_);
  hi = trunc (hi_);

  /* Populate the hash with zero entries */
  entries = xnmalloc (hi - lo + 1, sizeof *entries);
  for (i_d = lo; i_d <= hi; i_d += 1.0)
    {
      size_t ofs = i_d - lo;
      union value value = { i_d };
      entries[ofs] = freq_hmap_insert (freq_hash, &value, 0,
                                       value_hash (&value, 0, 0));
    }

  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    {
      double x = trunc (case_num (c, var));
      if (x >= lo && x <= hi)
        {
          size_t ofs = x - lo;
          struct freq *fr = entries[ofs];
          fr->count += dict_get_case_weight (dict, c, &warn);
        }
    }

  free (entries);

  return casereader_destroy (input);
}

/* Adds frequency counts of each value of VAR in INPUT to FREQ_HASH.  LO and HI
   and each input value is truncated to an integer.  Returns true if
   successful, false on input error.  It is the caller's responsibility to
   initialize FREQ_HASH and to free it when no longer required, even on
   failure. */
static bool
create_freq_hash (const struct dictionary *dict,
                  struct casereader *input,
                  const struct variable *var,
                  struct hmap *freq_hash)
{
  int width = var_get_width (var);
  bool warn = true;
  struct ccase *c;

  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    {
      const union value *value = case_data (c, var);
      size_t hash = value_hash (value, width, 0);
      double weight = dict_get_case_weight (dict, c, &warn);
      struct freq *f;

      f = freq_hmap_search (freq_hash, value, width, hash);
      if (f == NULL)
        f = freq_hmap_insert (freq_hash, value, width, hash);

      f->count += weight;
    }

  return casereader_destroy (input);
}

void
chisquare_execute (const struct dataset *ds,
                   struct casereader *input,
                   enum mv_class exclude,
                   const struct npar_test *test,
                   bool exact UNUSED,
                   double timer UNUSED)
{
  const struct dictionary *dict = dataset_dict (ds);
  int v, i;
  struct chisquare_test *cst = UP_CAST (test, struct chisquare_test,
                                        parent.parent);
  struct one_sample_test *ost = &cst->parent;
  double total_expected = 0.0;

  double *df = XCALLOC (ost->n_vars, double);
  double *xsq = XCALLOC (ost->n_vars, double);
  bool ok;

  for (i = 0 ; i < cst->n_expected ; ++i)
    total_expected += cst->expected[i];

  if (cst->ranged == false)
    {
      for (v = 0 ; v < ost->n_vars ; ++v)
        {
          const struct variable *var = ost->vars[v];

          struct hmap freq_hash = HMAP_INITIALIZER (freq_hash);
          struct casereader *reader =
            casereader_create_filter_missing (casereader_clone (input),
                                              &var, 1, exclude,
                                              NULL, NULL);
          if (!create_freq_hash (dict, reader, var, &freq_hash))
            {
              freq_hmap_destroy (&freq_hash, var_get_width (var));
              return;
            }

          size_t n_cells = hmap_count (&freq_hash);
          if (cst->n_expected > 0 && n_cells != cst->n_expected)
            {
              msg (ME, _("CHISQUARE test specified %d expected values, but "
                         "variable %s has %zu distinct values."),
                   cst->n_expected, var_get_name (var), n_cells);
              freq_hmap_destroy (&freq_hash, var_get_width (var));
              continue;
            }

          struct pivot_table *table = pivot_table_create__ (
            pivot_value_new_variable (var), "Chisquare");
          pivot_table_set_weight_var (table, dict_get_weight (dict));

          pivot_dimension_create (
            table, PIVOT_AXIS_COLUMN, N_("Statistics"),
            N_("Observed N"), PIVOT_RC_COUNT,
            N_("Expected N"), PIVOT_RC_OTHER,
            N_("Residual"), PIVOT_RC_RESIDUAL);

          struct freq **ff = freq_hmap_sort (&freq_hash, var_get_width (var));

          double total_obs = 0.0;
          for (size_t i = 0; i < n_cells; i++)
            total_obs += ff[i]->count;

          struct pivot_dimension *values = pivot_dimension_create (
            table, PIVOT_AXIS_ROW, N_("Value"));
          values->root->show_label = true;

          xsq[v] = 0.0;
          for (size_t i = 0; i < n_cells; i++)
            {
              int row = pivot_category_create_leaf (
                values->root, pivot_value_new_var_value (
                  var, &ff[i]->values[0]));

              double exp = (cst->n_expected > 0
                            ? cst->expected[i] * total_obs / total_expected
                            : total_obs / (double) n_cells);
              double entries[] = {
                ff[i]->count,
                exp,
                ff[i]->count - exp,
              };
              for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
                pivot_table_put2 (
                  table, j, row, pivot_value_new_number (entries[j]));

              xsq[v] += (ff[i]->count - exp) * (ff[i]->count - exp) / exp;
            }

          df[v] = n_cells - 1.0;

          int row = pivot_category_create_leaf (
            values->root, pivot_value_new_text (N_("Total")));
          pivot_table_put2 (table, 0, row,
                            pivot_value_new_number (total_obs));

          pivot_table_submit (table);

          freq_hmap_destroy (&freq_hash, var_get_width (var));
          free (ff);
        }
    }
  else  /* ranged == true */
    {
      struct pivot_table *table = pivot_table_create (N_("Frequencies"));
      pivot_table_set_weight_var (table, dict_get_weight (dict));

      pivot_dimension_create (
        table, PIVOT_AXIS_COLUMN, N_("Statistics"),
        N_("Category"),
        N_("Observed N"), PIVOT_RC_COUNT,
        N_("Expected N"), PIVOT_RC_OTHER,
        N_("Residual"), PIVOT_RC_RESIDUAL);

      struct pivot_dimension *var_dim
        = pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Variable"));
      for (size_t i = 0 ; i < ost->n_vars ; ++i)
        pivot_category_create_leaf (var_dim->root,
                                    pivot_value_new_variable (ost->vars[i]));

      struct pivot_dimension *category_dim
        = pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Category"));
      size_t n_cells = cst->hi - cst->lo + 1;
      for (size_t i = 0 ; i < n_cells; ++i)
        pivot_category_create_leaf (category_dim->root,
                                    pivot_value_new_integer (i + 1));
      pivot_category_create_leaves (category_dim->root, N_("Total"));

      for (size_t v = 0 ; v < ost->n_vars ; ++v)
        {
          const struct variable *var = ost->vars[v];
          struct casereader *reader =
            casereader_create_filter_missing (casereader_clone (input),
                                              &var, 1, exclude,
                                              NULL, NULL);
          struct hmap freq_hash = HMAP_INITIALIZER (freq_hash);
          if (!create_freq_hash_with_range (dict, reader, var,
                                            cst->lo, cst->hi, &freq_hash))
            {
              freq_hmap_destroy (&freq_hash, var_get_width (var));
              continue;
            }

          struct freq **ff = freq_hmap_sort (&freq_hash, var_get_width (var));

          double total_obs = 0.0;
          for (size_t i = 0 ; i < hmap_count (&freq_hash) ; ++i)
            total_obs += ff[i]->count;

          xsq[v] = 0.0;
          for (size_t i = 0 ; i < hmap_count (&freq_hash) ; ++i)
            {
              /* Category. */
              pivot_table_put3 (table, 0, v, i,
                                pivot_value_new_var_value (
                                  var, &ff[i]->values[0]));

              double exp = (cst->n_expected > 0
                            ? cst->expected[i] * total_obs / total_expected
                            : total_obs / (double) hmap_count (&freq_hash));
              double entries[] = {
                ff[i]->count,
                exp,
                ff[i]->count - exp,
              };
              for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
                pivot_table_put3 (table, j + 1, v, i,
                                  pivot_value_new_number (entries[j]));


              xsq[v] += (ff[i]->count - exp) * (ff[i]->count - exp) / exp;
            }

          df[v] = n_cells - 1.0;

          freq_hmap_destroy (&freq_hash, var_get_width (var));
          free (ff);

          pivot_table_put3 (table, 1, v, n_cells,
                            pivot_value_new_number (total_obs));
        }

      pivot_table_submit (table);
    }
  ok = !taint_has_tainted_successor (casereader_get_taint (input));
  casereader_destroy (input);

  if (ok)
    {
      struct pivot_table *table = pivot_table_create (N_("Test Statistics"));

      pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                              N_("Chi-square"), PIVOT_RC_OTHER,
                              N_("df"), PIVOT_RC_INTEGER,
                              N_("Asymp. Sig."), PIVOT_RC_SIGNIFICANCE);

      struct pivot_dimension *variables = pivot_dimension_create (
        table, PIVOT_AXIS_ROW, N_("Variable"));

      for (size_t v = 0 ; v < ost->n_vars ; ++v)
        {
          const struct variable *var = ost->vars[v];

          int row = pivot_category_create_leaf (
            variables->root, pivot_value_new_variable (var));

          double sig = gsl_cdf_chisq_Q (xsq[v], df[v]);
          double entries[] = { xsq[v], df[v], sig };
          for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
            pivot_table_put2 (table, i, row,
                              pivot_value_new_number (entries[i]));
        }
      pivot_table_submit (table);
    }

  free (xsq);
  free (df);
}

