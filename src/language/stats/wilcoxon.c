/* Pspp - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/stats/wilcoxon.h"

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
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "math/sort.h"
#include "math/wilcoxon-sig.h"
#include "output/pivot-table.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

static double
append_difference (const struct ccase *c, casenumber n UNUSED, void *aux)
{
  const variable_pair *vp = aux;

  return case_data (c, (*vp)[0])->f - case_data (c, (*vp)[1])->f;
}

static void show_ranks_box (const struct wilcoxon_state *,
			    const struct two_sample_test *,
			    const struct dictionary *);

static void show_tests_box (const struct wilcoxon_state *,
			    const struct two_sample_test *,
			    bool exact, double timer);



static void
distinct_callback (double v UNUSED, casenumber n, double w UNUSED, void *aux)
{
  struct wilcoxon_state *ws = aux;

  ws->tiebreaker += pow3 (n) - n;
}

#define WEIGHT_IDX 2

void
wilcoxon_execute (const struct dataset *ds,
		  struct casereader *input,
		  enum mv_class exclude,
		  const struct npar_test *test,
		  bool exact,
		  double timer)
{
  int i;
  bool warn = true;
  const struct dictionary *dict = dataset_dict (ds);
  const struct two_sample_test *t2s = UP_CAST (test, const struct two_sample_test, parent);

  struct wilcoxon_state *ws = XCALLOC (t2s->n_pairs,  struct wilcoxon_state);
  const struct variable *weight = dict_get_weight (dict);
  struct variable *weightx = dict_create_internal_var (WEIGHT_IDX, 0);
  struct caseproto *proto;

  input =
    casereader_create_filter_weight (input, dict, &warn, NULL);

  proto = caseproto_create ();
  proto = caseproto_add_width (proto, 0);
  proto = caseproto_add_width (proto, 0);
  if (weight != NULL)
    proto = caseproto_add_width (proto, 0);

  for (i = 0 ; i < t2s->n_pairs; ++i)
    {
      struct casereader *r = casereader_clone (input);
      struct casewriter *writer;
      struct ccase *c;
      struct subcase ordering;
      variable_pair *vp = &t2s->pairs[i];

      ws[i].sign = dict_create_internal_var (0, 0);
      ws[i].absdiff = dict_create_internal_var (1, 0);

      r = casereader_create_filter_missing (r, *vp, 2,
					    exclude,
					    NULL, NULL);

      subcase_init_var (&ordering, ws[i].absdiff, SC_ASCEND);
      writer = sort_create_writer (&ordering, proto);
      subcase_destroy (&ordering);

      for (; (c = casereader_read (r)) != NULL; case_unref (c))
	{
	  struct ccase *output = case_create (proto);
	  double d = append_difference (c, 0, vp);

	  if (d > 0)
	    {
	      case_data_rw (output, ws[i].sign)->f = 1.0;

	    }
	  else if (d < 0)
	    {
	      case_data_rw (output, ws[i].sign)->f = -1.0;
	    }
	  else
	    {
	      double w = 1.0;
	      if (weight)
		w = case_data (c, weight)->f;

	      /* Central point values should be dropped */
	      ws[i].n_zeros += w;
              case_unref (output);
              continue;
	    }

	  case_data_rw (output, ws[i].absdiff)->f = fabs (d);

	  if (weight)
	   case_data_rw (output, weightx)->f = case_data (c, weight)->f;

	  casewriter_write (writer, output);
	}
      casereader_destroy (r);
      ws[i].reader = casewriter_make_reader (writer);
    }
  caseproto_unref (proto);

  for (i = 0 ; i < t2s->n_pairs; ++i)
    {
      struct casereader *rr ;
      struct ccase *c;
      enum rank_error err = 0;

      rr = casereader_create_append_rank (ws[i].reader, ws[i].absdiff,
					  weight ? weightx : NULL, &err,
					  distinct_callback, &ws[i]
					);

      for (; (c = casereader_read (rr)) != NULL; case_unref (c))
	{
	  double sign = case_data (c, ws[i].sign)->f;
	  double rank = case_data_idx (c, weight ? 3 : 2)->f;
	  double w = 1.0;
	  if (weight)
	    w = case_data (c, weightx)->f;

	  if (sign > 0)
	    {
	      ws[i].positives.sum += rank * w;
	      ws[i].positives.n += w;
	    }
	  else if (sign < 0)
	    {
	      ws[i].negatives.sum += rank * w;
	      ws[i].negatives.n += w;
	    }
	  else
	    NOT_REACHED ();
	}

      casereader_destroy (rr);
    }

  casereader_destroy (input);

  dict_destroy_internal_var (weightx);

  show_ranks_box (ws, t2s, dict);
  show_tests_box (ws, t2s, exact, timer);

  for (i = 0 ; i < t2s->n_pairs; ++i)
    {
      dict_destroy_internal_var (ws[i].sign);
      dict_destroy_internal_var (ws[i].absdiff);
    }

  free (ws);
}

static void
put_row (struct pivot_table *table, int var_idx, int sign_idx,
         double n, double sum)
{
  pivot_table_put3 (table, 0, sign_idx, var_idx, pivot_value_new_number (n));
  if (sum != SYSMIS)
    {
      pivot_table_put3 (table, 1, sign_idx, var_idx,
                        pivot_value_new_number (sum / n));
      pivot_table_put3 (table, 2, sign_idx, var_idx,
                        pivot_value_new_number (sum));
    }
}

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
show_ranks_box (const struct wilcoxon_state *ws,
		const struct two_sample_test *t2s,
		const struct dictionary *dict)
{
  struct pivot_table *table = pivot_table_create (N_("Ranks"));
  pivot_table_set_weight_var (table, dict_get_weight (dict));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_COUNT,
                          N_("Mean Rank"), PIVOT_RC_OTHER,
                          N_("Sum of Ranks"), PIVOT_RC_OTHER);

  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Sign"),
                          N_("Negative Ranks"), N_("Positive Ranks"),
                          N_("Ties"), N_("Total"));

  struct pivot_dimension *pairs = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Pairs"));

  for (size_t i = 0 ; i < t2s->n_pairs; ++i)
    {
      variable_pair *vp = &t2s->pairs[i];
      int pair_idx = add_pair_leaf (pairs, vp);

      const struct wilcoxon_state *w = &ws[i];
      put_row (table, pair_idx, 0, w->negatives.n, w->negatives.sum);
      put_row (table, pair_idx, 1, w->positives.n, w->positives.sum);
      put_row (table, pair_idx, 2, w->n_zeros, SYSMIS);
      put_row (table, pair_idx, 3,
               w->n_zeros + w->positives.n + w->negatives.n, SYSMIS);
    }

  pivot_table_submit (table);
}


static void
show_tests_box (const struct wilcoxon_state *ws,
		const struct two_sample_test *t2s,
		bool exact,
		double timer UNUSED
		)
{
  struct pivot_table *table = pivot_table_create (N_("Test Statistics"));

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Statistics"),
    N_("Z"), PIVOT_RC_OTHER,
    N_("Asymp. Sig. (2-tailed)"), PIVOT_RC_SIGNIFICANCE);
  if (exact)
    pivot_category_create_leaves (
      statistics->root,
      N_("Exact Sig. (2-tailed)"), PIVOT_RC_SIGNIFICANCE,
      N_("Exact Sig. (1-tailed)"), PIVOT_RC_SIGNIFICANCE);

  struct pivot_dimension *pairs = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Pairs"));

  struct pivot_footnote *too_many_pairs = pivot_table_create_footnote (
    table, pivot_value_new_text (
      N_("Too many pairs to calculate exact significance")));

  for (size_t i = 0 ; i < t2s->n_pairs; ++i)
    {
      variable_pair *vp = &t2s->pairs[i];
      int pair_idx = add_pair_leaf (pairs, vp);

      double n = ws[i].positives.n + ws[i].negatives.n;
      double z = MIN (ws[i].positives.sum, ws[i].negatives.sum);
      z -= n * (n + 1)/ 4.0;
      z /= sqrt (n * (n + 1) * (2*n + 1)/24.0 - ws[i].tiebreaker / 48.0);

      double entries[4];
      int n_entries = 0;
      entries[n_entries++] = z;
      entries[n_entries++] = 2.0 * gsl_cdf_ugaussian_P (z);

      int footnote_idx = -1;
      if (exact)
	{
	  double p = LevelOfSignificanceWXMPSR (ws[i].positives.sum, n);
	  if (p < 0)
	    {
              footnote_idx = n_entries;
              entries[n_entries++] = SYSMIS;
	    }
	  else
            {
              entries[n_entries++] = p;
              entries[n_entries++] = p / 2.0;
            }
        }

      for (int j = 0; j < n_entries; j++)
        {
          struct pivot_value *value = pivot_value_new_number (entries[j]);
          if (j == footnote_idx)
            pivot_value_add_footnote (value, too_many_pairs);
          pivot_table_put2 (table, j, pair_idx, value);
        }
    }

  pivot_table_submit (table);
}
