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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include "median.h"

#include <gsl/gsl_cdf.h>

#include "data/format.h"


#include "data/variable.h"
#include "data/case.h"
#include "data/dictionary.h"
#include "data/dataset.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/subcase.h"
#include "data/value.h"

#include "math/percentiles.h"
#include "math/sort.h"

#include "libpspp/cast.h"
#include "libpspp/hmap.h"
#include "libpspp/array.h"
#include "libpspp/str.h"
#include "libpspp/misc.h"

#include "output/pivot-table.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)


struct val_node
{
  struct hmap_node node;
  union value val;
  casenumber le;
  casenumber gt;
};

struct results
{
  const struct variable *var;
  struct val_node **sorted_array;
  double n;
  double median;
  double chisq;
};



static int
val_node_cmp_3way (const void *a_, const void *b_, const void *aux)
{
  const struct variable *indep_var = aux;
  const struct val_node *const *a = a_;
  const struct val_node *const *b = b_;

  return value_compare_3way (&(*a)->val, &(*b)->val, var_get_width (indep_var));
}

static void
show_frequencies (const struct n_sample_test *nst, const struct results *results,  int n_vals, const struct dictionary *);

static void
show_test_statistics (const struct n_sample_test *nst, const struct results *results, int, const struct dictionary *);


static struct val_node *
find_value (const struct hmap *map, const union value *val,
            const struct variable *var)
{
  struct val_node *foo = NULL;
  size_t hash = value_hash (val, var_get_width (var), 0);
  HMAP_FOR_EACH_WITH_HASH (foo, struct val_node, node, hash, map)
    if (value_equal (val, &foo->val, var_get_width (var)))
      break;

  return foo;
}

void
median_execute (const struct dataset *ds,
                struct casereader *input,
                enum mv_class exclude,
                const struct npar_test *test,
                bool exact UNUSED,
                double timer UNUSED)
{
  const struct dictionary *dict = dataset_dict (ds);
  const struct variable *wvar = dict_get_weight (dict);
  bool warn = true;
  int v;
  const struct median_test *mt = UP_CAST (test, const struct median_test,
                                          parent.parent);

  const struct n_sample_test *nst = UP_CAST (test, const struct n_sample_test,
                                          parent);

  const bool n_sample_test = (value_compare_3way (&nst->val2, &nst->val1,
                                       var_get_width (nst->indep_var)) > 0);

  struct results *results = XCALLOC (nst->n_vars,  struct results);
  int n_vals = 0;
  for (v = 0; v < nst->n_vars; ++v)
    {
      double count = 0;
      double cc = 0;
      double median = mt->median;
      const struct variable *var = nst->vars[v];
      struct ccase *c;
      struct hmap map = HMAP_INITIALIZER (map);
      struct casereader *r = casereader_clone (input);



      if (n_sample_test == false)
        {
          struct val_node *vn = XZALLOC (struct val_node);
          value_clone (&vn->val,  &nst->val1, var_get_width (nst->indep_var));
          hmap_insert (&map, &vn->node, value_hash (&nst->val1,
                                            var_get_width (nst->indep_var), 0));

          vn = xzalloc (sizeof *vn);
          value_clone (&vn->val,  &nst->val2, var_get_width (nst->indep_var));
          hmap_insert (&map, &vn->node, value_hash (&nst->val2,
                                            var_get_width (nst->indep_var), 0));
        }

      if (median == SYSMIS)
        {
          struct percentile *ptl;
          struct order_stats *os;

          struct casereader *rr;
          struct subcase sc;
          struct casewriter *writer;
          subcase_init_var (&sc, var, SC_ASCEND);
          rr = casereader_clone (r);
          writer = sort_create_writer (&sc, casereader_get_proto (rr));

          for (; (c = casereader_read (rr)) != NULL;)
            {
              if (var_is_value_missing (var, case_data (c, var)) & exclude)
                {
                  case_unref (c);
                  continue;
                }

              cc += dict_get_case_weight (dict, c, &warn);
              casewriter_write (writer, c);
            }
          subcase_uninit (&sc);
          casereader_destroy (rr);

          rr = casewriter_make_reader (writer);

          ptl = percentile_create (0.5, cc);
          os = &ptl->parent;

          order_stats_accumulate (&os, 1,
                                  rr,
                                  wvar,
                                  var,
                                  exclude);

          median = percentile_calculate (ptl, PC_HAVERAGE);
          statistic_destroy (&ptl->parent.parent);
        }

      results[v].median = median;


      for (; (c = casereader_read (r)) != NULL; case_unref (c))
        {
          struct val_node *vn ;
          const double weight = dict_get_case_weight (dict, c, &warn);
          const union value *val = case_data (c, var);
          const union value *indep_val = case_data (c, nst->indep_var);

          if (var_is_value_missing (var, case_data (c, var)) & exclude)
            {
              continue;
            }

          if (n_sample_test)
            {
              int width = var_get_width (nst->indep_var);
              /* Ignore out of range values */
              if (
                  value_compare_3way (indep_val, &nst->val1, width) < 0
                ||
                  value_compare_3way (indep_val, &nst->val2, width) > 0
                )
                {
                  continue;
                }
            }

          vn = find_value (&map, indep_val, nst->indep_var);
          if (vn == NULL)
            {
              if (n_sample_test == true)
                {
                  int width = var_get_width (nst->indep_var);
                  vn = xzalloc (sizeof *vn);
                  value_clone (&vn->val,  indep_val, width);

                  hmap_insert (&map, &vn->node, value_hash (indep_val, width, 0));
                }
              else
                {
                  continue;
                }
            }

          if (val->f <= median)
            vn->le += weight;
          else
            vn->gt += weight;

          count += weight;
        }
      casereader_destroy (r);

      {
        int x = 0;
        struct val_node *vn = NULL;
        double r_0 = 0;
        double r_1 = 0;
        HMAP_FOR_EACH (vn, struct val_node, node, &map)
          {
            r_0 += vn->le;
            r_1 += vn->gt;
          }

        results[v].n = count;
        results[v].sorted_array = XCALLOC (hmap_count (&map), struct val_node *);
        results[v].var = var;

        HMAP_FOR_EACH (vn, struct val_node, node, &map)
          {
            double e_0j = r_0 * (vn->le + vn->gt) / count;
            double e_1j = r_1 * (vn->le + vn->gt) / count;

            results[v].chisq += pow2 (vn->le - e_0j) / e_0j;
            results[v].chisq += pow2 (vn->gt - e_1j) / e_1j;

            results[v].sorted_array[x++] = vn;
          }

        n_vals = x;
        hmap_destroy (&map);

        sort (results[v].sorted_array, x, sizeof (*results[v].sorted_array),
              val_node_cmp_3way, nst->indep_var);

      }
    }

  casereader_destroy (input);

  show_frequencies (nst, results,  n_vals, dict);
  show_test_statistics (nst, results, n_vals, dict);

  for (v = 0; v < nst->n_vars; ++v)
    {
      int i;
      const struct results *rs = results + v;

      for (i = 0; i < n_vals; ++i)
        {
          struct val_node *vn = rs->sorted_array[i];
          value_destroy (&vn->val, var_get_width (nst->indep_var));
          free (vn);
        }
      free (rs->sorted_array);
    }
  free (results);
}



static void
show_frequencies (const struct n_sample_test *nst, const struct results *results,  int n_vals, const struct dictionary *dict)
{
  struct pivot_table *table = pivot_table_create (N_("Frequencies"));
  pivot_table_set_weight_var (table, dict_get_weight (dict));

  struct pivot_dimension *indep = pivot_dimension_create__ (
    table, PIVOT_AXIS_COLUMN, pivot_value_new_variable (nst->indep_var));
  indep->root->show_label = true;
  for (int i = 0; i < n_vals; ++i)
    pivot_category_create_leaf_rc (
      indep->root, pivot_value_new_var_value (
        nst->indep_var, &results->sorted_array[i]->val), PIVOT_RC_COUNT);
  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Statistics"),
                          N_("> Median"), N_("≤ Median"));

  struct pivot_dimension *dep = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Dependent Variables"));

  for (int v = 0; v < nst->n_vars; ++v)
    {
      const struct results *rs = &results[v];

      int dep_idx = pivot_category_create_leaf (
        dep->root, pivot_value_new_variable (rs->var));

      for (int indep_idx = 0; indep_idx < n_vals; indep_idx++)
        {
          const struct val_node *vn = rs->sorted_array[indep_idx];
          pivot_table_put3 (table, indep_idx, 0, dep_idx,
                            pivot_value_new_number (vn->gt));
          pivot_table_put3 (table, indep_idx, 1, dep_idx,
                            pivot_value_new_number (vn->le));
        }
    }

  pivot_table_submit (table);
}

static void
show_test_statistics (const struct n_sample_test *nst,
                      const struct results *results,
                      int n_vals,
                      const struct dictionary *dict)
{
  struct pivot_table *table = pivot_table_create (N_("Test Statistics"));
  pivot_table_set_weight_var (table, dict_get_weight (dict));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_COUNT,
                          N_("Median"),
                          N_("Chi-Square"), PIVOT_RC_OTHER,
                          N_("df"), PIVOT_RC_COUNT,
                          N_("Asymp. Sig."), PIVOT_RC_SIGNIFICANCE);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));

  for (int v = 0; v < nst->n_vars; ++v)
    {
      double df = n_vals - 1;
      const struct results *rs = &results[v];

      int var_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (rs->var));

      double entries[] = {
        rs->n,
        rs->median,
        rs->chisq,
        df,
        gsl_cdf_chisq_Q (rs->chisq, df),
      };
      for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
        {
          struct pivot_value *value
            = pivot_value_new_number (entries[i]);
          if (i == 1)
            value->numeric.format = var_get_print_format (rs->var);
          pivot_table_put2 (table, i, var_idx, value);
        }
    }

  pivot_table_submit (table);
}
