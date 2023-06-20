/* Pspp - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2022 Free Software Foundation, Inc.

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

#include "kruskal-wallis.h"

#include <gsl/gsl_cdf.h>
#include <math.h>

#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/subcase.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/hmap.h"
#include "libpspp/bt.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "math/sort.h"
#include "output/pivot-table.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

/* Returns true iff the independent variable lies between nst->val1 and  nst->val2 */
static bool
include_func (const struct ccase *c, void *aux)
{
  const struct n_sample_test *nst = aux;

  const union value *smaller = 0;
  const union value *larger = 0;
  int x = value_compare_3way (&nst->val1, &nst->val2, var_get_width (nst->indep_var));
   if (x < 0)
    {
      smaller = &nst->val1;
      larger = &nst->val2;
    }
  else
    {
      smaller = &nst->val2;
      larger = &nst->val1;
    }

  if (0 < value_compare_3way (smaller, case_data (c, nst->indep_var),
                              var_get_width (nst->indep_var)))
    return false;

  if (0 > value_compare_3way (larger, case_data (c, nst->indep_var),
                              var_get_width (nst->indep_var)))
    return false;

  return true;
}


struct rank_entry
{
  struct hmap_node node;
  struct bt_node btn;
  union value group;

  double sum_of_ranks;
  double n;
};


static int
compare_rank_entries_3way (const struct bt_node *a,
                           const struct bt_node *b,
                           const void *aux)
{
  const struct variable *var = aux;
  const struct rank_entry *rea = BT_DATA (a, struct rank_entry, btn);
  const struct rank_entry *reb = BT_DATA (b, struct rank_entry, btn);

  return value_compare_3way (&rea->group, &reb->group, var_get_width (var));
}


/* Return the entry with the key GROUP or null if there is no such entry */
static struct rank_entry *
find_rank_entry (const struct hmap *map, const union value *group, size_t width)
{
  struct rank_entry *re = NULL;
  size_t hash  = value_hash (group, width, 0);

  HMAP_FOR_EACH_WITH_HASH (re, struct rank_entry, node, hash, map)
    {
      if (0 == value_compare_3way (group, &re->group, width))
        return re;
    }

  return re;
}

/* Calculates the adjustment necessary for tie compensation */
static void
distinct_callback (double v UNUSED, casenumber t, double w UNUSED, void *aux)
{
  double *tiebreaker = aux;

  *tiebreaker += pow3 (t) - t;
}


struct kw
{
  struct hmap map;
  double h;
};

static void show_ranks_box (const struct n_sample_test *, const struct kw *);
static void show_sig_box (const struct n_sample_test *, const struct kw *);

void
kruskal_wallis_execute (const struct dataset *ds,
                        struct casereader *input,
                        enum mv_class exclude,
                        const struct npar_test *test,
                        bool exact UNUSED,
                        double timer UNUSED)
{
  int i;
  struct ccase *c;
  bool warn = true;
  const struct dictionary *dict = dataset_dict (ds);
  const struct n_sample_test *nst = UP_CAST (test, const struct n_sample_test, parent);
  const struct caseproto *proto ;
  size_t rank_idx ;

  int total_n_groups = 0.0;

  struct kw *kw = XCALLOC (nst->n_vars,  struct kw);

  /* If the independent variable is missing, then we ignore the case */
  input = casereader_create_filter_missing (input,
                                            &nst->indep_var, 1,
                                            exclude,
                                            NULL, NULL);

  input = casereader_create_filter_weight (input, dict, &warn, NULL);

  /* Remove all those cases which are outside the range (val1, val2) */
  input = casereader_create_filter_func (input, include_func, NULL,
        CONST_CAST (struct n_sample_test *, nst), NULL);

  proto = casereader_get_proto (input);
  rank_idx = caseproto_get_n_widths (proto);

  /* Rank cases by the v value */
  for (i = 0; i < nst->n_vars; ++i)
    {
      double tiebreaker = 0.0;
      bool warn = true;
      enum rank_error rerr = 0;
      struct casereader *rr;
      struct casereader *r = casereader_clone (input);

      r = sort_execute_1var (r, nst->vars[i]);

      /* Ignore missings in the test variable */
      r = casereader_create_filter_missing (r, &nst->vars[i], 1,
                                            exclude,
                                            NULL, NULL);

      rr = casereader_create_append_rank (r,
                                          nst->vars[i],
                                          dict_get_weight (dict),
                                          &rerr,
                                          distinct_callback, &tiebreaker);

      hmap_init (&kw[i].map);
      for (; (c = casereader_read (rr)); case_unref (c))
        {
          const union value *group = case_data (c, nst->indep_var);
          const size_t group_var_width = var_get_width (nst->indep_var);
          struct rank_entry *rank = find_rank_entry (&kw[i].map, group, group_var_width);

          if (NULL == rank)
            {
              rank = xzalloc (sizeof *rank);
              value_clone (&rank->group, group, group_var_width);

              hmap_insert (&kw[i].map, &rank->node,
                           value_hash (&rank->group, group_var_width, 0));
            }

          rank->sum_of_ranks += case_num_idx (c, rank_idx);
          rank->n += dict_get_case_weight (dict, c, &warn);

          /* If this assertion fires, then either the data wasn't sorted or some other
             problem occurred */
          assert (rerr == 0);
        }

      casereader_destroy (rr);

      /* Calculate the value of h */
      {
        struct rank_entry *mre;
        double n = 0.0;

        HMAP_FOR_EACH (mre, struct rank_entry, node, &kw[i].map)
          {
            kw[i].h += pow2 (mre->sum_of_ranks) / mre->n;
            n += mre->n;

            total_n_groups ++;
          }
        kw[i].h *= 12 / (n * (n + 1));
        kw[i].h -= 3 * (n + 1) ;

        kw[i].h /= 1 - tiebreaker/ (pow3 (n) - n);
      }
    }

  casereader_destroy (input);

  show_ranks_box (nst, kw);
  show_sig_box (nst, kw);

  /* Cleanup allocated memory */
  for (i = 0 ; i < nst->n_vars; ++i)
    {
      struct rank_entry *mre, *next;
      HMAP_FOR_EACH_SAFE (mre, next, struct rank_entry, node, &kw[i].map)
        {
          hmap_delete (&kw[i].map, &mre->node);
          free (mre);
        }
      hmap_destroy (&kw[i].map);
    }

  free (kw);
}

static void
show_ranks_box (const struct n_sample_test *nst, const struct kw *kw)
{
  struct pivot_table *table = pivot_table_create (N_("Ranks"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_INTEGER,
                          N_("Mean Rank"), PIVOT_RC_OTHER);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));

  for (size_t i = 0 ; i < nst->n_vars ; ++i)
    {
      /* Sort the rank entries, by iteratin the hash and putting the entries
         into a binary tree. */
      struct bt bt = BT_INITIALIZER(compare_rank_entries_3way, nst->vars[i]);
      struct rank_entry *re_x;
      HMAP_FOR_EACH (re_x, struct rank_entry, node, &kw[i].map)
        bt_insert (&bt, &re_x->btn);

      /* Report the rank entries in sorted order. */
      struct pivot_category *group = pivot_category_create_group__ (
        variables->root, pivot_value_new_variable (nst->vars[i]));
      int tot = 0;
      const struct rank_entry *re;
      BT_FOR_EACH (re, struct rank_entry, btn, &bt)
        {
          struct string str = DS_EMPTY_INITIALIZER;
          var_append_value_name (nst->indep_var, &re->group, &str);
          int row = pivot_category_create_leaf (
            group, pivot_value_new_user_text_nocopy (ds_steal_cstr (&str)));

          double entries[] = { re->n, re->sum_of_ranks / re->n };
          for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
            pivot_table_put2 (table, j, row,
                              pivot_value_new_number (entries[j]));

          tot += re->n;
        }

      int row = pivot_category_create_leaves (group, N_("Total"));
      pivot_table_put2 (table, 0, row, pivot_value_new_number (tot));
    }

  pivot_table_submit (table);
}

static void
show_sig_box (const struct n_sample_test *nst, const struct kw *kw)
{
  struct pivot_table *table = pivot_table_create (N_("Test Statistics"));

  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Statistics"),
                          N_("Chi-Square"), PIVOT_RC_OTHER,
                          N_("df"), PIVOT_RC_INTEGER,
                          N_("Asymp. Sig."), PIVOT_RC_SIGNIFICANCE);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Variables"));

  for (size_t i = 0 ; i < nst->n_vars; ++i)
    {
      int col = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (nst->vars[i]));

      double df = hmap_count (&kw[i].map) - 1;
      double sig = gsl_cdf_chisq_Q (kw[i].h, df);
      double entries[] = { kw[i].h, df, sig };
      for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
        pivot_table_put2 (table, j, col, pivot_value_new_number (entries[j]));
    }

  pivot_table_submit (table);
}
