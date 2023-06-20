/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include <gsl/gsl_cdf.h>
#include <gsl/gsl_matrix.h>
#include <math.h>

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/commands/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "math/correlation.h"
#include "math/covariance.h"
#include "math/moments.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"
#include "gl/minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


struct corr
  {
    size_t n_vars_total;
    size_t n_vars1;

    const struct variable **vars;
  };


/* Handling of missing values. */
enum corr_missing_type
  {
    CORR_PAIRWISE,    /* Handle missing values on a per-variable-pair basis. */
    CORR_LISTWISE     /* Discard entire case if any variable is missing. */
  };

struct corr_opts
{
  enum corr_missing_type missing_type;
  enum mv_class exclude;      /* Classes of missing values to exclude. */

  bool sig;   /* Flag significant values or not */
  int tails;  /* Report significance with how many tails ? */
  bool descriptive_stats;
  bool xprod_stats;

  const struct variable *wv;  /* The weight variable (if any) */
};


static void
output_descriptives (const struct corr *corr, const struct corr_opts *opts,
                     const gsl_matrix *means,
                     const gsl_matrix *vars, const gsl_matrix *ns)
{
  struct pivot_table *table = pivot_table_create (
    N_("Descriptive Statistics"));
  pivot_table_set_weight_var (table, opts->wv);

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("Mean"), PIVOT_RC_OTHER,
                          N_("Std. Deviation"), PIVOT_RC_OTHER,
                          N_("N"), PIVOT_RC_COUNT);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable"));

  for (size_t r = 0; r < corr->n_vars_total; ++r)
    {
      const struct variable *v = corr->vars[r];

      int row = pivot_category_create_leaf (variables->root,
                                            pivot_value_new_variable (v));

      double mean = gsl_matrix_get (means, r, 0);
      /* Here we want to display the non-biased estimator */
      double n = gsl_matrix_get (ns, r, 0);
      double stddev = sqrt (gsl_matrix_get (vars, r, 0) * n / (n - 1));
      double entries[] = { mean, stddev, n };
      for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
        pivot_table_put2 (table, i, row, pivot_value_new_number (entries[i]));
    }

  pivot_table_submit (table);
}

static void
output_correlation (const struct corr *corr, const struct corr_opts *opts,
                    const gsl_matrix *cm, const gsl_matrix *samples,
                    const gsl_matrix *cv)
{
  struct pivot_table *table = pivot_table_create (N_("Correlations"));
  pivot_table_set_weight_var (table, opts->wv);

  /* Column variable dimension. */
  struct pivot_dimension *columns = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Variables"));

  size_t matrix_cols = (corr->n_vars_total > corr->n_vars1
                        ? corr->n_vars_total - corr->n_vars1
                        : corr->n_vars1);
  for (size_t c = 0; c < matrix_cols; c++)
    {
      const struct variable *v = corr->n_vars_total > corr->n_vars1 ?
        corr->vars[corr->n_vars1 + c] : corr->vars[c];
      pivot_category_create_leaf (columns->root, pivot_value_new_variable (v));
    }

  /* Statistics dimension. */
  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Statistics"),
    N_("Pearson Correlation"), PIVOT_RC_CORRELATION,
    opts->tails == 2 ? N_("Sig. (2-tailed)") : N_("Sig. (1-tailed)"),
    PIVOT_RC_SIGNIFICANCE);

  if (opts->xprod_stats)
    pivot_category_create_leaves (statistics->root, N_("Cross-products"),
                                  N_("Covariance"));

  if (opts->missing_type != CORR_LISTWISE)
    pivot_category_create_leaves (statistics->root, N_("N"), PIVOT_RC_COUNT);

  /* Row variable dimension. */
  struct pivot_dimension *rows = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));
  for (size_t r = 0; r < corr->n_vars1; r++)
    pivot_category_create_leaf (rows->root,
                                pivot_value_new_variable (corr->vars[r]));

  struct pivot_footnote *sig_footnote = pivot_table_create_footnote (
    table, pivot_value_new_text (N_("Significant at .05 level")));

  for (size_t r = 0; r < corr->n_vars1; r++)
    for (size_t c = 0; c < matrix_cols; c++)
      {
        const int col_index = (corr->n_vars_total > corr->n_vars1
                               ? corr->n_vars1 + c
                               : c);
        double pearson = gsl_matrix_get (cm, r, col_index);
        double w = gsl_matrix_get (samples, r, col_index);
        double sig = opts->tails * significance_of_correlation (pearson, w);

        double entries[5];
        int n = 0;
        entries[n++] = pearson;
        entries[n++] = col_index != r ? sig : SYSMIS;
        if (opts->xprod_stats)
          {
            double cov = gsl_matrix_get (cv, r, col_index);
            const double xprod_dev = cov * w;
            cov *= w / (w - 1.0);

            entries[n++] = xprod_dev;
            entries[n++] = cov;
          }
        if (opts->missing_type != CORR_LISTWISE)
          entries[n++] = w;

        for (int i = 0; i < n; i++)
          if (entries[i] != SYSMIS)
            {
              struct pivot_value *v = pivot_value_new_number (entries[i]);
              if (!i && opts->sig && col_index != r && sig < 0.05)
                pivot_value_add_footnote (v, sig_footnote);
              pivot_table_put3 (table, c, i, r, v);
            }
      }

  pivot_table_submit (table);
}


static void
run_corr (struct casereader *r, const struct corr_opts *opts, const struct corr *corr)
{
  struct covariance *cov = covariance_2pass_create (
    corr->n_vars_total, corr->vars, NULL,opts->wv, opts->exclude, true);

  struct casereader *rc = casereader_clone (r);
  struct ccase *c;
  for (; (c = casereader_read (r)); case_unref (c))
    covariance_accumulate_pass1 (cov, c);
  for (; (c = casereader_read (rc)); case_unref (c))
    covariance_accumulate_pass2 (cov, c);
  casereader_destroy (rc);

  gsl_matrix *cov_matrix = covariance_calculate (cov);
  if (!cov_matrix)
    {
      msg (SE, _("The data for the chosen variables are all missing or empty."));
      covariance_destroy (cov);
      return;
    }

  const gsl_matrix *samples_matrix = covariance_moments (cov, MOMENT_NONE);
  const gsl_matrix *var_matrix = covariance_moments (cov, MOMENT_VARIANCE);
  const gsl_matrix *mean_matrix = covariance_moments (cov, MOMENT_MEAN);

  gsl_matrix *corr_matrix = correlation_from_covariance (cov_matrix, var_matrix);

  if (opts->descriptive_stats)
    output_descriptives (corr, opts, mean_matrix, var_matrix, samples_matrix);

  output_correlation (corr, opts, corr_matrix, samples_matrix, cov_matrix);

  covariance_destroy (cov);
  gsl_matrix_free (corr_matrix);
  gsl_matrix_free (cov_matrix);
}

int
cmd_correlations (struct lexer *lexer, struct dataset *ds)
{
  size_t n_all_vars = 0; /* Total number of variables involved in this command */
  const struct dictionary *dict = dataset_dict (ds);

  struct corr *corrs = NULL;
  size_t n_corrs = 0;
  size_t allocated_corrs = 0;

  struct corr_opts opts = {
    .missing_type = CORR_PAIRWISE,
    .wv = dict_get_weight (dict),
    .tails = 2,
    .exclude = MV_ANY,
  };

  /* Parse CORRELATIONS. */
  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);
      if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "PAIRWISE"))
                opts.missing_type = CORR_PAIRWISE;
              else if (lex_match_id (lexer, "LISTWISE"))
                opts.missing_type = CORR_LISTWISE;
              else if (lex_match_id (lexer, "INCLUDE"))
                opts.exclude = MV_SYSTEM;
              else if (lex_match_id (lexer, "EXCLUDE"))
                opts.exclude = MV_ANY;
              else
                {
                  lex_error_expecting (lexer, "PAIRWISE", "LISTWISE",
                                       "INCLUDE", "EXCLUDE");
                  goto error;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "PRINT"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "TWOTAIL"))
                opts.tails = 2;
              else if (lex_match_id (lexer, "ONETAIL"))
                opts.tails = 1;
              else if (lex_match_id (lexer, "SIG"))
                opts.sig = false;
              else if (lex_match_id (lexer, "NOSIG"))
                opts.sig = true;
              else
                {
                  lex_error_expecting (lexer, "TWOTAIL", "ONETAIL",
                                       "SIG", "NOSIG");
                  goto error;
                }

              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "STATISTICS"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "DESCRIPTIVES"))
                opts.descriptive_stats = true;
              else if (lex_match_id (lexer, "XPROD"))
                opts.xprod_stats = true;
              else if (lex_token (lexer) == T_ALL)
                {
                  opts.descriptive_stats = opts.xprod_stats = true;
                  lex_get (lexer);
                }
              else
                {
                  lex_error_expecting (lexer, "DESCRIPTIVES", "XPROD", "ALL");
                  goto error;
                }

              lex_match (lexer, T_COMMA);
            }
        }
      else
        {
          if (lex_match_id (lexer, "VARIABLES"))
            lex_match (lexer, T_EQUALS);

          const struct variable **vars;
          size_t n_vars1;
          if (!parse_variables_const (lexer, dict, &vars, &n_vars1, PV_NUMERIC))
            goto error;

          size_t n_vars_total = n_vars1;
          if (lex_match (lexer, T_WITH)
              && !parse_variables_const (lexer, dict, &vars, &n_vars_total,
                                         PV_NUMERIC | PV_APPEND))
            goto error;

          if (n_corrs >= allocated_corrs)
            corrs = x2nrealloc (corrs, &allocated_corrs, sizeof *corrs);
          corrs[n_corrs++] = (struct corr) {
            .n_vars1 = n_vars1,
            .n_vars_total = n_vars_total,
            .vars = vars,
          };

          n_all_vars += n_vars_total;
        }
    }
  if (n_corrs == 0)
    {
      lex_ofs_error (lexer, 0, lex_ofs (lexer) - 1,
                     _("No variables specified."));
      goto error;
    }

  const struct variable **all_vars = xmalloc (n_all_vars * sizeof *all_vars);
  const struct variable **vv = all_vars;
  for (size_t i = 0; i < n_corrs; ++i)
    {
      const struct corr *c = &corrs[i];
      for (size_t v = 0; v < c->n_vars_total; ++v)
        *vv++ = c->vars[v];
    }

  struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds), dict);
  struct casereader *group;
  while (casegrouper_get_next_group (grouper, &group))
    {
      for (size_t i = 0; i < n_corrs; ++i)
        {
          /* FIXME: No need to iterate the data multiple times */
          struct casereader *r = casereader_clone (group);

          if (opts.missing_type == CORR_LISTWISE)
            r = casereader_create_filter_missing (r, all_vars, n_all_vars,
                                                  opts.exclude, NULL, NULL);


          run_corr (r, &opts, &corrs[i]);
          casereader_destroy (r);
        }
      casereader_destroy (group);
    }
  bool ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  free (all_vars);

  /* Done. */
  for (size_t i = 0; i < n_corrs; i++)
    free (corrs[i].vars);
  free (corrs);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

error:
  for (size_t i = 0; i < n_corrs; i++)
    free (corrs[i].vars);
  free (corrs);
  return CMD_FAILURE;
}
