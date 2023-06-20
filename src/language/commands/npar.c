/* PSPP - a program for statistical analysis. -*-c-*-
   Copyright (C) 2006, 2008, 2009, 2010, 2011, 2016 Free Software Foundation, Inc.

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

#include "language/commands/npar.h"

#include <stdlib.h>
#include <math.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/settings.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "language/commands/binomial.h"
#include "language/commands/chisquare.h"
#include "language/commands/ks-one-sample.h"
#include "language/commands/cochran.h"
#include "language/commands/friedman.h"
#include "language/commands/jonckheere-terpstra.h"
#include "language/commands/kruskal-wallis.h"
#include "language/commands/mann-whitney.h"
#include "language/commands/mcnemar.h"
#include "language/commands/median.h"
#include "language/commands/npar-summary.h"
#include "language/commands/runs.h"
#include "language/commands/sign.h"
#include "language/commands/wilcoxon.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmapx.h"
#include "libpspp/message.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/taint.h"
#include "math/moments.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* NPAR TESTS structure. */
struct npar_specs
{
  struct pool *pool;
  struct npar_test **test;
  size_t n_tests;

  const struct variable **vv; /* Compendium of all variables
                                  (those mentioned on ANY subcommand */
  int n_vars; /* Number of variables in vv */

  enum mv_class filter;    /* Missing values to filter. */
  bool listwise_missing;

  bool descriptives;       /* Descriptive statistics should be calculated */
  bool quartiles;          /* Quartiles should be calculated */

  bool exact;  /* Whether exact calculations have been requested */
  double timer;   /* Maximum time (in minutes) to wait for exact calculations */
};


/* Prototype for custom subcommands of NPAR TESTS. */
static bool npar_chisquare (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_binomial (struct lexer *, struct dataset *,  struct npar_specs *);
static bool npar_ks_one_sample (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_runs (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_friedman (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_kendall (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_cochran (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_wilcoxon (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_sign (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_kruskal_wallis (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_jonckheere_terpstra (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_mann_whitney (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_mcnemar (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_median (struct lexer *, struct dataset *, struct npar_specs *);
static bool npar_method (struct lexer *, struct npar_specs *);

/* Command parsing functions. */

static int
parse_npar_tests (struct lexer *lexer, struct dataset *ds,
                  struct npar_specs *nps)
{
  bool seen_missing = false;
  bool seen_method = false;
  lex_match (lexer, T_SLASH);
  do
    {
      if (lex_match_id (lexer, "COCHRAN"))
        {
          if (!npar_cochran (lexer, ds, nps))
            return false;
        }
      else if (lex_match_id (lexer, "FRIEDMAN"))
        {
          if (!npar_friedman (lexer, ds, nps))
            return false;
        }
      else if (lex_match_id (lexer, "KENDALL"))
        {
          if (!npar_kendall (lexer, ds, nps))
            return false;
        }
      else if (lex_match_id (lexer, "RUNS"))
        {
          if (!npar_runs (lexer, ds, nps))
            return false;
        }
      else if (lex_match_id (lexer, "CHISQUARE"))
        {
          lex_match (lexer, T_EQUALS);
          if (!npar_chisquare (lexer, ds, nps))
            return false;
        }
      else if (lex_match_id (lexer, "BINOMIAL"))
        {
          lex_match (lexer, T_EQUALS);
          if (!npar_binomial (lexer, ds, nps))
            return false;
        }
      else if (lex_match_phrase (lexer, "K-S") ||
               lex_match_phrase (lexer, "KOLMOGOROV-SMIRNOV"))
        {
          lex_match (lexer, T_EQUALS);
          if (!npar_ks_one_sample (lexer, ds, nps))
            return false;
        }
      else if (lex_match_phrase (lexer, "J-T") ||
               lex_match_phrase (lexer, "JONCKHEERE-TERPSTRA"))
        {
          lex_match (lexer, T_EQUALS);
          if (!npar_jonckheere_terpstra (lexer, ds, nps))
            return false;
        }
      else if (lex_match_phrase (lexer, "K-W") ||
               lex_match_phrase (lexer, "KRUSKAL-WALLIS"))
        {
          lex_match (lexer, T_EQUALS);
          if (!npar_kruskal_wallis (lexer, ds, nps))
            return false;
        }
      else if (lex_match_phrase (lexer, "MCNEMAR"))
        {
          lex_match (lexer, T_EQUALS);
          if (!npar_mcnemar (lexer, ds, nps))
            return false;
        }
      else if (lex_match_phrase (lexer, "M-W") ||
               lex_match_phrase (lexer, "MANN-WHITNEY"))
        {
          lex_match (lexer, T_EQUALS);
          if (!npar_mann_whitney (lexer, ds, nps))
            return false;
        }
      else if (lex_match_phrase (lexer, "MEDIAN"))
        {
          if (!npar_median (lexer, ds, nps))
            return false;
        }
      else if (lex_match_id (lexer, "WILCOXON"))
        {
          lex_match (lexer, T_EQUALS);
          if (!npar_wilcoxon (lexer, ds, nps))
            return false;
        }
      else if (lex_match_id (lexer, "SIGN"))
        {
          lex_match (lexer, T_EQUALS);
          if (!npar_sign (lexer, ds, nps))
            return false;
        }
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          if (seen_missing)
            {
              lex_sbc_only_once (lexer, "MISSING");
              return false;
            }
          seen_missing = true;
          while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
            {
              if (lex_match_id (lexer, "ANALYSIS"))
                nps->listwise_missing = false;
              else if (lex_match_id (lexer, "LISTWISE"))
                nps->listwise_missing = true;
              else if (lex_match_id (lexer, "INCLUDE"))
                nps->filter = MV_SYSTEM;
              else if (lex_match_id (lexer, "EXCLUDE"))
                nps->filter = MV_ANY;
              else
                {
                  lex_error_expecting (lexer, "ANALYSIS", "LISTWISE",
                                       "INCLUDE", "EXCLUDE");
                  return false;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "METHOD"))
        {
          lex_match (lexer, T_EQUALS);
          if (seen_method)
            {
              lex_sbc_only_once (lexer, "METHOD");
              return false;
            }
          seen_method = true;
          if (!npar_method (lexer, nps))
            return false;
        }
      else if (lex_match_id (lexer, "STATISTICS"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
            {
              if (lex_match_id (lexer, "DESCRIPTIVES"))
                nps->descriptives = true;
              else if (lex_match_id (lexer, "QUARTILES"))
                nps->quartiles = true;
              else if (lex_match (lexer, T_ALL))
                nps->descriptives = nps->quartiles = true;
              else
                {
                  lex_error_expecting (lexer, "DESCRIPTIVES", "QUARTILES",
                                       "ALL");
                  return false;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "ALGORITHM"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "COMPATIBLE"))
            settings_set_cmd_algorithm (COMPATIBLE);
          else if (lex_match_id (lexer, "ENHANCED"))
            settings_set_cmd_algorithm (ENHANCED);
          else
            {
              lex_error_expecting (lexer, "COMPATIBLE", "ENHANCED");
              return false;
            }
        }
      else
        {
          lex_error_expecting (lexer, "COCHRAN", "FRIEDMAN", "KENDALL", "RUNS",
                               "CHISQUARE", "BINOMIAL", "K-S", "J-T", "K-W",
                               "MCNEMAR", "M-W", "MEDIAN", "WILCOXON",
                               "SIGN", "MISSING", "METHOD", "STATISTICS",
                               "ALGORITHM");
          return false;
        }
    }
  while (lex_match (lexer, T_SLASH));

  return true;
}

static void one_sample_insert_variables (const struct npar_test *test,
                                         struct hmapx *);

static void two_sample_insert_variables (const struct npar_test *test,
                                         struct hmapx *);

static void n_sample_insert_variables (const struct npar_test *test,
                                       struct hmapx *);

static void
npar_execute (struct casereader *input,
             const struct npar_specs *specs,
             const struct dataset *ds)
{
  struct descriptives *summary_descriptives = NULL;

  for (size_t t = 0; t < specs->n_tests; ++t)
    {
      const struct npar_test *test = specs->test[t];
      test->execute (ds, casereader_clone (input), specs->filter,
                     test, specs->exact, specs->timer);
    }

  if (specs->descriptives && specs->n_vars > 0)
    {
      summary_descriptives = xnmalloc (sizeof (*summary_descriptives),
                                       specs->n_vars);

      npar_summary_calc_descriptives (summary_descriptives,
                                      casereader_clone (input),
                                      dataset_dict (ds),
                                      specs->vv, specs->n_vars,
                                      specs->filter);
    }

  if ((specs->descriptives || specs->quartiles)
       && !taint_has_tainted_successor (casereader_get_taint (input)))
    do_summary_box (summary_descriptives, specs->vv, specs->n_vars,
                    dict_get_weight_format (dataset_dict (ds)));

  free (summary_descriptives);
  casereader_destroy (input);
}

int
cmd_npar_tests (struct lexer *lexer, struct dataset *ds)
{
  struct npar_specs npar_specs = {
    .pool = pool_create (),
    .filter = MV_ANY,
    .listwise_missing = false,
  };

  if (!parse_npar_tests (lexer, ds, &npar_specs))
    {
      pool_destroy (npar_specs.pool);
      return CMD_FAILURE;
    }

  struct hmapx var_map = HMAPX_INITIALIZER (var_map);
  for (size_t i = 0; i < npar_specs.n_tests; ++i)
    {
      const struct npar_test *test = npar_specs.test[i];
      test->insert_variables (test, &var_map);
    }

  struct hmapx_node *node;
  struct variable *var;
  npar_specs.vv = pool_alloc (npar_specs.pool,
                              hmapx_count (&var_map) * sizeof *npar_specs.vv);
  HMAPX_FOR_EACH (var, node, &var_map)
    npar_specs.vv[npar_specs.n_vars++] = var;
  assert (npar_specs.n_vars == hmapx_count (&var_map));

  sort (npar_specs.vv, npar_specs.n_vars, sizeof *npar_specs.vv,
         compare_var_ptrs_by_name, NULL);

  struct casereader *input = proc_open (ds);
  if (npar_specs.listwise_missing)
    input = casereader_create_filter_missing (input,
                                              npar_specs.vv,
                                              npar_specs.n_vars,
                                              npar_specs.filter,
                                              NULL, NULL);

  struct casegrouper *grouper = casegrouper_create_splits (input, dataset_dict (ds));
  struct casereader *group;
  while (casegrouper_get_next_group (grouper, &group))
    npar_execute (group, &npar_specs, ds);
  bool ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  pool_destroy (npar_specs.pool);
  hmapx_destroy (&var_map);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

static void
add_test (struct npar_specs *specs, struct npar_test *nt)
{
  specs->test = pool_realloc (specs->pool, specs->test,
                              (specs->n_tests + 1) * sizeof *specs->test);

  specs->test[specs->n_tests++] = nt;
}

static bool
npar_runs (struct lexer *lexer, struct dataset *ds,
           struct npar_specs *specs)
{
  struct runs_test *rt = pool_alloc (specs->pool, sizeof (*rt));
  struct one_sample_test *tp = &rt->parent;
  struct npar_test *nt = &tp->parent;

  nt->execute = runs_execute;
  nt->insert_variables = one_sample_insert_variables;

  if (!lex_force_match (lexer, T_LPAREN))
    return false;

  if (lex_match_id (lexer, "MEAN"))
    rt->cp_mode = CP_MEAN;
  else if (lex_match_id (lexer, "MEDIAN"))
    rt->cp_mode = CP_MEDIAN;
  else if (lex_match_id (lexer, "MODE"))
    rt->cp_mode = CP_MODE;
  else if (lex_is_number (lexer))
    {
      rt->cutpoint = lex_number (lexer);
      rt->cp_mode = CP_CUSTOM;
      lex_get (lexer);
    }
  else
    {
      lex_error (lexer, _("Syntax error expecting %s, %s, %s or a number."),
                 "MEAN", "MEDIAN", "MODE");
      return false;
    }

  if (!lex_force_match_phrase (lexer, ")="))
    return false;

  if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
                                   &tp->vars, &tp->n_vars,
                                   PV_NO_SCRATCH | PV_NO_DUPLICATE | PV_NUMERIC))
    return false;

  add_test (specs, nt);
  return true;
}

static bool
npar_friedman (struct lexer *lexer, struct dataset *ds,
               struct npar_specs *specs)
{
  struct friedman_test *ft = pool_alloc (specs->pool, sizeof (*ft));
  struct one_sample_test *ost = &ft->parent;
  struct npar_test *nt = &ost->parent;

  ft->kendalls_w = false;
  nt->execute = friedman_execute;
  nt->insert_variables = one_sample_insert_variables;

  lex_match (lexer, T_EQUALS);

  if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
                                   &ost->vars, &ost->n_vars,
                                   PV_NO_SCRATCH | PV_NO_DUPLICATE | PV_NUMERIC))
    return false;

  add_test (specs, nt);
  return true;
}

static bool
npar_kendall (struct lexer *lexer, struct dataset *ds,
               struct npar_specs *specs)
{
  struct friedman_test *kt = pool_alloc (specs->pool, sizeof (*kt));
  struct one_sample_test *ost = &kt->parent;
  struct npar_test *nt = &ost->parent;

  kt->kendalls_w = true;
  nt->execute = friedman_execute;
  nt->insert_variables = one_sample_insert_variables;

  lex_match (lexer, T_EQUALS);

  if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
                                   &ost->vars, &ost->n_vars,
                                   PV_NO_SCRATCH | PV_NO_DUPLICATE | PV_NUMERIC))
    return false;

  add_test (specs, nt);
  return true;
}


static bool
npar_cochran (struct lexer *lexer, struct dataset *ds,
               struct npar_specs *specs)
{
  struct one_sample_test *ft = pool_alloc (specs->pool, sizeof (*ft));
  struct npar_test *nt = &ft->parent;

  nt->execute = cochran_execute;
  nt->insert_variables = one_sample_insert_variables;

  lex_match (lexer, T_EQUALS);

  if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
                                   &ft->vars, &ft->n_vars,
                                   PV_NO_SCRATCH | PV_NO_DUPLICATE | PV_NUMERIC))
    return false;

  add_test (specs, nt);
  return true;
}

static bool
npar_chisquare (struct lexer *lexer, struct dataset *ds,
                struct npar_specs *specs)
{
  struct chisquare_test *cstp = pool_alloc (specs->pool, sizeof (*cstp));
  struct one_sample_test *tp = &cstp->parent;
  struct npar_test *nt = &tp->parent;

  nt->execute = chisquare_execute;
  nt->insert_variables = one_sample_insert_variables;

  if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
                                   &tp->vars, &tp->n_vars,
                                   PV_NO_SCRATCH | PV_NO_DUPLICATE))
    return false;

  cstp->ranged = false;

  if (lex_match (lexer, T_LPAREN))
    {
      cstp->ranged = true;
      if (!lex_force_num (lexer))
        return false;
      cstp->lo = lex_number (lexer);
      lex_get (lexer);

      if (!lex_force_match (lexer, T_COMMA))
        return false;
      if (!lex_force_num_range_open (lexer, "HI", cstp->lo, DBL_MAX))
        return false;
      cstp->hi = lex_number (lexer);
      lex_get (lexer);
      if (!lex_force_match (lexer, T_RPAREN))
        return false;
    }

  cstp->n_expected = 0;
  cstp->expected = NULL;
  int expected_start = 0;
  int expected_end = 0;
  if (lex_match_phrase (lexer, "/EXPECTED"))
    {
      if (!lex_force_match (lexer, T_EQUALS))
        return false;

      if (!lex_match_id (lexer, "EQUAL"))
        {
          expected_start = lex_ofs (lexer);
          while (lex_is_number (lexer))
            {
              int n = 1;
              double f = lex_number (lexer);
              lex_get (lexer);
              if (lex_match (lexer, T_ASTERISK))
                {
                  n = f;
                  if (!lex_force_num (lexer))
                    return false;
                  f = lex_number (lexer);
                  lex_get (lexer);
                }
              lex_match (lexer, T_COMMA);

              cstp->n_expected += n;
              cstp->expected = pool_realloc (specs->pool,
                                             cstp->expected,
                                             sizeof (double) * cstp->n_expected);
              for (int i = cstp->n_expected - n; i < cstp->n_expected; ++i)
                cstp->expected[i] = f;
            }
          expected_end = lex_ofs (lexer) - 1;
        }
    }

  if (cstp->ranged && cstp->n_expected > 0 &&
       cstp->n_expected != cstp->hi - cstp->lo + 1)
    {
      lex_ofs_error (lexer, expected_start, expected_end,
                     _("%d expected values were given, but the specified "
                       "range (%d-%d) requires exactly %d values."),
                     cstp->n_expected, cstp->lo, cstp->hi,
                     cstp->hi - cstp->lo +1);
      return false;
    }

  add_test (specs, nt);
  return true;
}

static bool
npar_binomial (struct lexer *lexer, struct dataset *ds,
               struct npar_specs *specs)
{
  struct binomial_test *btp = pool_alloc (specs->pool, sizeof (*btp));
  struct one_sample_test *tp = &btp->parent;
  struct npar_test *nt = &tp->parent;

  nt->execute = binomial_execute;
  nt->insert_variables = one_sample_insert_variables;

  btp->category1 = btp->category2 = btp->cutpoint = SYSMIS;

  btp->p = 0.5;

  if (lex_match (lexer, T_LPAREN))
    {
      if (!lex_force_num (lexer))
        return false;
      btp->p = lex_number (lexer);
      lex_get (lexer);
      if (!lex_force_match (lexer, T_RPAREN))
        return false;
      if (!lex_force_match (lexer, T_EQUALS))
        return false;
    }

  if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
                                   &tp->vars, &tp->n_vars,
                                   PV_NUMERIC | PV_NO_SCRATCH | PV_NO_DUPLICATE))
    return false;
  if (lex_match (lexer, T_LPAREN))
    {
      if (!lex_force_num (lexer))
        return false;
      btp->category1 = lex_number (lexer);
      lex_get (lexer);
      if (lex_match (lexer, T_COMMA))
        {
          if (!lex_force_num (lexer))
            return false;
          btp->category2 = lex_number (lexer);
          lex_get (lexer);
        }
      else
        btp->cutpoint = btp->category1;

      if (!lex_force_match (lexer, T_RPAREN))
        return false;
    }

  add_test (specs, nt);
  return true;
}

static void
ks_one_sample_parse_params (struct lexer *lexer, struct ks_one_sample_test *kst, int params)
{
  assert (params == 1 || params == 2);

  if (lex_is_number (lexer))
    {
      kst->p[0] = lex_number (lexer);

      lex_get (lexer);
      if (params == 2)
        {
          lex_match (lexer, T_COMMA);
          if (lex_force_num (lexer))
            {
              kst->p[1] = lex_number (lexer);
              lex_get (lexer);
            }
        }
    }
}

static bool
npar_ks_one_sample (struct lexer *lexer, struct dataset *ds, struct npar_specs *specs)
{
  struct ks_one_sample_test *kst = pool_alloc (specs->pool, sizeof (*kst));
  struct one_sample_test *tp = &kst->parent;
  struct npar_test *nt = &tp->parent;

  nt->execute = ks_one_sample_execute;
  nt->insert_variables = one_sample_insert_variables;

  kst->p[0] = kst->p[1] = SYSMIS;

  if (!lex_force_match (lexer, T_LPAREN))
    return false;

  if (lex_match_id (lexer, "NORMAL"))
    {
      kst->dist = KS_NORMAL;
      ks_one_sample_parse_params (lexer, kst, 2);
    }
  else if (lex_match_id (lexer, "POISSON"))
    {
      kst->dist = KS_POISSON;
      ks_one_sample_parse_params (lexer, kst, 1);
    }
  else if (lex_match_id (lexer, "UNIFORM"))
    {
      kst->dist = KS_UNIFORM;
      ks_one_sample_parse_params (lexer, kst, 2);
    }
  else if (lex_match_id (lexer, "EXPONENTIAL"))
    {
      kst->dist = KS_EXPONENTIAL;
      ks_one_sample_parse_params (lexer, kst, 1);
    }
  else
    {
      lex_error_expecting (lexer, "NORMAL", "POISSON", "UNIFORM",
                           "EXPONENTIAL");
      return false;
    }

  if (!lex_force_match (lexer, T_RPAREN))
    return false;

  lex_match (lexer, T_EQUALS);

  if (!parse_variables_const_pool (lexer, specs->pool, dataset_dict (ds),
                                   &tp->vars, &tp->n_vars,
                                   PV_NUMERIC | PV_NO_SCRATCH | PV_NO_DUPLICATE))
    return false;

  add_test (specs, nt);

  return true;
}

static bool
parse_two_sample_related_test (struct lexer *lexer,
                               const struct dictionary *dict,
                               struct two_sample_test *tp,
                               struct pool *pool)
{
  tp->parent.insert_variables = two_sample_insert_variables;

  const struct variable **v1;
  size_t n1;
  int vars_start = lex_ofs (lexer);
  if (!parse_variables_const_pool (lexer, pool, dict, &v1, &n1,
                                   PV_NUMERIC | PV_NO_SCRATCH | PV_DUPLICATE))
    return false;

  bool with = false;
  bool paired = false;
  const struct variable **v2 = NULL;
  size_t n2 = 0;
  if (lex_match (lexer, T_WITH))
    {
      with = true;
      if (!parse_variables_const_pool (lexer, pool, dict, &v2, &n2,
                                       PV_NUMERIC | PV_NO_SCRATCH | PV_DUPLICATE))
        return false;
      int vars_end = lex_ofs (lexer) - 1;

      if (lex_match (lexer, T_LPAREN))
        {
          if (!lex_force_match_phrase (lexer, "PAIRED)"))
            return false;
          paired = true;

          if (n1 != n2)
            {
              lex_ofs_error (lexer, vars_start, vars_end,
                             _("PAIRED was specified, but the number of "
                               "variables preceding WITH (%zu) does not match "
                               "the number following (%zu)."),
                             n1, n2);
              return false;
            }
        }
    }

  tp->n_pairs = (paired ? n1
                 : with ? n1 * n2
                 : (n1 * (n1 - 1)) / 2);
  tp->pairs = pool_alloc (pool, sizeof (variable_pair) * tp->n_pairs);

  size_t n = 0;
  if (!with)
    for (size_t i = 0; i < n1 - 1; ++i)
      for (size_t j = i + 1; j < n1; ++j)
        {
          assert (n < tp->n_pairs);
          tp->pairs[n][0] = v1[i];
          tp->pairs[n][1] = v1[j];
          n++;
        }
  else if (paired)
    {
      assert (n1 == n2);
      for (size_t i = 0; i < n1; ++i)
        {
          tp->pairs[n][0] = v1[i];
          tp->pairs[n][1] = v2[i];
          n++;
        }
    }
  else
    {
      for (size_t i = 0; i < n1; ++i)
        for (size_t j = 0; j < n2; ++j)
          {
            tp->pairs[n][0] = v1[i];
            tp->pairs[n][1] = v2[j];
            n++;
          }
    }
  assert (n == tp->n_pairs);

  return true;
}

static bool
parse_n_sample_related_test (struct lexer *lexer, const struct dictionary *dict,
                             struct n_sample_test *nst, struct pool *pool)
{
  if (!parse_variables_const_pool (lexer, pool, dict, &nst->vars, &nst->n_vars,
                                   PV_NUMERIC | PV_NO_SCRATCH | PV_NO_DUPLICATE))
    return false;

  if (!lex_force_match (lexer, T_BY))
    return false;

  nst->indep_var = parse_variable_const (lexer, dict);
  if (!nst->indep_var)
    return false;

  if (!lex_force_match (lexer, T_LPAREN))
    return false;

  value_init (&nst->val1, var_get_width (nst->indep_var));
  if (!parse_value (lexer, &nst->val1, nst->indep_var))
    {
      value_destroy (&nst->val1, var_get_width (nst->indep_var));
      return false;
    }

  lex_match (lexer, T_COMMA);

  value_init (&nst->val2, var_get_width (nst->indep_var));
  if (!parse_value (lexer, &nst->val2, nst->indep_var))
    {
      value_destroy (&nst->val2, var_get_width (nst->indep_var));
      return false;
    }

  if (!lex_force_match (lexer, T_RPAREN))
    return false;

  return true;
}

static bool
npar_wilcoxon (struct lexer *lexer,
               struct dataset *ds,
               struct npar_specs *specs)
{
  struct two_sample_test *tp = pool_alloc (specs->pool, sizeof (*tp));
  struct npar_test *nt = &tp->parent;
  nt->execute = wilcoxon_execute;

  if (!parse_two_sample_related_test (lexer, dataset_dict (ds),
                                      tp, specs->pool))
    return false;

  add_test (specs, nt);
  return true;
}

static bool
npar_mann_whitney (struct lexer *lexer,
                   struct dataset *ds,
                   struct npar_specs *specs)
{
  struct n_sample_test *tp = pool_alloc (specs->pool, sizeof (*tp));
  struct npar_test *nt = &tp->parent;

  nt->insert_variables = n_sample_insert_variables;
  nt->execute = mann_whitney_execute;

  if (!parse_n_sample_related_test (lexer, dataset_dict (ds), tp, specs->pool))
    return false;

  add_test (specs, nt);
  return true;
}

static bool
npar_median (struct lexer *lexer,
             struct dataset *ds,
             struct npar_specs *specs)
{
  struct median_test *mt = pool_alloc (specs->pool, sizeof (*mt));
  struct n_sample_test *tp = &mt->parent;
  struct npar_test *nt = &tp->parent;

  mt->median = SYSMIS;

  if (lex_match (lexer, T_LPAREN))
    {
      if (!lex_force_num (lexer))
        return false;
      mt->median = lex_number (lexer);
      lex_get (lexer);

      if (!lex_force_match (lexer, T_RPAREN))
        return false;
    }

  lex_match (lexer, T_EQUALS);

  nt->insert_variables = n_sample_insert_variables;
  nt->execute = median_execute;

  if (!parse_n_sample_related_test (lexer, dataset_dict (ds), tp, specs->pool))
    return false;

  add_test (specs, nt);
  return true;
}

static bool
npar_sign (struct lexer *lexer, struct dataset *ds,
           struct npar_specs *specs)
{
  struct two_sample_test *tp = pool_alloc (specs->pool, sizeof (*tp));
  struct npar_test *nt = &tp->parent;

  nt->execute = sign_execute;

  if (!parse_two_sample_related_test (lexer, dataset_dict (ds),
                                      tp, specs->pool))
    return false;

  add_test (specs, nt);
  return true;
}

static bool
npar_mcnemar (struct lexer *lexer, struct dataset *ds,
           struct npar_specs *specs)
{
  struct two_sample_test *tp = pool_alloc (specs->pool, sizeof (*tp));
  struct npar_test *nt = &tp->parent;

  nt->execute = mcnemar_execute;

  if (!parse_two_sample_related_test (lexer, dataset_dict (ds),
                                      tp, specs->pool))
    return false;

  add_test (specs, nt);
  return true;
}


static bool
npar_jonckheere_terpstra (struct lexer *lexer, struct dataset *ds,
                      struct npar_specs *specs)
{
  struct n_sample_test *tp = pool_alloc (specs->pool, sizeof (*tp));
  struct npar_test *nt = &tp->parent;

  nt->insert_variables = n_sample_insert_variables;
  nt->execute = jonckheere_terpstra_execute;

  if (!parse_n_sample_related_test (lexer, dataset_dict (ds), tp, specs->pool))
    return false;

  add_test (specs, nt);
  return true;
}

static bool
npar_kruskal_wallis (struct lexer *lexer, struct dataset *ds,
                      struct npar_specs *specs)
{
  struct n_sample_test *tp = pool_alloc (specs->pool, sizeof (*tp));
  struct npar_test *nt = &tp->parent;

  nt->insert_variables = n_sample_insert_variables;

  nt->execute = kruskal_wallis_execute;

  if (!parse_n_sample_related_test (lexer, dataset_dict (ds), tp, specs->pool))
    return false;

  add_test (specs, nt);
  return true;
}

static void
insert_variable_into_map (struct hmapx *var_map, const struct variable *var)
{
  size_t hash = hash_pointer (var, 0);
  struct hmapx_node *node;
  const struct variable *v = NULL;

  HMAPX_FOR_EACH_WITH_HASH (v, node, hash, var_map)
    if (v == var)
      return;

  hmapx_insert (var_map, CONST_CAST (struct variable *, var), hash);
}

/* Insert the variables for TEST into VAR_MAP */
static void
one_sample_insert_variables (const struct npar_test *test,
                             struct hmapx *var_map)
{
  const struct one_sample_test *ost = UP_CAST (test, const struct one_sample_test, parent);

  for (size_t i = 0; i < ost->n_vars; ++i)
    insert_variable_into_map (var_map, ost->vars[i]);
}


static void
two_sample_insert_variables (const struct npar_test *test,
                             struct hmapx *var_map)
{
  const struct two_sample_test *tst = UP_CAST (test, const struct two_sample_test, parent);

  for (size_t i = 0; i < tst->n_pairs; ++i)
    {
      variable_pair *pair = &tst->pairs[i];

      insert_variable_into_map (var_map, (*pair)[0]);
      insert_variable_into_map (var_map, (*pair)[1]);
    }
}

static void
n_sample_insert_variables (const struct npar_test *test,
                           struct hmapx *var_map)
{
  const struct n_sample_test *tst = UP_CAST (test, const struct n_sample_test, parent);

  for (size_t i = 0; i < tst->n_vars; ++i)
    insert_variable_into_map (var_map, tst->vars[i]);

  insert_variable_into_map (var_map, tst->indep_var);
}

static bool
npar_method (struct lexer *lexer,  struct npar_specs *specs)
{
  if (lex_match_id (lexer, "EXACT"))
    {
      specs->exact = true;
      specs->timer = 0.0;
      if (lex_match_id (lexer, "TIMER"))
        {
          specs->timer = 5.0;

          if (lex_match (lexer, T_LPAREN))
            {
              if (!lex_force_num (lexer))
                return false;
              specs->timer = lex_number (lexer);
              lex_get (lexer);
              if (!lex_force_match (lexer, T_RPAREN))
                return false;
            }
        }
    }

  return true;
}
