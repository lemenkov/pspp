/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2006, 2007, 2009, 2010, 2011, 2012, 2013, 2014, 2016 Free Software Foundation, Inc

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
#include <gsl/gsl_cdf.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"
#include "data/subcase.h"
#include "data/casewriter.h"
#include "data/short-names.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "language/commands/sort-criteria.h"
#include "math/sort.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/stringi-set.h"
#include "libpspp/taint.h"
#include "output/pivot-table.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

struct rank;

typedef double (*rank_function_t) (const struct rank*, double c, double cc, double cc_1,
                                   int i, double w);

static double rank_proportion (const struct rank *, double c, double cc, double cc_1,
                               int i, double w);

static double rank_normal (const struct rank *, double c, double cc, double cc_1,
                           int i, double w);

static double rank_percent (const struct rank *, double c, double cc, double cc_1,
                            int i, double w);

static double rank_rfraction (const struct rank *, double c, double cc, double cc_1,
                              int i, double w);

static double rank_rank (const struct rank *, double c, double cc, double cc_1,
                         int i, double w);

static double rank_n (const struct rank *, double c, double cc, double cc_1,
                      int i, double w);

static double rank_savage (const struct rank *, double c, double cc, double cc_1,
                           int i, double w);

static double rank_ntiles (const struct rank *, double c, double cc, double cc_1,
                           int i, double w);


enum rank_func
  {
    RANK,
    NORMAL,
    PERCENT,
    RFRACTION,
    PROPORTION,
    N,
    NTILES,
    SAVAGE,
    n_RANK_FUNCS
  };

static const struct fmt_spec dest_format[n_RANK_FUNCS] = {
  [RANK]       = { .type = FMT_F, .w = 9, .d = 3 },
  [NORMAL]     = { .type = FMT_F, .w = 6, .d = 4 },
  [PERCENT]    = { .type = FMT_F, .w = 6, .d = 2 },
  [RFRACTION]  = { .type = FMT_F, .w = 6, .d = 4 },
  [PROPORTION] = { .type = FMT_F, .w = 6, .d = 4 },
  [N]          = { .type = FMT_F, .w = 6, .d = 0 },
  [NTILES]     = { .type = FMT_F, .w = 3, .d = 0 },
  [SAVAGE]     = { .type = FMT_F, .w = 8, .d = 4 }
};

static const char * const function_name[n_RANK_FUNCS] = {
  "RANK",
  "NORMAL",
  "PERCENT",
  "RFRACTION",
  "PROPORTION",
  "N",
  "NTILES",
  "SAVAGE"
};

static const rank_function_t rank_func[n_RANK_FUNCS] = {
  rank_rank,
  rank_normal,
  rank_percent,
  rank_rfraction,
  rank_proportion,
  rank_n,
  rank_ntiles,
  rank_savage
};

static enum measure rank_measures[n_RANK_FUNCS] = {
  [RANK] = MEASURE_ORDINAL,
  [NORMAL] = MEASURE_ORDINAL,
  [PERCENT] = MEASURE_ORDINAL,
  [RFRACTION] = MEASURE_ORDINAL,
  [PROPORTION] = MEASURE_ORDINAL,
  [N] = MEASURE_SCALE,
  [NTILES] = MEASURE_ORDINAL,
  [SAVAGE] = MEASURE_ORDINAL,
};

enum ties
  {
    TIES_LOW,
    TIES_HIGH,
    TIES_MEAN,
    TIES_CONDENSE
  };

enum fraction
  {
    FRAC_BLOM,
    FRAC_RANKIT,
    FRAC_TUKEY,
    FRAC_VW
  };

struct rank_spec
{
  enum rank_func rfunc;
  const char **dest_names;
  const char **dest_labels;
};

/* If NEW_NAME exists in DICT or NEW_NAMES, returns NULL without changing
   anything.  Otherwise, inserts NEW_NAME in NEW_NAMES and returns the copy of
   NEW_NAME now in NEW_NAMES.  In any case, frees NEW_NAME. */
static const char *
try_new_name (char *new_name,
              const struct dictionary *dict, struct stringi_set *new_names)
{
  const char *retval = (!dict_lookup_var (dict, new_name)
                        && stringi_set_insert (new_names, new_name)
                        ? stringi_set_find_node (new_names, new_name)->string
                        : NULL);
  free (new_name);
  return retval;
}

/* Returns a variable name for storing ranks of a variable named SRC_NAME
   according to the rank function F.  The name chosen will not be one already in
   DICT or NEW_NAMES.

   If successful, adds the new name to NEW_NAMES and returns the name added.
   If no name can be generated, returns NULL. */
static const char *
rank_choose_dest_name (struct dictionary *dict, struct stringi_set *new_names,
                       enum rank_func f, const char *src_name)
{
  /* Try the first character of the ranking function followed by the first 7
     bytes of the srcinal variable name. */
  char *src_name_7 = utf8_encoding_trunc (src_name, dict_get_encoding (dict),
                                          7);
  const char *s = try_new_name (
    xasprintf ("%c%s", function_name[f][0], src_name_7), dict, new_names);
  free (src_name_7);
  if (s)
    return s;

  /* Try "fun###". */
  for (int i = 1; i <= 999; i++)
    {
      s = try_new_name (xasprintf ("%.3s%03d", function_name[f], i),
                        dict, new_names);
      if (s)
        return s;
    }

  /* Try "RNKfn##". */
  for (int i = 1; i <= 99; i++)
    {
      s = try_new_name (xasprintf ("RNK%.2s%02d", function_name[f], i),
                        dict, new_names);
      if (s)
        return s;
    }

  msg (ME, _("Cannot generate variable name for ranking %s with %s.  "
             "All candidates in use."),
       src_name, function_name[f]);
  return NULL;
}

struct rank
{
  struct dictionary *dict;

  struct subcase sc;

  const struct variable **vars;
  size_t n_vars;

  const struct variable **group_vars;
  size_t n_group_vars;


  enum mv_class exclude;

  struct rank_spec *rs;
  size_t n_rs;

  enum ties ties;

  enum fraction fraction;
  int k_ntiles;

  bool print;

  /* Pool on which cell functions may allocate data */
  struct pool *pool;
};


static void
destroy_rank (struct rank *rank)
{
  free (rank->vars);
  free (rank->group_vars);
  subcase_uninit (&rank->sc);
  pool_destroy (rank->pool);
}

static bool
parse_into (struct lexer *lexer, struct rank *cmd,
            struct stringi_set *new_names)
{
  enum rank_func rfunc;
  if (lex_match_id (lexer, "RANK"))
    rfunc = RANK;
  else if (lex_match_id (lexer, "NORMAL"))
    rfunc = NORMAL;
  else if (lex_match_id (lexer, "RFRACTION"))
    rfunc = RFRACTION;
  else if (lex_match_id (lexer, "N"))
    rfunc = N;
  else if (lex_match_id (lexer, "SAVAGE"))
    rfunc = SAVAGE;
  else if (lex_match_id (lexer, "PERCENT"))
    rfunc = PERCENT;
  else if (lex_match_id (lexer, "PROPORTION"))
    rfunc = PROPORTION;
  else if (lex_match_id (lexer, "NTILES"))
    {
      if (!lex_force_match (lexer, T_LPAREN)
          || !lex_force_int_range (lexer, "NTILES", 1, INT_MAX))
        return false;

      cmd->k_ntiles = lex_integer (lexer);
      lex_get (lexer);

      if (!lex_force_match (lexer, T_RPAREN))
        return false;

      rfunc = NTILES;
    }
  else
    {
      lex_error_expecting (lexer, "RANK", "NORMAL", "RFRACTION", "N",
                           "SAVAGE", "PERCENT", "PROPORTION", "NTILES");
      return false;
    }

  cmd->rs = pool_realloc (cmd->pool, cmd->rs, sizeof (*cmd->rs) * (cmd->n_rs + 1));
  struct rank_spec *rs = &cmd->rs[cmd->n_rs++];
  *rs = (struct rank_spec) {
    .rfunc = rfunc,
    .dest_names = pool_calloc (cmd->pool, cmd->n_vars,
                               sizeof *rs->dest_names),
  };

  if (lex_match_id (lexer, "INTO"))
    {
      int vars_start = lex_ofs (lexer);
      size_t var_count = 0;
      while (lex_token (lexer) == T_ID)
        {
          const char *name = lex_tokcstr (lexer);

          if (var_count >= subcase_get_n_fields (&cmd->sc))
            lex_ofs_error (lexer, vars_start, lex_ofs (lexer),
                           _("Too many variables in %s clause."), "INTO");
          else if (dict_lookup_var (cmd->dict, name) != NULL)
            lex_error (lexer, _("Variable %s already exists."), name);
          else if (stringi_set_contains (new_names, name))
            lex_error (lexer, _("Duplicate variable name %s."), name);
          else
            {
              stringi_set_insert (new_names, name);
              rs->dest_names[var_count++] = pool_strdup (cmd->pool, name);
              lex_get (lexer);
              continue;
            }

          /* Error path. */
          return false;
        }
    }

  return true;
}

/* Hardly a rank function. */
static double
rank_n (const struct rank *cmd UNUSED, double c UNUSED, double cc UNUSED, double cc_1 UNUSED,
        int i UNUSED, double w)
{
  return w;
}

static double
rank_rank (const struct rank *cmd, double c, double cc, double cc_1,
           int i, double w UNUSED)
{
  double rank;

  if (c >= 1.0)
    {
      switch (cmd->ties)
        {
        case TIES_LOW:
          rank = cc_1 + 1;
          break;
        case TIES_HIGH:
          rank = cc;
          break;
        case TIES_MEAN:
          rank = cc_1 + (c + 1.0)/ 2.0;
          break;
        case TIES_CONDENSE:
          rank = i;
          break;
        default:
          NOT_REACHED ();
        }
    }
  else
    {
      switch (cmd->ties)
        {
        case TIES_LOW:
          rank = cc_1;
          break;
        case TIES_HIGH:
          rank = cc;
          break;
        case TIES_MEAN:
          rank = cc_1 + c / 2.0;
          break;
        case TIES_CONDENSE:
          rank = i;
          break;
        default:
          NOT_REACHED ();
        }
    }

  return rank;
}


static double
rank_rfraction (const struct rank *cmd, double c, double cc, double cc_1,
                int i, double w)
{
  return rank_rank (cmd, c, cc, cc_1, i, w) / w;
}


static double
rank_percent (const struct rank *cmd, double c, double cc, double cc_1,
              int i, double w)
{
  return rank_rank (cmd, c, cc, cc_1, i, w) * 100.0 / w;
}


static double
rank_proportion (const struct rank *cmd, double c, double cc, double cc_1,
                 int i, double w)
{
  const double r =  rank_rank (cmd, c, cc, cc_1, i, w);

  double f;

  switch (cmd->fraction)
    {
    case FRAC_BLOM:
      f =  (r - 3.0/8.0) / (w + 0.25);
      break;
    case FRAC_RANKIT:
      f = (r - 0.5) / w;
      break;
    case FRAC_TUKEY:
      f = (r - 1.0/3.0) / (w + 1.0/3.0);
      break;
    case FRAC_VW:
      f = r / (w + 1.0);
      break;
    default:
      NOT_REACHED ();
    }


  return (f > 0) ? f : SYSMIS;
}

static double
rank_normal (const struct rank *cmd, double c, double cc, double cc_1,
             int i, double w)
{
  double f = rank_proportion (cmd, c, cc, cc_1, i, w);

  return gsl_cdf_ugaussian_Pinv (f);
}

static double
rank_ntiles (const struct rank *cmd, double c, double cc, double cc_1,
             int i, double w)
{
  double r = rank_rank (cmd, c, cc, cc_1, i, w);


  return (floor ((r * cmd->k_ntiles) / (w + 1)) + 1);
}

/* Expected value of the order statistics from an exponential distribution */
static double
ee (int j, double w_star)
{
  double sum = 0.0;

  for (int k = 1; k <= j; k++)
    sum += 1.0 / (w_star + 1 - k);

  return sum;
}


static double
rank_savage (const struct rank *cmd UNUSED, double c, double cc, double cc_1,
             int i UNUSED, double w)
{
  double int_part;
  const int i_1 = floor (cc_1);
  const int i_2 = floor (cc);

  const double w_star = (modf (w, &int_part) == 0) ? w : floor (w) + 1;

  const double g_1 = cc_1 - i_1;
  const double g_2 = cc - i_2;

  /* The second factor is infinite, when the first is zero.
     Therefore, evaluate the second, only when the first is non-zero */
  const double expr1 =  (1 - g_1) ? (1 - g_1) * ee(i_1+1, w_star) : (1 - g_1);
  const double expr2 =  g_2 ? g_2 * ee (i_2+1, w_star) : g_2;

  if (i_1 == i_2)
    return ee (i_1 + 1, w_star) - 1;

  if (i_1 + 1 == i_2)
    return ((expr1 + expr2)/c) - 1;

  if (i_1 + 2 <= i_2)
    {
      double sigma = 0.0;
      for (int j = i_1 + 2; j <= i_2; ++j)
        sigma += ee (j, w_star);
      return ((expr1 + expr2 + sigma) / c) -1;
    }

  NOT_REACHED ();
}

static double
sum_weights (const struct casereader *input, int weight_idx)
{
  if (weight_idx == -1)
    return casereader_count_cases (input);

  double w = 0.0;

  struct casereader *pass = casereader_clone (input);
  struct ccase *c;
  for (; (c = casereader_read (pass)) != NULL; case_unref (c))
    w += case_num_idx (c, weight_idx);
  casereader_destroy (pass);

  return w;
}

static void
rank_sorted_file (struct casereader *input,
                  struct casewriter *output,
                  int weight_idx,
                  const struct rank *cmd)
{
  int tie_group = 1;
  double cc = 0.0;

  /* Get total group weight. */
  double w = sum_weights (input, weight_idx);

  /* Do ranking. */
  struct subcase input_var = SUBCASE_EMPTY_INITIALIZER;
  subcase_add (&input_var, 0, 0, SC_ASCEND);
  struct casegrouper *tie_grouper = casegrouper_create_subcase (input, &input_var);
  subcase_uninit (&input_var);

  struct casereader *tied_cases;
  for (; casegrouper_get_next_group (tie_grouper, &tied_cases);
       casereader_destroy (tied_cases))
    {
      double tw = sum_weights (tied_cases, weight_idx);
      double cc_1 = cc;
      cc += tw;

      taint_propagate (casereader_get_taint (tied_cases),
                       casewriter_get_taint (output));

      /* Rank tied cases. */
      struct ccase *c;
      for (; (c = casereader_read (tied_cases)) != NULL; case_unref (c))
        {
          struct ccase *out_case = case_create (casewriter_get_proto (output));
          *case_num_rw_idx (out_case, 0) = case_num_idx (c, 1);
          for (size_t i = 0; i < cmd->n_rs; ++i)
            {
              rank_function_t func = rank_func[cmd->rs[i].rfunc];
              double rank = func (cmd, tw, cc, cc_1, tie_group, w);
              *case_num_rw_idx (out_case, i + 1) = rank;
            }

          casewriter_write (output, out_case);
        }
      tie_group++;
    }
  casegrouper_destroy (tie_grouper);
}


static bool
rank_cmd (struct dataset *ds,  const struct rank *cmd);

static const char *
fraction_name (const struct rank *cmd)
{
  switch (cmd->fraction)
    {
    case FRAC_BLOM:   return "BLOM";
    case FRAC_RANKIT: return "RANKIT";
    case FRAC_TUKEY:  return "TUKEY";
    case FRAC_VW:     return "VW";
    default:          NOT_REACHED ();
    }
}

/* Returns a label for a variable derived from SRC_VAR with function F. */
static const char *
create_var_label (struct rank *cmd, const struct variable *src_var,
                  enum rank_func f)
{
  if (cmd->n_group_vars > 0)
    {
      struct string group_var_str = DS_EMPTY_INITIALIZER;
      for (size_t g = 0; g < cmd->n_group_vars; ++g)
        {
          if (g > 0)
            ds_put_cstr (&group_var_str, " ");
          ds_put_cstr (&group_var_str, var_get_name (cmd->group_vars[g]));
        }

      const char *label = pool_asprintf (
        cmd->pool, _("%s of %s by %s"), function_name[f],
        var_get_name (src_var), ds_cstr (&group_var_str));
      ds_destroy (&group_var_str);
      return label;
    }
  else
    return pool_asprintf (cmd->pool, _("%s of %s"),
                          function_name[f], var_get_name (src_var));
}

int
cmd_rank (struct lexer *lexer, struct dataset *ds)
{
  struct stringi_set new_names = STRINGI_SET_INITIALIZER (new_names);
  struct rank rank = {
    .sc = SUBCASE_EMPTY_INITIALIZER,
    .exclude = MV_ANY,
    .dict = dataset_dict (ds),
    .ties = TIES_MEAN,
    .fraction = FRAC_BLOM,
    .print = true,
    .pool = pool_create (),
  };

  if (lex_match_id (lexer, "VARIABLES") && !lex_force_match (lexer, T_EQUALS))
    goto error;

  if (!parse_sort_criteria (lexer, rank.dict, &rank.sc, &rank.vars, NULL))
    goto error;
  rank.n_vars = rank.sc.n_fields;

  if (lex_match (lexer, T_BY)
      && !parse_variables_const (lexer, rank.dict,
                                 &rank.group_vars, &rank.n_group_vars,
                                 PV_NO_DUPLICATE | PV_NO_SCRATCH))
    goto error;

  while (lex_token (lexer) != T_ENDCMD)
    {
      if (!lex_force_match (lexer, T_SLASH))
        goto error;
      if (lex_match_id (lexer, "TIES"))
        {
          if (!lex_force_match (lexer, T_EQUALS))
            goto error;
          if (lex_match_id (lexer, "MEAN"))
            rank.ties = TIES_MEAN;
          else if (lex_match_id (lexer, "LOW"))
            rank.ties = TIES_LOW;
          else if (lex_match_id (lexer, "HIGH"))
            rank.ties = TIES_HIGH;
          else if (lex_match_id (lexer, "CONDENSE"))
            rank.ties = TIES_CONDENSE;
          else
            {
              lex_error_expecting (lexer, "MEAN", "LOW", "HIGH", "CONDENSE");
              goto error;
            }
        }
      else if (lex_match_id (lexer, "FRACTION"))
        {
          if (!lex_force_match (lexer, T_EQUALS))
            goto error;
          if (lex_match_id (lexer, "BLOM"))
            rank.fraction = FRAC_BLOM;
          else if (lex_match_id (lexer, "TUKEY"))
            rank.fraction = FRAC_TUKEY;
          else if (lex_match_id (lexer, "VW"))
            rank.fraction = FRAC_VW;
          else if (lex_match_id (lexer, "RANKIT"))
            rank.fraction = FRAC_RANKIT;
          else
            {
              lex_error_expecting (lexer, "BLOM", "TUKEY", "VW", "RANKIT");
              goto error;
            }
        }
      else if (lex_match_id (lexer, "PRINT"))
        {
          if (!lex_force_match (lexer, T_EQUALS))
            goto error;
          if (lex_match_id (lexer, "YES"))
            rank.print = true;
          else if (lex_match_id (lexer, "NO"))
            rank.print = false;
          else
            {
              lex_error_expecting (lexer, "YES", "NO");
              goto error;
            }
        }
      else if (lex_match_id (lexer, "MISSING"))
        {
          if (!lex_force_match (lexer, T_EQUALS))
            goto error;
          if (lex_match_id (lexer, "INCLUDE"))
            rank.exclude = MV_SYSTEM;
          else if (lex_match_id (lexer, "EXCLUDE"))
            rank.exclude = MV_ANY;
          else
            {
              lex_error_expecting (lexer, "INCLUDE", "EXCLUDE");
              goto error;
            }
        }
      else if (!parse_into (lexer, &rank, &new_names))
        goto error;
    }


  /* If no rank specs are given, then apply a default */
  if (rank.n_rs == 0)
    {
      struct rank_spec *rs = pool_malloc (rank.pool, sizeof *rs);
      *rs = (struct rank_spec) {
        .rfunc = RANK,
        .dest_names = pool_calloc (rank.pool, rank.n_vars,
                                   sizeof *rs->dest_names),
      };

      rank.rs = rs;
      rank.n_rs = 1;
    }

  /* Choose variable names for all rank destinations which haven't already been
     created with INTO. */
  for (struct rank_spec *rs = rank.rs; rs < &rank.rs[rank.n_rs]; rs++)
    {
      rs->dest_labels = pool_calloc (rank.pool, rank.n_vars,
                                     sizeof *rs->dest_labels);
      for (int v = 0; v < rank.n_vars;  v ++)
        {
          const char **dst_name = &rs->dest_names[v];
          if (*dst_name == NULL)
            {
              *dst_name = rank_choose_dest_name (rank.dict, &new_names,
                                                 rs->rfunc,
                                                 var_get_name (rank.vars[v]));
              if (*dst_name == NULL)
                goto error;
            }

          rs->dest_labels[v] = create_var_label (&rank, rank.vars[v],
                                                 rs->rfunc);
        }
    }

  if (rank.print)
    {
      struct pivot_table *table = pivot_table_create (
        N_("Variables Created by RANK"));

      pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("New Variable"),
                              N_("New Variable"), N_("Function"),
                              N_("Fraction"), N_("Grouping Variables"));

      struct pivot_dimension *variables = pivot_dimension_create (
        table, PIVOT_AXIS_ROW, N_("Existing Variable"),
        N_("Existing Variable"));
      variables->root->show_label = true;

      for (size_t i = 0; i <  rank.n_rs; ++i)
        {
          for (size_t v = 0; v < rank.n_vars;  v ++)
            {
              int row_idx = pivot_category_create_leaf (
                variables->root, pivot_value_new_variable (rank.vars[v]));

              struct string group_vars = DS_EMPTY_INITIALIZER;
              for (int g = 0; g < rank.n_group_vars; ++g)
                {
                  if (g)
                    ds_put_byte (&group_vars, ' ');
                  ds_put_cstr (&group_vars, var_get_name (rank.group_vars[g]));
                }

              enum rank_func rfunc = rank.rs[i].rfunc;
              bool has_fraction = rfunc == NORMAL || rfunc == PROPORTION;
              const char *entries[] =
                {
                  rank.rs[i].dest_names[v],
                  function_name[rank.rs[i].rfunc],
                  has_fraction ? fraction_name (&rank) : NULL,
                  rank.n_group_vars ? ds_cstr (&group_vars) : NULL,
                };
              for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
                {
                  const char *entry = entries[j];
                  if (entry)
                    pivot_table_put2 (table, j, row_idx,
                                      pivot_value_new_user_text (entry, -1));
                }
              ds_destroy (&group_vars);
            }
        }

      pivot_table_submit (table);
    }

  /* Do the ranking */
  rank_cmd (ds, &rank);

  destroy_rank (&rank);
  stringi_set_destroy (&new_names);
  return CMD_SUCCESS;

 error:
  destroy_rank (&rank);
  stringi_set_destroy (&new_names);
  return CMD_FAILURE;
}

/* RANK transformation. */
struct rank_trns
  {
    struct caseproto *proto;

    int order_case_idx;

    struct rank_trns_input_var *input_vars;
    size_t n_input_vars;

    size_t n_funcs;
  };

struct rank_trns_input_var
  {
    struct casereader *input;
    struct ccase *current;

    size_t *output_var_indexes;
  };

static void
advance_ranking (struct rank_trns_input_var *iv)
{
  case_unref (iv->current);
  iv->current = casereader_read (iv->input);
}

static struct ccase *
rank_translate (struct ccase *c, void *trns_)
{
  struct rank_trns *trns = trns_;

  c = case_unshare_and_resize (c, trns->proto);
  double order = case_num_idx (c, trns->order_case_idx);
  for (struct rank_trns_input_var *iv = trns->input_vars;
       iv < &trns->input_vars[trns->n_input_vars]; iv++)
    {
      for (size_t i = 0; i < trns->n_funcs; i++)
        *case_num_rw_idx (c, iv->output_var_indexes[i]) = SYSMIS;

      while (iv->current != NULL)
        {
          double iv_order = case_num_idx (iv->current, 0);
          if (iv_order == order)
            {
              for (size_t i = 0; i < trns->n_funcs; i++)
                *case_num_rw_idx (c, iv->output_var_indexes[i])
                  = case_num_idx (iv->current, i + 1);
              advance_ranking (iv);
              break;
            }
          else if (iv_order > order)
            break;
          else
            advance_ranking (iv);
        }
      }
  return c;
}

static bool
rank_trns_free (void *trns_)
{
  struct rank_trns *trns = trns_;

  caseproto_unref (trns->proto);
  for (struct rank_trns_input_var *iv = trns->input_vars;
       iv < &trns->input_vars[trns->n_input_vars]; iv++)
    {
      casereader_destroy (iv->input);
      case_unref (iv->current);

      free (iv->output_var_indexes);
    }
  free (trns->input_vars);
  free (trns);

  return true;
}

static const struct casereader_translator_class rank_trns_class = {
  .translate = rank_translate,
  .destroy = rank_trns_free,
};

static bool
rank_cmd (struct dataset *ds, const struct rank *cmd)
{
  struct dictionary *d = dataset_dict (ds);
  struct variable *weight_var = dict_get_weight (d);
  bool ok = true;

  struct variable *order_var = add_permanent_ordering_transformation (ds);

  /* Create output files. */
  struct caseproto *output_proto = caseproto_create ();
  for (size_t i = 0; i < cmd->n_rs + 1; i++)
    output_proto = caseproto_add_width (output_proto, 0);

  struct subcase by_order;
  subcase_init (&by_order, 0, 0, SC_ASCEND);

  struct casewriter **outputs = xnmalloc (cmd->n_vars, sizeof *outputs);
  for (size_t i = 0; i < cmd->n_vars; i++)
    outputs[i] = sort_create_writer (&by_order, output_proto);

  subcase_uninit (&by_order);
  caseproto_unref (output_proto);

  /* Open the active file and make one pass per input variable. */
  struct casereader *input = proc_open (ds);
  input = casereader_create_filter_weight (input, d, NULL, NULL);
  for (size_t i = 0; i < cmd->n_vars; ++i)
    {
      const struct variable *input_var = cmd->vars[i];

      /* Discard cases that have missing values of input variable. */
      struct casereader *input_pass
        = i == cmd->n_vars - 1 ? input : casereader_clone (input);
      input_pass = casereader_create_filter_missing (input_pass, &input_var, 1,
                                                     cmd->exclude, NULL, NULL);

      /* Keep only the columns we really need, to save time and space when we
         sort them just below.

         After this projection, the input_pass case indexes look like:

           - 0: input_var.
           - 1: order_var.
           - 2 and up: cmd->n_group_vars group variables
           - 2 + cmd->n_group_vars and up: split variables
           - 2 + cmd->n_group_vars + n_split_vars: weight var
      */
      struct subcase projection = SUBCASE_EMPTY_INITIALIZER;
      subcase_add_var_always (&projection, input_var, SC_ASCEND);
      subcase_add_var_always (&projection, order_var, SC_ASCEND);
      subcase_add_vars_always (&projection,
                               cmd->group_vars, cmd->n_group_vars);
      subcase_add_vars_always (&projection, dict_get_split_vars (d),
                               dict_get_n_splits (d));
      int weight_idx;
      if (weight_var != NULL)
        {
          subcase_add_var_always (&projection, weight_var, SC_ASCEND);
          weight_idx = 2 + cmd->n_group_vars + dict_get_n_splits (d);
        }
      else
        weight_idx = -1;
      input_pass = casereader_project (input_pass, &projection);
      subcase_uninit (&projection);

      /* Prepare 'group_vars' as the set of grouping variables. */
      struct subcase group_vars = SUBCASE_EMPTY_INITIALIZER;
      for (size_t j = 0; j < cmd->n_group_vars; j++)
        subcase_add_always (&group_vars,
                            j + 2, var_get_width (cmd->group_vars[j]),
                            SC_ASCEND);

      /* Prepare 'rank_ordering' for sorting with the group variables as
         primary key and the input variable as secondary key. */
      struct subcase rank_ordering;
      subcase_clone (&rank_ordering, &group_vars);
      subcase_add (&rank_ordering, 0, 0, subcase_get_direction (&cmd->sc, i));

      /* Group by split variables */
      struct subcase split_vars = SUBCASE_EMPTY_INITIALIZER;
      for (size_t j = 0; j < dict_get_n_splits (d); j++)
        subcase_add_always (&split_vars, 2 + j + cmd->n_group_vars,
                            var_get_width (dict_get_split_vars (d)[j]),
                            SC_ASCEND);

      struct casegrouper *split_grouper
        = casegrouper_create_subcase (input_pass, &split_vars);
      subcase_uninit (&split_vars);

      struct casereader *split_group;
      while (casegrouper_get_next_group (split_grouper, &split_group))
        {
          struct casereader *ordered;
          struct casegrouper *by_grouper;
          struct casereader *by_group;

          ordered = sort_execute (split_group, &rank_ordering);
          by_grouper = casegrouper_create_subcase (ordered, &group_vars);
          while (casegrouper_get_next_group (by_grouper, &by_group))
            rank_sorted_file (by_group, outputs[i], weight_idx, cmd);
          ok = casegrouper_destroy (by_grouper) && ok;
        }
      subcase_uninit (&group_vars);
      subcase_uninit (&rank_ordering);

      ok = casegrouper_destroy (split_grouper) && ok;
    }
  ok = proc_commit (ds) && ok;

  /* Re-fetch the dictionary and order variable, because if TEMPORARY was in
     effect then there's a new dictionary. */
  d = dataset_dict (ds);
  order_var = dict_lookup_var_assert (d, "$ORDER");

  /* Merge the original data set with the ranks (which we already sorted on
     $ORDER). */
  struct rank_trns *trns = xmalloc (sizeof *trns);
  *trns = (struct rank_trns) {
    .order_case_idx = var_get_dict_index (order_var),
    .input_vars = xnmalloc (cmd->n_vars, sizeof *trns->input_vars),
    .n_input_vars = cmd->n_vars,
    .n_funcs = cmd->n_rs,
  };
  for (size_t i = 0; i < trns->n_input_vars; i++)
    {
      struct rank_trns_input_var *iv = &trns->input_vars[i];

      iv->input = casewriter_make_reader (outputs[i]);
      iv->current = casereader_read (iv->input);
      iv->output_var_indexes = xnmalloc (trns->n_funcs,
                                         sizeof *iv->output_var_indexes);
      for (size_t j = 0; j < trns->n_funcs; j++)
        {
          struct rank_spec *rs = &cmd->rs[j];
          struct variable *var;

          var = dict_create_var_assert (d, rs->dest_names[i], 0);
          var_set_both_formats (var, dest_format[rs->rfunc]);
          var_set_label (var, rs->dest_labels[i]);
          var_set_measure (var, rank_measures[rs->rfunc]);

          iv->output_var_indexes[j] = var_get_dict_index (var);
        }
    }
  free (outputs);

  trns->proto = caseproto_ref (dict_get_proto (d)),
  dataset_set_source (ds, casereader_translate_stateless (
                        dataset_steal_source (ds), trns->proto,
                        &rank_trns_class, trns));

  /* Delete our sort key, which we don't need anymore. */
  dataset_delete_vars (ds, &order_var, 1);

  return ok;
}
