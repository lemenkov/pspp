/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2013, 2015, 2016 Free Software Foundation, Inc.

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

#include <math.h>

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"
#include "math/moments.h"
#include "output/pivot-table.h"
#include "output/text-item.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

struct cronbach
{
  const struct variable **items;
  size_t n_items;
  double alpha;
  double sum_of_variances;
  double variance_of_sums;
  int totals_idx;          /* Casereader index into the totals */

  struct moments1 **m ;    /* Moments of the items */
  struct moments1 *total ; /* Moments of the totals */
};

#if 0
static void
dump_cronbach (const struct cronbach *s)
{
  int i;
  printf ("N items %d\n", s->n_items);
  for (i = 0 ; i < s->n_items; ++i)
    {
      printf ("%s\n", var_get_name (s->items[i]));
    }

  printf ("Totals idx %d\n", s->totals_idx);

  printf ("scale variance %g\n", s->variance_of_sums);
  printf ("alpha %g\n", s->alpha);
  putchar ('\n');
}
#endif

enum model
  {
    MODEL_ALPHA,
    MODEL_SPLIT
  };


enum summary_opts
  {
    SUMMARY_TOTAL = 0x0001,
  };


struct reliability
{
  const struct variable **variables;
  size_t n_variables;
  enum mv_class exclude;

  struct cronbach *sc;
  int n_sc;

  int total_start;

  struct string scale_name;

  enum model model;
  int split_point;


  enum summary_opts summary;

  const struct variable *wv;
};


static bool run_reliability (struct dataset *ds, const struct reliability *reliability);

static void
reliability_destroy (struct reliability *rel)
{
  int j;
  ds_destroy (&rel->scale_name);
  if (rel->sc)
    for (j = 0; j < rel->n_sc ; ++j)
      {
	int x;
	free (rel->sc[j].items);
        moments1_destroy (rel->sc[j].total);
        if (rel->sc[j].m)
          for (x = 0; x < rel->sc[j].n_items; ++x)
            free (rel->sc[j].m[x]);
	free (rel->sc[j].m);
      }

  free (rel->sc);
  free (rel->variables);
}

int
cmd_reliability (struct lexer *lexer, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);

  struct reliability reliability;
  reliability.n_variables = 0;
  reliability.variables = NULL;
  reliability.model = MODEL_ALPHA;
  reliability.exclude = MV_ANY;
  reliability.summary = 0;
  reliability.n_sc = 0;
  reliability.sc = NULL;
  reliability.wv = dict_get_weight (dict);
  reliability.total_start = 0;
  ds_init_empty (&reliability.scale_name);


  lex_match (lexer, T_SLASH);

  if (!lex_force_match_id (lexer, "VARIABLES"))
    {
      goto error;
    }

  lex_match (lexer, T_EQUALS);

  if (!parse_variables_const (lexer, dict, &reliability.variables, &reliability.n_variables,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  if (reliability.n_variables < 2)
    msg (MW, _("Reliability on a single variable is not useful."));


  {
    int i;
    struct cronbach *c;
    /* Create a default Scale */

    reliability.n_sc = 1;
    reliability.sc = xzalloc (sizeof (struct cronbach) * reliability.n_sc);

    ds_assign_cstr (&reliability.scale_name, "ANY");

    c = &reliability.sc[0];
    c->n_items = reliability.n_variables;
    c->items = xzalloc (sizeof (struct variable*) * c->n_items);

    for (i = 0 ; i < c->n_items ; ++i)
      c->items[i] = reliability.variables[i];
  }



  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "SCALE"))
	{
	  struct const_var_set *vs;
	  if (! lex_force_match (lexer, T_LPAREN))
	    goto error;

	  if (! lex_force_string (lexer))
	    goto error;

	  ds_assign_substring (&reliability.scale_name, lex_tokss (lexer));

	  lex_get (lexer);

	  if (! lex_force_match (lexer, T_RPAREN))
	    goto error;

          lex_match (lexer, T_EQUALS);

	  vs = const_var_set_create_from_array (reliability.variables, reliability.n_variables);

	  free (reliability.sc->items);
	  if (!parse_const_var_set_vars (lexer, vs, &reliability.sc->items, &reliability.sc->n_items, 0))
	    {
	      const_var_set_destroy (vs);
	      goto error;
	    }

	  const_var_set_destroy (vs);
	}
      else if (lex_match_id (lexer, "MODEL"))
	{
          lex_match (lexer, T_EQUALS);
	  if (lex_match_id (lexer, "ALPHA"))
	    {
	      reliability.model = MODEL_ALPHA;
	    }
	  else if (lex_match_id (lexer, "SPLIT"))
	    {
	      reliability.model = MODEL_SPLIT;
	      reliability.split_point = -1;

	      if (lex_match (lexer, T_LPAREN)
		   && lex_force_num (lexer))
		{
		  reliability.split_point = lex_number (lexer);
		  lex_get (lexer);
		  if (! lex_force_match (lexer, T_RPAREN))
		    goto error;
		}
	    }
	  else
	    goto error;
	}
      else if (lex_match_id (lexer, "SUMMARY"))
        {
          lex_match (lexer, T_EQUALS);
	  if (lex_match_id (lexer, "TOTAL"))
	    {
	      reliability.summary |= SUMMARY_TOTAL;
	    }
	  else if (lex_match (lexer, T_ALL))
	    {
	      reliability.summary = 0xFFFF;
	    }
	  else
	    goto error;
	}
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
	      if (lex_match_id (lexer, "INCLUDE"))
		{
		  reliability.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  reliability.exclude = MV_ANY;
		}
	      else
		{
                  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "STATISTICS"))
        {
          lex_match (lexer, T_EQUALS);
          msg (SW, _("The STATISTICS subcommand is not yet implemented.  "
                     "No statistics will be produced."));
          while (lex_match (lexer, T_ID))
            continue;
        }
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }

  if (reliability.model == MODEL_SPLIT)
    {
      int i;
      const struct cronbach *s;

      if (reliability.split_point >= reliability.n_variables)
        {
          msg (ME, _("The split point must be less than the number of variables"));
          goto error;
        }

      reliability.n_sc += 2 ;
      reliability.sc = xrealloc (reliability.sc, sizeof (struct cronbach) * reliability.n_sc);

      s = &reliability.sc[0];

      reliability.sc[1].n_items =
	(reliability.split_point == -1) ? s->n_items / 2 : reliability.split_point;

      reliability.sc[2].n_items = s->n_items - reliability.sc[1].n_items;
      reliability.sc[1].items = xzalloc (sizeof (struct variable *)
				 * reliability.sc[1].n_items);

      reliability.sc[2].items = xzalloc (sizeof (struct variable *) *
				 reliability.sc[2].n_items);

      for  (i = 0; i < reliability.sc[1].n_items ; ++i)
	reliability.sc[1].items[i] = s->items[i];

      while (i < s->n_items)
	{
	  reliability.sc[2].items[i - reliability.sc[1].n_items] = s->items[i];
	  i++;
	}
    }

  if (reliability.summary & SUMMARY_TOTAL)
    {
      int i;
      const int base_sc = reliability.n_sc;

      reliability.total_start = base_sc;

      reliability.n_sc +=  reliability.sc[0].n_items ;
      reliability.sc = xrealloc (reliability.sc, sizeof (struct cronbach) * reliability.n_sc);


      for (i = 0 ; i < reliability.sc[0].n_items; ++i)
	{
	  int v_src;
	  int v_dest = 0;
	  struct cronbach *s = &reliability.sc[i + base_sc];

	  s->n_items = reliability.sc[0].n_items - 1;
	  s->items = xzalloc (sizeof (struct variable *) * s->n_items);
	  for (v_src = 0 ; v_src < reliability.sc[0].n_items ; ++v_src)
	    {
	      if (v_src != i)
		s->items[v_dest++] = reliability.sc[0].items[v_src];
	    }
	}
    }


  if (! run_reliability (ds, &reliability))
    goto error;

  reliability_destroy (&reliability);
  return CMD_SUCCESS;

 error:
  reliability_destroy (&reliability);
  return CMD_FAILURE;
}


static void
do_reliability (struct casereader *group, struct dataset *ds,
		const struct reliability *rel);


static void reliability_summary_total (const struct reliability *rel);

static void reliability_statistics (const struct reliability *rel);


static bool
run_reliability (struct dataset *ds, const struct reliability *reliability)
{
  struct dictionary *dict = dataset_dict (ds);
  bool ok;
  struct casereader *group;

  struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds), dict);
  int si;

  for (si = 0 ; si < reliability->n_sc; ++si)
    {
      struct cronbach *s = &reliability->sc[si];
      int i;

      s->m = xzalloc (sizeof *s->m * s->n_items);
      s->total = moments1_create (MOMENT_VARIANCE);

      for (i = 0 ; i < s->n_items ; ++i)
	s->m[i] = moments1_create (MOMENT_VARIANCE);
    }


  while (casegrouper_get_next_group (grouper, &group))
    {
      do_reliability (group, ds, reliability);

      reliability_statistics (reliability);

      if (reliability->summary & SUMMARY_TOTAL)
	reliability_summary_total (reliability);
    }

  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  return ok;
}





/* Return the sum of all the item variables in S */
static  double
append_sum (const struct ccase *c, casenumber n UNUSED, void *aux)
{
  double sum = 0;
  const struct cronbach *s = aux;

  int v;
  for (v = 0 ; v < s->n_items; ++v)
    {
      sum += case_data (c, s->items[v])->f;
    }

  return sum;
};

static void
case_processing_summary (casenumber n_valid, casenumber n_missing,
			 const struct dictionary *dict);


static double
alpha (int k, double sum_of_variances, double variance_of_sums)
{
  return k / (k - 1.0) * (1 - sum_of_variances / variance_of_sums);
}

static void
do_reliability (struct casereader *input, struct dataset *ds,
		const struct reliability *rel)
{
  int i;
  int si;
  struct ccase *c;
  casenumber n_missing ;
  casenumber n_valid = 0;


  for (si = 0 ; si < rel->n_sc; ++si)
    {
      struct cronbach *s = &rel->sc[si];

      moments1_clear (s->total);

      for (i = 0 ; i < s->n_items ; ++i)
        moments1_clear (s->m[i]);
    }

  input = casereader_create_filter_missing (input,
					    rel->variables,
					    rel->n_variables,
					    rel->exclude,
					    &n_missing,
					    NULL);

  for (si = 0 ; si < rel->n_sc; ++si)
    {
      struct cronbach *s = &rel->sc[si];


      s->totals_idx = caseproto_get_n_widths (casereader_get_proto (input));
      input =
	casereader_create_append_numeric (input, append_sum,
					  s, NULL);
    }

  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    {
      double weight = 1.0;
      n_valid ++;

      for (si = 0; si < rel->n_sc; ++si)
	{
	  struct cronbach *s = &rel->sc[si];

	  for (i = 0 ; i < s->n_items ; ++i)
	    moments1_add (s->m[i], case_data (c, s->items[i])->f, weight);

	  moments1_add (s->total, case_data_idx (c, s->totals_idx)->f, weight);
	}
    }
  casereader_destroy (input);

  for (si = 0; si < rel->n_sc; ++si)
    {
      struct cronbach *s = &rel->sc[si];

      s->sum_of_variances = 0;
      for (i = 0 ; i < s->n_items ; ++i)
	{
	  double weight, mean, variance;
	  moments1_calculate (s->m[i], &weight, &mean, &variance, NULL, NULL);

	  s->sum_of_variances += variance;
	}

      moments1_calculate (s->total, NULL, NULL, &s->variance_of_sums,
			  NULL, NULL);

      s->alpha =
	alpha (s->n_items, s->sum_of_variances, s->variance_of_sums);
    }

  text_item_submit (text_item_create_nocopy (
                      TEXT_ITEM_TITLE,
                      xasprintf (_("Scale: %s"), ds_cstr (&rel->scale_name)),
                      NULL));

  case_processing_summary (n_valid, n_missing, dataset_dict (ds));
}





static void
case_processing_summary (casenumber n_valid, casenumber n_missing,
			 const struct dictionary *dict)
{
  struct pivot_table *table = pivot_table_create (
    N_("Case Processing Summary"));
  pivot_table_set_weight_var (table, dict_get_weight (dict));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_COUNT,
                          N_("Percent"), PIVOT_RC_PERCENT);

  struct pivot_dimension *cases = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Cases"), N_("Valid"), N_("Excluded"),
    N_("Total"));
  cases->root->show_label = true;

  casenumber total = n_missing + n_valid;

  struct entry
    {
      int stat_idx;
      int case_idx;
      double x;
    }
  entries[] = {
    { 0, 0, n_valid },
    { 0, 1, n_missing },
    { 0, 2, total },
    { 1, 0, 100.0 * n_valid / total },
    { 1, 1, 100.0 * n_missing / total },
    { 1, 2, 100.0 }
  };

  for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
    {
      const struct entry *e = &entries[i];
      pivot_table_put2 (table, e->stat_idx, e->case_idx,
                        pivot_value_new_number (e->x));
    }

  pivot_table_submit (table);
}

static void
reliability_summary_total (const struct reliability *rel)
{
  struct pivot_table *table = pivot_table_create (N_("Item-Total Statistics"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("Scale Mean if Item Deleted"),
                          N_("Scale Variance if Item Deleted"),
                          N_("Corrected Item-Total Correlation"),
                          N_("Cronbach's Alpha if Item Deleted"));

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));

  for (size_t i = 0 ; i < rel->sc[0].n_items; ++i)
    {
      const struct cronbach *s = &rel->sc[rel->total_start + i];

      int var_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (rel->sc[0].items[i]));

      double mean;
      moments1_calculate (s->total, NULL, &mean, NULL, NULL, NULL);

      double var;
      moments1_calculate (rel->sc[0].m[i], NULL, NULL, &var, NULL, NULL);
      double cov
        = (rel->sc[0].variance_of_sums + var - s->variance_of_sums) / 2.0;

      double entries[] = {
        mean,
        s->variance_of_sums,
        (cov - var) / sqrt (var * s->variance_of_sums),
        s->alpha,
      };
      for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
        pivot_table_put2 (table, i, var_idx,
                          pivot_value_new_number (entries[i]));
    }

  pivot_table_submit (table);
}


static void
reliability_statistics (const struct reliability *rel)
{
  struct pivot_table *table = pivot_table_create (
    N_("Reliability Statistics"));
  pivot_table_set_weight_var (table, rel->wv);

  if (rel->model == MODEL_ALPHA)
    {
      pivot_dimension_create (table, PIVOT_AXIS_COLUMN,
                              N_("Statistics"),
                              N_("Cronbach's Alpha"), PIVOT_RC_OTHER,
                              N_("N of Items"), PIVOT_RC_COUNT);

      const struct cronbach *s = &rel->sc[0];
      pivot_table_put1 (table, 0, pivot_value_new_number (s->alpha));
      pivot_table_put1 (table, 1, pivot_value_new_number (s->n_items));
    }
  else
    {
      struct pivot_dimension *statistics = pivot_dimension_create (
        table, PIVOT_AXIS_ROW, N_("Statistics"));
      struct pivot_category *alpha = pivot_category_create_group (
        statistics->root, N_("Cronbach's Alpha"));
      pivot_category_create_group (alpha, N_("Part 1"),
                                   N_("Value"), PIVOT_RC_OTHER,
                                   N_("N of Items"), PIVOT_RC_COUNT);
      pivot_category_create_group (alpha, N_("Part 2"),
                                   N_("Value"), PIVOT_RC_OTHER,
                                   N_("N of Items"), PIVOT_RC_COUNT);
      pivot_category_create_leaves (alpha,
                                    N_("Total N of Items"), PIVOT_RC_COUNT);
      pivot_category_create_leaves (statistics->root,
                                    N_("Correlation Between Forms"),
                                    PIVOT_RC_OTHER);
      pivot_category_create_group (statistics->root,
                                   N_("Spearman-Brown Coefficient"),
                                   N_("Equal Length"), PIVOT_RC_OTHER,
                                   N_("Unequal Length"), PIVOT_RC_OTHER);
      pivot_category_create_leaves (statistics->root,
                                    N_("Guttman Split-Half Coefficient"),
                                    PIVOT_RC_OTHER);

      /* R is the correlation between the two parts */
      double r0 = rel->sc[0].variance_of_sums -
        rel->sc[1].variance_of_sums -
        rel->sc[2].variance_of_sums ;
      double r1 = (r0 / sqrt (rel->sc[1].variance_of_sums)
                   / sqrt (rel->sc[2].variance_of_sums)
                   / 2.0);

      /* Guttman Split Half Coefficient */
      double g = 2 * r0 / rel->sc[0].variance_of_sums;

      double tmp = (1.0 - r1*r1) * rel->sc[1].n_items * rel->sc[2].n_items /
        pow2 (rel->sc[0].n_items);

      double entries[] = {
        rel->sc[1].alpha,
        rel->sc[1].n_items,
        rel->sc[2].alpha,
        rel->sc[2].n_items,
        rel->sc[1].n_items + rel->sc[2].n_items,
        r1,
        2 * r1 / (1.0 + r1),
        (sqrt (pow4 (r1) + 4 * pow2 (r1) * tmp) - pow2 (r1)) / (2 * tmp),
        g,
      };
      for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
        pivot_table_put1 (table, i, pivot_value_new_number (entries[i]));
    }

  pivot_table_submit (table);
}
