/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-2000, 2009-2014 Free Software Foundation, Inc.

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
#include <limits.h>
#include <math.h>
#include <stdlib.h>

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/subcase.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/commands/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "math/moments.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* DESCRIPTIVES private data. */

/* Handling of missing values. */
enum dsc_missing_type
  {
    DSC_VARIABLE,       /* Handle missing values on a per-variable basis. */
    DSC_LISTWISE        /* Discard entire case if any variable is missing. */
  };

/* Describes properties of a distribution for the purpose of
   calculating a Z-score. */
struct dsc_z_score
  {
    const struct variable *src_var;   /* Variable on which z-score is based. */
    struct variable *z_var;     /* New z-score variable. */
    double mean;                /* Distribution mean. */
    double std_dev;                /* Distribution standard deviation. */
  };

/* DESCRIPTIVES transformation (for calculating Z-scores). */
struct dsc_trns
  {
    struct dsc_z_score *z_scores; /* Array of Z-scores. */
    size_t n_z_scores;            /* Number of Z-scores. */
    const struct variable **vars;     /* Variables for listwise missing checks. */
    size_t n_vars;              /* Number of variables. */
    enum dsc_missing_type missing_type; /* Treatment of missing values. */
    enum mv_class exclude;      /* Classes of missing values to exclude. */
    const struct variable *filter;    /* Dictionary FILTER BY variable. */
    struct casereader *z_reader; /* Reader for count, mean, stddev. */
    casenumber count;            /* Number left in this SPLIT FILE group.*/
    bool ok;
  };

/* Statistics.  Used as bit indexes, so must be 32 or fewer. */
enum dsc_statistic
  {
    DSC_MEAN = 0, DSC_SEMEAN, DSC_STDDEV, DSC_VARIANCE, DSC_KURTOSIS,
    DSC_SEKURT, DSC_SKEWNESS, DSC_SESKEW, DSC_RANGE, DSC_MIN,
    DSC_MAX, DSC_SUM, DSC_N_STATS,

    /* Only valid as sort criteria. */
    DSC_NAME = -2,              /* Sort by name. */
    DSC_NONE = -1               /* Unsorted. */
  };

/* Describes one statistic. */
struct dsc_statistic_info
  {
    const char *identifier;     /* Identifier. */
    const char *name;                /* Full name. */
    enum moment moment;                /* Highest moment needed to calculate. */
  };

/* Table of statistics, indexed by DSC_*. */
static const struct dsc_statistic_info dsc_info[DSC_N_STATS] =
  {
    {"MEAN", N_("Mean"), MOMENT_MEAN},
    {"SEMEAN", N_("S.E. Mean"), MOMENT_VARIANCE},
    {"STDDEV", N_("Std Dev"), MOMENT_VARIANCE},
    {"VARIANCE", N_("Variance"), MOMENT_VARIANCE},
    {"KURTOSIS", N_("Kurtosis"), MOMENT_KURTOSIS},
    {"SEKURTOSIS", N_("S.E. Kurt"), MOMENT_NONE},
    {"SKEWNESS", N_("Skewness"), MOMENT_SKEWNESS},
    {"SESKEWNESS", N_("S.E. Skew"), MOMENT_NONE},
    {"RANGE", N_("Range"), MOMENT_NONE},
    {"MINIMUM", N_("Minimum"), MOMENT_NONE},
    {"MAXIMUM", N_("Maximum"), MOMENT_NONE},
    {"SUM", N_("Sum"), MOMENT_MEAN},
  };

/* Statistics calculated by default if none are explicitly
   requested. */
#define DEFAULT_STATS                                                   \
        ((1UL << DSC_MEAN) | (1UL << DSC_STDDEV) | (1UL << DSC_MIN)     \
         | (1UL << DSC_MAX))

/* A variable specified on DESCRIPTIVES. */
struct dsc_var
  {
    const struct variable *v;         /* Variable to calculate on. */
    char *z_name;                     /* Name for z-score variable. */
    double valid, missing;        /* Valid, missing counts. */
    struct moments *moments;    /* Moments. */
    double min, max;            /* Maximum and mimimum values. */
    double stats[DSC_N_STATS];        /* All the stats' values. */
  };

/* A DESCRIPTIVES procedure. */
struct dsc_proc
  {
    /* Per-variable info. */
    struct dictionary *dict;    /* Dictionary. */
    struct dsc_var *vars;       /* Variables. */
    size_t n_vars;              /* Number of variables. */

    /* User options. */
    enum dsc_missing_type missing_type; /* Treatment of missing values. */
    enum mv_class exclude;      /* Classes of missing values to exclude. */

    /* Accumulated results. */
    double missing_listwise;    /* Sum of weights of cases missing listwise. */
    double valid;               /* Sum of weights of valid cases. */
    bool bad_warn;               /* Warn if bad weight found. */
    enum dsc_statistic sort_by_stat; /* Statistic to sort by; -1: name. */
    enum subcase_direction sort_direction;
    unsigned long show_stats;   /* Statistics to display. */
    unsigned long calc_stats;   /* Statistics to calculate. */
    enum moment max_moment;     /* Highest moment needed for stats. */

    /* Z scores. */
    struct casewriter *z_writer; /* Mean and stddev per SPLIT FILE group. */
  };

/* Parsing. */
static enum dsc_statistic match_statistic (struct lexer *);
static void free_dsc_proc (struct dsc_proc *);

/* Z-score functions. */
static bool try_name (const struct dictionary *dict,
                      struct dsc_proc *dsc, const char *name);
static char *generate_z_varname (const struct dictionary *dict,
                                 struct dsc_proc *dsc,
                                 const char *name, int *n_zs);
static void dump_z_table (struct dsc_proc *);
static void setup_z_trns (struct dsc_proc *, struct dataset *);

/* Procedure execution functions. */
static void calc_descriptives (struct dsc_proc *, struct casereader *,
                               struct dataset *);
static void display (struct dsc_proc *dsc);

/* Parser and outline. */

/* Handles DESCRIPTIVES. */
int
cmd_descriptives (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  const struct variable **vars = NULL;
  size_t n_vars = 0;
  bool save_z_scores = false;
  int n_zs = 0;

  /* Create and initialize dsc. */
  struct dsc_proc *dsc = xmalloc (sizeof *dsc);
  *dsc = (struct dsc_proc) {
    .dict = dict,
    .missing_type = DSC_VARIABLE,
    .exclude = MV_ANY,
    .bad_warn = 1,
    .sort_by_stat = DSC_NONE,
    .sort_direction = SC_ASCEND,
    .show_stats = DEFAULT_STATS,
    .calc_stats = DEFAULT_STATS,
  };

  /* Parse DESCRIPTIVES. */
  int z_ofs = 0;
  while (lex_token (lexer) != T_ENDCMD)
    {
      if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "VARIABLE"))
                dsc->missing_type = DSC_VARIABLE;
              else if (lex_match_id (lexer, "LISTWISE"))
                dsc->missing_type = DSC_LISTWISE;
              else if (lex_match_id (lexer, "INCLUDE"))
                dsc->exclude = MV_SYSTEM;
              else
                {
                  lex_error_expecting (lexer, "VARIABLE", "LISTWISE",
                                       "INCLUDE");
                  goto error;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "SAVE"))
        {
          save_z_scores = true;
          z_ofs = lex_ofs (lexer) - 1;
        }
      else if (lex_match_id (lexer, "FORMAT"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "LABELS")
                  || lex_match_id (lexer, "NOLABELS")
                  || lex_match_id (lexer, "INDEX")
                  || lex_match_id (lexer, "NOINDEX")
                  || lex_match_id (lexer, "LINE")
                  || lex_match_id (lexer, "SERIAL"))
                {
                  /* Ignore. */
                }
              else
                {
                  lex_error_expecting (lexer, "LABELS", "NOLABELS",
                                       "INDEX", "NOINDEX", "LINE", "SERIAL");
                  goto error;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "STATISTICS"))
        {
          lex_match (lexer, T_EQUALS);
          dsc->show_stats = 0;
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (lex_match (lexer, T_ALL))
                dsc->show_stats |= (1UL << DSC_N_STATS) - 1;
              else if (lex_match_id (lexer, "DEFAULT"))
                dsc->show_stats |= DEFAULT_STATS;
              else
                {
                  enum dsc_statistic s = match_statistic (lexer);
                  if (s == DSC_NONE)
                    goto error;
                  dsc->show_stats |= 1UL << s;
                }
              lex_match (lexer, T_COMMA);
            }
          if (dsc->show_stats == 0)
            dsc->show_stats = DEFAULT_STATS;
        }
      else if (lex_match_id (lexer, "SORT"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "NAME"))
            dsc->sort_by_stat = DSC_NAME;
          else
            {
              dsc->sort_by_stat = match_statistic (lexer);
              if (dsc->sort_by_stat == DSC_NONE)
                dsc->sort_by_stat = DSC_MEAN;
            }
          if (lex_match (lexer, T_LPAREN))
            {
              if (lex_match_id (lexer, "A"))
                dsc->sort_direction = SC_ASCEND;
              else if (lex_match_id (lexer, "D"))
                dsc->sort_direction = SC_DESCEND;
              else
                {
                  lex_error_expecting (lexer, "A", "D");
                  goto error;
                }
              if (!lex_force_match (lexer, T_RPAREN))
                goto error;
            }
        }
      else if (n_vars == 0)
        {
          lex_match_phrase (lexer, "VARIABLES=");
          while (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_SLASH)
            {
              if (!parse_variables_const (lexer, dict, &vars, &n_vars,
                                    PV_APPEND | PV_NO_DUPLICATE | PV_NUMERIC))
                goto error;

              dsc->vars = xnrealloc ((void *)dsc->vars, n_vars, sizeof *dsc->vars);
              for (size_t i = dsc->n_vars; i < n_vars; i++)
                dsc->vars[i] = (struct dsc_var) { .v = vars[i] };
              dsc->n_vars = n_vars;

              if (lex_match (lexer, T_LPAREN))
                {
                  if (!lex_force_id (lexer))
                    goto error;
                  z_ofs = lex_ofs (lexer);
                  if (try_name (dict, dsc, lex_tokcstr (lexer)))
                    {
                      struct dsc_var *dsc_var = &dsc->vars[dsc->n_vars - 1];
                      dsc_var->z_name = xstrdup (lex_tokcstr (lexer));
                      n_zs++;
                    }
                  else
                    lex_error (lexer, _("Z-score variable name %s would be "
                                        "a duplicate variable name."),
                               lex_tokcstr (lexer));
                  lex_get (lexer);
                  if (!lex_force_match (lexer, T_RPAREN))
                    goto error;
                }
            }
        }
      else
        {
          lex_error_expecting (lexer, "MISSING", "SAVE", "FORMAT", "STATISTICS",
                               "SORT", "VARIABLES");
          goto error;
        }

      lex_match (lexer, T_SLASH);
    }
  if (n_vars == 0)
    {
      msg (SE, _("No variables specified."));
      goto error;
    }

  /* Construct z-score varnames, show translation table. */
  if (n_zs || save_z_scores)
    {
      if (save_z_scores)
        {
          int n_gens = 0;

          for (size_t i = 0; i < dsc->n_vars; i++)
            {
              struct dsc_var *dsc_var = &dsc->vars[i];
              if (dsc_var->z_name == NULL)
                {
                  const char *name = var_get_name (dsc_var->v);
                  dsc_var->z_name = generate_z_varname (dict, dsc, name,
                                                        &n_gens);
                  if (dsc_var->z_name == NULL)
                    goto error;

                  n_zs++;
                }
            }
        }

      /* It would be better to handle Z scores correctly (however we define
         that) when TEMPORARY is in effect, but in the meantime this at least
         prevents a use-after-free error.  See bug #38786.  */
      if (proc_make_temporary_transformations_permanent (ds))
        lex_ofs_msg (lexer, SW, z_ofs, z_ofs,
                     _("DESCRIPTIVES with Z scores ignores TEMPORARY.  "
                       "Temporary transformations will be made permanent."));

      struct caseproto *proto = caseproto_create ();
      for (size_t i = 0; i < 1 + 2 * n_zs; i++)
        proto = caseproto_add_width (proto, 0);
      dsc->z_writer = autopaging_writer_create (proto);
      caseproto_unref (proto);

      dump_z_table (dsc);
    }

  /* Figure out statistics to display. */
  if (dsc->show_stats & (1UL << DSC_SKEWNESS))
    dsc->show_stats |= 1UL << DSC_SESKEW;
  if (dsc->show_stats & (1UL << DSC_KURTOSIS))
    dsc->show_stats |= 1UL << DSC_SEKURT;

  /* Figure out which statistics to calculate. */
  dsc->calc_stats = dsc->show_stats;
  if (n_zs > 0)
    dsc->calc_stats |= (1UL << DSC_MEAN) | (1UL << DSC_STDDEV);
  if (dsc->sort_by_stat >= 0)
    dsc->calc_stats |= 1UL << dsc->sort_by_stat;
  if (dsc->show_stats & (1UL << DSC_SESKEW))
    dsc->calc_stats |= 1UL << DSC_SKEWNESS;
  if (dsc->show_stats & (1UL << DSC_SEKURT))
    dsc->calc_stats |= 1UL << DSC_KURTOSIS;

  /* Figure out maximum moment needed and allocate moments for
     the variables. */
  dsc->max_moment = MOMENT_NONE;
  for (size_t i = 0; i < DSC_N_STATS; i++)
    if (dsc->calc_stats & (1UL << i) && dsc_info[i].moment > dsc->max_moment)
      dsc->max_moment = dsc_info[i].moment;
  if (dsc->max_moment != MOMENT_NONE)
    for (size_t i = 0; i < dsc->n_vars; i++)
      dsc->vars[i].moments = moments_create (dsc->max_moment);

  /* Data pass. */
  struct casegrouper *grouper = casegrouper_create_splits (proc_open_filtering (
                                                             ds, false), dict);
  struct casereader *group;
  while (casegrouper_get_next_group (grouper, &group))
    calc_descriptives (dsc, group, ds);
  bool ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  /* Z-scoring! */
  if (ok && n_zs)
    setup_z_trns (dsc, ds);

  /* Done. */
  free (vars);
  free_dsc_proc (dsc);
  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

 error:
  free (vars);
  free_dsc_proc (dsc);
  return CMD_FAILURE;
}

/* Returns the statistic named by the current token and skips past the token.
   Returns DSC_NONE if no statistic is given (e.g., subcommand with no
   specifiers). Emits an error if the current token ID does not name a
   statistic. */
static enum dsc_statistic
match_statistic (struct lexer *lexer)
{
  if (lex_token (lexer) == T_ID)
    {
      for (enum dsc_statistic stat = 0; stat < DSC_N_STATS; stat++)
        if (lex_match_id (lexer, dsc_info[stat].identifier))
          return stat;

      const char *stat_names[DSC_N_STATS];
      for (enum dsc_statistic stat = 0; stat < DSC_N_STATS; stat++)
        stat_names[stat] = dsc_info[stat].identifier;
      lex_error_expecting_array (lexer, stat_names,
                                 sizeof stat_names / sizeof *stat_names);
      lex_get (lexer);
    }

  return DSC_NONE;
}

/* Frees DSC. */
static void
free_dsc_proc (struct dsc_proc *dsc)
{
  if (dsc == NULL)
    return;

  for (size_t i = 0; i < dsc->n_vars; i++)
    {
      struct dsc_var *dsc_var = &dsc->vars[i];
      free (dsc_var->z_name);
      moments_destroy (dsc_var->moments);
    }
  casewriter_destroy (dsc->z_writer);
  free (dsc->vars);
  free (dsc);
}

/* Z scores. */

/* Returns false if NAME is a duplicate of any existing variable name or
   of any previously-declared z-var name; otherwise returns true. */
static bool
try_name (const struct dictionary *dict, struct dsc_proc *dsc,
          const char *name)
{
  if (dict_lookup_var (dict, name) != NULL)
    return false;
  for (size_t i = 0; i < dsc->n_vars; i++)
    {
      struct dsc_var *dsc_var = &dsc->vars[i];
      if (dsc_var->z_name != NULL && !utf8_strcasecmp (dsc_var->z_name, name))
        return false;
    }
  return true;
}

/* Generates a name for a Z-score variable based on a variable
   named VAR_NAME, given that *Z_CNT generated variable names are
   known to already exist.  If successful, returns the new name
   as a dynamically allocated string.  On failure, returns NULL. */
static char *
generate_z_varname (const struct dictionary *dict, struct dsc_proc *dsc,
                    const char *var_name, int *n_zs)
{
  /* Try a name based on the original variable name. */
  char *z_name = xasprintf ("Z%s", var_name);
  char *trunc_name = utf8_encoding_trunc (z_name, dict_get_encoding (dict),
                                          ID_MAX_LEN);
  free (z_name);
  if (try_name (dict, dsc, trunc_name))
    return trunc_name;
  free (trunc_name);

  /* Generate a synthetic name. */
  for (;;)
    {
      char name[16];

      (*n_zs)++;

      if (*n_zs <= 99)
        sprintf (name, "ZSC%03d", *n_zs);
      else if (*n_zs <= 108)
        sprintf (name, "STDZ%02d", *n_zs - 99);
      else if (*n_zs <= 117)
        sprintf (name, "ZZZZ%02d", *n_zs - 108);
      else if (*n_zs <= 126)
        sprintf (name, "ZQZQ%02d", *n_zs - 117);
      else
        {
          msg (SE, _("Ran out of generic names for Z-score variables.  "
                     "There are only 126 generic names: ZSC001-ZSC099, "
                     "STDZ01-STDZ09, ZZZZ01-ZZZZ09, ZQZQ01-ZQZQ09."));
          return NULL;
        }

      if (try_name (dict, dsc, name))
        return xstrdup (name);
    }
  NOT_REACHED();
}

/* Outputs a table describing the mapping between source
   variables and Z-score variables. */
static void
dump_z_table (struct dsc_proc *dsc)
{
  struct pivot_table *table = pivot_table_create (
    N_("Mapping of Variables to Z-scores"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Names"),
                          N_("Source"), N_("Target"));

  struct pivot_dimension *names = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variables"));
  names->hide_all_labels = true;

  for (size_t i = 0; i < dsc->n_vars; i++)
    if (dsc->vars[i].z_name != NULL)
      {
        int row = pivot_category_create_leaf (names->root,
                                              pivot_value_new_number (i));

        pivot_table_put2 (table, 0, row,
                          pivot_value_new_variable (dsc->vars[i].v));
        pivot_table_put2 (table, 1, row,
                          pivot_value_new_user_text (dsc->vars[i].z_name, -1));
      }

  pivot_table_submit (table);
}

static void
descriptives_set_all_sysmis_zscores (const struct dsc_trns *t, struct ccase *c)
{
  for (const struct dsc_z_score *z = t->z_scores;
       z < t->z_scores + t->n_z_scores; z++)
    *case_num_rw (c, z->z_var) = SYSMIS;
}

/* Transformation function to calculate Z-scores. Will return SYSMIS if any of
   the following are true: 1) mean or standard deviation is SYSMIS 2) score is
   SYSMIS 3) score is user missing and they were not included in the original
   analyis. 4) any of the variables in the original analysis were missing
   (either system or user-missing values that weren't included).
*/
static enum trns_result
descriptives_trns_proc (void *trns_, struct ccase **c,
                        casenumber case_idx UNUSED)
{
  struct dsc_trns *t = trns_;

  *c = case_unshare (*c);

  if (t->filter)
    {
      double f = case_num (*c, t->filter);
      if (f == 0.0 || var_is_num_missing (t->filter, f))
        {
          descriptives_set_all_sysmis_zscores (t, *c);
          return TRNS_CONTINUE;
        }
    }

  if (t->count <= 0)
    {
      struct ccase *z_case = casereader_read (t->z_reader);
      if (z_case)
        {
          size_t z_idx = 0;

          t->count = case_num_idx (z_case, z_idx++);
          for (struct dsc_z_score *z = t->z_scores;
               z < t->z_scores + t->n_z_scores; z++)
            {
              z->mean = case_num_idx (z_case, z_idx++);
              z->std_dev = case_num_idx (z_case, z_idx++);
            }
          case_unref (z_case);
        }
      else
        {
          if (t->ok)
            {
              msg (SE,  _("Internal error processing Z scores.  "
                          "Please report this to %s."),
                   PACKAGE_BUGREPORT);
              t->ok = false;
            }
          descriptives_set_all_sysmis_zscores (t, *c);
          return TRNS_CONTINUE;
        }
    }
  t->count--;

  if (t->missing_type == DSC_LISTWISE)
    {
      assert (t->vars != NULL);
      for (const struct variable **vars = t->vars; vars < t->vars + t->n_vars;
           vars++)
        {
          double score = case_num (*c, *vars);
          if (var_is_num_missing (*vars, score) & t->exclude)
            {
              descriptives_set_all_sysmis_zscores (t, *c);
              return TRNS_CONTINUE;
            }
        }
    }

  for (struct dsc_z_score *z = t->z_scores; z < t->z_scores + t->n_z_scores;
       z++)
    {
      double input = case_num (*c, z->src_var);
      double *output = case_num_rw (*c, z->z_var);

      if (z->mean == SYSMIS || z->std_dev == SYSMIS
          || var_is_num_missing (z->src_var, input) & t->exclude)
        *output = SYSMIS;
      else
        *output = (input - z->mean) / z->std_dev;
    }
  return TRNS_CONTINUE;
}

/* Frees a descriptives_trns struct. */
static bool
descriptives_trns_free (void *trns_)
{
  struct dsc_trns *t = trns_;
  bool ok = t->ok && !casereader_error (t->z_reader);

  free (t->z_scores);
  casereader_destroy (t->z_reader);
  assert ((t->missing_type != DSC_LISTWISE) != (t->vars != NULL));
  free (t->vars);
  free (t);

  return ok;
}

static const struct trns_class descriptives_trns_class = {
  .name = "DESCRIPTIVES (Z scores)",
  .execute = descriptives_trns_proc,
  .destroy = descriptives_trns_free,
};

/* Sets up a transformation to calculate Z scores. */
static void
setup_z_trns (struct dsc_proc *dsc, struct dataset *ds)
{
  size_t n = 0;
  for (size_t i = 0; i < dsc->n_vars; i++)
    if (dsc->vars[i].z_name != NULL)
      n++;

  struct dsc_trns *t = xmalloc (sizeof *t);
  *t = (struct dsc_trns) {
    .z_scores = xmalloc (n * sizeof *t->z_scores),
    .n_z_scores = n,
    .missing_type = dsc->missing_type,
    .exclude = dsc->exclude,
    .filter = dict_get_filter (dataset_dict (ds)),
    .z_reader = casewriter_make_reader (dsc->z_writer),
    .ok = true,
  };
  if (t->missing_type == DSC_LISTWISE)
    {
      t->n_vars = dsc->n_vars;
      t->vars = xnmalloc (t->n_vars, sizeof *t->vars);
      for (size_t i = 0; i < t->n_vars; i++)
        t->vars[i] = dsc->vars[i].v;
    }
  dsc->z_writer = NULL;

  n = 0;
  for (size_t i = 0; i < dsc->n_vars; i++)
    {
      struct dsc_var *dv = &dsc->vars[i];
      if (dv->z_name != NULL)
        {
          struct variable *dst_var = dict_create_var_assert (dataset_dict (ds),
                                                             dv->z_name, 0);

          char *label = xasprintf (_("Z-score of %s"), var_to_string (dv->v));
          var_set_label (dst_var, label);
          free (label);

          struct dsc_z_score *z = &t->z_scores[n++];
          *z = (struct dsc_z_score) {
            .src_var = dv->v,
            .z_var = dst_var,
          };
        }
    }

  add_transformation (ds, &descriptives_trns_class, t);
}

/* Statistical calculation. */

static bool listwise_missing (struct dsc_proc *dsc, const struct ccase *c);

/* Calculates and displays descriptive statistics for the cases
   in CF. */
static void
calc_descriptives (struct dsc_proc *dsc, struct casereader *group,
                   struct dataset *ds)
{
  output_split_file_values_peek (ds, group);
  group = casereader_create_filter_weight (group, dataset_dict (ds),
                                           NULL, NULL);

  struct casereader *pass1 = group;
  struct casereader *pass2 = (dsc->max_moment <= MOMENT_MEAN ? NULL
                              : casereader_clone (pass1));
  for (size_t i = 0; i < dsc->n_vars; i++)
    {
      struct dsc_var *dv = &dsc->vars[i];

      dv->valid = dv->missing = 0.0;
      if (dv->moments != NULL)
        moments_clear (dv->moments);
      dv->min = DBL_MAX;
      dv->max = -DBL_MAX;
    }
  dsc->missing_listwise = 0.;
  dsc->valid = 0.;

  /* First pass to handle most of the work. */
  casenumber count = 0;
  const struct variable *filter = dict_get_filter (dataset_dict (ds));
  struct ccase *c;
  for (; (c = casereader_read (pass1)) != NULL; case_unref (c))
    {
      double weight = dict_get_case_weight (dataset_dict (ds), c, NULL);

      if (filter)
        {
          double f = case_num (c, filter);
          if (f == 0.0 || var_is_num_missing (filter, f))
            continue;
        }

      /* Check for missing values. */
      if (listwise_missing (dsc, c))
        {
          dsc->missing_listwise += weight;
          if (dsc->missing_type == DSC_LISTWISE)
            continue;
        }
      dsc->valid += weight;

      for (size_t i = 0; i < dsc->n_vars; i++)
        {
          struct dsc_var *dv = &dsc->vars[i];
          double x = case_num (c, dv->v);

          if (var_is_num_missing (dv->v, x) & dsc->exclude)
            {
              dv->missing += weight;
              continue;
            }

          if (dv->moments != NULL)
            moments_pass_one (dv->moments, x, weight);

          if (x < dv->min)
            dv->min = x;
          if (x > dv->max)
            dv->max = x;
        }

      count++;
    }
  if (!casereader_destroy (pass1))
    {
      casereader_destroy (pass2);
      return;
    }

  /* Second pass for higher-order moments. */
  if (dsc->max_moment > MOMENT_MEAN)
    {
      for (; (c = casereader_read (pass2)) != NULL; case_unref (c))
        {
          double weight = dict_get_case_weight (dataset_dict (ds), c, NULL);

          if (filter)
            {
              double f = case_num (c, filter);
              if (f == 0.0 || var_is_num_missing (filter, f))
                continue;
            }

          /* Check for missing values. */
          if (dsc->missing_type == DSC_LISTWISE && listwise_missing (dsc, c))
            continue;

          for (size_t i = 0; i < dsc->n_vars; i++)
            {
              struct dsc_var *dv = &dsc->vars[i];
              double x = case_num (c, dv->v);

              if (var_is_num_missing (dv->v, x) & dsc->exclude)
                continue;

              if (dv->moments != NULL)
                moments_pass_two (dv->moments, x, weight);
            }
        }
      if (!casereader_destroy (pass2))
        return;
    }

  /* Calculate results. */
  size_t z_idx = 0;
  if (dsc->z_writer && count > 0)
    {
      c = case_create (casewriter_get_proto (dsc->z_writer));
      *case_num_rw_idx (c, z_idx++) = count;
    }
  else
    c = NULL;

  for (size_t i = 0; i < dsc->n_vars; i++)
    {
      struct dsc_var *dv = &dsc->vars[i];

      for (size_t j = 0; j < DSC_N_STATS; j++)
        dv->stats[j] = SYSMIS;

      double W = dsc->valid - dv->missing;
      dv->valid = W;

      if (dv->moments != NULL)
        moments_calculate (dv->moments, NULL,
                           &dv->stats[DSC_MEAN], &dv->stats[DSC_VARIANCE],
                           &dv->stats[DSC_SKEWNESS], &dv->stats[DSC_KURTOSIS]);
      if (dsc->calc_stats & (1UL << DSC_SEMEAN)
          && dv->stats[DSC_VARIANCE] != SYSMIS && W > 0.)
        dv->stats[DSC_SEMEAN] = sqrt (dv->stats[DSC_VARIANCE]) / sqrt (W);
      if (dsc->calc_stats & (1UL << DSC_STDDEV)
          && dv->stats[DSC_VARIANCE] != SYSMIS)
        dv->stats[DSC_STDDEV] = sqrt (dv->stats[DSC_VARIANCE]);
      if (dsc->calc_stats & (1UL << DSC_SEKURT))
        if (dv->stats[DSC_KURTOSIS] != SYSMIS)
            dv->stats[DSC_SEKURT] = calc_sekurt (W);
      if (dsc->calc_stats & (1UL << DSC_SESKEW)
          && dv->stats[DSC_SKEWNESS] != SYSMIS)
        dv->stats[DSC_SESKEW] = calc_seskew (W);
      dv->stats[DSC_RANGE] = ((dv->min == DBL_MAX || dv->max == -DBL_MAX)
                              ? SYSMIS : dv->max - dv->min);
      dv->stats[DSC_MIN] = dv->min == DBL_MAX ? SYSMIS : dv->min;
      dv->stats[DSC_MAX] = dv->max == -DBL_MAX ? SYSMIS : dv->max;
      if (dsc->calc_stats & (1UL << DSC_SUM))
        dv->stats[DSC_SUM] = W * dv->stats[DSC_MEAN];

      if (dv->z_name && c != NULL)
        {
          *case_num_rw_idx (c, z_idx++) = dv->stats[DSC_MEAN];
          *case_num_rw_idx (c, z_idx++) = dv->stats[DSC_STDDEV];
        }
    }

  if (c != NULL)
    casewriter_write (dsc->z_writer, c);

  /* Output results. */
  display (dsc);
}

/* Returns true if any of the descriptives variables in DSC's
   variable list have missing values in case C, false otherwise. */
static bool
listwise_missing (struct dsc_proc *dsc, const struct ccase *c)
{
  for (size_t i = 0; i < dsc->n_vars; i++)
    {
      struct dsc_var *dv = &dsc->vars[i];
      double x = case_num (c, dv->v);

      if (var_is_num_missing (dv->v, x) & dsc->exclude)
        return true;
    }
  return false;
}

/* Statistical display. */

static algo_compare_func descriptives_compare_dsc_vars;

/* Displays a table of descriptive statistics for DSC. */
static void
display (struct dsc_proc *dsc)
{
  struct pivot_table *table = pivot_table_create (
    N_("Descriptive Statistics"));
  pivot_table_set_weight_var (table, dict_get_weight (dsc->dict));

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Statistics"));
  pivot_category_create_leaf_rc (
    statistics->root, pivot_value_new_text (N_("N")), PIVOT_RC_COUNT);
  for (int i = 0; i < DSC_N_STATS; i++)
    if (dsc->show_stats & (1UL << i))
      pivot_category_create_leaf (statistics->root,
                                  pivot_value_new_text (dsc_info[i].name));

  if (dsc->sort_by_stat != DSC_NONE)
    sort (dsc->vars, dsc->n_vars, sizeof *dsc->vars,
          descriptives_compare_dsc_vars, dsc);

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable"));
  for (size_t i = 0; i < dsc->n_vars; i++)
    {
      const struct dsc_var *dv = &dsc->vars[i];

      int row = pivot_category_create_leaf (variables->root,
                                            pivot_value_new_variable (dv->v));

      int column = 0;
      pivot_table_put2 (table, column++, row,
                        pivot_value_new_number (dv->valid));

      for (int j = 0; j < DSC_N_STATS; j++)
        if (dsc->show_stats & (1UL << j))
          {
            union value v = { .f = dv->stats[j] };
            struct pivot_value *pv = (j == DSC_MIN || j == DSC_MAX
                                      ? pivot_value_new_var_value (dv->v, &v)
                                      : pivot_value_new_number (dv->stats[j]));
            pivot_table_put2 (table, column++, row, pv);
          }
    }

  int row = pivot_category_create_leaves (
    variables->root, N_("Valid N (listwise)"), N_("Missing N (listwise)"));
  pivot_table_put2 (table, 0, row, pivot_value_new_number (dsc->valid));
  pivot_table_put2 (table, 0, row + 1,
                    pivot_value_new_number (dsc->missing_listwise));
  pivot_table_submit (table);
}

/* Compares `struct dsc_var's A and B according to the ordering
   specified by CMD. */
static int
descriptives_compare_dsc_vars (const void *a_, const void *b_, const void *dsc_)
{
  const struct dsc_var *a = a_;
  const struct dsc_var *b = b_;
  const struct dsc_proc *dsc = dsc_;

  int result;

  if (dsc->sort_by_stat == DSC_NAME)
    result = utf8_strcasecmp (var_get_name (a->v), var_get_name (b->v));
  else
    {
      double as = a->stats[dsc->sort_by_stat];
      double bs = b->stats[dsc->sort_by_stat];

      result = as < bs ? -1 : as > bs;
    }

  if (dsc->sort_direction == SC_DESCEND)
    result = -result;

  return result;
}
