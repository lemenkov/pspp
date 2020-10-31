/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2011, 2012, 2013, 2014,
   2020 Free Software Foundation, Inc.

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

#include <float.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_matrix.h>
#include <math.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/value.h"
#include "language/command.h"
#include "language/dictionary/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/ll.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/taint.h"
#include "linreg/sweep.h"
#include "tukey/tukey.h"
#include "math/categoricals.h"
#include "math/interaction.h"
#include "math/covariance.h"
#include "math/levene.h"
#include "math/moments.h"
#include "output/pivot-table.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Workspace variable for each dependent variable */
struct per_var_ws
{
  struct interaction *iact;
  struct categoricals *cat;
  struct covariance *cov;
  struct levene *nl;

  double n;

  double sst;
  double sse;
  double ssa;

  int n_groups;

  double mse;
};

/* Per category data */
struct descriptive_data
{
  const struct variable *var;
  struct moments1 *mom;

  double minimum;
  double maximum;
};

enum missing_type
  {
    MISS_LISTWISE,
    MISS_ANALYSIS,
  };

enum statistics
  {
    STATS_DESCRIPTIVES = 0x0001,
    STATS_HOMOGENEITY = 0x0002
  };

struct coeff_node
{
  struct ll ll;
  double coeff;
};


struct contrasts_node
{
  struct ll ll;
  struct ll_list coefficient_list;
};


struct oneway_spec;

typedef double df_func (const struct per_var_ws *pvw, const struct moments1 *mom_i, const struct moments1 *mom_j);
typedef double ts_func (int k, const struct moments1 *mom_i, const struct moments1 *mom_j, double std_err);
typedef double p1tail_func (double ts, double df1, double df2);

typedef double pinv_func (double std_err, double alpha, double df, int k, const struct moments1 *mom_i, const struct moments1 *mom_j);


struct posthoc
{
  const char *syntax;
  const char *label;

  df_func *dff;
  ts_func *tsf;
  p1tail_func *p1f;

  pinv_func *pinv;
};

struct oneway_spec
{
  size_t n_vars;
  const struct variable **vars;

  const struct variable *indep_var;

  enum statistics stats;

  enum missing_type missing_type;
  enum mv_class exclude;

  /* List of contrasts */
  struct ll_list contrast_list;

  /* The weight variable */
  const struct variable *wv;
  const struct fmt_spec *wfmt;

  /* The confidence level for multiple comparisons */
  double alpha;

  int *posthoc;
  int n_posthoc;
};

static double
df_common (const struct per_var_ws *pvw, const struct moments1 *mom_i UNUSED, const struct moments1 *mom_j UNUSED)
{
  return  pvw->n - pvw->n_groups;
}

static double
df_individual (const struct per_var_ws *pvw UNUSED, const struct moments1 *mom_i, const struct moments1 *mom_j)
{
  double n_i, var_i;
  double n_j, var_j;
  double nom,denom;

  moments1_calculate (mom_i, &n_i, NULL, &var_i, 0, 0);
  moments1_calculate (mom_j, &n_j, NULL, &var_j, 0, 0);

  if (n_i <= 1.0 || n_j <= 1.0)
    return SYSMIS;

  nom = pow2 (var_i/n_i + var_j/n_j);
  denom = pow2 (var_i/n_i) / (n_i - 1) + pow2 (var_j/n_j) / (n_j - 1);

  return nom / denom;
}

static double lsd_pinv (double std_err, double alpha, double df, int k UNUSED, const struct moments1 *mom_i UNUSED, const struct moments1 *mom_j UNUSED)
{
  return std_err * gsl_cdf_tdist_Pinv (1.0 - alpha / 2.0, df);
}

static double bonferroni_pinv (double std_err, double alpha, double df, int k, const struct moments1 *mom_i UNUSED, const struct moments1 *mom_j UNUSED)
{
  const int m = k * (k - 1) / 2;
  return std_err * gsl_cdf_tdist_Pinv (1.0 - alpha / (2.0 * m), df);
}

static double sidak_pinv (double std_err, double alpha, double df, int k, const struct moments1 *mom_i UNUSED, const struct moments1 *mom_j UNUSED)
{
  const double m = k * (k - 1) / 2;
  double lp = 1.0 - exp (log (1.0 - alpha) / m) ;
  return std_err * gsl_cdf_tdist_Pinv (1.0 - lp / 2.0, df);
}

static double tukey_pinv (double std_err, double alpha, double df, int k, const struct moments1 *mom_i UNUSED, const struct moments1 *mom_j UNUSED)
{
  if (k < 2 || df < 2)
    return SYSMIS;

  return std_err / sqrt (2.0)  * qtukey (1 - alpha, 1.0, k, df, 1, 0);
}

static double scheffe_pinv (double std_err, double alpha, double df, int k, const struct moments1 *mom_i UNUSED, const struct moments1 *mom_j UNUSED)
{
  double x = (k - 1) * gsl_cdf_fdist_Pinv (1.0 - alpha, k - 1, df);
  return std_err * sqrt (x);
}

static double gh_pinv (double std_err UNUSED, double alpha, double df, int k, const struct moments1 *mom_i, const struct moments1 *mom_j)
{
  double n_i, mean_i, var_i;
  double n_j, mean_j, var_j;
  double m;

  moments1_calculate (mom_i, &n_i, &mean_i, &var_i, 0, 0);
  moments1_calculate (mom_j, &n_j, &mean_j, &var_j, 0, 0);

  m = sqrt ((var_i/n_i + var_j/n_j) / 2.0);

  if (k < 2 || df < 2)
    return SYSMIS;

  return m * qtukey (1 - alpha, 1.0, k, df, 1, 0);
}


static double
multiple_comparison_sig (double std_err,
				       const struct per_var_ws *pvw,
				       const struct descriptive_data *dd_i, const struct descriptive_data *dd_j,
				       const struct posthoc *ph)
{
  int k = pvw->n_groups;
  double df = ph->dff (pvw, dd_i->mom, dd_j->mom);
  double ts = ph->tsf (k, dd_i->mom, dd_j->mom, std_err);
  if (df == SYSMIS)
    return SYSMIS;
  return  ph->p1f (ts, k - 1, df);
}

static double
mc_half_range (const struct oneway_spec *cmd, const struct per_var_ws *pvw, double std_err, const struct descriptive_data *dd_i, const struct descriptive_data *dd_j, const struct posthoc *ph)
{
  int k = pvw->n_groups;
  double df = ph->dff (pvw, dd_i->mom, dd_j->mom);
  if (df == SYSMIS)
    return SYSMIS;

  return ph->pinv (std_err, cmd->alpha, df, k, dd_i->mom, dd_j->mom);
}

static double tukey_1tailsig (double ts, double df1, double df2)
{
  double twotailedsig;

  if (df2 < 2 || df1 < 1)
    return SYSMIS;

  twotailedsig = 1.0 - ptukey (ts, 1.0, df1 + 1, df2, 1, 0);

  return twotailedsig / 2.0;
}

static double lsd_1tailsig (double ts, double df1 UNUSED, double df2)
{
  return ts < 0 ? gsl_cdf_tdist_P (ts, df2) : gsl_cdf_tdist_Q (ts, df2);
}

static double sidak_1tailsig (double ts, double df1, double df2)
{
  double ex = (df1 + 1.0) * df1 / 2.0;
  double lsd_sig = 2 * lsd_1tailsig (ts, df1, df2);

  return 0.5 * (1.0 - pow (1.0 - lsd_sig, ex));
}

static double bonferroni_1tailsig (double ts, double df1, double df2)
{
  const int m = (df1 + 1) * df1 / 2;

  double p = ts < 0 ? gsl_cdf_tdist_P (ts, df2) : gsl_cdf_tdist_Q (ts, df2);
  p *= m;

  return p > 0.5 ? 0.5 : p;
}

static double scheffe_1tailsig (double ts, double df1, double df2)
{
  return 0.5 * gsl_cdf_fdist_Q (ts, df1, df2);
}


static double tukey_test_stat (int k UNUSED, const struct moments1 *mom_i, const struct moments1 *mom_j, double std_err)
{
  double ts;
  double n_i, mean_i, var_i;
  double n_j, mean_j, var_j;

  moments1_calculate (mom_i, &n_i, &mean_i, &var_i, 0, 0);
  moments1_calculate (mom_j, &n_j, &mean_j, &var_j, 0, 0);

  ts =  (mean_i - mean_j) / std_err;
  ts = fabs (ts) * sqrt (2.0);

  return ts;
}

static double lsd_test_stat (int k UNUSED, const struct moments1 *mom_i, const struct moments1 *mom_j, double std_err)
{
  double n_i, mean_i, var_i;
  double n_j, mean_j, var_j;

  moments1_calculate (mom_i, &n_i, &mean_i, &var_i, 0, 0);
  moments1_calculate (mom_j, &n_j, &mean_j, &var_j, 0, 0);

  return (mean_i - mean_j) / std_err;
}

static double scheffe_test_stat (int k, const struct moments1 *mom_i, const struct moments1 *mom_j, double std_err)
{
  double t;
  double n_i, mean_i, var_i;
  double n_j, mean_j, var_j;

  moments1_calculate (mom_i, &n_i, &mean_i, &var_i, 0, 0);
  moments1_calculate (mom_j, &n_j, &mean_j, &var_j, 0, 0);

  t = (mean_i - mean_j) / std_err;
  t = pow2 (t);
  t /= k - 1;

  return t;
}

static double gh_test_stat (int k UNUSED, const struct moments1 *mom_i, const struct moments1 *mom_j, double std_err UNUSED)
{
  double ts;
  double thing;
  double n_i, mean_i, var_i;
  double n_j, mean_j, var_j;

  moments1_calculate (mom_i, &n_i, &mean_i, &var_i, 0, 0);
  moments1_calculate (mom_j, &n_j, &mean_j, &var_j, 0, 0);

  thing = var_i / n_i + var_j / n_j;
  thing /= 2.0;
  thing = sqrt (thing);

  ts = (mean_i - mean_j) / thing;

  return fabs (ts);
}



static const struct posthoc ph_tests [] =
  {
    { "LSD",        N_("LSD"),          df_common, lsd_test_stat,     lsd_1tailsig,          lsd_pinv},
    { "TUKEY",      N_("Tukey HSD"),    df_common, tukey_test_stat,   tukey_1tailsig,        tukey_pinv},
    { "BONFERRONI", N_("Bonferroni"),   df_common, lsd_test_stat,     bonferroni_1tailsig,   bonferroni_pinv},
    { "SCHEFFE",    N_("Scheffé"),      df_common, scheffe_test_stat, scheffe_1tailsig,      scheffe_pinv},
    { "GH",         N_("Games-Howell"), df_individual, gh_test_stat,  tukey_1tailsig,        gh_pinv},
    { "SIDAK",      N_("Šidák"),        df_common, lsd_test_stat,     sidak_1tailsig,        sidak_pinv}
  };


struct oneway_workspace
{
  /* The number of distinct values of the independent variable, when all
     missing values are disregarded */
  int actual_number_of_groups;

  struct per_var_ws *vws;

  /* An array of descriptive data.  One for each dependent variable */
  struct descriptive_data **dd_total;
};

/* Routines to show the output tables */
static void show_anova_table (const struct oneway_spec *, const struct oneway_workspace *);
static void show_descriptives (const struct oneway_spec *, const struct oneway_workspace *);
static void show_homogeneity (const struct oneway_spec *, const struct oneway_workspace *);

static void output_oneway (const struct oneway_spec *, struct oneway_workspace *ws);
static void run_oneway (const struct oneway_spec *cmd, struct casereader *input, const struct dataset *ds);


static void
destroy_coeff_list (struct contrasts_node *coeff_list)
{
  struct coeff_node *cn = NULL;
  struct coeff_node *cnx = NULL;
  struct ll_list *cl = &coeff_list->coefficient_list;

  ll_for_each_safe (cn, cnx, struct coeff_node, ll, cl)
    {
      free (cn);
    }

  free (coeff_list);
}

static void
oneway_cleanup (struct oneway_spec *cmd)
{
  struct contrasts_node *coeff_list  = NULL;
  struct contrasts_node *coeff_next  = NULL;
  ll_for_each_safe (coeff_list, coeff_next, struct contrasts_node, ll, &cmd->contrast_list)
    {
      destroy_coeff_list (coeff_list);
    }

  free (cmd->posthoc);
}



int
cmd_oneway (struct lexer *lexer, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);
  struct oneway_spec oneway ;
  oneway.n_vars = 0;
  oneway.vars = NULL;
  oneway.indep_var = NULL;
  oneway.stats = 0;
  oneway.missing_type = MISS_ANALYSIS;
  oneway.exclude = MV_ANY;
  oneway.wv = dict_get_weight (dict);
  oneway.wfmt = dict_get_weight_format (dict);
  oneway.alpha = 0.05;
  oneway.posthoc = NULL;
  oneway.n_posthoc = 0;

  ll_init (&oneway.contrast_list);


  if (lex_match (lexer, T_SLASH))
    {
      if (!lex_force_match_id (lexer, "VARIABLES"))
	{
	  goto error;
	}
      lex_match (lexer, T_EQUALS);
    }

  if (!parse_variables_const (lexer, dict,
			      &oneway.vars, &oneway.n_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  if (!lex_force_match (lexer, T_BY))
    goto error;

  oneway.indep_var = parse_variable_const (lexer, dict);
  if (oneway.indep_var == NULL)
    goto error;

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "STATISTICS"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "DESCRIPTIVES"))
		{
		  oneway.stats |= STATS_DESCRIPTIVES;
		}
	      else if (lex_match_id (lexer, "HOMOGENEITY"))
		{
		  oneway.stats |= STATS_HOMOGENEITY;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "POSTHOC"))
	{
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      int p;
	      bool method = false;
	      for (p = 0 ; p < sizeof (ph_tests) / sizeof (struct posthoc); ++p)
		{
		  if (lex_match_id (lexer, ph_tests[p].syntax))
		    {
		      oneway.n_posthoc++;
		      oneway.posthoc = xrealloc (oneway.posthoc, sizeof (*oneway.posthoc) * oneway.n_posthoc);
		      oneway.posthoc[oneway.n_posthoc - 1] = p;
		      method = true;
		      break;
		    }
		}
	      if (method == false)
		{
		  if (lex_match_id (lexer, "ALPHA"))
		    {
		      if (!lex_force_match (lexer, T_LPAREN))
			goto error;
		      if (! lex_force_num (lexer))
			goto error;
		      oneway.alpha = lex_number (lexer);
		      lex_get (lexer);
		      if (!lex_force_match (lexer, T_RPAREN))
			goto error;
		    }
		  else
		    {
		      msg (SE, _("The post hoc analysis method %s is not supported."), lex_tokcstr (lexer));
		      lex_error (lexer, NULL);
		      goto error;
		    }
		}
	    }
	}
      else if (lex_match_id (lexer, "CONTRAST"))
	{
	  struct contrasts_node *cl = xzalloc (sizeof *cl);

	  struct ll_list *coefficient_list = &cl->coefficient_list;
          lex_match (lexer, T_EQUALS);

	  ll_init (coefficient_list);

          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_is_number (lexer))
		{
		  struct coeff_node *cc = xmalloc (sizeof *cc);
		  cc->coeff = lex_number (lexer);

		  ll_push_tail (coefficient_list, &cc->ll);
		  lex_get (lexer);
		}
	      else
		{
		  destroy_coeff_list (cl);
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }

	  if (ll_count (coefficient_list) <= 0)
            {
              destroy_coeff_list (cl);
              goto error;
            }

	  ll_push_tail (&oneway.contrast_list, &cl->ll);
	}
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
	      if (lex_match_id (lexer, "INCLUDE"))
		{
		  oneway.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  oneway.exclude = MV_ANY;
		}
	      else if (lex_match_id (lexer, "LISTWISE"))
		{
		  oneway.missing_type = MISS_LISTWISE;
		}
	      else if (lex_match_id (lexer, "ANALYSIS"))
		{
		  oneway.missing_type = MISS_ANALYSIS;
		}
	      else
		{
                  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }


  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;

    grouper = casegrouper_create_splits (proc_open (ds), dict);
    while (casegrouper_get_next_group (grouper, &group))
      run_oneway (&oneway, group, ds);
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  oneway_cleanup (&oneway);
  free (oneway.vars);
  return CMD_SUCCESS;

 error:
  oneway_cleanup (&oneway);
  free (oneway.vars);
  return CMD_FAILURE;
}





static struct descriptive_data *
dd_create (const struct variable *var)
{
  struct descriptive_data *dd = xmalloc (sizeof *dd);

  dd->mom = moments1_create (MOMENT_VARIANCE);
  dd->minimum = DBL_MAX;
  dd->maximum = -DBL_MAX;
  dd->var = var;

  return dd;
}

static void
dd_destroy (struct descriptive_data *dd)
{
  moments1_destroy (dd->mom);
  free (dd);
}

static void *
makeit (const void *aux1, void *aux2 UNUSED)
{
  const struct variable *var = aux1;

  struct descriptive_data *dd = dd_create (var);

  return dd;
}

static void
killit (const void *aux1 UNUSED, void *aux2 UNUSED, void *user_data)
{
  struct descriptive_data *dd = user_data;

  dd_destroy (dd);
}


static void
updateit (const void *aux1, void *aux2, void *user_data,
	  const struct ccase *c, double weight)
{
  struct descriptive_data *dd = user_data;

  const struct variable *varp = aux1;

  const union value *valx = case_data (c, varp);

  struct descriptive_data *dd_total = aux2;

  moments1_add (dd->mom, valx->f, weight);
  if (valx->f < dd->minimum)
    dd->minimum = valx->f;

  if (valx->f > dd->maximum)
    dd->maximum = valx->f;

  {
    const struct variable *var = dd_total->var;
    const union value *val = case_data (c, var);

    moments1_add (dd_total->mom,
		  val->f,
		  weight);

    if (val->f < dd_total->minimum)
      dd_total->minimum = val->f;

    if (val->f > dd_total->maximum)
      dd_total->maximum = val->f;
  }
}

static void
run_oneway (const struct oneway_spec *cmd,
            struct casereader *input,
            const struct dataset *ds)
{
  int v;
  struct taint *taint;
  struct dictionary *dict = dataset_dict (ds);
  struct casereader *reader;
  struct ccase *c;

  struct oneway_workspace ws;

  ws.actual_number_of_groups = 0;
  ws.vws = xzalloc (cmd->n_vars * sizeof (*ws.vws));
  ws.dd_total = xmalloc (sizeof (struct descriptive_data) * cmd->n_vars);

  for (v = 0 ; v < cmd->n_vars; ++v)
    ws.dd_total[v] = dd_create (cmd->vars[v]);

  for (v = 0; v < cmd->n_vars; ++v)
    {
      static const struct payload payload =
        {
          .create = makeit,
          .update = updateit,
          .calculate = NULL,
          .destroy = killit
        };

      ws.vws[v].iact = interaction_create (cmd->indep_var);
      ws.vws[v].cat = categoricals_create (&ws.vws[v].iact, 1, cmd->wv,
                                           cmd->exclude);

      categoricals_set_payload (ws.vws[v].cat, &payload,
				CONST_CAST (struct variable *, cmd->vars[v]),
				ws.dd_total[v]);


      ws.vws[v].cov = covariance_2pass_create (1, &cmd->vars[v],
					       ws.vws[v].cat,
					       cmd->wv, cmd->exclude, true);
      ws.vws[v].nl = levene_create (var_get_width (cmd->indep_var), NULL);
    }

  c = casereader_peek (input, 0);
  if (c == NULL)
    {
      casereader_destroy (input);
      goto finish;
    }
  output_split_file_values (ds, c);
  case_unref (c);

  taint = taint_clone (casereader_get_taint (input));

  input = casereader_create_filter_missing (input, &cmd->indep_var, 1,
                                            cmd->exclude, NULL, NULL);
  if (cmd->missing_type == MISS_LISTWISE)
    input = casereader_create_filter_missing (input, cmd->vars, cmd->n_vars,
                                              cmd->exclude, NULL, NULL);
  input = casereader_create_filter_weight (input, dict, NULL, NULL);

  reader = casereader_clone (input);
  for (; (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      int i;
      double w = dict_get_case_weight (dict, c, NULL);

      for (i = 0; i < cmd->n_vars; ++i)
	{
	  struct per_var_ws *pvw = &ws.vws[i];
	  const struct variable *v = cmd->vars[i];
	  const union value *val = case_data (c, v);

	  if (MISS_ANALYSIS == cmd->missing_type)
	    {
	      if (var_is_value_missing (v, val, cmd->exclude))
		continue;
	    }

	  covariance_accumulate_pass1 (pvw->cov, c);
	  levene_pass_one (pvw->nl, val->f, w, case_data (c, cmd->indep_var));
	}
    }
  casereader_destroy (reader);

  reader = casereader_clone (input);
  for (; (c = casereader_read (reader)); case_unref (c))
    {
      int i;
      double w = dict_get_case_weight (dict, c, NULL);
      for (i = 0; i < cmd->n_vars; ++i)
	{
	  struct per_var_ws *pvw = &ws.vws[i];
	  const struct variable *v = cmd->vars[i];
	  const union value *val = case_data (c, v);

	  if (MISS_ANALYSIS == cmd->missing_type)
	    {
	      if (var_is_value_missing (v, val, cmd->exclude))
		continue;
	    }

	  covariance_accumulate_pass2 (pvw->cov, c);
	  levene_pass_two (pvw->nl, val->f, w, case_data (c, cmd->indep_var));
	}
    }
  casereader_destroy (reader);

  reader = casereader_clone (input);
  for (; (c = casereader_read (reader)); case_unref (c))
    {
      int i;
      double w = dict_get_case_weight (dict, c, NULL);

      for (i = 0; i < cmd->n_vars; ++i)
	{
	  struct per_var_ws *pvw = &ws.vws[i];
	  const struct variable *v = cmd->vars[i];
	  const union value *val = case_data (c, v);

	  if (MISS_ANALYSIS == cmd->missing_type)
	    {
	      if (var_is_value_missing (v, val, cmd->exclude))
		continue;
	    }

	  levene_pass_three (pvw->nl, val->f, w, case_data (c, cmd->indep_var));
	}
    }
  casereader_destroy (reader);


  for (v = 0; v < cmd->n_vars; ++v)
    {
      const gsl_matrix *ucm;
      gsl_matrix *cm;
      struct per_var_ws *pvw = &ws.vws[v];
      const struct categoricals *cats = covariance_get_categoricals (pvw->cov);
      const bool ok = categoricals_sane (cats);

      if (! ok)
	{
	  msg (MW,
	       _("Dependent variable %s has no non-missing values.  No analysis for this variable will be done."),
	       var_get_name (cmd->vars[v]));
	  continue;
	}

      ucm = covariance_calculate_unnormalized (pvw->cov);

      cm = gsl_matrix_alloc (ucm->size1, ucm->size2);
      gsl_matrix_memcpy (cm, ucm);

      moments1_calculate (ws.dd_total[v]->mom, &pvw->n, NULL, NULL, NULL, NULL);

      pvw->sst = gsl_matrix_get (cm, 0, 0);

      reg_sweep (cm, 0);

      pvw->sse = gsl_matrix_get (cm, 0, 0);
      gsl_matrix_free (cm);

      pvw->ssa = pvw->sst - pvw->sse;

      pvw->n_groups = categoricals_n_total (cats);

      pvw->mse = (pvw->sst - pvw->ssa) / (pvw->n - pvw->n_groups);
    }

  for (v = 0; v < cmd->n_vars; ++v)
    {
      const struct categoricals *cats = covariance_get_categoricals (ws.vws[v].cov);

      if (! categoricals_is_complete (cats))
	{
	  continue;
	}

      if (categoricals_n_total (cats) > ws.actual_number_of_groups)
	ws.actual_number_of_groups = categoricals_n_total (cats);
    }

  casereader_destroy (input);

  if (!taint_has_tainted_successor (taint))
    output_oneway (cmd, &ws);

  taint_destroy (taint);

 finish:

  for (v = 0; v < cmd->n_vars; ++v)
    {
      covariance_destroy (ws.vws[v].cov);
      levene_destroy (ws.vws[v].nl);
      dd_destroy (ws.dd_total[v]);
      interaction_destroy (ws.vws[v].iact);
    }

  free (ws.vws);
  free (ws.dd_total);
}

static void show_contrast_coeffs (const struct oneway_spec *cmd, const struct oneway_workspace *ws);
static void show_contrast_tests (const struct oneway_spec *cmd, const struct oneway_workspace *ws);
static void show_comparisons (const struct oneway_spec *cmd, const struct oneway_workspace *ws, int depvar);

static void
output_oneway (const struct oneway_spec *cmd, struct oneway_workspace *ws)
{
  size_t i = 0;

  /* Check the sanity of the given contrast values */
  struct contrasts_node *coeff_list  = NULL;
  struct contrasts_node *coeff_next  = NULL;
  ll_for_each_safe (coeff_list, coeff_next, struct contrasts_node, ll, &cmd->contrast_list)
    {
      struct coeff_node *cn = NULL;
      double sum = 0;
      struct ll_list *cl = &coeff_list->coefficient_list;
      ++i;

      if (ll_count (cl) != ws->actual_number_of_groups)
	{
	  msg (SW,
	       _("In contrast list %zu, the number of coefficients (%zu) does not equal the number of groups (%d). This contrast list will be ignored."),
	       i, ll_count (cl), ws->actual_number_of_groups);

	  ll_remove (&coeff_list->ll);
	  destroy_coeff_list (coeff_list);
	  continue;
	}

      ll_for_each (cn, struct coeff_node, ll, cl)
	sum += cn->coeff;

      if (sum != 0.0)
	msg (SW, _("Coefficients for contrast %zu do not total zero"), i);
    }

  if (cmd->stats & STATS_DESCRIPTIVES)
    show_descriptives (cmd, ws);

  if (cmd->stats & STATS_HOMOGENEITY)
    show_homogeneity (cmd, ws);

  show_anova_table (cmd, ws);

  if (ll_count (&cmd->contrast_list) > 0)
    {
      show_contrast_coeffs (cmd, ws);
      show_contrast_tests (cmd, ws);
    }

  if (cmd->posthoc)
    {
      int v;
      for (v = 0 ; v < cmd->n_vars; ++v)
	{
	  const struct categoricals *cats = covariance_get_categoricals (ws->vws[v].cov);

	  if (categoricals_is_complete (cats))
	    show_comparisons (cmd, ws, v);
	}
    }
}


/* Show the ANOVA table */
static void
show_anova_table (const struct oneway_spec *cmd, const struct oneway_workspace *ws)
{
  struct pivot_table *table = pivot_table_create (N_("ANOVA"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("Sum of Squares"), PIVOT_RC_OTHER,
                          N_("df"), PIVOT_RC_INTEGER,
                          N_("Mean Square"), PIVOT_RC_OTHER,
                          N_("F"), PIVOT_RC_OTHER,
                          N_("Sig."), PIVOT_RC_SIGNIFICANCE);

  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Type"),
                          N_("Between Groups"), N_("Within Groups"),
                          N_("Total"));

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));

  for (size_t i = 0; i < cmd->n_vars; ++i)
    {
      int var_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (cmd->vars[i]));

      const struct per_var_ws *pvw = &ws->vws[i];

      double n;
      moments1_calculate (ws->dd_total[i]->mom, &n, NULL, NULL, NULL, NULL);

      double df1 = pvw->n_groups - 1;
      double df2 = n - pvw->n_groups;
      double msa = pvw->ssa / df1;
      double F = msa / pvw->mse ;

      struct entry
        {
          int stat_idx;
          int type_idx;
          double x;
        }
      entries[] = {
        /* Sums of Squares. */
        { 0, 0, pvw->ssa },
        { 0, 1, pvw->sse },
        { 0, 2, pvw->sst },
        /* Degrees of Freedom. */
        { 1, 0, df1 },
        { 1, 1, df2 },
        { 1, 2, n - 1 },
        /* Mean Squares. */
        { 2, 0, msa },
        { 2, 1, pvw->mse },
        /* F. */
        { 3, 0, F },
        /* Significance. */
        { 4, 0, gsl_cdf_fdist_Q (F, df1, df2) },
      };
      for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
        {
          const struct entry *e = &entries[j];
          pivot_table_put3 (table, e->stat_idx, e->type_idx, var_idx,
                            pivot_value_new_number (e->x));
        }
    }

  pivot_table_submit (table);
}

/* Show the descriptives table */
static void
show_descriptives (const struct oneway_spec *cmd, const struct oneway_workspace *ws)
{
  if (!cmd->n_vars)
    return;
  const struct categoricals *cats = covariance_get_categoricals (
    ws->vws[0].cov);

  struct pivot_table *table = pivot_table_create (N_("Descriptives"));
  pivot_table_set_weight_format (table, cmd->wfmt);

  const double confidence = 0.95;

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Statistics"),
    N_("N"), PIVOT_RC_COUNT, N_("Mean"), N_("Std. Deviation"),
    N_("Std. Error"));
  struct pivot_category *interval = pivot_category_create_group__ (
    statistics->root,
    pivot_value_new_text_format (N_("%g%% Confidence Interval for Mean"),
                                 confidence * 100.0));
  pivot_category_create_leaves (interval, N_("Lower Bound"),
                                N_("Upper Bound"));
  pivot_category_create_leaves (statistics->root,
                                N_("Minimum"), N_("Maximum"));

  struct pivot_dimension *indep_var = pivot_dimension_create__ (
    table, PIVOT_AXIS_ROW, pivot_value_new_variable (cmd->indep_var));
  indep_var->root->show_label = true;
  size_t n;
  union value *values = categoricals_get_var_values (cats, cmd->indep_var, &n);
  for (size_t j = 0; j < n; j++)
    pivot_category_create_leaf (
      indep_var->root, pivot_value_new_var_value (cmd->indep_var, &values[j]));
  pivot_category_create_leaf (
    indep_var->root, pivot_value_new_text_format (N_("Total")));

  struct pivot_dimension *dep_var = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Dependent Variable"));

  const double q = (1.0 - confidence) / 2.0;
  for (int v = 0; v < cmd->n_vars; ++v)
    {
      int dep_var_idx = pivot_category_create_leaf (
        dep_var->root, pivot_value_new_variable (cmd->vars[v]));

      struct per_var_ws *pvw = &ws->vws[v];
      const struct categoricals *cats = covariance_get_categoricals (pvw->cov);

      int count;
      for (count = 0; count < categoricals_n_total (cats); ++count)
	{
	  const struct descriptive_data *dd
            = categoricals_get_user_data_by_category (cats, count);

	  double n, mean, variance;
	  moments1_calculate (dd->mom, &n, &mean, &variance, NULL, NULL);

	  double std_dev = sqrt (variance);
	  double std_error = std_dev / sqrt (n) ;
	  double T = gsl_cdf_tdist_Qinv (q, n - 1);

          double entries[] = {
            n,
            mean,
            std_dev,
            std_error,
            mean - T * std_error,
            mean + T * std_error,
            dd->minimum,
            dd->maximum,
          };
          for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
            pivot_table_put3 (table, i, count, dep_var_idx,
                              pivot_value_new_number (entries[i]));
	}

      if (categoricals_is_complete (cats))
        {
          double n, mean, variance;
          moments1_calculate (ws->dd_total[v]->mom, &n, &mean, &variance,
                              NULL, NULL);

          double std_dev = sqrt (variance);
          double std_error = std_dev / sqrt (n) ;
          double T = gsl_cdf_tdist_Qinv (q, n - 1);

          double entries[] = {
            n,
            mean,
            std_dev,
            std_error,
            mean - T * std_error,
            mean + T * std_error,
            ws->dd_total[v]->minimum,
            ws->dd_total[v]->maximum,
          };
          for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
            pivot_table_put3 (table, i, count, dep_var_idx,
                              pivot_value_new_number (entries[i]));
        }
    }

  pivot_table_submit (table);
}

/* Show the homogeneity table */
static void
show_homogeneity (const struct oneway_spec *cmd, const struct oneway_workspace *ws)
{
  struct pivot_table *table = pivot_table_create (
    N_("Test of Homogeneity of Variances"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("Levene Statistic"), PIVOT_RC_OTHER,
                          N_("df1"), PIVOT_RC_INTEGER,
                          N_("df2"), PIVOT_RC_INTEGER,
                          N_("Sig."), PIVOT_RC_SIGNIFICANCE);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));

  for (int v = 0; v < cmd->n_vars; ++v)
    {
      int var_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (cmd->vars[v]));

      double n;
      moments1_calculate (ws->dd_total[v]->mom, &n, NULL, NULL, NULL, NULL);

      const struct per_var_ws *pvw = &ws->vws[v];
      double df1 = pvw->n_groups - 1;
      double df2 = n - pvw->n_groups;
      double F = levene_calculate (pvw->nl);

      double entries[] =
        {
          F,
          df1,
          df2,
          gsl_cdf_fdist_Q (F, df1, df2),
        };
      for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
        pivot_table_put2 (table, i, var_idx,
                          pivot_value_new_number (entries[i]));
    }

  pivot_table_submit (table);
}


/* Show the contrast coefficients table */
static void
show_contrast_coeffs (const struct oneway_spec *cmd, const struct oneway_workspace *ws)
{
  struct pivot_table *table = pivot_table_create (N_("Contrast Coefficients"));

  struct pivot_dimension *indep_var = pivot_dimension_create__ (
    table, PIVOT_AXIS_COLUMN, pivot_value_new_variable (cmd->indep_var));
  indep_var->root->show_label = true;

  struct pivot_dimension *contrast = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Contrast"));
  contrast->root->show_label = true;

  const struct covariance *cov = ws->vws[0].cov;

  const struct contrasts_node *cn;
  int c_num = 1;
  ll_for_each (cn, struct contrasts_node, ll, &cmd->contrast_list)
    {
      int contrast_idx = pivot_category_create_leaf (
        contrast->root, pivot_value_new_integer (c_num++));

      const struct coeff_node *coeffn;
      int indep_idx = 0;
      ll_for_each (coeffn, struct coeff_node, ll, &cn->coefficient_list)
	{
	  const struct categoricals *cats = covariance_get_categoricals (cov);
	  const struct ccase *gcc = categoricals_get_case_by_category (
            cats, indep_idx);

          if (!contrast_idx)
            pivot_category_create_leaf (
              indep_var->root, pivot_value_new_var_value (
                cmd->indep_var, case_data (gcc, cmd->indep_var)));

          pivot_table_put2 (table, indep_idx++, contrast_idx,
                            pivot_value_new_integer (coeffn->coeff));
	}
    }

  pivot_table_submit (table);
}

/* Show the results of the contrast tests */
static void
show_contrast_tests (const struct oneway_spec *cmd, const struct oneway_workspace *ws)
{
  struct pivot_table *table = pivot_table_create (N_("Contrast Tests"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("Value of Contrast"), PIVOT_RC_OTHER,
                          N_("Std. Error"), PIVOT_RC_OTHER,
                          N_("t"), PIVOT_RC_OTHER,
                          N_("df"), PIVOT_RC_OTHER,
                          N_("Sig. (2-tailed)"), PIVOT_RC_SIGNIFICANCE);

  struct pivot_dimension *contrasts = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Contrast"));
  contrasts->root->show_label = true;
  int n_contrasts = ll_count (&cmd->contrast_list);
  for (int i = 1; i <= n_contrasts; i++)
    pivot_category_create_leaf (contrasts->root, pivot_value_new_integer (i));

  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Assumption"),
                          N_("Assume equal variances"),
                          N_("Does not assume equal variances"));

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Dependent Variable"));

  for (int v = 0; v < cmd->n_vars; ++v)
    {
      const struct per_var_ws *pvw = &ws->vws[v];
      const struct categoricals *cats = covariance_get_categoricals (pvw->cov);
      if (!categoricals_is_complete (cats))
	continue;

      int var_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (cmd->vars[v]));

      struct contrasts_node *cn;
      int contrast_idx = 0;
      ll_for_each (cn, struct contrasts_node, ll, &cmd->contrast_list)
	{

	  /* Note: The calculation of the degrees of freedom in the
	     "variances not equal" case is painfull!!
	     The following formula may help to understand it:
	     \frac{\left (\sum_{i=1}^k{c_i^2\frac{s_i^2}{n_i}}\right)^2}
	     {
	     \sum_{i=1}^k\left (
	     \frac{\left (c_i^2\frac{s_i^2}{n_i}\right)^2}  {n_i-1}
	     \right)
	     }
	  */

	  double grand_n;
	  moments1_calculate (ws->dd_total[v]->mom, &grand_n, NULL, NULL,
                              NULL, NULL);
	  double df = grand_n - pvw->n_groups;

	  double contrast_value = 0.0;
	  double coef_msq = 0.0;
	  double sec_vneq = 0.0;
	  double df_denominator = 0.0;
	  double df_numerator = 0.0;
          struct coeff_node *coeffn;
	  int ci = 0;
          ll_for_each (coeffn, struct coeff_node, ll, &cn->coefficient_list)
	    {
	      const struct descriptive_data *dd
                = categoricals_get_user_data_by_category (cats, ci);
	      const double coef = coeffn->coeff;

	      double n, mean, variance;
	      moments1_calculate (dd->mom, &n, &mean, &variance, NULL, NULL);

	      double winv = variance / n;
	      contrast_value += coef * mean;
	      coef_msq += pow2 (coef) / n;
	      sec_vneq += pow2 (coef) * variance / n;
	      df_numerator += pow2 (coef) * winv;
	      df_denominator += pow2(pow2 (coef) * winv) / (n - 1);

              ci++;
	    }
	  sec_vneq = sqrt (sec_vneq);
	  df_numerator = pow2 (df_numerator);

	  double std_error_contrast = sqrt (pvw->mse * coef_msq);
	  double T = contrast_value / std_error_contrast;
	  double T_ne = contrast_value / sec_vneq;
	  double df_ne = df_numerator / df_denominator;

          struct entry
            {
              int stat_idx;
              int assumption_idx;
              double x;
            }
          entries[] =
            {
              /* Assume equal. */
              { 0, 0, contrast_value },
              { 1, 0, std_error_contrast },
              { 2, 0, T },
              { 3, 0, df },
              { 4, 0, 2 * gsl_cdf_tdist_Q (fabs(T), df) },
              /* Do not assume equal. */
              { 0, 1, contrast_value },
              { 1, 1, sec_vneq },
              { 2, 1, T_ne },
              { 3, 1, df_ne },
              { 4, 1, 2 * gsl_cdf_tdist_Q (fabs(T_ne), df_ne) },
            };

          for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
            {
              const struct entry *e = &entries[i];
              pivot_table_put4 (
                table, e->stat_idx, contrast_idx, e->assumption_idx, var_idx,
                pivot_value_new_number (e->x));
            }

          contrast_idx++;
	}
    }

  pivot_table_submit (table);
}

static void
show_comparisons (const struct oneway_spec *cmd, const struct oneway_workspace *ws, int v)
{
  struct pivot_table *table = pivot_table_create__ (
    pivot_value_new_user_text_nocopy (xasprintf (
                                        _("Multiple Comparisons (%s)"),
                                        var_to_string (cmd->vars[v]))),
    "Multiple Comparisons");
  table->look.omit_empty = true;

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Statistics"),
    N_("Mean Difference (I - J)"), PIVOT_RC_OTHER,
    N_("Std. Error"), PIVOT_RC_OTHER,
    N_("Sig."), PIVOT_RC_SIGNIFICANCE);
  struct pivot_category *interval = pivot_category_create_group__ (
    statistics->root,
    pivot_value_new_text_format (N_("%g%% Confidence Interval"),
                                 (1 - cmd->alpha) * 100.0));
  pivot_category_create_leaves (interval,
                                N_("Lower Bound"), PIVOT_RC_OTHER,
                                N_("Upper Bound"), PIVOT_RC_OTHER);

  struct pivot_dimension *j_family = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("(J) Family"));
  j_family->root->show_label = true;

  struct pivot_dimension *i_family = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("(J) Family"));
  i_family->root->show_label = true;

  const struct per_var_ws *pvw = &ws->vws[v];
  const struct categoricals *cat = pvw->cat;
  for (int i = 0; i < pvw->n_groups; i++)
    {
      const struct ccase *gcc = categoricals_get_case_by_category (cat, i);
      for (int j = 0; j < 2; j++)
        pivot_category_create_leaf (
          j ? j_family->root : i_family->root,
          pivot_value_new_var_value (cmd->indep_var,
                                     case_data (gcc, cmd->indep_var)));
    }

  struct pivot_dimension *test = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Test"));

  for (int p = 0; p < cmd->n_posthoc; ++p)
    {
      const struct posthoc *ph = &ph_tests[cmd->posthoc[p]];

      int test_idx = pivot_category_create_leaf (
        test->root, pivot_value_new_text (ph->label));

      for (int i = 0; i < pvw->n_groups ; ++i)
	{
	  struct descriptive_data *dd_i
            = categoricals_get_user_data_by_category (cat, i);
	  double weight_i, mean_i, var_i;
	  moments1_calculate (dd_i->mom, &weight_i, &mean_i, &var_i, 0, 0);

	  for (int j = 0 ; j < pvw->n_groups; ++j)
	    {
	      if (j == i)
		continue;

	      struct descriptive_data *dd_j
                = categoricals_get_user_data_by_category (cat, j);
	      double weight_j, mean_j, var_j;
	      moments1_calculate (dd_j->mom, &weight_j, &mean_j, &var_j, 0, 0);

	      double std_err = pvw->mse;
	      std_err *= weight_i + weight_j;
	      std_err /= weight_i * weight_j;
	      std_err = sqrt (std_err);

              double sig = 2 * multiple_comparison_sig (std_err, pvw,
                                                        dd_i, dd_j, ph);
	      double half_range = mc_half_range (cmd, pvw, std_err,
                                                 dd_i, dd_j, ph);
              double entries[] = {
                mean_i - mean_j,
                std_err,
                sig,
                (mean_i - mean_j) - half_range,
                (mean_i - mean_j) + half_range,
              };
              for (size_t k = 0; k < sizeof entries / sizeof *entries; k++)
                pivot_table_put4 (table, k, j, i, test_idx,
                                  pivot_value_new_number (entries[k]));
	    }
	}
    }

  pivot_table_submit (table);
}
