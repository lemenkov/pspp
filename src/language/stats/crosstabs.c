/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012, 2013, 2014, 2016 Free Software Foundation, Inc.

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

/* FIXME:

   - How to calculate significance of some symmetric and directional measures?
   - How to calculate ASE for symmetric Somers ' d?
   - How to calculate ASE for Goodman and Kruskal's tau?
   - How to calculate approx. T of symmetric uncertainty coefficient?

*/

#include <config.h>

#include <ctype.h>
#include <float.h>
#include <gsl/gsl_cdf.h>
#include <stdlib.h>
#include <stdio.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/data-out.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/stats/freq.h"
#include "language/dictionary/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/hmapx.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "output/pivot-table.h"
#include "output/charts/barchart.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xsize.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Kinds of cells in the crosstabulation. */
#define CRS_CELLS                                               \
    C(COUNT, N_("Count"), PIVOT_RC_COUNT)                       \
    C(EXPECTED, N_("Expected"), PIVOT_RC_OTHER)                 \
    C(ROW, N_("Row %"), PIVOT_RC_PERCENT)                       \
    C(COLUMN, N_("Column %"), PIVOT_RC_PERCENT)                 \
    C(TOTAL, N_("Total %"), PIVOT_RC_PERCENT)                   \
    C(RESIDUAL, N_("Residual"), PIVOT_RC_RESIDUAL)              \
    C(SRESIDUAL, N_("Std. Residual"), PIVOT_RC_RESIDUAL)        \
    C(ASRESIDUAL, N_("Adjusted Residual"), PIVOT_RC_RESIDUAL)
enum crs_cell
  {
#define C(KEYWORD, STRING, RC) CRS_CL_##KEYWORD,
    CRS_CELLS
#undef C
  };
enum {
#define C(KEYWORD, STRING, RC) + 1
  CRS_N_CELLS = CRS_CELLS
#undef C
};
#define CRS_ALL_CELLS ((1u << CRS_N_CELLS) - 1)

/* Kinds of statistics. */
#define CRS_STATISTICS                          \
    S(CHISQ)                                    \
    S(PHI)                                      \
    S(CC)                                       \
    S(LAMBDA)                                   \
    S(UC)                                       \
    S(BTAU)                                     \
    S(CTAU)                                     \
    S(RISK)                                     \
    S(GAMMA)                                    \
    S(D)                                        \
    S(KAPPA)                                    \
    S(ETA)                                      \
    S(CORR)
enum crs_statistic_index {
#define S(KEYWORD) CRS_ST_##KEYWORD##_INDEX,
  CRS_STATISTICS
#undef S
};
enum crs_statistic_bit {
#define S(KEYWORD) CRS_ST_##KEYWORD = 1u << CRS_ST_##KEYWORD##_INDEX,
  CRS_STATISTICS
#undef S
};
enum {
#define S(KEYWORD) + 1
  CRS_N_STATISTICS = CRS_STATISTICS
#undef S
};
#define CRS_ALL_STATISTICS ((1u << CRS_N_STATISTICS) - 1)

/* Number of chi-square statistics. */
#define N_CHISQ 5

/* Number of symmetric statistics. */
#define N_SYMMETRIC 9

/* Number of directional statistics. */
#define N_DIRECTIONAL 13

/* Indexes into the 'vars' member of struct crosstabulation and
   struct crosstab member. */
enum
  {
    ROW_VAR = 0,                /* Row variable. */
    COL_VAR = 1                 /* Column variable. */
    /* Higher indexes cause multiple tables to be output. */
  };

struct xtab_var
  {
    const struct variable *var;
    union value *values;
    size_t n_values;
  };

/* A crosstabulation of 2 or more variables. */
struct crosstabulation
  {
    struct crosstabs_proc *proc;
    struct fmt_spec weight_format; /* Format for weight variable. */
    double missing;             /* Weight of missing cases. */

    /* Variables (2 or more). */
    int n_vars;
    struct xtab_var *vars;

    /* Constants (0 or more). */
    int n_consts;
    struct xtab_var *const_vars;
    size_t *const_indexes;

    /* Data. */
    struct hmap data;
    struct freq **entries;
    size_t n_entries;

    /* Number of statistically interesting columns/rows
       (columns/rows with data in them). */
    int ns_cols, ns_rows;

    /* Matrix contents. */
    double *mat;		/* Matrix proper. */
    double *row_tot;		/* Row totals. */
    double *col_tot;		/* Column totals. */
    double total;		/* Grand total. */
  };

/* Integer mode variable info. */
struct var_range
  {
    struct hmap_node hmap_node; /* In struct crosstabs_proc var_ranges map. */
    const struct variable *var; /* The variable. */
    int min;			/* Minimum value. */
    int max;			/* Maximum value + 1. */
    int count;			/* max - min. */
  };

struct crosstabs_proc
  {
    const struct dictionary *dict;
    enum { INTEGER, GENERAL } mode;
    enum mv_class exclude;
    bool barchart;
    bool bad_warn;
    struct fmt_spec weight_format;

    /* Variables specifies on VARIABLES. */
    const struct variable **variables;
    size_t n_variables;
    struct hmap var_ranges;

    /* TABLES. */
    struct crosstabulation *pivots;
    int n_pivots;

    /* CELLS. */
    int n_cells;		/* Number of cells requested. */
    unsigned int cells;         /* Bit k is 1 if cell k is requested. */
    int a_cells[CRS_N_CELLS];   /* 0...n_cells-1 are the requested cells. */

    /* Rounding of cells. */
    bool round_case_weights;    /* Round case weights? */
    bool round_cells;           /* If !round_case_weights, round cells? */
    bool round_down;            /* Round down? (otherwise to nearest) */

    /* STATISTICS. */
    unsigned int statistics;    /* Bit k is 1 if statistic k is requested. */

    bool descending;            /* True if descending sort order is requested. */
  };

static bool parse_crosstabs_tables (struct lexer *, struct dataset *,
                                    struct crosstabs_proc *);
static bool parse_crosstabs_variables (struct lexer *, struct dataset *,
                                       struct crosstabs_proc *);

static const struct var_range *get_var_range (const struct crosstabs_proc *,
                                              const struct variable *);

static bool should_tabulate_case (const struct crosstabulation *,
                                  const struct ccase *, enum mv_class exclude);
static void tabulate_general_case (struct crosstabulation *, const struct ccase *,
                                   double weight);
static void tabulate_integer_case (struct crosstabulation *, const struct ccase *,
                                   double weight);
static void postcalc (struct crosstabs_proc *);

static double
round_weight (const struct crosstabs_proc *proc, double weight)
{
  return proc->round_down ? floor (weight) : floor (weight + 0.5);
}

#define FOR_EACH_POPULATED_COLUMN(C, XT) \
  for (int C = next_populated_column (0, XT); \
       C < (XT)->vars[COL_VAR].n_values;      \
       C = next_populated_column (C + 1, XT))
static int
next_populated_column (int c, const struct crosstabulation *xt)
{
  int n_columns = xt->vars[COL_VAR].n_values;
  for (; c < n_columns; c++)
    if (xt->col_tot[c])
      break;
  return c;
}

#define FOR_EACH_POPULATED_ROW(R, XT) \
  for (int R = next_populated_row (0, XT); R < (XT)->vars[ROW_VAR].n_values; \
       R = next_populated_row (R + 1, XT))
static int
next_populated_row (int r, const struct crosstabulation *xt)
{
  int n_rows = xt->vars[ROW_VAR].n_values;
  for (; r < n_rows; r++)
    if (xt->row_tot[r])
      break;
  return r;
}

/* Parses and executes the CROSSTABS procedure. */
int
cmd_crosstabs (struct lexer *lexer, struct dataset *ds)
{
  int result = CMD_FAILURE;

  struct crosstabs_proc proc = {
    .dict = dataset_dict (ds),
    .mode = GENERAL,
    .exclude = MV_ANY,
    .barchart = false,
    .bad_warn = true,
    .weight_format = *dict_get_weight_format (dataset_dict (ds)),

    .variables = NULL,
    .n_variables = 0,
    .var_ranges = HMAP_INITIALIZER (proc.var_ranges),

    .pivots = NULL,
    .n_pivots = 0,

    .cells = 1u << CRS_CL_COUNT,
    /* n_cells and a_cells will be filled in later. */

    .round_case_weights = false,
    .round_cells = false,
    .round_down = false,

    .statistics = 0,

    .descending = false,
  };
  bool show_tables = true;
  lex_match (lexer, T_SLASH);
  for (;;)
    {
      if (lex_match_id (lexer, "VARIABLES"))
        {
          if (!parse_crosstabs_variables (lexer, ds, &proc))
            goto exit;
        }
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "TABLE"))
            proc.exclude = MV_ANY;
          else if (lex_match_id (lexer, "INCLUDE"))
            proc.exclude = MV_SYSTEM;
          else if (lex_match_id (lexer, "REPORT"))
            proc.exclude = MV_NEVER;
          else
            {
              lex_error (lexer, NULL);
              goto exit;
            }
        }
      else if (lex_match_id (lexer, "COUNT"))
        {
          lex_match (lexer, T_EQUALS);

          /* Default is CELL. */
          proc.round_case_weights = false;
          proc.round_cells = true;

          while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
            {
              if (lex_match_id (lexer, "ASIS"))
                {
                  proc.round_case_weights = false;
                  proc.round_cells = false;
                }
              else if (lex_match_id (lexer, "CASE"))
                {
                  proc.round_case_weights = true;
                  proc.round_cells = false;
                }
              else if (lex_match_id (lexer, "CELL"))
                {
                  proc.round_case_weights = false;
                  proc.round_cells = true;
                }
              else if (lex_match_id (lexer, "ROUND"))
                proc.round_down = false;
              else if (lex_match_id (lexer, "TRUNCATE"))
                proc.round_down = true;
              else
                {
                  lex_error (lexer, NULL);
                  goto exit;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "FORMAT"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
            {
              if (lex_match_id (lexer, "AVALUE"))
                proc.descending = false;
              else if (lex_match_id (lexer, "DVALUE"))
                proc.descending = true;
              else if (lex_match_id (lexer, "TABLES"))
                show_tables = true;
              else if (lex_match_id (lexer, "NOTABLES"))
                show_tables = false;
              else
                {
                  lex_error (lexer, NULL);
                  goto exit;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "BARCHART"))
        proc.barchart = true;
      else if (lex_match_id (lexer, "CELLS"))
        {
          lex_match (lexer, T_EQUALS);

          if (lex_match_id (lexer, "NONE"))
            proc.cells = 0;
          else if (lex_match (lexer, T_ALL))
            proc.cells = CRS_ALL_CELLS;
          else
            {
              proc.cells = 0;
              while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
                {
#define C(KEYWORD, STRING, RC)                                  \
                  if (lex_match_id (lexer, #KEYWORD))           \
                    {                                           \
                      proc.cells |= 1u << CRS_CL_##KEYWORD;     \
                      continue;                                 \
                    }
                  CRS_CELLS
#undef C
                  lex_error (lexer, NULL);
                  goto exit;
                }
              if (!proc.cells)
                proc.cells = ((1u << CRS_CL_COUNT) | (1u << CRS_CL_ROW)
                              | (1u << CRS_CL_COLUMN) | (1u << CRS_CL_TOTAL));
            }
        }
      else if (lex_match_id (lexer, "STATISTICS"))
        {
          lex_match (lexer, T_EQUALS);

          if (lex_match_id (lexer, "NONE"))
            proc.statistics = 0;
          else if (lex_match (lexer, T_ALL))
            proc.statistics = CRS_ALL_STATISTICS;
          else
            {
              proc.statistics = 0;
              while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
                {
#define S(KEYWORD)                                              \
                  if (lex_match_id (lexer, #KEYWORD))           \
                    {                                           \
                      proc.statistics |= CRS_ST_##KEYWORD;      \
                      continue;                                 \
                    }
                  CRS_STATISTICS
#undef S
                  lex_error (lexer, NULL);
                  goto exit;
                }
              if (!proc.statistics)
                proc.statistics = CRS_ST_CHISQ;
            }
        }
      else if (!parse_crosstabs_tables (lexer, ds, &proc))
        goto exit;

      if (!lex_match (lexer, T_SLASH))
        break;
    }
  if (!lex_end_of_command (lexer))
    goto exit;

  if (!proc.n_pivots)
    {
      msg (SE, _("At least one crosstabulation must be requested (using "
                 "the TABLES subcommand)."));
      goto exit;
    }

  /* Cells. */
  if (!show_tables)
    proc.cells = 0;
  for (int i = 0; i < CRS_N_CELLS; i++)
    if (proc.cells & (1u << i))
      proc.a_cells[proc.n_cells++] = i;
  assert (proc.n_cells < CRS_N_CELLS);

  /* Missing values. */
  if (proc.mode == GENERAL && proc.exclude == MV_NEVER)
    {
      msg (SE, _("Missing mode %s not allowed in general mode.  "
		 "Assuming %s."), "REPORT", "MISSING=TABLE");
      proc.exclude = MV_ANY;
    }

  struct casereader *input = casereader_create_filter_weight (proc_open (ds),
                                                              dataset_dict (ds),
                                                              NULL, NULL);
  struct casegrouper *grouper = casegrouper_create_splits (input, dataset_dict (ds));
  struct casereader *group;
  while (casegrouper_get_next_group (grouper, &group))
    {
      struct ccase *c;

      /* Output SPLIT FILE variables. */
      c = casereader_peek (group, 0);
      if (c != NULL)
        {
          output_split_file_values (ds, c);
          case_unref (c);
        }

      /* Initialize hash tables. */
      for (struct crosstabulation *xt = &proc.pivots[0];
           xt < &proc.pivots[proc.n_pivots]; xt++)
        hmap_init (&xt->data);

      /* Tabulate. */
      for (; (c = casereader_read (group)) != NULL; case_unref (c))
        for (struct crosstabulation *xt = &proc.pivots[0];
             xt < &proc.pivots[proc.n_pivots]; xt++)
          {
            double weight = dict_get_case_weight (dataset_dict (ds), c,
                                                  &proc.bad_warn);
            if (proc.round_case_weights)
              {
                weight = round_weight (&proc, weight);
                if (weight == 0.)
                  continue;
              }
            if (should_tabulate_case (xt, c, proc.exclude))
              {
                if (proc.mode == GENERAL)
                  tabulate_general_case (xt, c, weight);
                else
                  tabulate_integer_case (xt, c, weight);
              }
            else
              xt->missing += weight;
          }
      casereader_destroy (group);

      /* Output. */
      postcalc (&proc);
    }
  bool ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  result = ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

exit:
  free (proc.variables);

  struct var_range *range, *next_range;
  HMAP_FOR_EACH_SAFE (range, next_range, struct var_range, hmap_node,
                      &proc.var_ranges)
    {
      hmap_delete (&proc.var_ranges, &range->hmap_node);
      free (range);
    }
  for (struct crosstabulation *xt = &proc.pivots[0];
       xt < &proc.pivots[proc.n_pivots]; xt++)
    {
      free (xt->vars);
      free (xt->const_vars);
      free (xt->const_indexes);
    }
  free (proc.pivots);

  return result;
}

/* Parses the TABLES subcommand. */
static bool
parse_crosstabs_tables (struct lexer *lexer, struct dataset *ds,
                        struct crosstabs_proc *proc)
{
  const struct variable ***by = NULL;
  size_t *by_nvar = NULL;
  bool ok = false;

  /* Ensure that this is a TABLES subcommand. */
  if (!lex_match_id (lexer, "TABLES")
      && (lex_token (lexer) != T_ID ||
	  dict_lookup_var (dataset_dict (ds), lex_tokcstr (lexer)) == NULL)
      && lex_token (lexer) != T_ALL)
    {
      lex_error (lexer, NULL);
      return false;
    }
  lex_match (lexer, T_EQUALS);

  struct const_var_set *var_set
    = (proc->variables
       ? const_var_set_create_from_array (proc->variables,
                                          proc->n_variables)
       : const_var_set_create_from_dict (dataset_dict (ds)));

  size_t nx = 1;
  int n_by = 0;
  for (;;)
    {
      by = xnrealloc (by, n_by + 1, sizeof *by);
      by_nvar = xnrealloc (by_nvar, n_by + 1, sizeof *by_nvar);
      if (!parse_const_var_set_vars (lexer, var_set, &by[n_by], &by_nvar[n_by],
                                     PV_NO_DUPLICATE | PV_NO_SCRATCH))
	goto done;
      if (xalloc_oversized (nx, by_nvar[n_by]))
        {
          msg (SE, _("Too many cross-tabulation variables or dimensions."));
          goto done;
        }
      nx *= by_nvar[n_by];
      n_by++;

      if (!lex_match (lexer, T_BY))
	{
	  if (n_by < 2)
	    goto done;
	  else
	    break;
	}
    }

  int *by_iter = xcalloc (n_by, sizeof *by_iter);
  proc->pivots = xnrealloc (proc->pivots,
                            proc->n_pivots + nx, sizeof *proc->pivots);
  for (int i = 0; i < nx; i++)
    {
      struct crosstabulation *xt = &proc->pivots[proc->n_pivots++];

      *xt = (struct crosstabulation) {
        .proc = proc,
        .weight_format = proc->weight_format,
        .missing = 0.,
        .n_vars = n_by,
        .vars = xcalloc (n_by, sizeof *xt->vars),
        .n_consts = 0,
        .const_vars = NULL,
        .const_indexes = NULL,
      };

      for (int j = 0; j < n_by; j++)
        xt->vars[j].var = by[j][by_iter[j]];

      for (int j = n_by - 1; j >= 0; j--)
        {
          if (++by_iter[j] < by_nvar[j])
            break;
          by_iter[j] = 0;
        }
    }
  free (by_iter);
  ok = true;

done:
  /* All return paths lead here. */
  for (int i = 0; i < n_by; i++)
    free (by[i]);
  free (by);
  free (by_nvar);

  const_var_set_destroy (var_set);

  return ok;
}

/* Parses the VARIABLES subcommand. */
static bool
parse_crosstabs_variables (struct lexer *lexer, struct dataset *ds,
                           struct crosstabs_proc *proc)
{
  if (proc->n_pivots)
    {
      msg (SE, _("%s must be specified before %s."), "VARIABLES", "TABLES");
      return false;
    }

  lex_match (lexer, T_EQUALS);

  for (;;)
    {
      size_t orig_nv = proc->n_variables;

      if (!parse_variables_const (lexer, dataset_dict (ds),
                                  &proc->variables, &proc->n_variables,
                                  (PV_APPEND | PV_NUMERIC
                                   | PV_NO_DUPLICATE | PV_NO_SCRATCH)))
	return false;

      if (!lex_force_match (lexer, T_LPAREN))
	  goto error;

      if (!lex_force_int (lexer))
	goto error;
      long min = lex_integer (lexer);
      lex_get (lexer);

      lex_match (lexer, T_COMMA);

      if (!lex_force_int_range (lexer, NULL, min, LONG_MAX))
	goto error;
      long max = lex_integer (lexer);
      lex_get (lexer);

      if (!lex_force_match (lexer, T_RPAREN))
        goto error;

      for (size_t i = orig_nv; i < proc->n_variables; i++)
        {
          const struct variable *var = proc->variables[i];
          struct var_range *vr = xmalloc (sizeof *vr);

          vr->var = var;
          vr->min = min;
	  vr->max = max;
	  vr->count = max - min + 1;
          hmap_insert (&proc->var_ranges, &vr->hmap_node,
                       hash_pointer (var, 0));
	}

      if (lex_token (lexer) == T_SLASH)
	break;
    }

  proc->mode = INTEGER;
  return true;

 error:
  free (proc->variables);
  proc->variables = NULL;
  proc->n_variables = 0;
  return false;
}

/* Data file processing. */

static const struct var_range *
get_var_range (const struct crosstabs_proc *proc, const struct variable *var)
{
  if (!hmap_is_empty (&proc->var_ranges))
    {
      const struct var_range *range;

      HMAP_FOR_EACH_IN_BUCKET (range, struct var_range, hmap_node,
                               hash_pointer (var, 0), &proc->var_ranges)
        if (range->var == var)
          return range;
    }

  return NULL;
}

static bool
should_tabulate_case (const struct crosstabulation *xt, const struct ccase *c,
                      enum mv_class exclude)
{
  int j;
  for (j = 0; j < xt->n_vars; j++)
    {
      const struct variable *var = xt->vars[j].var;
      const struct var_range *range = get_var_range (xt->proc, var);

      if (var_is_value_missing (var, case_data (c, var), exclude))
        return false;

      if (range != NULL)
        {
          double num = case_num (c, var);
          if (num < range->min || num >= range->max + 1.)
            return false;
        }
    }
  return true;
}

static void
tabulate_integer_case (struct crosstabulation *xt, const struct ccase *c,
                       double weight)
{
  struct freq *te;
  size_t hash;
  int j;

  hash = 0;
  for (j = 0; j < xt->n_vars; j++)
    {
      /* Throw away fractional parts of values. */
      hash = hash_int (case_num (c, xt->vars[j].var), hash);
    }

  HMAP_FOR_EACH_WITH_HASH (te, struct freq, node, hash, &xt->data)
    {
      for (j = 0; j < xt->n_vars; j++)
        if ((int) case_num (c, xt->vars[j].var) != (int) te->values[j].f)
          goto no_match;

      /* Found an existing entry. */
      te->count += weight;
      return;

    no_match: ;
    }

  /* No existing entry.  Create a new one. */
  te = xmalloc (table_entry_size (xt->n_vars));
  te->count = weight;
  for (j = 0; j < xt->n_vars; j++)
    te->values[j].f = (int) case_num (c, xt->vars[j].var);
  hmap_insert (&xt->data, &te->node, hash);
}

static void
tabulate_general_case (struct crosstabulation *xt, const struct ccase *c,
                       double weight)
{
  struct freq *te;
  size_t hash;
  int j;

  hash = 0;
  for (j = 0; j < xt->n_vars; j++)
    {
      const struct variable *var = xt->vars[j].var;
      hash = value_hash (case_data (c, var), var_get_width (var), hash);
    }

  HMAP_FOR_EACH_WITH_HASH (te, struct freq, node, hash, &xt->data)
    {
      for (j = 0; j < xt->n_vars; j++)
        {
          const struct variable *var = xt->vars[j].var;
          if (!value_equal (case_data (c, var), &te->values[j],
                            var_get_width (var)))
            goto no_match;
        }

      /* Found an existing entry. */
      te->count += weight;
      return;

    no_match: ;
    }

  /* No existing entry.  Create a new one. */
  te = xmalloc (table_entry_size (xt->n_vars));
  te->count = weight;
  for (j = 0; j < xt->n_vars; j++)
    {
      const struct variable *var = xt->vars[j].var;
      value_clone (&te->values[j], case_data (c, var), var_get_width (var));
    }
  hmap_insert (&xt->data, &te->node, hash);
}

/* Post-data reading calculations. */

static int compare_table_entry_vars_3way (const struct freq *a,
                                          const struct freq *b,
                                          const struct crosstabulation *xt,
                                          int idx0, int idx1);
static int compare_table_entry_3way (const void *ap_, const void *bp_,
                                     const void *xt_);
static int compare_table_entry_3way_inv (const void *ap_, const void *bp_,
                                     const void *xt_);

static void enum_var_values (const struct crosstabulation *, int var_idx,
                             bool descending);
static void free_var_values (const struct crosstabulation *, int var_idx);
static void output_crosstabulation (struct crosstabs_proc *,
                                struct crosstabulation *);
static void make_crosstabulation_subset (struct crosstabulation *xt,
                                     size_t row0, size_t row1,
                                     struct crosstabulation *subset);
static void make_summary_table (struct crosstabs_proc *);
static bool find_crosstab (struct crosstabulation *, size_t *row0p,
                           size_t *row1p);

static void
postcalc (struct crosstabs_proc *proc)
{
  /* Round hash table entries, if requested

     If this causes any of the cell counts to fall to zero, delete those
     cells. */
  if (proc->round_cells)
    for (struct crosstabulation *xt = proc->pivots;
         xt < &proc->pivots[proc->n_pivots]; xt++)
      {
        struct freq *e, *next;
        HMAP_FOR_EACH_SAFE (e, next, struct freq, node, &xt->data)
          {
            e->count = round_weight (proc, e->count);
            if (e->count == 0.0)
              {
                hmap_delete (&xt->data, &e->node);
                free (e);
              }
          }
      }

  /* Convert hash tables into sorted arrays of entries. */
  for (struct crosstabulation *xt = proc->pivots;
       xt < &proc->pivots[proc->n_pivots]; xt++)
    {
      struct freq *e;

      xt->n_entries = hmap_count (&xt->data);
      xt->entries = xnmalloc (xt->n_entries, sizeof *xt->entries);
      size_t i = 0;
      HMAP_FOR_EACH (e, struct freq, node, &xt->data)
        xt->entries[i++] = e;
      hmap_destroy (&xt->data);

      sort (xt->entries, xt->n_entries, sizeof *xt->entries,
            proc->descending ? compare_table_entry_3way_inv : compare_table_entry_3way,
	    xt);

    }

  make_summary_table (proc);

  /* Output each pivot table. */
  for (struct crosstabulation *xt = proc->pivots;
       xt < &proc->pivots[proc->n_pivots]; xt++)
    {
      output_crosstabulation (proc, xt);
      if (proc->barchart)
        {
          int n_vars = (xt->n_vars > 2 ? 2 : xt->n_vars);
          const struct variable **vars = xcalloc (n_vars, sizeof *vars);
          for (size_t i = 0; i < n_vars; i++)
            vars[i] = xt->vars[i].var;
          chart_submit (barchart_create (vars, n_vars, _("Count"),
                                         false,
                                         xt->entries, xt->n_entries));
          free (vars);
        }
    }

  /* Free output and prepare for next split file. */
  for (struct crosstabulation *xt = proc->pivots;
       xt < &proc->pivots[proc->n_pivots]; xt++)
    {
      xt->missing = 0.0;

      /* Free the members that were allocated in this function(and the values
         owned by the entries.

         The other pointer members are either both allocated and destroyed at a
         lower level (in output_crosstabulation), or both allocated and
         destroyed at a higher level (in crs_custom_tables and free_proc,
         respectively). */
      for (size_t i = 0; i < xt->n_vars; i++)
        {
          int width = var_get_width (xt->vars[i].var);
          if (value_needs_init (width))
            {
              size_t j;

              for (j = 0; j < xt->n_entries; j++)
                value_destroy (&xt->entries[j]->values[i], width);
            }
        }

      for (size_t i = 0; i < xt->n_entries; i++)
        free (xt->entries[i]);
      free (xt->entries);
    }
}

static void
make_crosstabulation_subset (struct crosstabulation *xt, size_t row0,
                             size_t row1, struct crosstabulation *subset)
{
  *subset = *xt;
  if (xt->n_vars > 2)
    {
      assert (xt->n_consts == 0);
      subset->n_vars = 2;
      subset->vars = xt->vars;

      subset->n_consts = xt->n_vars - 2;
      subset->const_vars = xt->vars + 2;
      subset->const_indexes = xcalloc (subset->n_consts,
                                       sizeof *subset->const_indexes);
      for (size_t i = 0; i < subset->n_consts; i++)
        {
          const union value *value = &xt->entries[row0]->values[2 + i];

          for (size_t j = 0; j < xt->vars[2 + i].n_values; j++)
            if (value_equal (&xt->vars[2 + i].values[j], value,
                             var_get_width (xt->vars[2 + i].var)))
              {
                subset->const_indexes[i] = j;
                goto found;
              }
          NOT_REACHED ();
        found: ;
        }
    }
  subset->entries = &xt->entries[row0];
  subset->n_entries = row1 - row0;
}

static int
compare_table_entry_var_3way (const struct freq *a,
                              const struct freq *b,
                              const struct crosstabulation *xt,
                              int idx)
{
  return value_compare_3way (&a->values[idx], &b->values[idx],
                             var_get_width (xt->vars[idx].var));
}

static int
compare_table_entry_vars_3way (const struct freq *a,
                               const struct freq *b,
                               const struct crosstabulation *xt,
                               int idx0, int idx1)
{
  int i;

  for (i = idx1 - 1; i >= idx0; i--)
    {
      int cmp = compare_table_entry_var_3way (a, b, xt, i);
      if (cmp != 0)
        return cmp;
    }
  return 0;
}

/* Compare the struct freq at *AP to the one at *BP and
   return a strcmp()-type result. */
static int
compare_table_entry_3way (const void *ap_, const void *bp_, const void *xt_)
{
  const struct freq *const *ap = ap_;
  const struct freq *const *bp = bp_;
  const struct freq *a = *ap;
  const struct freq *b = *bp;
  const struct crosstabulation *xt = xt_;
  int cmp;

  cmp = compare_table_entry_vars_3way (a, b, xt, 2, xt->n_vars);
  if (cmp != 0)
    return cmp;

  cmp = compare_table_entry_var_3way (a, b, xt, ROW_VAR);
  if (cmp != 0)
    return cmp;

  return compare_table_entry_var_3way (a, b, xt, COL_VAR);
}

/* Inverted version of compare_table_entry_3way */
static int
compare_table_entry_3way_inv (const void *ap_, const void *bp_, const void *xt_)
{
  return -compare_table_entry_3way (ap_, bp_, xt_);
}

/* Output a table summarizing the cases processed. */
static void
make_summary_table (struct crosstabs_proc *proc)
{
  struct pivot_table *table = pivot_table_create (N_("Summary"));
  pivot_table_set_weight_var (table, dict_get_weight (proc->dict));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_COUNT,
                          N_("Percent"), PIVOT_RC_PERCENT);

  struct pivot_dimension *cases = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Cases"),
    N_("Valid"), N_("Missing"), N_("Total"));
  cases->root->show_label = true;

  struct pivot_dimension *tables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Crosstabulation"));
  for (struct crosstabulation *xt = &proc->pivots[0];
       xt < &proc->pivots[proc->n_pivots]; xt++)
    {
      struct string name = DS_EMPTY_INITIALIZER;
      for (size_t i = 0; i < xt->n_vars; i++)
        {
          if (i > 0)
            ds_put_cstr (&name, " × ");
          ds_put_cstr (&name, var_to_string (xt->vars[i].var));
        }

      int row = pivot_category_create_leaf (
        tables->root,
        pivot_value_new_user_text_nocopy (ds_steal_cstr (&name)));

      double valid = 0.;
      for (size_t i = 0; i < xt->n_entries; i++)
        valid += xt->entries[i]->count;

      double n[3];
      n[0] = valid;
      n[1] = xt->missing;
      n[2] = n[0] + n[1];
      for (int i = 0; i < 3; i++)
        {
          pivot_table_put3 (table, 0, i, row, pivot_value_new_number (n[i]));
          pivot_table_put3 (table, 1, i, row,
                            pivot_value_new_number (n[i] / n[2] * 100.0));
        }
    }

  pivot_table_submit (table);
}

/* Output. */

static struct pivot_table *create_crosstab_table (
  struct crosstabs_proc *, struct crosstabulation *,
  size_t crs_leaves[CRS_N_CELLS]);
static struct pivot_table *create_chisq_table (struct crosstabulation *);
static struct pivot_table *create_sym_table (struct crosstabulation *);
static struct pivot_table *create_risk_table (
  struct crosstabulation *, struct pivot_dimension **risk_statistics);
static struct pivot_table *create_direct_table (struct crosstabulation *);
static void display_crosstabulation (struct crosstabs_proc *,
                                     struct crosstabulation *,
                                     struct pivot_table *,
                                     size_t crs_leaves[CRS_N_CELLS]);
static void display_chisq (struct crosstabulation *, struct pivot_table *);
static void display_symmetric (struct crosstabs_proc *,
                               struct crosstabulation *, struct pivot_table *);
static void display_risk (struct crosstabulation *, struct pivot_table *,
                          struct pivot_dimension *risk_statistics);
static void display_directional (struct crosstabs_proc *,
                                 struct crosstabulation *,
                                 struct pivot_table *);
static void delete_missing (struct crosstabulation *);
static void build_matrix (struct crosstabulation *);

/* Output pivot table XT in the context of PROC. */
static void
output_crosstabulation (struct crosstabs_proc *proc, struct crosstabulation *xt)
{
  for (size_t i = 0; i < xt->n_vars; i++)
    enum_var_values (xt, i, proc->descending);

  if (xt->vars[COL_VAR].n_values == 0)
    {
      struct string vars;
      int i;

      ds_init_cstr (&vars, var_to_string (xt->vars[0].var));
      for (i = 1; i < xt->n_vars; i++)
        ds_put_format (&vars, " × %s", var_to_string (xt->vars[i].var));

      /* TRANSLATORS: The %s here describes a crosstabulation.  It takes the
         form "var1 * var2 * var3 * ...".  */
      msg (SW, _("Crosstabulation %s contained no non-missing cases."),
           ds_cstr (&vars));

      ds_destroy (&vars);
      for (size_t i = 0; i < xt->n_vars; i++)
        free_var_values (xt, i);
      return;
    }

  size_t crs_leaves[CRS_N_CELLS];
  struct pivot_table *table = (proc->cells
                               ? create_crosstab_table (proc, xt, crs_leaves)
                               : NULL);
  struct pivot_table *chisq = (proc->statistics & CRS_ST_CHISQ
                               ? create_chisq_table (xt)
                               : NULL);
  struct pivot_table *sym
    = (proc->statistics & (CRS_ST_PHI | CRS_ST_CC | CRS_ST_BTAU | CRS_ST_CTAU
                           | CRS_ST_GAMMA | CRS_ST_CORR | CRS_ST_KAPPA)
       ? create_sym_table (xt)
       : NULL);
  struct pivot_dimension *risk_statistics = NULL;
  struct pivot_table *risk = (proc->statistics & CRS_ST_RISK
                              ? create_risk_table (xt, &risk_statistics)
                              : NULL);
  struct pivot_table *direct
    = (proc->statistics & (CRS_ST_LAMBDA | CRS_ST_UC | CRS_ST_D | CRS_ST_ETA)
       ? create_direct_table (xt)
       : NULL);

  size_t row0 = 0;
  size_t row1 = 0;
  while (find_crosstab (xt, &row0, &row1))
    {
      struct crosstabulation x;

      make_crosstabulation_subset (xt, row0, row1, &x);

      size_t n_rows = x.vars[ROW_VAR].n_values;
      size_t n_cols = x.vars[COL_VAR].n_values;
      if (size_overflow_p (xtimes (xtimes (n_rows, n_cols), sizeof (double))))
        xalloc_die ();
      x.row_tot = xmalloc (n_rows * sizeof *x.row_tot);
      x.col_tot = xmalloc (n_cols * sizeof *x.col_tot);
      x.mat = xmalloc (n_rows * n_cols * sizeof *x.mat);

      build_matrix (&x);

      /* Find the first variable that differs from the last subtable. */
      if (table)
        display_crosstabulation (proc, &x, table, crs_leaves);

      if (proc->exclude == MV_NEVER)
	delete_missing (&x);

      if (chisq)
        display_chisq (&x, chisq);

      if (sym)
        display_symmetric (proc, &x, sym);
      if (risk)
        display_risk (&x, risk, risk_statistics);
      if (direct)
        display_directional (proc, &x, direct);

      free (x.mat);
      free (x.row_tot);
      free (x.col_tot);
      free (x.const_indexes);
    }

  if (table)
    pivot_table_submit (table);

  if (chisq)
    pivot_table_submit (chisq);

  if (sym)
    pivot_table_submit (sym);

  if (risk)
    {
      if (!pivot_table_is_empty (risk))
        pivot_table_submit (risk);
      else
        pivot_table_unref (risk);
    }

  if (direct)
    pivot_table_submit (direct);

  for (size_t i = 0; i < xt->n_vars; i++)
    free_var_values (xt, i);
}

static void
build_matrix (struct crosstabulation *x)
{
  const int col_var_width = var_get_width (x->vars[COL_VAR].var);
  const int row_var_width = var_get_width (x->vars[ROW_VAR].var);
  size_t n_rows = x->vars[ROW_VAR].n_values;
  size_t n_cols = x->vars[COL_VAR].n_values;
  int col, row;
  double *mp;
  struct freq **p;

  mp = x->mat;
  col = row = 0;
  for (p = x->entries; p < &x->entries[x->n_entries]; p++)
    {
      const struct freq *te = *p;

      while (!value_equal (&x->vars[ROW_VAR].values[row],
                           &te->values[ROW_VAR], row_var_width))
        {
          for (; col < n_cols; col++)
            *mp++ = 0.0;
          col = 0;
          row++;
        }

      while (!value_equal (&x->vars[COL_VAR].values[col],
                           &te->values[COL_VAR], col_var_width))
        {
          *mp++ = 0.0;
          col++;
        }

      *mp++ = te->count;
      if (++col >= n_cols)
        {
          col = 0;
          row++;
        }
    }
  while (mp < &x->mat[n_cols * n_rows])
    *mp++ = 0.0;
  assert (mp == &x->mat[n_cols * n_rows]);

  /* Column totals, row totals, ns_rows. */
  mp = x->mat;
  for (col = 0; col < n_cols; col++)
    x->col_tot[col] = 0.0;
  for (row = 0; row < n_rows; row++)
    x->row_tot[row] = 0.0;
  x->ns_rows = 0;
  for (row = 0; row < n_rows; row++)
    {
      bool row_is_empty = true;
      for (col = 0; col < n_cols; col++)
        {
          if (*mp != 0.0)
            {
              row_is_empty = false;
              x->col_tot[col] += *mp;
              x->row_tot[row] += *mp;
            }
          mp++;
        }
      if (!row_is_empty)
        x->ns_rows++;
    }
  assert (mp == &x->mat[n_cols * n_rows]);

  /* ns_cols. */
  x->ns_cols = 0;
  for (col = 0; col < n_cols; col++)
    for (row = 0; row < n_rows; row++)
      if (x->mat[col + row * n_cols] != 0.0)
        {
          x->ns_cols++;
          break;
        }

  /* Grand total. */
  x->total = 0.0;
  for (col = 0; col < n_cols; col++)
    x->total += x->col_tot[col];
}

static void
add_var_dimension (struct pivot_table *table, const struct xtab_var *var,
                   enum pivot_axis_type axis_type, bool total)
{
  struct pivot_dimension *d = pivot_dimension_create__ (
    table, axis_type, pivot_value_new_variable (var->var));

  struct pivot_footnote *missing_footnote = pivot_table_create_footnote (
    table, pivot_value_new_text (N_("Missing value")));

  struct pivot_category *group = pivot_category_create_group__ (
    d->root, pivot_value_new_variable (var->var));
  for (size_t j = 0; j < var->n_values; j++)
    {
      struct pivot_value *value = pivot_value_new_var_value (
        var->var, &var->values[j]);
      if (var_is_value_missing (var->var, &var->values[j], MV_ANY))
        pivot_value_add_footnote (value, missing_footnote);
      pivot_category_create_leaf (group, value);
    }

  if (total)
    pivot_category_create_leaf (d->root, pivot_value_new_text (N_("Total")));
}

static struct pivot_table *
create_crosstab_table (struct crosstabs_proc *proc, struct crosstabulation *xt,
                       size_t crs_leaves[CRS_N_CELLS])
{
  /* Title. */
  struct string title = DS_EMPTY_INITIALIZER;
  for (size_t i = 0; i < xt->n_vars; i++)
    {
      if (i)
        ds_put_cstr (&title, " × ");
      ds_put_cstr (&title, var_to_string (xt->vars[i].var));
    }
  for (size_t i = 0; i < xt->n_consts; i++)
    {
      const struct variable *var = xt->const_vars[i].var;
      const union value *value = &xt->entries[0]->values[2 + i];
      char *s;

      ds_put_format (&title, ", %s=", var_to_string (var));

      /* Insert the formatted value of VAR without any leading spaces. */
      s = data_out (value, var_get_encoding (var), var_get_print_format (var),
                    settings_get_fmt_settings ());
      ds_put_cstr (&title, s + strspn (s, " "));
      free (s);
    }
  struct pivot_table *table = pivot_table_create__ (
    pivot_value_new_user_text_nocopy (ds_steal_cstr (&title)),
    "Crosstabulation");
  pivot_table_set_weight_format (table, &proc->weight_format);

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Statistics"));

  struct statistic
    {
      const char *label;
      const char *rc;
    };
  static const struct statistic stats[CRS_N_CELLS] =
    {
#define C(KEYWORD, STRING, RC) { STRING, RC },
      CRS_CELLS
#undef C
    };
  for (size_t i = 0; i < CRS_N_CELLS; i++)
    if (proc->cells & (1u << i) && stats[i].label)
        crs_leaves[i] = pivot_category_create_leaf_rc (
          statistics->root, pivot_value_new_text (stats[i].label),
          stats[i].rc);

  for (size_t i = 0; i < xt->n_vars; i++)
    add_var_dimension (table, &xt->vars[i],
                       i == COL_VAR ? PIVOT_AXIS_COLUMN : PIVOT_AXIS_ROW,
                       true);

  return table;
}

static struct pivot_table *
create_chisq_table (struct crosstabulation *xt)
{
  struct pivot_table *chisq = pivot_table_create (N_("Chi-Square Tests"));
  pivot_table_set_weight_format (chisq, &xt->weight_format);

  pivot_dimension_create (
    chisq, PIVOT_AXIS_ROW, N_("Statistics"),
    N_("Pearson Chi-Square"),
    N_("Likelihood Ratio"),
    N_("Fisher's Exact Test"),
    N_("Continuity Correction"),
    N_("Linear-by-Linear Association"),
    N_("N of Valid Cases"), PIVOT_RC_COUNT);

  pivot_dimension_create (
    chisq, PIVOT_AXIS_COLUMN, N_("Statistics"),
    N_("Value"), PIVOT_RC_OTHER,
    N_("df"), PIVOT_RC_COUNT,
    N_("Asymptotic Sig. (2-tailed)"), PIVOT_RC_SIGNIFICANCE,
    N_("Exact Sig. (2-tailed)"), PIVOT_RC_SIGNIFICANCE,
    N_("Exact Sig. (1-tailed)"), PIVOT_RC_SIGNIFICANCE);

  for (size_t i = 2; i < xt->n_vars; i++)
    add_var_dimension (chisq, &xt->vars[i], PIVOT_AXIS_ROW, false);

  return chisq;
}

/* Symmetric measures. */
static struct pivot_table *
create_sym_table (struct crosstabulation *xt)
{
  struct pivot_table *sym = pivot_table_create (N_("Symmetric Measures"));
  pivot_table_set_weight_format (sym, &xt->weight_format);

  pivot_dimension_create (
    sym, PIVOT_AXIS_COLUMN, N_("Values"),
    N_("Value"), PIVOT_RC_OTHER,
    N_("Asymp. Std. Error"), PIVOT_RC_OTHER,
    N_("Approx. T"), PIVOT_RC_OTHER,
    N_("Approx. Sig."), PIVOT_RC_SIGNIFICANCE);

  struct pivot_dimension *statistics = pivot_dimension_create (
    sym, PIVOT_AXIS_ROW, N_("Statistics"));
  pivot_category_create_group (
    statistics->root, N_("Nominal by Nominal"),
    N_("Phi"), N_("Cramer's V"), N_("Contingency Coefficient"));
  pivot_category_create_group (
    statistics->root, N_("Ordinal by Ordinal"),
    N_("Kendall's tau-b"), N_("Kendall's tau-c"),
    N_("Gamma"), N_("Spearman Correlation"));
  pivot_category_create_group (
    statistics->root, N_("Interval by Interval"),
    N_("Pearson's R"));
  pivot_category_create_group (
    statistics->root, N_("Measure of Agreement"),
    N_("Kappa"));
  pivot_category_create_leaves (statistics->root, N_("N of Valid Cases"),
                                PIVOT_RC_COUNT);

  for (size_t i = 2; i < xt->n_vars; i++)
    add_var_dimension (sym, &xt->vars[i], PIVOT_AXIS_ROW, false);

  return sym;
}

/* Risk estimate. */
static struct pivot_table *
create_risk_table (struct crosstabulation *xt,
                   struct pivot_dimension **risk_statistics)
{
  struct pivot_table *risk = pivot_table_create (N_("Risk Estimate"));
  pivot_table_set_weight_format (risk, &xt->weight_format);

  struct pivot_dimension *values = pivot_dimension_create (
    risk, PIVOT_AXIS_COLUMN, N_("Values"),
    N_("Value"), PIVOT_RC_OTHER);
  pivot_category_create_group (
  /* xgettext:no-c-format */
    values->root, N_("95% Confidence Interval"),
    N_("Lower"), PIVOT_RC_OTHER,
    N_("Upper"), PIVOT_RC_OTHER);

  *risk_statistics = pivot_dimension_create (
    risk, PIVOT_AXIS_ROW, N_("Statistics"));

  for (size_t i = 2; i < xt->n_vars; i++)
    add_var_dimension (risk, &xt->vars[i], PIVOT_AXIS_ROW, false);

  return risk;
}

static void
create_direct_stat (struct pivot_category *parent,
                    const struct crosstabulation *xt,
                    const char *name, bool symmetric)
{
  struct pivot_category *group = pivot_category_create_group (
    parent, name);
  if (symmetric)
    pivot_category_create_leaf (group, pivot_value_new_text (N_("Symmetric")));

  char *row_label = xasprintf (_("%s Dependent"),
                               var_to_string (xt->vars[ROW_VAR].var));
  pivot_category_create_leaf (group, pivot_value_new_user_text_nocopy (
                                row_label));

  char *col_label = xasprintf (_("%s Dependent"),
                               var_to_string (xt->vars[COL_VAR].var));
  pivot_category_create_leaf (group, pivot_value_new_user_text_nocopy (
                                col_label));
}

/* Directional measures. */
static struct pivot_table *
create_direct_table (struct crosstabulation *xt)
{
  struct pivot_table *direct = pivot_table_create (N_("Directional Measures"));
  pivot_table_set_weight_format (direct, &xt->weight_format);

  pivot_dimension_create (
    direct, PIVOT_AXIS_COLUMN, N_("Values"),
    N_("Value"), PIVOT_RC_OTHER,
    N_("Asymp. Std. Error"), PIVOT_RC_OTHER,
    N_("Approx. T"), PIVOT_RC_OTHER,
    N_("Approx. Sig."), PIVOT_RC_SIGNIFICANCE);

  struct pivot_dimension *statistics = pivot_dimension_create (
    direct, PIVOT_AXIS_ROW, N_("Statistics"));
  struct pivot_category *nn = pivot_category_create_group (
    statistics->root, N_("Nominal by Nominal"));
  create_direct_stat (nn, xt, N_("Lambda"), true);
  create_direct_stat (nn, xt, N_("Goodman and Kruskal tau"), false);
  create_direct_stat (nn, xt, N_("Uncertainty Coefficient"), true);
  struct pivot_category *oo = pivot_category_create_group (
    statistics->root, N_("Ordinal by Ordinal"));
  create_direct_stat (oo, xt, N_("Somers' d"), true);
  struct pivot_category *ni = pivot_category_create_group (
    statistics->root, N_("Nominal by Interval"));
  create_direct_stat (ni, xt, N_("Eta"), false);

  for (size_t i = 2; i < xt->n_vars; i++)
    add_var_dimension (direct, &xt->vars[i], PIVOT_AXIS_ROW, false);

  return direct;
}

/* Delete missing rows and columns for statistical analysis when
   /MISSING=REPORT. */
static void
delete_missing (struct crosstabulation *xt)
{
  size_t n_rows = xt->vars[ROW_VAR].n_values;
  size_t n_cols = xt->vars[COL_VAR].n_values;
  int r, c;

  for (r = 0; r < n_rows; r++)
    if (var_is_num_missing (xt->vars[ROW_VAR].var,
                            xt->vars[ROW_VAR].values[r].f, MV_USER))
      {
        for (c = 0; c < n_cols; c++)
          xt->mat[c + r * n_cols] = 0.;
        xt->ns_rows--;
      }


  for (c = 0; c < n_cols; c++)
    if (var_is_num_missing (xt->vars[COL_VAR].var,
                            xt->vars[COL_VAR].values[c].f, MV_USER))
      {
        for (r = 0; r < n_rows; r++)
          xt->mat[c + r * n_cols] = 0.;
        xt->ns_cols--;
      }
}

static bool
find_crosstab (struct crosstabulation *xt, size_t *row0p, size_t *row1p)
{
  size_t row0 = *row1p;
  size_t row1;

  if (row0 >= xt->n_entries)
    return false;

  for (row1 = row0 + 1; row1 < xt->n_entries; row1++)
    {
      struct freq *a = xt->entries[row0];
      struct freq *b = xt->entries[row1];
      if (compare_table_entry_vars_3way (a, b, xt, 2, xt->n_vars) != 0)
        break;
    }
  *row0p = row0;
  *row1p = row1;
  return true;
}

/* Compares `union value's A_ and B_ and returns a strcmp()-like
   result.  WIDTH_ points to an int which is either 0 for a
   numeric value or a string width for a string value. */
static int
compare_value_3way (const void *a_, const void *b_, const void *width_)
{
  const union value *a = a_;
  const union value *b = b_;
  const int *width = width_;

  return value_compare_3way (a, b, *width);
}

/* Inverted version of the above */
static int
compare_value_3way_inv (const void *a_, const void *b_, const void *width_)
{
  return -compare_value_3way (a_, b_, width_);
}


/* Given an array of ENTRY_CNT table_entry structures starting at
   ENTRIES, creates a sorted list of the values that the variable
   with index VAR_IDX takes on.  Stores the array of the values in
   XT->values and the number of values in XT->n_values. */
static void
enum_var_values (const struct crosstabulation *xt, int var_idx,
                 bool descending)
{
  struct xtab_var *xv = &xt->vars[var_idx];
  const struct var_range *range = get_var_range (xt->proc, xv->var);

  if (range)
    {
      xv->values = xnmalloc (range->count, sizeof *xv->values);
      xv->n_values = range->count;
      for (size_t i = 0; i < range->count; i++)
        xv->values[i].f = range->min + i;
    }
  else
    {
      int width = var_get_width (xv->var);
      struct hmapx_node *node;
      const union value *iter;
      struct hmapx set;

      hmapx_init (&set);
      for (size_t i = 0; i < xt->n_entries; i++)
        {
          const struct freq *te = xt->entries[i];
          const union value *value = &te->values[var_idx];
          size_t hash = value_hash (value, width, 0);

          HMAPX_FOR_EACH_WITH_HASH (iter, node, hash, &set)
            if (value_equal (iter, value, width))
              goto next_entry;

          hmapx_insert (&set, (union value *) value, hash);

        next_entry: ;
        }

      xv->n_values = hmapx_count (&set);
      xv->values = xnmalloc (xv->n_values, sizeof *xv->values);
      size_t i = 0;
      HMAPX_FOR_EACH (iter, node, &set)
        xv->values[i++] = *iter;
      hmapx_destroy (&set);

      sort (xv->values, xv->n_values, sizeof *xv->values,
	    descending ? compare_value_3way_inv : compare_value_3way,
	    &width);
    }
}

static void
free_var_values (const struct crosstabulation *xt, int var_idx)
{
  struct xtab_var *xv = &xt->vars[var_idx];
  free (xv->values);
  xv->values = NULL;
  xv->n_values = 0;
}

/* Displays the crosstabulation table. */
static void
display_crosstabulation (struct crosstabs_proc *proc,
                         struct crosstabulation *xt, struct pivot_table *table,
                         size_t crs_leaves[CRS_N_CELLS])
{
  size_t n_rows = xt->vars[ROW_VAR].n_values;
  size_t n_cols = xt->vars[COL_VAR].n_values;

  size_t *indexes = xnmalloc (table->n_dimensions, sizeof *indexes);
  assert (xt->n_vars == 2);
  for (size_t i = 0; i < xt->n_consts; i++)
    indexes[i + 3] = xt->const_indexes[i];

  /* Put in the actual cells. */
  double *mp = xt->mat;
  for (size_t r = 0; r < n_rows; r++)
    {
      if (!xt->row_tot[r] && proc->mode != INTEGER)
        continue;

      indexes[ROW_VAR + 1] = r;
      for (size_t c = 0; c < n_cols; c++)
        {
          if (!xt->col_tot[c] && proc->mode != INTEGER)
            continue;

          indexes[COL_VAR + 1] = c;

          double expected_value = xt->row_tot[r] * xt->col_tot[c] / xt->total;
          double residual = *mp - expected_value;
          double sresidual = residual / sqrt (expected_value);
          double asresidual = (sresidual
                               * (1. - xt->row_tot[r] / xt->total)
                               * (1. - xt->col_tot[c] / xt->total));
          double entries[CRS_N_CELLS] = {
            [CRS_CL_COUNT] = *mp,
            [CRS_CL_ROW] = *mp / xt->row_tot[r] * 100.,
            [CRS_CL_COLUMN] = *mp / xt->col_tot[c] * 100.,
            [CRS_CL_TOTAL] = *mp / xt->total * 100.,
            [CRS_CL_EXPECTED] = expected_value,
            [CRS_CL_RESIDUAL] = residual,
            [CRS_CL_SRESIDUAL] = sresidual,
            [CRS_CL_ASRESIDUAL] = asresidual,
          };
          for (size_t i = 0; i < proc->n_cells; i++)
            {
              int cell = proc->a_cells[i];
              indexes[0] = crs_leaves[cell];
              pivot_table_put (table, indexes, table->n_dimensions,
                               pivot_value_new_number (entries[cell]));
            }

          mp++;
        }
    }

  /* Row totals. */
  for (size_t r = 0; r < n_rows; r++)
    {
      if (!xt->row_tot[r] && proc->mode != INTEGER)
        continue;

      double expected_value = xt->row_tot[r] / xt->total;
      double entries[CRS_N_CELLS] = {
        [CRS_CL_COUNT] = xt->row_tot[r],
        [CRS_CL_ROW] = 100.0,
        [CRS_CL_COLUMN] = expected_value * 100.,
        [CRS_CL_TOTAL] = expected_value * 100.,
        [CRS_CL_EXPECTED] = expected_value,
        [CRS_CL_RESIDUAL] = SYSMIS,
        [CRS_CL_SRESIDUAL] = SYSMIS,
        [CRS_CL_ASRESIDUAL] = SYSMIS,
      };
      for (size_t i = 0; i < proc->n_cells; i++)
        {
          int cell = proc->a_cells[i];
          double entry = entries[cell];
          if (entry != SYSMIS)
            {
              indexes[ROW_VAR + 1] = r;
              indexes[COL_VAR + 1] = n_cols;
              indexes[0] = crs_leaves[cell];
              pivot_table_put (table, indexes, table->n_dimensions,
                               pivot_value_new_number (entry));
            }
        }
    }

  for (size_t c = 0; c <= n_cols; c++)
    {
      if (c < n_cols && !xt->col_tot[c] && proc->mode != INTEGER)
        continue;

      double ct = c < n_cols ? xt->col_tot[c] : xt->total;
      double expected_value = ct / xt->total;
      double entries[CRS_N_CELLS] = {
        [CRS_CL_COUNT] = ct,
        [CRS_CL_ROW] = expected_value * 100.0,
        [CRS_CL_COLUMN] = 100.0,
        [CRS_CL_TOTAL] = expected_value * 100.,
        [CRS_CL_EXPECTED] = expected_value,
        [CRS_CL_RESIDUAL] = SYSMIS,
        [CRS_CL_SRESIDUAL] = SYSMIS,
        [CRS_CL_ASRESIDUAL] = SYSMIS,
      };
      for (size_t i = 0; i < proc->n_cells; i++)
        {
          int cell = proc->a_cells[i];
          double entry = entries[cell];
          if (entry != SYSMIS)
            {
              indexes[ROW_VAR + 1] = n_rows;
              indexes[COL_VAR + 1] = c;
              indexes[0] = crs_leaves[cell];
              pivot_table_put (table, indexes, table->n_dimensions,
                               pivot_value_new_number (entry));
            }
        }
    }

  free (indexes);
}

static void calc_r (struct crosstabulation *,
                    double *XT, double *Y, double *, double *, double *);
static void calc_chisq (struct crosstabulation *,
                        double[N_CHISQ], int[N_CHISQ], double *, double *);

/* Display chi-square statistics. */
static void
display_chisq (struct crosstabulation *xt, struct pivot_table *chisq)
{
  double chisq_v[N_CHISQ];
  double fisher1, fisher2;
  int df[N_CHISQ];
  calc_chisq (xt, chisq_v, df, &fisher1, &fisher2);

  size_t *indexes = xnmalloc (chisq->n_dimensions, sizeof *indexes);
  assert (xt->n_vars == 2);
  for (size_t i = 0; i < xt->n_consts; i++)
    indexes[i + 2] = xt->const_indexes[i];
  for (int i = 0; i < N_CHISQ; i++)
    {
      indexes[0] = i;

      double entries[5] = { SYSMIS, SYSMIS, SYSMIS, SYSMIS, SYSMIS };
      if (i == 2)
        {
          entries[3] = fisher2;
          entries[4] = fisher1;
        }
      else if (chisq_v[i] != SYSMIS)
        {
          entries[0] = chisq_v[i];
          entries[1] = df[i];
          entries[2] = gsl_cdf_chisq_Q (chisq_v[i], df[i]);
        }

      for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
        if (entries[j] != SYSMIS)
          {
            indexes[1] = j;
            pivot_table_put (chisq, indexes, chisq->n_dimensions,
                             pivot_value_new_number (entries[j]));
        }
    }

  indexes[0] = 5;
  indexes[1] = 0;
  pivot_table_put (chisq, indexes, chisq->n_dimensions,
                   pivot_value_new_number (xt->total));

  free (indexes);
}

static int calc_symmetric (struct crosstabs_proc *, struct crosstabulation *,
                           double[N_SYMMETRIC], double[N_SYMMETRIC],
			   double[N_SYMMETRIC],
                           double[3], double[3], double[3]);

/* Display symmetric measures. */
static void
display_symmetric (struct crosstabs_proc *proc, struct crosstabulation *xt,
                   struct pivot_table *sym)
{
  double sym_v[N_SYMMETRIC], sym_ase[N_SYMMETRIC], sym_t[N_SYMMETRIC];
  double somers_d_v[3], somers_d_ase[3], somers_d_t[3];

  if (!calc_symmetric (proc, xt, sym_v, sym_ase, sym_t,
                       somers_d_v, somers_d_ase, somers_d_t))
    return;

  size_t *indexes = xnmalloc (sym->n_dimensions, sizeof *indexes);
  assert (xt->n_vars == 2);
  for (size_t i = 0; i < xt->n_consts; i++)
    indexes[i + 2] = xt->const_indexes[i];

  for (int i = 0; i < N_SYMMETRIC; i++)
    {
      if (sym_v[i] == SYSMIS)
	continue;

      indexes[1] = i;

      double entries[] = { sym_v[i], sym_ase[i], sym_t[i] };
      for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
        if (entries[j] != SYSMIS)
          {
            indexes[0] = j;
            pivot_table_put (sym, indexes, sym->n_dimensions,
                             pivot_value_new_number (entries[j]));
          }
    }

  indexes[1] = N_SYMMETRIC;
  indexes[0] = 0;
  struct pivot_value *total = pivot_value_new_number (xt->total);
  pivot_value_set_rc (sym, total, PIVOT_RC_COUNT);
  pivot_table_put (sym, indexes, sym->n_dimensions, total);

  free (indexes);
}

static bool calc_risk (struct crosstabulation *,
                       double[], double[], double[], union value *,
                       double *);

/* Display risk estimate. */
static void
display_risk (struct crosstabulation *xt, struct pivot_table *risk,
              struct pivot_dimension *risk_statistics)
{
  double risk_v[3], lower[3], upper[3], n_valid;
  union value c[2];
  if (!calc_risk (xt, risk_v, upper, lower, c, &n_valid))
    return;

  size_t *indexes = xnmalloc (risk->n_dimensions, sizeof *indexes);
  assert (xt->n_vars == 2);
  for (size_t i = 0; i < xt->n_consts; i++)
    indexes[i + 2] = xt->const_indexes[i];

  for (int i = 0; i < 3; i++)
    {
      const struct variable *cv = xt->vars[COL_VAR].var;
      const struct variable *rv = xt->vars[ROW_VAR].var;

      if (risk_v[i] == SYSMIS)
	continue;

      struct string label = DS_EMPTY_INITIALIZER;
      switch (i)
	{
	case 0:
          ds_put_format (&label, _("Odds Ratio for %s"), var_to_string (rv));
          ds_put_cstr (&label, " (");
          var_append_value_name (rv, &c[0], &label);
          ds_put_cstr (&label, " / ");
          var_append_value_name (rv, &c[1], &label);
          ds_put_cstr (&label, ")");
	  break;
	case 1:
	case 2:
          ds_put_format (&label, _("For cohort %s = "), var_to_string (cv));
          var_append_value_name (cv, &xt->vars[ROW_VAR].values[i - 1], &label);
	  break;
	}

      indexes[1] = pivot_category_create_leaf (
        risk_statistics->root,
        pivot_value_new_user_text_nocopy (ds_steal_cstr (&label)));

      double entries[] = { risk_v[i], lower[i], upper[i] };
      for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
        {
          indexes[0] = j;
          pivot_table_put (risk, indexes, risk->n_dimensions,
                           pivot_value_new_number (entries[i]));
        }
    }
  indexes[1] = pivot_category_create_leaf (
    risk_statistics->root,
    pivot_value_new_text (N_("N of Valid Cases")));
  indexes[0] = 0;
  pivot_table_put (risk, indexes, risk->n_dimensions,
                   pivot_value_new_number (n_valid));
  free (indexes);
}

static int calc_directional (struct crosstabs_proc *, struct crosstabulation *,
                             double[N_DIRECTIONAL], double[N_DIRECTIONAL],
			     double[N_DIRECTIONAL], double[N_DIRECTIONAL]);

/* Display directional measures. */
static void
display_directional (struct crosstabs_proc *proc,
                     struct crosstabulation *xt, struct pivot_table *direct)
{
  double direct_v[N_DIRECTIONAL];
  double direct_ase[N_DIRECTIONAL];
  double direct_t[N_DIRECTIONAL];
  double sig[N_DIRECTIONAL];
  if (!calc_directional (proc, xt, direct_v, direct_ase, direct_t, sig))
    return;

  size_t *indexes = xnmalloc (direct->n_dimensions, sizeof *indexes);
  assert (xt->n_vars == 2);
  for (size_t i = 0; i < xt->n_consts; i++)
    indexes[i + 2] = xt->const_indexes[i];

  for (int i = 0; i < N_DIRECTIONAL; i++)
    {
      if (direct_v[i] == SYSMIS)
	continue;

      indexes[1] = i;

      double entries[] = {
        direct_v[i], direct_ase[i], direct_t[i], sig[i],
      };
      for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
        if (entries[j] != SYSMIS)
          {
            indexes[0] = j;
            pivot_table_put (direct, indexes, direct->n_dimensions,
                             pivot_value_new_number (entries[j]));
          }
    }

  free (indexes);
}

/* Statistical calculations. */

/* Returns the value of the logarithm of gamma (factorial) function for an integer
   argument XT. */
static double
log_gamma_int (double xt)
{
  double r = 0;
  int i;

  for (i = 2; i < xt; i++)
    r += log(i);

  return r;
}

/* Calculate P_r as specified in _SPSS Statistical Algorithms_,
   Appendix 5. */
static inline double
Pr (int a, int b, int c, int d)
{
  return exp (log_gamma_int (a + b + 1.) -  log_gamma_int (a + 1.)
	    + log_gamma_int (c + d + 1.) - log_gamma_int (b + 1.)
	    + log_gamma_int (a + c + 1.) - log_gamma_int (c + 1.)
	    + log_gamma_int (b + d + 1.) - log_gamma_int (d + 1.)
	    - log_gamma_int (a + b + c + d + 1.));
}

/* Swap the contents of A and B. */
static inline void
swap (int *a, int *b)
{
  int t = *a;
  *a = *b;
  *b = t;
}

/* Calculate significance for Fisher's exact test as specified in
   _SPSS Statistical Algorithms_, Appendix 5. */
static void
calc_fisher (int a, int b, int c, int d, double *fisher1, double *fisher2)
{
  int xt;
  double pn1;

  if (MIN (c, d) < MIN (a, b))
    swap (&a, &c), swap (&b, &d);
  if (MIN (b, d) < MIN (a, c))
    swap (&a, &b), swap (&c, &d);
  if (b * c < a * d)
    {
      if (b < c)
	swap (&a, &b), swap (&c, &d);
      else
	swap (&a, &c), swap (&b, &d);
    }

  pn1 = Pr (a, b, c, d);
  *fisher1 = pn1;
  for (xt = 1; xt <= a; xt++)
    {
      *fisher1 += Pr (a - xt, b + xt, c + xt, d - xt);
    }

  *fisher2 = *fisher1;

  for (xt = 1; xt <= b; xt++)
    {
      double p = Pr (a + xt, b - xt, c - xt, d + xt);
      if (p < pn1)
	*fisher2 += p;
    }
}

/* Calculates chi-squares into CHISQ.  MAT is a matrix with N_COLS
   columns with values COLS and N_ROWS rows with values ROWS.  Values
   in the matrix sum to xt->total. */
static void
calc_chisq (struct crosstabulation *xt,
            double chisq[N_CHISQ], int df[N_CHISQ],
	    double *fisher1, double *fisher2)
{
  chisq[0] = chisq[1] = 0.;
  chisq[2] = chisq[3] = chisq[4] = SYSMIS;
  *fisher1 = *fisher2 = SYSMIS;

  df[0] = df[1] = (xt->ns_cols - 1) * (xt->ns_rows - 1);

  if (xt->ns_rows <= 1 || xt->ns_cols <= 1)
    {
      chisq[0] = chisq[1] = SYSMIS;
      return;
    }

  size_t n_cols = xt->vars[COL_VAR].n_values;
  FOR_EACH_POPULATED_ROW (r, xt)
    FOR_EACH_POPULATED_COLUMN (c, xt)
      {
        const double expected = xt->row_tot[r] * xt->col_tot[c] / xt->total;
        const double freq = xt->mat[n_cols * r + c];
        const double residual = freq - expected;

        chisq[0] += residual * residual / expected;
        if (freq)
          chisq[1] += freq * log (expected / freq);
      }

  if (chisq[0] == 0.)
    chisq[0] = SYSMIS;

  if (chisq[1] != 0.)
    chisq[1] *= -2.;
  else
    chisq[1] = SYSMIS;

  /* Calculate Yates and Fisher exact test. */
  if (xt->ns_cols == 2 && xt->ns_rows == 2)
    {
      double f11, f12, f21, f22;

      {
	int nz_cols[2];

        int j = 0;
        FOR_EACH_POPULATED_COLUMN (c, xt)
          {
            nz_cols[j++] = c;
            if (j == 2)
              break;
          }
	assert (j == 2);

	f11 = xt->mat[nz_cols[0]];
	f12 = xt->mat[nz_cols[1]];
	f21 = xt->mat[nz_cols[0] + n_cols];
	f22 = xt->mat[nz_cols[1] + n_cols];
      }

      /* Yates. */
      {
	const double xt_ = fabs (f11 * f22 - f12 * f21) - 0.5 * xt->total;

	if (xt_ > 0.)
	  chisq[3] = (xt->total * pow2 (xt_)
		      / (f11 + f12) / (f21 + f22)
		      / (f11 + f21) / (f12 + f22));
	else
	  chisq[3] = 0.;

	df[3] = 1.;
      }

      /* Fisher. */
      calc_fisher (f11 + .5, f12 + .5, f21 + .5, f22 + .5, fisher1, fisher2);
    }

  /* Calculate Mantel-Haenszel. */
  if (var_is_numeric (xt->vars[ROW_VAR].var)
      && var_is_numeric (xt->vars[COL_VAR].var))
    {
      double r, ase_0, ase_1;
      calc_r (xt, (double *) xt->vars[ROW_VAR].values,
              (double *) xt->vars[COL_VAR].values,
              &r, &ase_0, &ase_1);

      chisq[4] = (xt->total - 1.) * r * r;
      df[4] = 1;
    }
}

/* Calculate the value of Pearson's r.  r is stored into R, its T value into
   T, and standard error into ERROR.  The row and column values must be
   passed in XT and Y. */
static void
calc_r (struct crosstabulation *xt,
        double *XT, double *Y, double *r, double *t, double *error)
{
  size_t n_rows = xt->vars[ROW_VAR].n_values;
  size_t n_cols = xt->vars[COL_VAR].n_values;
  double SX, SY, S, T;
  double Xbar, Ybar;
  double sum_XYf, sum_X2Y2f;
  double sum_Xr, sum_X2r;
  double sum_Yc, sum_Y2c;
  int i, j;

  for (sum_X2Y2f = sum_XYf = 0., i = 0; i < n_rows; i++)
    for (j = 0; j < n_cols; j++)
      {
	double fij = xt->mat[j + i * n_cols];
	double product = XT[i] * Y[j];
	double temp = fij * product;
	sum_XYf += temp;
	sum_X2Y2f += temp * product;
      }

  for (sum_Xr = sum_X2r = 0., i = 0; i < n_rows; i++)
    {
      sum_Xr += XT[i] * xt->row_tot[i];
      sum_X2r += pow2 (XT[i]) * xt->row_tot[i];
    }
  Xbar = sum_Xr / xt->total;

  for (sum_Yc = sum_Y2c = 0., i = 0; i < n_cols; i++)
    {
      sum_Yc += Y[i] * xt->col_tot[i];
      sum_Y2c += Y[i] * Y[i] * xt->col_tot[i];
    }
  Ybar = sum_Yc / xt->total;

  S = sum_XYf - sum_Xr * sum_Yc / xt->total;
  SX = sum_X2r - pow2 (sum_Xr) / xt->total;
  SY = sum_Y2c - pow2 (sum_Yc) / xt->total;
  T = sqrt (SX * SY);
  *r = S / T;
  *t = *r / sqrt (1 - pow2 (*r)) * sqrt (xt->total - 2);

  {
    double s, c, y, t;

    for (s = c = 0., i = 0; i < n_rows; i++)
      for (j = 0; j < n_cols; j++)
	{
	  double Xresid, Yresid;
	  double temp;

	  Xresid = XT[i] - Xbar;
	  Yresid = Y[j] - Ybar;
	  temp = (T * Xresid * Yresid
		  - ((S / (2. * T))
		     * (Xresid * Xresid * SY + Yresid * Yresid * SX)));
	  y = xt->mat[j + i * n_cols] * temp * temp - c;
	  t = s + y;
	  c = (t - s) - y;
	  s = t;
	}
    *error = sqrt (s) / (T * T);
  }
}

/* Calculate symmetric statistics and their asymptotic standard
   errors.  Returns 0 if none could be calculated. */
static int
calc_symmetric (struct crosstabs_proc *proc, struct crosstabulation *xt,
                double v[N_SYMMETRIC], double ase[N_SYMMETRIC],
		double t[N_SYMMETRIC],
                double somers_d_v[3], double somers_d_ase[3],
                double somers_d_t[3])
{
  size_t n_rows = xt->vars[ROW_VAR].n_values;
  size_t n_cols = xt->vars[COL_VAR].n_values;
  int q, i;

  q = MIN (xt->ns_rows, xt->ns_cols);
  if (q <= 1)
    return 0;

  for (i = 0; i < N_SYMMETRIC; i++)
    v[i] = ase[i] = t[i] = SYSMIS;

  /* Phi, Cramer's V, contingency coefficient. */
  if (proc->statistics & (CRS_ST_PHI | CRS_ST_CC))
    {
      double Xp = 0.;	/* Pearson chi-square. */

      FOR_EACH_POPULATED_ROW (r, xt)
        FOR_EACH_POPULATED_COLUMN (c, xt)
          {
            double expected = xt->row_tot[r] * xt->col_tot[c] / xt->total;
            double freq = xt->mat[n_cols * r + c];
            double residual = freq - expected;

            Xp += residual * residual / expected;
          }

      if (proc->statistics & CRS_ST_PHI)
	{
	  v[0] = sqrt (Xp / xt->total);
	  v[1] = sqrt (Xp / (xt->total * (q - 1)));
	}
      if (proc->statistics & CRS_ST_CC)
	v[2] = sqrt (Xp / (Xp + xt->total));
    }

  if (proc->statistics & (CRS_ST_BTAU | CRS_ST_CTAU
                          | CRS_ST_GAMMA | CRS_ST_D))
    {
      double *cum;
      double Dr, Dc;
      double P, Q;
      double btau_cum, ctau_cum, gamma_cum, d_yx_cum, d_xy_cum;
      double btau_var;
      int r, c;

      Dr = Dc = pow2 (xt->total);
      for (r = 0; r < n_rows; r++)
        Dr -= pow2 (xt->row_tot[r]);
      for (c = 0; c < n_cols; c++)
        Dc -= pow2 (xt->col_tot[c]);

      cum = xnmalloc (n_cols * n_rows, sizeof *cum);
      for (c = 0; c < n_cols; c++)
        {
          double ct = 0.;

          for (r = 0; r < n_rows; r++)
            cum[c + r * n_cols] = ct += xt->mat[c + r * n_cols];
        }

      /* P and Q. */
      {
	int i, j;
	double Cij, Dij;

	P = Q = 0.;
	for (i = 0; i < n_rows; i++)
	  {
	    Cij = Dij = 0.;

	    for (j = 1; j < n_cols; j++)
	      Cij += xt->col_tot[j] - cum[j + i * n_cols];

	    if (i > 0)
	      for (j = 1; j < n_cols; j++)
		Dij += cum[j + (i - 1) * n_cols];

	    for (j = 0;;)
	      {
		double fij = xt->mat[j + i * n_cols];
		P += fij * Cij;
		Q += fij * Dij;

		if (++j == n_cols)
		  break;
		assert (j < n_cols);

		Cij -= xt->col_tot[j] - cum[j + i * n_cols];
		Dij += xt->col_tot[j - 1] - cum[j - 1 + i * n_cols];

		if (i > 0)
		  {
		    Cij += cum[j - 1 + (i - 1) * n_cols];
		    Dij -= cum[j + (i - 1) * n_cols];
		  }
	      }
	  }
      }

      if (proc->statistics & CRS_ST_BTAU)
	v[3] = (P - Q) / sqrt (Dr * Dc);
      if (proc->statistics & CRS_ST_CTAU)
	v[4] = (q * (P - Q)) / (pow2 (xt->total) * (q - 1));
      if (proc->statistics & CRS_ST_GAMMA)
	v[5] = (P - Q) / (P + Q);

      /* ASE for tau-b, tau-c, gamma.  Calculations could be
	 eliminated here, at expense of memory.  */
      {
	int i, j;
	double Cij, Dij;

	btau_cum = ctau_cum = gamma_cum = d_yx_cum = d_xy_cum = 0.;
	for (i = 0; i < n_rows; i++)
	  {
	    Cij = Dij = 0.;

	    for (j = 1; j < n_cols; j++)
	      Cij += xt->col_tot[j] - cum[j + i * n_cols];

	    if (i > 0)
	      for (j = 1; j < n_cols; j++)
		Dij += cum[j + (i - 1) * n_cols];

	    for (j = 0;;)
	      {
		double fij = xt->mat[j + i * n_cols];

		if (proc->statistics & CRS_ST_BTAU)
		  {
		    const double temp = (2. * sqrt (Dr * Dc) * (Cij - Dij)
					 + v[3] * (xt->row_tot[i] * Dc
						   + xt->col_tot[j] * Dr));
		    btau_cum += fij * temp * temp;
		  }

		{
		  const double temp = Cij - Dij;
		  ctau_cum += fij * temp * temp;
		}

		if (proc->statistics & CRS_ST_GAMMA)
		  {
		    const double temp = Q * Cij - P * Dij;
		    gamma_cum += fij * temp * temp;
		  }

		if (proc->statistics & CRS_ST_D)
		  {
		    d_yx_cum += fij * pow2 (Dr * (Cij - Dij)
                                            - (P - Q) * (xt->total - xt->row_tot[i]));
		    d_xy_cum += fij * pow2 (Dc * (Dij - Cij)
                                            - (Q - P) * (xt->total - xt->col_tot[j]));
		  }

		if (++j == n_cols)
		  break;
		assert (j < n_cols);

		Cij -= xt->col_tot[j] - cum[j + i * n_cols];
		Dij += xt->col_tot[j - 1] - cum[j - 1 + i * n_cols];

		if (i > 0)
		  {
		    Cij += cum[j - 1 + (i - 1) * n_cols];
		    Dij -= cum[j + (i - 1) * n_cols];
		  }
	      }
	  }
      }

      btau_var = ((btau_cum
		   - (xt->total * pow2 (xt->total * (P - Q) / sqrt (Dr * Dc) * (Dr + Dc))))
		  / pow2 (Dr * Dc));
      if (proc->statistics & CRS_ST_BTAU)
	{
	  ase[3] = sqrt (btau_var);
	  t[3] = v[3] / (2 * sqrt ((ctau_cum - (P - Q) * (P - Q) / xt->total)
				   / (Dr * Dc)));
	}
      if (proc->statistics & CRS_ST_CTAU)
	{
	  ase[4] = ((2 * q / ((q - 1) * pow2 (xt->total)))
		    * sqrt (ctau_cum - (P - Q) * (P - Q) / xt->total));
	  t[4] = v[4] / ase[4];
	}
      if (proc->statistics & CRS_ST_GAMMA)
	{
	  ase[5] = ((4. / ((P + Q) * (P + Q))) * sqrt (gamma_cum));
	  t[5] = v[5] / (2. / (P + Q)
			 * sqrt (ctau_cum - (P - Q) * (P - Q) / xt->total));
	}
      if (proc->statistics & CRS_ST_D)
	{
	  somers_d_v[0] = (P - Q) / (.5 * (Dc + Dr));
	  somers_d_ase[0] = SYSMIS;
	  somers_d_t[0] = (somers_d_v[0]
			   / (4 / (Dc + Dr)
			      * sqrt (ctau_cum - pow2 (P - Q) / xt->total)));
	  somers_d_v[1] = (P - Q) / Dc;
	  somers_d_ase[1] = 2. / pow2 (Dc) * sqrt (d_xy_cum);
	  somers_d_t[1] = (somers_d_v[1]
			   / (2. / Dc
			      * sqrt (ctau_cum - pow2 (P - Q) / xt->total)));
	  somers_d_v[2] = (P - Q) / Dr;
	  somers_d_ase[2] = 2. / pow2 (Dr) * sqrt (d_yx_cum);
	  somers_d_t[2] = (somers_d_v[2]
			   / (2. / Dr
			      * sqrt (ctau_cum - pow2 (P - Q) / xt->total)));
	}

      free (cum);
    }

  /* Spearman correlation, Pearson's r. */
  if (proc->statistics & CRS_ST_CORR)
    {
      double *R = xmalloc (sizeof *R * n_rows);
      double *C = xmalloc (sizeof *C * n_cols);

      {
	double y, t, c = 0., s = 0.;
	int i = 0;

	for (;;)
	  {
	    R[i] = s + (xt->row_tot[i] + 1.) / 2.;
	    y = xt->row_tot[i] - c;
	    t = s + y;
	    c = (t - s) - y;
	    s = t;
	    if (++i == n_rows)
	      break;
	    assert (i < n_rows);
	  }
      }

      {
	double y, t, c = 0., s = 0.;
	int j = 0;

	for (;;)
	  {
	    C[j] = s + (xt->col_tot[j] + 1.) / 2;
	    y = xt->col_tot[j] - c;
	    t = s + y;
	    c = (t - s) - y;
	    s = t;
	    if (++j == n_cols)
	      break;
	    assert (j < n_cols);
	  }
      }

      calc_r (xt, R, C, &v[6], &t[6], &ase[6]);

      free (R);
      free (C);

      calc_r (xt, (double *) xt->vars[ROW_VAR].values,
              (double *) xt->vars[COL_VAR].values,
              &v[7], &t[7], &ase[7]);
    }

  /* Cohen's kappa. */
  if (proc->statistics & CRS_ST_KAPPA && xt->ns_rows == xt->ns_cols)
    {
      double ase_under_h0;
      double sum_fii, sum_rici, sum_fiiri_ci, sum_fijri_ci2, sum_riciri_ci;
      int i, j;

      for (sum_fii = sum_rici = sum_fiiri_ci = sum_riciri_ci = 0., i = j = 0;
	   i < xt->ns_rows; i++, j++)
	{
	  double prod, sum;

	  while (xt->col_tot[j] == 0.)
	    j++;

	  prod = xt->row_tot[i] * xt->col_tot[j];
	  sum = xt->row_tot[i] + xt->col_tot[j];

	  sum_fii += xt->mat[j + i * n_cols];
	  sum_rici += prod;
	  sum_fiiri_ci += xt->mat[j + i * n_cols] * sum;
	  sum_riciri_ci += prod * sum;
	}
      for (sum_fijri_ci2 = 0., i = 0; i < xt->ns_rows; i++)
	for (j = 0; j < xt->ns_cols; j++)
	  {
	    double sum = xt->row_tot[i] + xt->col_tot[j];
	    sum_fijri_ci2 += xt->mat[j + i * n_cols] * sum * sum;
	  }

      v[8] = (xt->total * sum_fii - sum_rici) / (pow2 (xt->total) - sum_rici);

      ase_under_h0 = sqrt ((pow2 (xt->total) * sum_rici
			    + sum_rici * sum_rici
			    - xt->total * sum_riciri_ci)
			   / (xt->total * (pow2 (xt->total) - sum_rici) * (pow2 (xt->total) - sum_rici)));

      ase[8] = sqrt (xt->total * (((sum_fii * (xt->total - sum_fii))
				/ pow2 (pow2 (xt->total) - sum_rici))
			       + ((2. * (xt->total - sum_fii)
				   * (2. * sum_fii * sum_rici
				      - xt->total * sum_fiiri_ci))
				  / pow3 (pow2 (xt->total) - sum_rici))
			       + (pow2 (xt->total - sum_fii)
				  * (xt->total * sum_fijri_ci2 - 4.
				     * sum_rici * sum_rici)
				  / pow4 (pow2 (xt->total) - sum_rici))));

      t[8] = v[8] / ase_under_h0;
    }

  return 1;
}

/* Calculate risk estimate. */
static bool
calc_risk (struct crosstabulation *xt,
           double *value, double *upper, double *lower, union value *c,
           double *n_valid)
{
  size_t n_cols = xt->vars[COL_VAR].n_values;
  double f11, f12, f21, f22;
  double v;

  for (int i = 0; i < 3; i++)
    value[i] = upper[i] = lower[i] = SYSMIS;

  if (xt->ns_rows != 2 || xt->ns_cols != 2)
    return false;

  {
    /* Find populated columns. */
    int nz_cols[2];
    int n = 0;
    FOR_EACH_POPULATED_COLUMN (c, xt)
      nz_cols[n++] = c;
    assert (n == 2);

    /* Find populated rows. */
    int nz_rows[2];
    n = 0;
    FOR_EACH_POPULATED_ROW (r, xt)
      nz_rows[n++] = r;
    assert (n == 2);

    f11 = xt->mat[nz_cols[0] + n_cols * nz_rows[0]];
    f12 = xt->mat[nz_cols[1] + n_cols * nz_rows[0]];
    f21 = xt->mat[nz_cols[0] + n_cols * nz_rows[1]];
    f22 = xt->mat[nz_cols[1] + n_cols * nz_rows[1]];
    *n_valid = f11 + f12 + f21 + f22;

    c[0] = xt->vars[COL_VAR].values[nz_cols[0]];
    c[1] = xt->vars[COL_VAR].values[nz_cols[1]];
  }

  value[0] = (f11 * f22) / (f12 * f21);
  v = sqrt (1. / f11 + 1. / f12 + 1. / f21 + 1. / f22);
  lower[0] = value[0] * exp (-1.960 * v);
  upper[0] = value[0] * exp (1.960 * v);

  value[1] = (f11 * (f21 + f22)) / (f21 * (f11 + f12));
  v = sqrt ((f12 / (f11 * (f11 + f12)))
	    + (f22 / (f21 * (f21 + f22))));
  lower[1] = value[1] * exp (-1.960 * v);
  upper[1] = value[1] * exp (1.960 * v);

  value[2] = (f12 * (f21 + f22)) / (f22 * (f11 + f12));
  v = sqrt ((f11 / (f12 * (f11 + f12)))
	    + (f21 / (f22 * (f21 + f22))));
  lower[2] = value[2] * exp (-1.960 * v);
  upper[2] = value[2] * exp (1.960 * v);

  return true;
}

/* Calculate directional measures. */
static int
calc_directional (struct crosstabs_proc *proc, struct crosstabulation *xt,
                  double v[N_DIRECTIONAL], double ase[N_DIRECTIONAL],
		  double t[N_DIRECTIONAL], double sig[N_DIRECTIONAL])
{
  size_t n_rows = xt->vars[ROW_VAR].n_values;
  size_t n_cols = xt->vars[COL_VAR].n_values;
  for (int i = 0; i < N_DIRECTIONAL; i++)
    v[i] = ase[i] = t[i] = sig[i] = SYSMIS;

  /* Lambda. */
  if (proc->statistics & CRS_ST_LAMBDA)
    {
      /* Find maximum for each row and their sum. */
      double *fim = xnmalloc (n_rows, sizeof *fim);
      int *fim_index = xnmalloc (n_rows, sizeof *fim_index);
      double sum_fim = 0.0;
      for (int i = 0; i < n_rows; i++)
	{
	  double max = xt->mat[i * n_cols];
	  int index = 0;

	  for (int j = 1; j < n_cols; j++)
	    if (xt->mat[j + i * n_cols] > max)
	      {
		max = xt->mat[j + i * n_cols];
		index = j;
	      }

          fim[i] = max;
	  sum_fim += max;
	  fim_index[i] = index;
	}

      /* Find maximum for each column. */
      double *fmj = xnmalloc (n_cols, sizeof *fmj);
      int *fmj_index = xnmalloc (n_cols, sizeof *fmj_index);
      double sum_fmj = 0.0;
      for (int j = 0; j < n_cols; j++)
	{
	  double max = xt->mat[j];
	  int index = 0;

	  for (int i = 1; i < n_rows; i++)
	    if (xt->mat[j + i * n_cols] > max)
	      {
		max = xt->mat[j + i * n_cols];
		index = i;
	      }

          fmj[j] = max;
	  sum_fmj += max;
	  fmj_index[j] = index;
	}

      /* Find maximum row total. */
      double rm = xt->row_tot[0];
      int rm_index = 0;
      for (int i = 1; i < n_rows; i++)
	if (xt->row_tot[i] > rm)
	  {
	    rm = xt->row_tot[i];
	    rm_index = i;
	  }

      /* Find maximum column total. */
      double cm = xt->col_tot[0];
      int cm_index = 0;
      for (int j = 1; j < n_cols; j++)
	if (xt->col_tot[j] > cm)
	  {
	    cm = xt->col_tot[j];
	    cm_index = j;
	  }

      v[0] = (sum_fim + sum_fmj - cm - rm) / (2. * xt->total - rm - cm);
      v[1] = (sum_fmj - rm) / (xt->total - rm);
      v[2] = (sum_fim - cm) / (xt->total - cm);

      /* ASE1 for Y given XT. */
      {
        double accum = 0.0;
	for (int i = 0; i < n_rows; i++)
          if (cm_index == fim_index[i])
            accum += fim[i];
        ase[2] = sqrt ((xt->total - sum_fim) * (sum_fim + cm - 2. * accum)
                       / pow3 (xt->total - cm));
      }

      /* ASE0 for Y given XT. */
      {
	double accum = 0.0;
	for (int i = 0; i < n_rows; i++)
	  if (cm_index != fim_index[i])
	    accum += (xt->mat[i * n_cols + fim_index[i]]
		      + xt->mat[i * n_cols + cm_index]);
	t[2] = v[2] / (sqrt (accum - pow2 (sum_fim - cm) / xt->total) / (xt->total - cm));
      }

      /* ASE1 for XT given Y. */
      {
        double accum = 0.0;
	for (int j = 0; j < n_cols; j++)
          if (rm_index == fmj_index[j])
            accum += fmj[j];
        ase[1] = sqrt ((xt->total - sum_fmj) * (sum_fmj + rm - 2. * accum)
                       / pow3 (xt->total - rm));
      }

      /* ASE0 for XT given Y. */
      {
	double accum = 0.0;
	for (int j = 0; j < n_cols; j++)
	  if (rm_index != fmj_index[j])
	    accum += (xt->mat[j + n_cols * fmj_index[j]]
		      + xt->mat[j + n_cols * rm_index]);
	t[1] = v[1] / (sqrt (accum - pow2 (sum_fmj - rm) / xt->total) / (xt->total - rm));
      }

      /* Symmetric ASE0 and ASE1. */
      {
	double accum0 = 0.0;
	double accum1 = 0.0;
	for (int i = 0; i < n_rows; i++)
	  for (int j = 0; j < n_cols; j++)
	    {
	      int temp0 = (fmj_index[j] == i) + (fim_index[i] == j);
	      int temp1 = (i == rm_index) + (j == cm_index);
	      accum0 += xt->mat[j + i * n_cols] * pow2 (temp0 - temp1);
	      accum1 += (xt->mat[j + i * n_cols]
			 * pow2 (temp0 + (v[0] - 1.) * temp1));
	    }
	ase[0] = sqrt (accum1 - 4. * xt->total * v[0] * v[0]) / (2. * xt->total - rm - cm);
	t[0] = v[0] / (sqrt (accum0 - pow2 (sum_fim + sum_fmj - cm - rm) / xt->total)
		       / (2. * xt->total - rm - cm));
      }

      for (int i = 0; i < 3; i++)
        sig[i] = 2 * gsl_cdf_ugaussian_Q (t[i]);

      free (fim);
      free (fim_index);
      free (fmj);
      free (fmj_index);

      /* Tau. */
      {
	double sum_fij2_ri = 0.0;
        double sum_fij2_ci = 0.0;
        FOR_EACH_POPULATED_ROW (i, xt)
          FOR_EACH_POPULATED_COLUMN (j, xt)
	    {
	      double temp = pow2 (xt->mat[j + i * n_cols]);
	      sum_fij2_ri += temp / xt->row_tot[i];
	      sum_fij2_ci += temp / xt->col_tot[j];
	    }

	double sum_ri2 = 0.0;
	for (int i = 0; i < n_rows; i++)
	  sum_ri2 += pow2 (xt->row_tot[i]);

        double sum_cj2 = 0.0;
	for (int j = 0; j < n_cols; j++)
	  sum_cj2 += pow2 (xt->col_tot[j]);

	v[3] = (xt->total * sum_fij2_ci - sum_ri2) / (pow2 (xt->total) - sum_ri2);
	v[4] = (xt->total * sum_fij2_ri - sum_cj2) / (pow2 (xt->total) - sum_cj2);
      }
    }

  if (proc->statistics & CRS_ST_UC)
    {
      double UX = 0.0;
      FOR_EACH_POPULATED_ROW (i, xt)
        UX -= xt->row_tot[i] / xt->total * log (xt->row_tot[i] / xt->total);

      double UY = 0.0;
      FOR_EACH_POPULATED_COLUMN (j, xt)
        UY -= xt->col_tot[j] / xt->total * log (xt->col_tot[j] / xt->total);

      double UXY = 0.0;
      double P = 0.0;
      for (int i = 0; i < n_rows; i++)
	for (int j = 0; j < n_cols; j++)
	  {
	    double entry = xt->mat[j + i * n_cols];

	    if (entry <= 0.)
	      continue;

	    P += entry * pow2 (log (xt->col_tot[j] * xt->row_tot[i] / (xt->total * entry)));
	    UXY -= entry / xt->total * log (entry / xt->total);
	  }

      double ase1_yx = 0.0;
      double ase1_xy = 0.0;
      double ase1_sym = 0.0;
      for (int i = 0; i < n_rows; i++)
	for (int j = 0; j < n_cols; j++)
	  {
	    double entry = xt->mat[j + i * n_cols];

	    if (entry <= 0.)
	      continue;

	    ase1_yx += entry * pow2 (UY * log (entry / xt->row_tot[i])
				    + (UX - UXY) * log (xt->col_tot[j] / xt->total));
	    ase1_xy += entry * pow2 (UX * log (entry / xt->col_tot[j])
				    + (UY - UXY) * log (xt->row_tot[i] / xt->total));
	    ase1_sym += entry * pow2 ((UXY
				      * log (xt->row_tot[i] * xt->col_tot[j] / pow2 (xt->total)))
				     - (UX + UY) * log (entry / xt->total));
	  }

      v[5] = 2. * ((UX + UY - UXY) / (UX + UY));
      ase[5] = (2. / (xt->total * pow2 (UX + UY))) * sqrt (ase1_sym);
      t[5] = SYSMIS;

      v[6] = (UX + UY - UXY) / UX;
      ase[6] = sqrt (ase1_xy) / (xt->total * UX * UX);
      t[6] = v[6] / (sqrt (P - xt->total * pow2 (UX + UY - UXY)) / (xt->total * UX));

      v[7] = (UX + UY - UXY) / UY;
      ase[7] = sqrt (ase1_yx) / (xt->total * UY * UY);
      t[7] = v[7] / (sqrt (P - xt->total * pow2 (UX + UY - UXY)) / (xt->total * UY));
    }

  /* Somers' D. */
  if (proc->statistics & CRS_ST_D)
    {
      double v_dummy[N_SYMMETRIC];
      double ase_dummy[N_SYMMETRIC];
      double t_dummy[N_SYMMETRIC];
      double somers_d_v[3];
      double somers_d_ase[3];
      double somers_d_t[3];

      if (calc_symmetric (proc, xt, v_dummy, ase_dummy, t_dummy,
                          somers_d_v, somers_d_ase, somers_d_t))
        {
          for (int i = 0; i < 3; i++)
            {
              v[8 + i] = somers_d_v[i];
              ase[8 + i] = somers_d_ase[i];
              t[8 + i] = somers_d_t[i];
              sig[8 + i] = 2 * gsl_cdf_ugaussian_Q (fabs (somers_d_t[i]));
            }
        }
    }

  /* Eta. */
  if (proc->statistics & CRS_ST_ETA)
    {
      /* X dependent. */
      double sum_Xr = 0.0;
      double sum_X2r = 0.0;
      for (int i = 0; i < n_rows; i++)
        {
          sum_Xr += xt->vars[ROW_VAR].values[i].f * xt->row_tot[i];
          sum_X2r += pow2 (xt->vars[ROW_VAR].values[i].f) * xt->row_tot[i];
        }
      double SX = sum_X2r - pow2 (sum_Xr) / xt->total;

      double SXW = 0.0;
      FOR_EACH_POPULATED_COLUMN (j, xt)
        {
          double cum = 0.0;

          for (int i = 0; i < n_rows; i++)
            {
              SXW += (pow2 (xt->vars[ROW_VAR].values[i].f)
                      * xt->mat[j + i * n_cols]);
              cum += (xt->vars[ROW_VAR].values[i].f
                      * xt->mat[j + i * n_cols]);
            }

          SXW -= cum * cum / xt->col_tot[j];
        }
      v[11] = sqrt (1. - SXW / SX);

      /* Y dependent. */
      double sum_Yc = 0.0;
      double sum_Y2c = 0.0;
      for (int i = 0; i < n_cols; i++)
        {
          sum_Yc += xt->vars[COL_VAR].values[i].f * xt->col_tot[i];
          sum_Y2c += pow2 (xt->vars[COL_VAR].values[i].f) * xt->col_tot[i];
        }
      double SY = sum_Y2c - pow2 (sum_Yc) / xt->total;

      double SYW = 0.0;
      FOR_EACH_POPULATED_ROW (i, xt)
        {
          double cum = 0.0;
          for (int j = 0; j < n_cols; j++)
            {
              SYW += (pow2 (xt->vars[COL_VAR].values[j].f)
                      * xt->mat[j + i * n_cols]);
              cum += (xt->vars[COL_VAR].values[j].f
                      * xt->mat[j + i * n_cols]);
            }

          SYW -= cum * cum / xt->row_tot[i];
        }
      v[12] = sqrt (1. - SYW / SY);
    }

  return 1;
}

/*
   Local Variables:
   mode: c
   End:
*/
