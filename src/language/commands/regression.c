/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2009, 2010, 2011, 2012, 2013, 2014,
   2016, 2017, 2019 Free Software Foundation, Inc.

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
#include <stdbool.h>

#include <gsl/gsl_math.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_matrix.h>

#include <data/dataset.h>
#include <data/casewriter.h>

#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"


#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dictionary.h"

#include "math/covariance.h"
#include "math/linreg.h"
#include "math/moments.h"

#include "libpspp/message.h"
#include "libpspp/taint.h"

#include "output/pivot-table.h"

#include "gl/intprops.h"
#include "gl/minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#define STATS_R      (1 << 0)
#define STATS_COEFF  (1 << 1)
#define STATS_ANOVA  (1 << 2)
#define STATS_OUTS   (1 << 3)
#define STATS_CI     (1 << 4)
#define STATS_BCOV   (1 << 5)
#define STATS_TOL    (1 << 6)

#define STATS_DEFAULT  (STATS_R | STATS_COEFF | STATS_ANOVA | STATS_OUTS)


struct regression
  {
    struct dataset *ds;

    const struct variable **vars;
    size_t n_vars;

    const struct variable **dep_vars;
    size_t n_dep_vars;

    unsigned int stats;
    double ci;

    bool resid;
    bool pred;

    bool origin;
  };

struct regression_workspace
{
  /* The new variables which will be introduced by /SAVE */
  const struct variable **predvars;
  const struct variable **residvars;

  /* A reader/writer pair to temporarily hold the
     values of the new variables */
  struct casewriter *writer;
  struct casereader *reader;

  /* Indeces of the new values in the reader/writer (-1 if not applicable) */
  int res_idx;
  int pred_idx;

  /* 0, 1 or 2 depending on what new variables are to be created */
  int extras;
};

static void run_regression (const struct regression *cmd,
                            struct regression_workspace *ws,
                            struct casereader *input);


/* Return a string based on PREFIX which may be used as the name
   of a new variable in DICT */
static char *
reg_get_name (const struct dictionary *dict, const char *prefix)
{
  for (size_t i = 1; ; i++)
    {
      char *name = xasprintf ("%s%zu", prefix, i);
      if (!dict_lookup_var (dict, name))
        return name;
      free (name);
    }
}

static const struct variable *
create_aux_var (struct dataset *ds, const char *prefix)
{
  struct dictionary *dict = dataset_dict (ds);
  char *name = reg_get_name (dict, prefix);
  struct variable *var = dict_create_var_assert (dict, name, 0);
  free (name);
  return var;
}

/* Auxiliary data for transformation when /SAVE is entered */
struct save_trans_data
  {
    int n_dep_vars;
    struct regression_workspace *ws;
  };

static bool
save_trans_free (void *aux)
{
  struct save_trans_data *save_trans_data = aux;
  free (save_trans_data->ws->predvars);
  free (save_trans_data->ws->residvars);

  casereader_destroy (save_trans_data->ws->reader);
  free (save_trans_data->ws);
  free (save_trans_data);
  return true;
}

static enum trns_result
save_trans_func (void *aux, struct ccase **c, casenumber x UNUSED)
{
  struct save_trans_data *save_trans_data = aux;
  struct regression_workspace *ws = save_trans_data->ws;
  struct ccase *in = casereader_read (ws->reader);

  if (in)
    {
      *c = case_unshare (*c);

      for (size_t k = 0; k < save_trans_data->n_dep_vars; ++k)
        {
          if (ws->pred_idx != -1)
            {
              double pred = case_num_idx (in, ws->extras * k + ws->pred_idx);
              *case_num_rw (*c, ws->predvars[k]) = pred;
            }

          if (ws->res_idx != -1)
            {
              double resid = case_num_idx (in, ws->extras * k + ws->res_idx);
              *case_num_rw (*c, ws->residvars[k]) = resid;
            }
        }
      case_unref (in);
    }

  return TRNS_CONTINUE;
}

int
cmd_regression (struct lexer *lexer, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);

  struct regression regression = {
    .ci = 0.95,
    .stats = STATS_DEFAULT,
    .pred = false,
    .resid = false,
    .ds = ds,
    .origin = false,
  };

  bool variables_seen = false;
  bool method_seen = false;
  bool dependent_seen = false;
  int save_start = 0;
  int save_end = 0;
  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "VARIABLES"))
        {
          if (method_seen)
            {
              lex_next_error (lexer, -1, -1,
                              _("VARIABLES may not appear after %s"), "METHOD");
              goto error;
            }
          if (dependent_seen)
            {
              lex_next_error (lexer, -1, -1,
                              _("VARIABLES may not appear after %s"), "DEPENDENT");
              goto error;
            }
          variables_seen = true;
          lex_match (lexer, T_EQUALS);

          if (!parse_variables_const (lexer, dict,
                                      &regression.vars, &regression.n_vars,
                                      PV_NO_DUPLICATE | PV_NUMERIC))
            goto error;
        }
      else if (lex_match_id (lexer, "DEPENDENT"))
        {
          dependent_seen = true;
          lex_match (lexer, T_EQUALS);

          free (regression.dep_vars);
          regression.n_dep_vars = 0;

          if (!parse_variables_const (lexer, dict,
                                      &regression.dep_vars,
                                      &regression.n_dep_vars,
                                      PV_NO_DUPLICATE | PV_NUMERIC))
            goto error;
        }
      else if (lex_match_id (lexer, "ORIGIN"))
        regression.origin = true;
      else if (lex_match_id (lexer, "NOORIGIN"))
        regression.origin = false;
      else if (lex_match_id (lexer, "METHOD"))
        {
          method_seen = true;
          lex_match (lexer, T_EQUALS);

          if (!lex_force_match_id (lexer, "ENTER"))
            goto error;

          if (!variables_seen)
            {
              if (!parse_variables_const (lexer, dict,
                                          &regression.vars, &regression.n_vars,
                                          PV_NO_DUPLICATE | PV_NUMERIC))
                goto error;
            }
        }
      else if (lex_match_id (lexer, "STATISTICS"))
        {
          unsigned long statistics = 0;
          lex_match (lexer, T_EQUALS);

          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              if (lex_match (lexer, T_ALL))
                statistics = ~0;
              else if (lex_match_id (lexer, "DEFAULTS"))
                statistics |= STATS_DEFAULT;
              else if (lex_match_id (lexer, "R"))
                statistics |= STATS_R;
              else if (lex_match_id (lexer, "COEFF"))
                statistics |= STATS_COEFF;
              else if (lex_match_id (lexer, "ANOVA"))
                statistics |= STATS_ANOVA;
              else if (lex_match_id (lexer, "BCOV"))
                statistics |= STATS_BCOV;
              else if (lex_match_id (lexer, "TOL"))
                statistics |= STATS_TOL;
              else if (lex_match_id (lexer, "CI"))
                {
                  statistics |= STATS_CI;

                  if (lex_match (lexer, T_LPAREN))
                    {
                      if (!lex_force_num (lexer))
                        goto error;
                      regression.ci = lex_number (lexer) / 100.0;
                      lex_get (lexer);

                      if (!lex_force_match (lexer, T_RPAREN))
                        goto error;
                    }
                }
              else
                {
                  lex_error_expecting (lexer, "ALL", "DEFAULTS", "R", "COEFF",
                                       "ANOVA", "BCOV", "TOL", "CI");
                  goto error;
                }
            }

          if (statistics)
            regression.stats = statistics;
        }
      else if (lex_match_id (lexer, "SAVE"))
        {
          save_start = lex_ofs (lexer) - 1;
          lex_match (lexer, T_EQUALS);

          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "PRED"))
                regression.pred = true;
              else if (lex_match_id (lexer, "RESID"))
                regression.resid = true;
              else
                {
                  lex_error_expecting (lexer, "PRED", "RESID");
                  goto error;
                }
            }
          save_end = lex_ofs (lexer) - 1;
        }
      else
        {
          lex_error_expecting (lexer, "VARIABLES", "DEPENDENT", "ORIGIN",
                               "NOORIGIN", "METHOD", "STATISTICS", "SAVE");
          goto error;
        }
    }

  if (!regression.vars)
    dict_get_vars (dict, &regression.vars, &regression.n_vars, 0);

  struct regression_workspace workspace = {
    .res_idx = -1,
    .pred_idx = -1,
  };

  bool save = regression.pred || regression.resid;
  if (save)
    {
      struct caseproto *proto = caseproto_create ();

      if (regression.resid)
        {
          workspace.res_idx = workspace.extras ++;
          workspace.residvars = xcalloc (regression.n_dep_vars, sizeof (*workspace.residvars));

          for (size_t i = 0; i < regression.n_dep_vars; ++i)
            {
              workspace.residvars[i] = create_aux_var (ds, "RES");
              proto = caseproto_add_width (proto, 0);
            }
        }

      if (regression.pred)
        {
          workspace.pred_idx = workspace.extras ++;
          workspace.predvars = xcalloc (regression.n_dep_vars, sizeof (*workspace.predvars));

          for (size_t i = 0; i < regression.n_dep_vars; ++i)
            {
              workspace.predvars[i] = create_aux_var (ds, "PRED");
              proto = caseproto_add_width (proto, 0);
            }
        }

      if (proc_make_temporary_transformations_permanent (ds))
        lex_ofs_msg (lexer, SW, save_start, save_end,
                     _("REGRESSION with SAVE ignores TEMPORARY.  "
                       "Temporary transformations will be made permanent."));

      if (dict_get_filter (dict))
        lex_ofs_msg (lexer, SW, save_start, save_end,
                     _("REGRESSION with SAVE ignores FILTER.  "
                       "All cases will be processed."));

      workspace.writer = autopaging_writer_create (proto);
      caseproto_unref (proto);
    }

  struct casegrouper *grouper = casegrouper_create_splits (
    proc_open_filtering (ds, !save), dict);
  struct casereader *group;
  while (casegrouper_get_next_group (grouper, &group))
    {
      run_regression (&regression,
                      &workspace,
                      group);

    }
  bool ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  if (workspace.writer)
    {
      struct save_trans_data *save_trans_data = xmalloc (sizeof *save_trans_data);
      struct casereader *r = casewriter_make_reader (workspace.writer);
      workspace.writer = NULL;
      workspace.reader = r;
      save_trans_data->ws = xmalloc (sizeof (workspace));
      memcpy (save_trans_data->ws, &workspace, sizeof (workspace));
      save_trans_data->n_dep_vars = regression.n_dep_vars;

      static const struct trns_class trns_class = {
        .name = "REGRESSION",
        .execute = save_trans_func,
        .destroy = save_trans_free,
      };
      add_transformation (ds, &trns_class, save_trans_data);
    }

  free (regression.vars);
  free (regression.dep_vars);
  return ok ? CMD_SUCCESS : CMD_FAILURE;

error:
  free (regression.vars);
  free (regression.dep_vars);
  return CMD_FAILURE;
}

/* Return the size of the union of dependent and independent variables */
static size_t
get_n_all_vars (const struct regression *cmd)
{
  size_t result = cmd->n_vars + cmd->n_dep_vars;
  for (size_t i = 0; i < cmd->n_dep_vars; i++)
    for (size_t j = 0; j < cmd->n_vars; j++)
      if (cmd->vars[j] == cmd->dep_vars[i])
        result--;
  return result;
}

/* Fill VARS with the union of dependent and independent variables */
static void
fill_all_vars (const struct variable **vars, const struct regression *cmd)
{
  for (size_t i = 0; i < cmd->n_vars; i++)
    vars[i] = cmd->vars[i];

  size_t x = 0;
  for (size_t i = 0; i < cmd->n_dep_vars; i++)
    {
      bool absent = true;
      for (size_t j = 0; j < cmd->n_vars; j++)
        if (cmd->dep_vars[i] == cmd->vars[j])
          {
            absent = false;
            break;
          }
      if (absent)
        vars[cmd->n_vars + x++] = cmd->dep_vars[i];
    }
}


/* Fill the array VARS, with all the predictor variables from CMD, except
   variable X */
static void
fill_predictor_x (const struct variable **vars, const struct variable *x, const struct regression *cmd)
{
  size_t n = 0;
  for (size_t i = 0; i < cmd->n_vars; i++)
    if (cmd->vars[i] != x)
      vars[n++] = cmd->vars[i];
}

/*
  Is variable k the dependent variable?
*/
static bool
is_depvar (const struct regression *cmd, size_t k, const struct variable *v)
{
  return v == cmd->vars[k];
}

/* Identify the explanatory variables in v_variables.  Returns
   the number of independent variables. */
static int
identify_indep_vars (const struct regression *cmd,
                     const struct variable **indep_vars,
                     const struct variable *depvar)
{
  int n_indep_vars = 0;

  for (size_t i = 0; i < cmd->n_vars; i++)
    if (!is_depvar (cmd, i, depvar))
      indep_vars[n_indep_vars++] = cmd->vars[i];
  if (n_indep_vars < 1 && is_depvar (cmd, 0, depvar))
    {
      /*
         There is only one independent variable, and it is the same
         as the dependent variable. Print a warning and continue.
       */
      msg (SW,
           _("The dependent variable is equal to the independent variable. "
             "The least squares line is therefore Y=X. "
             "Standard errors and related statistics may be meaningless."));
      n_indep_vars = 1;
      indep_vars[0] = cmd->vars[0];
    }
  return n_indep_vars;
}

static double
fill_covariance (gsl_matrix * cov, struct covariance *all_cov,
                 const struct variable **vars,
                 size_t n_vars, const struct variable *dep_var,
                 const struct variable **all_vars, size_t n_all_vars,
                 double *means)
{
  const gsl_matrix *cm = covariance_calculate_unnormalized (all_cov);
  if (!cm)
    return 0;

  size_t *rows = xnmalloc (cov->size1 - 1, sizeof (*rows));

  size_t dep_subscript = SIZE_MAX;
  for (size_t i = 0; i < n_all_vars; i++)
    {
      for (size_t j = 0; j < n_vars; j++)
        if (vars[j] == all_vars[i])
          rows[j] = i;
      if (all_vars[i] == dep_var)
        dep_subscript = i;
    }
  assert (dep_subscript != SIZE_MAX);

  const gsl_matrix *mean_matrix = covariance_moments (all_cov, MOMENT_MEAN);
  const gsl_matrix *ssize_matrix = covariance_moments (all_cov, MOMENT_NONE);
  for (size_t i = 0; i < cov->size1 - 1; i++)
    {
      means[i] = gsl_matrix_get (mean_matrix, rows[i], 0)
        / gsl_matrix_get (ssize_matrix, rows[i], 0);
      for (size_t j = 0; j < cov->size2 - 1; j++)
        {
          gsl_matrix_set (cov, i, j, gsl_matrix_get (cm, rows[i], rows[j]));
          gsl_matrix_set (cov, j, i, gsl_matrix_get (cm, rows[j], rows[i]));
        }
    }
  means[cov->size1 - 1] = gsl_matrix_get (mean_matrix, dep_subscript, 0)
    / gsl_matrix_get (ssize_matrix, dep_subscript, 0);
  const gsl_matrix *ssizes = covariance_moments (all_cov, MOMENT_NONE);
  double result = gsl_matrix_get (ssizes, dep_subscript, rows[0]);
  for (size_t i = 0; i < cov->size1 - 1; i++)
    {
      gsl_matrix_set (cov, i, cov->size1 - 1,
                      gsl_matrix_get (cm, rows[i], dep_subscript));
      gsl_matrix_set (cov, cov->size1 - 1, i,
                      gsl_matrix_get (cm, rows[i], dep_subscript));
      if (result > gsl_matrix_get (ssizes, rows[i], dep_subscript))
        result = gsl_matrix_get (ssizes, rows[i], dep_subscript);
    }
  gsl_matrix_set (cov, cov->size1 - 1, cov->size1 - 1,
                  gsl_matrix_get (cm, dep_subscript, dep_subscript));
  free (rows);
  return result;
}



struct model_container
{
  struct linreg **models;
};

/*
  STATISTICS subcommand output functions.
*/
static void reg_stats_r (const struct linreg *,     const struct variable *);
static void reg_stats_coeff (const struct regression *, const struct linreg *,
                             const struct model_container *, const gsl_matrix *,
                             const struct variable *);
static void reg_stats_anova (const struct linreg *, const struct variable *);
static void reg_stats_bcov (const struct linreg *,  const struct variable *);


static struct linreg **
run_regression_get_models (const struct regression *cmd,
                           struct casereader *input,
                           bool output)
{
  struct model_container *model_container = XCALLOC (cmd->n_vars, struct model_container);

  struct ccase *c;
  struct covariance *cov;
  struct casereader *reader;

  if (cmd->stats & STATS_TOL)
    for (size_t i = 0; i < cmd->n_vars; i++)
      {
        struct regression subreg = {
          .origin = cmd->origin,
          .ds = cmd->ds,
          .n_vars = cmd->n_vars - 1,
          .n_dep_vars = 1,
          .vars = xmalloc ((cmd->n_vars - 1) * sizeof *subreg.vars),
          .dep_vars = &cmd->vars[i],
          .stats = STATS_R,
          .ci = 0,
          .resid = false,
          .pred = false,
        };
        fill_predictor_x (subreg.vars, cmd->vars[i], cmd);

        model_container[i].models =
          run_regression_get_models (&subreg, input, false);
        free (subreg.vars);
      }

  size_t n_all_vars = get_n_all_vars (cmd);
  const struct variable **all_vars = xnmalloc (n_all_vars, sizeof (*all_vars));

  /* In the (rather pointless) case where the dependent variable is
     the independent variable, n_all_vars == 1.
     However this would result in a buffer overflow so we must
     over-allocate the space required in this malloc call.
     See bug #58599  */
  double *means = xnmalloc (MAX (2, n_all_vars), sizeof *means);
  fill_all_vars (all_vars, cmd);
  cov = covariance_1pass_create (n_all_vars, all_vars,
                                 dict_get_weight (dataset_dict (cmd->ds)),
                                 MV_ANY, cmd->origin == false);

  reader = casereader_clone (input);
  reader = casereader_create_filter_missing (reader, all_vars, n_all_vars,
                                             MV_ANY, NULL, NULL);

  struct casereader *r = casereader_clone (reader);
  for (; (c = casereader_read (r)) != NULL; case_unref (c))
      covariance_accumulate (cov, c);
  casereader_destroy (r);

  struct linreg **models = XCALLOC (cmd->n_dep_vars, struct linreg*);
  for (size_t k = 0; k < cmd->n_dep_vars; k++)
    {
      const struct variable **vars = xnmalloc (cmd->n_vars, sizeof *vars);
      const struct variable *dep_var = cmd->dep_vars[k];
      int n_indep = identify_indep_vars (cmd, vars, dep_var);
      gsl_matrix *cov_matrix = gsl_matrix_alloc (n_indep + 1, n_indep + 1);
      double n_data = fill_covariance (cov_matrix, cov, vars, n_indep,
                                       dep_var, all_vars, n_all_vars, means);
      models[k] = linreg_alloc (dep_var, vars,  n_data, n_indep, cmd->origin);
      for (size_t i = 0; i < n_indep; i++)
        linreg_set_indep_variable_mean (models[k], i, means[i]);
      linreg_set_depvar_mean (models[k], means[n_indep]);
      if (n_data > 0)
        {
          linreg_fit (cov_matrix, models[k]);

          if (output
              && !taint_has_tainted_successor (casereader_get_taint (input)))
            {
              /*
                Find the least-squares estimates and other statistics.
              */
              if (cmd->stats & STATS_R)
                reg_stats_r (models[k], dep_var);

              if (cmd->stats & STATS_ANOVA)
                reg_stats_anova (models[k], dep_var);

              if (cmd->stats & STATS_COEFF)
                reg_stats_coeff (cmd, models[k],
                                 model_container,
                                 cov_matrix, dep_var);

              if (cmd->stats & STATS_BCOV)
                reg_stats_bcov  (models[k], dep_var);
            }
        }
      else
        msg (SE, _("No valid data found. This command was skipped."));
      free (vars);
      gsl_matrix_free (cov_matrix);
    }

  casereader_destroy (reader);

  for (size_t i = 0; i < cmd->n_vars; i++)
    {
      if (model_container[i].models)
        linreg_unref (model_container[i].models[0]);
      free (model_container[i].models);
    }
  free (model_container);

  free (all_vars);
  free (means);
  covariance_destroy (cov);
  return models;
}

static void
run_regression (const struct regression *cmd,
                struct regression_workspace *ws,
                struct casereader *input)
{
  struct linreg **models = run_regression_get_models (cmd, input, true);

  if (ws->extras > 0)
    {
      struct ccase *c;
      struct casereader *r = casereader_clone (input);

      for (; (c = casereader_read (r)) != NULL; case_unref (c))
        {
          struct ccase *outc = case_create (casewriter_get_proto (ws->writer));
          for (int k = 0; k < cmd->n_dep_vars; k++)
            {
              const struct variable **vars = xnmalloc (cmd->n_vars, sizeof (*vars));
              const struct variable *dep_var = cmd->dep_vars[k];
              int n_indep = identify_indep_vars (cmd, vars, dep_var);
              double *vals = xnmalloc (n_indep, sizeof (*vals));
              for (int i = 0; i < n_indep; i++)
                {
                  const union value *tmp = case_data (c, vars[i]);
                  vals[i] = tmp->f;
                }

              if (cmd->pred)
                {
                  double pred = linreg_predict (models[k], vals, n_indep);
                  *case_num_rw_idx (outc, k * ws->extras + ws->pred_idx) = pred;
                }

              if (cmd->resid)
                {
                  double obs = case_num (c, linreg_dep_var (models[k]));
                  double res = linreg_residual (models[k], obs,  vals, n_indep);
                  *case_num_rw_idx (outc, k * ws->extras + ws->res_idx) = res;
                }
              free (vals);
              free (vars);
            }
          casewriter_write (ws->writer, outc);
        }
      casereader_destroy (r);
    }

  for (size_t k = 0; k < cmd->n_dep_vars; k++)
    linreg_unref (models[k]);

  free (models);
  casereader_destroy (input);
}




static void
reg_stats_r (const struct linreg * c, const struct variable *var)
{
  struct pivot_table *table = pivot_table_create__ (
    pivot_value_new_text_format (N_("Model Summary (%s)"),
                                 var_to_string (var)),
    "Model Summary");

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("R"), N_("R Square"), N_("Adjusted R Square"),
                          N_("Std. Error of the Estimate"));

  double rsq = linreg_ssreg (c) / linreg_sst (c);
  double adjrsq = (rsq -
                   (1.0 - rsq) * linreg_n_coeffs (c)
                   / (linreg_n_obs (c) - linreg_n_coeffs (c) - 1));
  double std_error = sqrt (linreg_mse (c));

  double entries[] = {
    sqrt (rsq), rsq, adjrsq, std_error
  };
  for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
    pivot_table_put1 (table, i, pivot_value_new_number (entries[i]));

  pivot_table_submit (table);
}

/*
  Table showing estimated regression coefficients.
*/
static void
reg_stats_coeff (const struct regression *cmd, const struct linreg *c,
                 const struct model_container *mc, const gsl_matrix *cov,
                 const struct variable *var)
{
  struct pivot_table *table = pivot_table_create__ (
    pivot_value_new_text_format (N_("Coefficients (%s)"), var_to_string (var)),
    "Coefficients");

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Statistics"));
  pivot_category_create_group (statistics->root,
                               N_("Unstandardized Coefficients"),
                               N_("B"), N_("Std. Error"));
  pivot_category_create_group (statistics->root,
                               N_("Standardized Coefficients"), N_("Beta"));
  pivot_category_create_leaves (statistics->root, N_("t"),
                                N_("Sig."), PIVOT_RC_SIGNIFICANCE);
  if (cmd->stats & STATS_CI)
    {
      struct pivot_category *interval = pivot_category_create_group__ (
        statistics->root, pivot_value_new_text_format (
          N_("%g%% Confidence Interval for B"),
          cmd->ci * 100.0));
      pivot_category_create_leaves (interval, N_("Lower Bound"),
                                    N_("Upper Bound"));
    }

  if (cmd->stats & STATS_TOL)
    pivot_category_create_group (statistics->root,
                                 N_("Collinearity Statistics"),
                                 N_("Tolerance"), N_("VIF"));


  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));

  double df = linreg_n_obs (c) - linreg_n_coeffs (c) - 1;
  double q = (1 - cmd->ci) / 2.0;  /* 2-tailed test */
  double tval = gsl_cdf_tdist_Qinv (q, df);

  if (!cmd->origin)
    {
      int var_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_text (N_("(Constant)")));

      double std_err = sqrt (gsl_matrix_get (linreg_cov (c), 0, 0));
      double t_stat = linreg_intercept (c) / std_err;
      double base_entries[] = {
        linreg_intercept (c),
        std_err,
        0.0,
        t_stat,
        2.0 * gsl_cdf_tdist_Q (fabs (t_stat),
                               linreg_n_obs (c) - linreg_n_coeffs (c)),
      };

      size_t col = 0;
      for (size_t i = 0; i < sizeof base_entries / sizeof *base_entries; i++)
        pivot_table_put2 (table, col++, var_idx,
                          pivot_value_new_number (base_entries[i]));

      if (cmd->stats & STATS_CI)
        {
          double interval_entries[] = {
            linreg_intercept (c) - tval * std_err,
            linreg_intercept (c) + tval * std_err,
          };

          for (size_t i = 0; i < sizeof interval_entries / sizeof *interval_entries; i++)
            pivot_table_put2 (table, col++, var_idx,
                              pivot_value_new_number (interval_entries[i]));
        }
    }

  for (size_t j = 0; j < linreg_n_coeffs (c); j++)
    {
      const struct variable *v = linreg_indep_var (c, j);
      int var_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (v));

      double std_err = sqrt (gsl_matrix_get (linreg_cov (c), j + 1, j + 1));
      double t_stat = linreg_coeff (c, j) / std_err;
      double base_entries[] = {
        linreg_coeff (c, j),
        sqrt (gsl_matrix_get (linreg_cov (c), j + 1, j + 1)),
        (sqrt (gsl_matrix_get (cov, j, j)) * linreg_coeff (c, j) /
         sqrt (gsl_matrix_get (cov, cov->size1 - 1, cov->size2 - 1))),
        t_stat,
        2 * gsl_cdf_tdist_Q (fabs (t_stat), df)
      };

      size_t col = 0;
      for (size_t i = 0; i < sizeof base_entries / sizeof *base_entries; i++)
        pivot_table_put2 (table, col++, var_idx,
                          pivot_value_new_number (base_entries[i]));

      if (cmd->stats & STATS_CI)
        {
          double interval_entries[] = {
            linreg_coeff (c, j)  - tval * std_err,
            linreg_coeff (c, j)  + tval * std_err,
          };


          for (size_t i = 0; i < sizeof interval_entries / sizeof *interval_entries; i++)
            pivot_table_put2 (table, col++, var_idx,
                              pivot_value_new_number (interval_entries[i]));
        }

      if (cmd->stats & STATS_TOL)
        {
          {
            struct linreg *m = mc[j].models[0];
            double rsq = linreg_ssreg (m) / linreg_sst (m);
            pivot_table_put2 (table, col++, var_idx, pivot_value_new_number (1.0 - rsq));
            pivot_table_put2 (table, col++, var_idx, pivot_value_new_number (1.0 / (1.0 - rsq)));
          }
        }
    }

  pivot_table_submit (table);
}

/*
  Display the ANOVA table.
*/
static void
reg_stats_anova (const struct linreg * c, const struct variable *var)
{
  struct pivot_table *table = pivot_table_create__ (
    pivot_value_new_text_format (N_("ANOVA (%s)"), var_to_string (var)),
    "ANOVA");

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("Sum of Squares"), PIVOT_RC_OTHER,
                          N_("df"), PIVOT_RC_INTEGER,
                          N_("Mean Square"), PIVOT_RC_OTHER,
                          N_("F"), PIVOT_RC_OTHER,
                          N_("Sig."), PIVOT_RC_SIGNIFICANCE);

  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Source"),
                          N_("Regression"), N_("Residual"), N_("Total"));

  double msm = linreg_ssreg (c) / linreg_dfmodel (c);
  double mse = linreg_mse (c);
  double F = msm / mse;

  struct entry
    {
      int stat_idx;
      int source_idx;
      double x;
    }
  entries[] = {
    /* Sums of Squares. */
    { 0, 0, linreg_ssreg (c) },
    { 0, 1, linreg_sse (c) },
    { 0, 2, linreg_sst (c) },
    /* Degrees of freedom. */
    { 1, 0, linreg_dfmodel (c) },
    { 1, 1, linreg_dferror (c) },
    { 1, 2, linreg_dftotal (c) },
    /* Mean Squares. */
    { 2, 0, msm },
    { 2, 1, mse },
    /* F */
    { 3, 0, F },
    /* Significance. */
    { 4, 0, gsl_cdf_fdist_Q (F, linreg_dfmodel (c), linreg_dferror (c)) },
  };
  for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
    {
      const struct entry *e = &entries[i];
      pivot_table_put2 (table, e->stat_idx, e->source_idx,
                        pivot_value_new_number (e->x));
    }

  pivot_table_submit (table);
}


static void
reg_stats_bcov (const struct linreg * c, const struct variable *var)
{
  struct pivot_table *table = pivot_table_create__ (
    pivot_value_new_text_format (N_("Coefficient Correlations (%s)"),
                                 var_to_string (var)),
    "Coefficient Correlations");

  for (size_t i = 0; i < 2; i++)
    {
      struct pivot_dimension *models = pivot_dimension_create (
        table, i ? PIVOT_AXIS_ROW : PIVOT_AXIS_COLUMN, N_("Models"));
      for (size_t j = 0; j < linreg_n_coeffs (c); j++)
        pivot_category_create_leaf (
          models->root, pivot_value_new_variable (
            linreg_indep_var (c, j)));
    }

  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Statistics"),
                          N_("Covariances"));

  for (size_t i = 0; i < linreg_n_coeffs (c); i++)
    for (size_t k = 0; k < linreg_n_coeffs (c); k++)
      {
        double cov = gsl_matrix_get (linreg_cov (c), MIN (i, k), MAX (i, k));
        pivot_table_put3 (table, k, i, 0, pivot_value_new_number (cov));
      }

  pivot_table_submit (table);
}
