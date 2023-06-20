/*
  PSPP - a program for statistical analysis.
  Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2011, 2014, 2015 Free Software Foundation, Inc.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include <stdlib.h>
#include <gsl/gsl_histogram.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "data/settings.h"
#include "data/value-labels.h"
#include "data/variable.h"

#include "language/commands/split-file.h"

#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "language/commands/freq.h"

#include "libpspp/array.h"
#include "libpspp/bit-vector.h"
#include "libpspp/compiler.h"
#include "libpspp/hmap.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"

#include "math/histogram.h"
#include "math/moments.h"
#include "math/chart-geometry.h"


#include "output/charts/barchart.h"
#include "output/charts/piechart.h"
#include "output/charts/plot-hist.h"
#include "output/pivot-table.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Percentiles to calculate. */

struct percentile
  {
    double p;        /* The percentile to calculate, between 0 and 1. */
    bool show;       /* True to show this percentile in the statistics box. */
  };

static int
percentile_compare_3way (const void *a_, const void *b_)
{
  const struct percentile *a = a_;
  const struct percentile *b = b_;

  return a->p < b->p ? -1 : a->p > b->p;
}

enum
  {
    FRQ_FREQ,
    FRQ_PERCENT
  };

enum sortprops
  {
    FRQ_AFREQ,
    FRQ_DFREQ,
    FRQ_AVALUE,
    FRQ_DVALUE
  };

#define STATISTICS                                      \
  S(FRQ_ST_MEAN,       "MEAN",      N_("Mean"))         \
  S(FRQ_ST_SEMEAN,     "SEMEAN",    N_("S.E. Mean"))    \
  S(FRQ_ST_MEDIAN,     "MEDIAN",    N_("Median"))       \
  S(FRQ_ST_MODE,       "MODE",      N_("Mode"))         \
  S(FRQ_ST_STDDEV,     "STDDEV",    N_("Std Dev"))      \
  S(FRQ_ST_VARIANCE,   "VARIANCE",  N_("Variance"))     \
  S(FRQ_ST_KURTOSIS,   "KURTOSIS",  N_("Kurtosis"))     \
  S(FRQ_ST_SEKURTOSIS, "SEKURTOSIS",N_("S.E. Kurt"))    \
  S(FRQ_ST_SKEWNESS,   "SKEWNESS",  N_("Skewness"))     \
  S(FRQ_ST_SESKEWNESS, "SESKEWNESS",N_("S.E. Skew"))    \
  S(FRQ_ST_RANGE,      "RANGE",     N_("Range"))        \
  S(FRQ_ST_MINIMUM,    "MINIMUM",   N_("Minimum"))      \
  S(FRQ_ST_MAXIMUM,    "MAXIMUM",   N_("Maximum"))      \
  S(FRQ_ST_SUM,        "SUM",       N_("Sum"))

enum frq_statistic
  {
#define S(ENUM, KEYWORD, NAME) ENUM,
STATISTICS
#undef S
  };

enum {
#define S(ENUM, KEYWORD, NAME) +1
  FRQ_ST_count = STATISTICS,
#undef S
};

static const char *st_keywords[FRQ_ST_count] = {
#define S(ENUM, KEYWORD, NAME) KEYWORD,
  STATISTICS
#undef S
};

static const char *st_names[FRQ_ST_count] = {
#define S(ENUM, KEYWORD, NAME) NAME,
  STATISTICS
#undef S
};

struct freq_tab
  {
    struct hmap data;           /* Hash table for accumulating counts. */
    struct freq *valid;         /* Valid freqs. */
    size_t n_valid;                /* Number of total freqs. */
    const struct dictionary *dict; /* Source of entries in the table. */

    struct freq *missing;       /* Missing freqs. */
    size_t n_missing;                /* Number of missing freqs. */

    /* Statistics. */
    double total_cases;                /* Sum of weights of all cases. */
    double valid_cases;                /* Sum of weights of valid cases. */
  };

struct frq_chart
  {
    double x_min;               /* X axis minimum value. */
    double x_max;               /* X axis maximum value. */
    int y_scale;                /* Y axis scale: FRQ_FREQ or FRQ_PERCENT. */

    /* Histograms only. */
    double y_max;               /* Y axis maximum value. */
    bool draw_normal;           /* Whether to draw normal curve. */

    /* Pie charts only. */
    bool include_missing;       /* Whether to include missing values. */
  };

/* Per-variable frequency data. */
struct var_freqs
  {
    const struct variable *var;

    /* Freqency table. */
    struct freq_tab tab;        /* Frequencies table to use. */

    /* Statistics. */
    double stat[FRQ_ST_count];
    double *percentiles;

    /* Variable attributes. */
    int width;
  };

struct frq_proc
  {
    struct var_freqs *vars;
    size_t n_vars;

    /* Percentiles to calculate and possibly display. */
    struct percentile *percentiles;
    size_t median_idx;
    size_t n_percentiles;

    /* Frequency table display. */
    long int max_categories;         /* Maximum categories to show. */
    int sort;                   /* FRQ_AVALUE or FRQ_DVALUE
                                   or FRQ_AFREQ or FRQ_DFREQ. */

    /* Statistics. */
    unsigned long stats;

    /* Histogram and pie chart settings. */
    struct frq_chart *hist, *pie, *bar;

    bool warn;
  };


struct freq_compare_aux
  {
    bool by_freq;
    bool ascending_freq;

    int width;
    bool ascending_value;
  };

static void calc_stats (const struct frq_proc *,
                        const struct var_freqs *, double d[FRQ_ST_count]);

static void do_piechart(const struct frq_chart *pie,
                        const struct variable *var,
                        const struct freq_tab *frq_tab);

static void do_barchart(const struct frq_chart *bar,
                        const struct variable **var,
                        const struct freq_tab *frq_tab);

static struct frq_stats_table *frq_stats_table_submit (
  struct frq_stats_table *, const struct frq_proc *,
  const struct dictionary *, const struct variable *wv,
  const struct ccase *example);
static void frq_stats_table_destroy (struct frq_stats_table *);

static int
compare_freq (const void *a_, const void *b_, const void *aux_)
{
  const struct freq_compare_aux *aux = aux_;
  const struct freq *a = a_;
  const struct freq *b = b_;

  if (aux->by_freq && a->count != b->count)
    {
      int cmp = a->count > b->count ? 1 : -1;
      return aux->ascending_freq ? cmp : -cmp;
    }
  else
    {
      int cmp = value_compare_3way (a->values, b->values, aux->width);
      return aux->ascending_value ? cmp : -cmp;
    }
}

/* Create a gsl_histogram from a freq_tab */
static struct histogram *freq_tab_to_hist (const struct frq_proc *,
                                           const struct var_freqs *);

static void
put_freq_row (struct pivot_table *table, int var_idx,
              double frequency, double percent,
              double valid_percent, double cum_percent)
{
  double entries[] = { frequency, percent, valid_percent, cum_percent };
  for (size_t i = 0; i < sizeof entries / sizeof *entries; i++)
    if (entries[i] != SYSMIS)
      pivot_table_put2 (table, i, var_idx,
                        pivot_value_new_number (entries[i]));
}

/* Displays a full frequency table for variable V. */
static void
dump_freq_table (const struct var_freqs *vf, const struct variable *wv)
{
  const struct freq_tab *ft = &vf->tab;

  struct pivot_table *table = pivot_table_create__ (pivot_value_new_variable (
                                                      vf->var), "Frequencies");
  pivot_table_set_weight_var (table, wv);

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("Frequency"), PIVOT_RC_COUNT,
                          N_("Percent"), PIVOT_RC_PERCENT,
                          N_("Valid Percent"), PIVOT_RC_PERCENT,
                          N_("Cumulative Percent"), PIVOT_RC_PERCENT);

  struct pivot_dimension *variable = pivot_dimension_create__ (
    table, PIVOT_AXIS_ROW, pivot_value_new_variable (vf->var));

  double cum_freq = 0.0;
  double cum_percent = 0.0;
  struct pivot_category *valid = NULL;
  for (const struct freq *f = ft->valid; f < ft->missing; f++)
    {
      cum_freq += f->count;
      double valid_percent = f->count / ft->valid_cases * 100.0;
      cum_percent += valid_percent;

      if (!valid)
        valid = pivot_category_create_group (variable->root, N_("Valid"));
      int var_idx = pivot_category_create_leaf (
        valid, pivot_value_new_var_value (vf->var, &f->values[0]));
      put_freq_row (table, var_idx, f->count,
                    f->count / ft->total_cases * 100.0,
                    valid_percent, cum_percent);
    }

  struct pivot_category *missing = NULL;
  size_t n_categories = ft->n_valid + ft->n_missing;
  for (const struct freq *f = ft->missing; f < &ft->valid[n_categories]; f++)
    {
      cum_freq += f->count;

      if (!missing)
        missing = pivot_category_create_group (variable->root, N_("Missing"));
      int var_idx = pivot_category_create_leaf (
        missing, pivot_value_new_var_value (vf->var, &f->values[0]));
      put_freq_row (table, var_idx, f->count,
                    f->count / ft->total_cases * 100.0, SYSMIS, SYSMIS);
    }

  int var_idx = pivot_category_create_leaf (
    variable->root, pivot_value_new_text (N_("Total")));
  put_freq_row (table, var_idx, cum_freq, cum_percent, SYSMIS, SYSMIS);

  pivot_table_submit (table);
}

/* Statistical display. */

static double
calc_percentile (double p, double valid_cases, double x1, double x2)
{
  double s, dummy;

  s = (settings_get_algorithm () != COMPATIBLE
       ? modf ((valid_cases - 1) * p, &dummy)
       : modf ((valid_cases + 1) * p - 1, &dummy));

  return x1 + (x2 - x1) * s;
}

/* Calculates all of the percentiles for VF within FRQ. */
static void
calc_percentiles (const struct frq_proc *frq, struct var_freqs *vf)
{
  if (!frq->n_percentiles)
    return;

  if (!vf->percentiles)
    vf->percentiles = xnmalloc (frq->n_percentiles, sizeof *vf->percentiles);

  const struct freq_tab *ft = &vf->tab;
  const double W = ft->valid_cases;
  size_t idx = 0;

  double rank = 0;
  for (const struct freq *f = ft->valid; f < ft->missing; f++)
    {
      rank += f->count;
      for (; idx < frq->n_percentiles; idx++)
        {
          struct percentile *pc = &frq->percentiles[idx];
          double tp;

          tp = (settings_get_algorithm () == ENHANCED
                ? (W - 1) * pc->p
                : (W + 1) * pc->p - 1);

          if (rank <= tp)
            break;

          if (tp + 1 < rank || f + 1 >= ft->missing)
            vf->percentiles[idx] = f->values[0].f;
          else
            vf->percentiles[idx] = calc_percentile (pc->p, W, f->values[0].f,
                                                    f[1].values[0].f);
        }
    }
  for (; idx < frq->n_percentiles; idx++)
    vf->percentiles[idx] = (ft->n_valid > 0
                            ? ft->valid[ft->n_valid - 1].values[0].f
                            : SYSMIS);
}

/* Returns true iff the value in struct freq F is non-missing
   for variable V. */
static bool
not_missing (const void *f_, const void *v_)
{
  const struct freq *f = f_;
  const struct variable *v = v_;

  return !var_is_value_missing (v, f->values);
}

/* Summarizes the frequency table data for variable V. */
static void
postprocess_freq_tab (const struct frq_proc *frq, struct var_freqs *vf)
{
  struct freq_tab *ft = &vf->tab;

  /* Extract data from hash table. */
  size_t count = hmap_count (&ft->data);
  struct freq *freqs = freq_hmap_extract (&ft->data);

  /* Put data into ft. */
  ft->valid = freqs;
  ft->n_valid = partition (freqs, count, sizeof *freqs, not_missing, vf->var);
  ft->missing = freqs + ft->n_valid;
  ft->n_missing = count - ft->n_valid;

  /* Sort data. */
  struct freq_compare_aux aux = {
    .by_freq = frq->sort == FRQ_AFREQ || frq->sort == FRQ_DFREQ,
    .ascending_freq = frq->sort != FRQ_DFREQ,
    .width = vf->width,
    .ascending_value = frq->sort != FRQ_DVALUE,
  };
  sort (ft->valid, ft->n_valid, sizeof *ft->valid, compare_freq, &aux);
  sort (ft->missing, ft->n_missing, sizeof *ft->missing, compare_freq, &aux);

  /* Summary statistics. */
  ft->valid_cases = 0.0;
  for (size_t i = 0; i < ft->n_valid; ++i)
    ft->valid_cases += ft->valid[i].count;

  ft->total_cases = ft->valid_cases;
  for (size_t i = 0; i < ft->n_missing; ++i)
    ft->total_cases += ft->missing[i].count;
}

/* Add data from case C to the frequency table. */
static void
calc (struct frq_proc *frq, const struct ccase *c, const struct dataset *ds)
{
  double weight = dict_get_case_weight (dataset_dict (ds), c, &frq->warn);
  for (size_t i = 0; i < frq->n_vars; i++)
    {
      struct var_freqs *vf = &frq->vars[i];
      const union value *value = case_data (c, vf->var);
      size_t hash = value_hash (value, vf->width, 0);
      struct freq *f;

      f = freq_hmap_search (&vf->tab.data, value, vf->width, hash);
      if (f == NULL)
        f = freq_hmap_insert (&vf->tab.data, value, vf->width, hash);

      f->count += weight;
    }
}

static void
output_splits_once (bool *need_splits, const struct dataset *ds,
                    const struct ccase *c)
{
  if (*need_splits)
    {
      output_split_file_values (ds, c);
      *need_splits = false;
    }
}

/* Finishes up with the variables after frequencies have been
   calculated.  Displays statistics, percentiles, ... */
static struct frq_stats_table *
postcalc (struct frq_proc *frq, const struct dataset *ds,
          struct ccase *example, struct frq_stats_table *fst)
{
  const struct dictionary *dict = dataset_dict (ds);
  const struct variable *wv = dict_get_weight (dict);

  for (size_t i = 0; i < frq->n_vars; i++)
    {
      struct var_freqs *vf = &frq->vars[i];
      postprocess_freq_tab (frq, vf);
      calc_percentiles (frq, vf);
    }

  enum split_type st = dict_get_split_type (dict);
  bool need_splits = true;
  if (frq->stats)
    {
      if (st != SPLIT_LAYERED)
        output_splits_once (&need_splits, ds, example);
      fst = frq_stats_table_submit (fst, frq, dict, wv, example);
    }

  for (size_t i = 0; i < frq->n_vars; i++)
    {
      struct var_freqs *vf = &frq->vars[i];

      /* Frequencies tables. */
      if (vf->tab.n_valid + vf->tab.n_missing <= frq->max_categories)
        {
          output_splits_once (&need_splits, ds, example);
          dump_freq_table (vf, wv);
        }

      if (frq->hist && var_is_numeric (vf->var) && vf->tab.n_valid > 0)
        {
          double d[FRQ_ST_count];
          struct histogram *histogram;

          calc_stats (frq, vf, d);

          histogram = freq_tab_to_hist (frq, vf);

          if (histogram)
            {
              output_splits_once (&need_splits, ds, example);
              chart_submit (histogram_chart_create (
                              histogram->gsl_hist, var_to_string(vf->var),
                              vf->tab.valid_cases,
                              d[FRQ_ST_MEAN],
                              d[FRQ_ST_STDDEV],
                              frq->hist->draw_normal));

              statistic_destroy (&histogram->parent);
            }
        }

      if (frq->pie)
        {
          output_splits_once (&need_splits, ds, example);
          do_piechart(frq->pie, vf->var, &vf->tab);
        }

      if (frq->bar)
        {
          output_splits_once (&need_splits, ds, example);
          do_barchart(frq->bar, &vf->var, &vf->tab);
        }

      free (vf->tab.valid);
      freq_hmap_destroy (&vf->tab.data, vf->width);
    }

  return fst;
}

static void
frq_run (struct frq_proc *frq, struct dataset *ds)
{
  struct frq_stats_table *fst = NULL;
  struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds),
                                                           dataset_dict (ds));
  struct casereader *group;
  while (casegrouper_get_next_group (grouper, &group))
    {
      for (size_t i = 0; i < frq->n_vars; i++)
        hmap_init (&frq->vars[i].tab.data);

      struct ccase *example = casereader_peek (group, 0);

      struct ccase *c;
      for (; (c = casereader_read (group)) != NULL; case_unref (c))
        calc (frq, c, ds);
      fst = postcalc (frq, ds, example, fst);
      casereader_destroy (group);

      case_unref (example);
    }
  frq_stats_table_destroy (fst);
  casegrouper_destroy (grouper);
  proc_commit (ds);
}

static void
add_percentile (struct frq_proc *frq, double p, bool show,
                size_t *allocated_percentiles)
{
  if (frq->n_percentiles >= *allocated_percentiles)
    frq->percentiles = x2nrealloc (frq->percentiles, allocated_percentiles,
                                   sizeof *frq->percentiles);
  frq->percentiles[frq->n_percentiles++] = (struct percentile) {
    .p = p,
    .show = show,
  };
}

int
cmd_frequencies (struct lexer *lexer, struct dataset *ds)
{
  bool ok = false;
  const struct variable **vars = NULL;

  size_t allocated_percentiles = 0;

  const unsigned long DEFAULT_STATS = (BIT_INDEX (FRQ_ST_MEAN)
                                       | BIT_INDEX (FRQ_ST_STDDEV)
                                       | BIT_INDEX (FRQ_ST_MINIMUM)
                                       | BIT_INDEX (FRQ_ST_MAXIMUM));
  struct frq_proc frq = {
    .sort = FRQ_AVALUE,
    .stats = DEFAULT_STATS,
    .max_categories = LONG_MAX,
    .median_idx = SIZE_MAX,
    .warn = true,
  };

  lex_match (lexer, T_SLASH);
  if (lex_match_id (lexer, "VARIABLES") && !lex_force_match (lexer, T_EQUALS))
    goto done;

  if (!parse_variables_const (lexer, dataset_dict (ds),
                              &vars, &frq.n_vars, PV_NO_DUPLICATE))
    goto done;

  frq.vars = xcalloc (frq.n_vars, sizeof *frq.vars);
  for (size_t i = 0; i < frq.n_vars; ++i)
    {
      frq.vars[i].var = vars[i];
      frq.vars[i].width = var_get_width (vars[i]);
    }

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "STATISTICS"))
        {
          lex_match (lexer, T_EQUALS);
          frq.stats = 0;

          int ofs = lex_ofs (lexer);
          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              for (int s = 0; s < FRQ_ST_count; s++)
                if (lex_match_id (lexer, st_keywords[s]))
                  {
                    frq.stats |= 1 << s;
                    goto next;
                  }

              if (lex_match_id (lexer, "DEFAULT"))
                frq.stats = DEFAULT_STATS;
              else if (lex_match (lexer, T_ALL))
                frq.stats = (1 << FRQ_ST_count) - 1;
              else if (lex_match_id (lexer, "NONE"))
                frq.stats = 0;
              else
                {
#define S(ENUM, KEYWORD, NAME) KEYWORD,
                  lex_error_expecting (lexer,
                                       STATISTICS
                                       "DEFAULT", "ALL", "NONE");
#undef S
                  goto done;
                }

            next:;
            }

          if (lex_ofs (lexer) == ofs)
            frq.stats = DEFAULT_STATS;
        }
      else if (lex_match_id (lexer, "PERCENTILES"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              if (!lex_force_num_range_closed (lexer, "PERCENTILES", 0, 100))
                goto done;
              add_percentile (&frq, lex_number (lexer) / 100.0, true,
                              &allocated_percentiles);
              lex_get (lexer);
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "FORMAT"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "TABLE"))
                {
                }
              else if (lex_match_id (lexer, "NOTABLE"))
                frq.max_categories = 0;
              else if (lex_match_id (lexer, "LIMIT"))
                {
                  if (!lex_force_match (lexer, T_LPAREN)
                      || !lex_force_int_range (lexer, "LIMIT", 0, INT_MAX))
                    goto done;

                  frq.max_categories = lex_integer (lexer);
                  lex_get (lexer);

                  if (!lex_force_match (lexer, T_RPAREN))
                    goto done;
                }
              else if (lex_match_id (lexer, "AVALUE"))
                frq.sort = FRQ_AVALUE;
              else if (lex_match_id (lexer, "DVALUE"))
                frq.sort = FRQ_DVALUE;
              else if (lex_match_id (lexer, "AFREQ"))
                frq.sort = FRQ_AFREQ;
              else if (lex_match_id (lexer, "DFREQ"))
                frq.sort = FRQ_DFREQ;
              else
                {
                  lex_error_expecting (lexer, "TABLE", "NOTABLE",
                                       "LIMIT", "AVALUE", "DVALUE",
                                       "AFREQ", "DFREQ");
                  goto done;
                }
            }
        }
      else if (lex_match_id (lexer, "NTILES"))
        {
          lex_match (lexer, T_EQUALS);

          if (!lex_force_int_range (lexer, "NTILES", 0, INT_MAX))
            goto done;

          int n = lex_integer (lexer);
          lex_get (lexer);
          for (int i = 0; i < n + 1; ++i)
            add_percentile (&frq, i / (double) n, true, &allocated_percentiles);
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
              goto done;
            }
        }
      else if (lex_match_id (lexer, "HISTOGRAM"))
        {
          double hi_min = -DBL_MAX;
          double hi_max = DBL_MAX;
          int hi_scale = FRQ_FREQ;
          int hi_freq = INT_MIN;
          int hi_pcnt = INT_MIN;
          bool hi_draw_normal = false;

          lex_match (lexer, T_EQUALS);

          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "NORMAL"))
                hi_draw_normal = true;
              else if (lex_match_id (lexer, "NONORMAL"))
                hi_draw_normal = false;
              else if (lex_match_id (lexer, "FREQ"))
                {
                  hi_scale = FRQ_FREQ;
                  if (lex_match (lexer, T_LPAREN))
                    {
                      if (!lex_force_int_range (lexer, "FREQ", 1, INT_MAX))
                        goto done;
                      hi_freq = lex_integer (lexer);
                      lex_get (lexer);
                      if (!lex_force_match (lexer, T_RPAREN))
                        goto done;
                    }
                }
              else if (lex_match_id (lexer, "PERCENT"))
                {
                  hi_scale = FRQ_PERCENT;
                  if (lex_match (lexer, T_LPAREN))
                    {
                      if (!lex_force_int_range (lexer, "PERCENT", 1, INT_MAX))
                        goto done;
                      hi_pcnt = lex_integer (lexer);
                      lex_get (lexer);
                      if (!lex_force_match (lexer, T_RPAREN))
                        goto done;
                    }
                }
              else if (lex_match_id (lexer, "MINIMUM"))
                {
                  if (!lex_force_match (lexer, T_LPAREN)
                      || !lex_force_num_range_closed (lexer, "MINIMUM",
                                                      -DBL_MAX, hi_max))
                    goto done;
                  hi_min = lex_number (lexer);
                  lex_get (lexer);
                  if (!lex_force_match (lexer, T_RPAREN))
                    goto done;
                }
              else if (lex_match_id (lexer, "MAXIMUM"))
                {
                  if (!lex_force_match (lexer, T_LPAREN)
                      || !lex_force_num_range_closed (lexer, "MAXIMUM",
                                                      hi_min, DBL_MAX))
                    goto done;
                  hi_max = lex_number (lexer);
                  lex_get (lexer);
                   if (!lex_force_match (lexer, T_RPAREN))
                    goto done;
                }
              else
                {
                  lex_error_expecting (lexer, "NORMAL", "NONORMAL",
                                       "FREQ", "PERCENT", "MINIMUM", "MAXIMUM");
                  goto done;
                }
            }

          free (frq.hist);
          frq.hist = xmalloc (sizeof *frq.hist);
          *frq.hist = (struct frq_chart) {
            .x_min = hi_min,
            .x_max = hi_max,
            .y_scale = hi_scale,
            .y_max = hi_scale == FRQ_FREQ ? hi_freq : hi_pcnt,
            .draw_normal = hi_draw_normal,
            .include_missing = false,
          };

          add_percentile (&frq, .25, false, &allocated_percentiles);
          add_percentile (&frq, .75, false, &allocated_percentiles);
        }
      else if (lex_match_id (lexer, "PIECHART"))
        {
          double pie_min = -DBL_MAX;
          double pie_max = DBL_MAX;
          bool pie_missing = true;

          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "MINIMUM"))
                {
                  if (!lex_force_match (lexer, T_LPAREN)
                      || !lex_force_num_range_closed (lexer, "MINIMUM",
                                                      -DBL_MAX, pie_max))
                    goto done;
                  pie_min = lex_number (lexer);
                  lex_get (lexer);
                  if (!lex_force_match (lexer, T_RPAREN))
                    goto done;
                }
              else if (lex_match_id (lexer, "MAXIMUM"))
                {
                  if (!lex_force_match (lexer, T_LPAREN)
                      || !lex_force_num_range_closed (lexer, "MAXIMUM",
                                                      pie_min, DBL_MAX))
                    goto done;
                  pie_max = lex_number (lexer);
                  lex_get (lexer);
                   if (!lex_force_match (lexer, T_RPAREN))
                    goto done;
                }
              else if (lex_match_id (lexer, "MISSING"))
                pie_missing = true;
              else if (lex_match_id (lexer, "NOMISSING"))
                pie_missing = false;
              else
                {
                  lex_error_expecting (lexer, "MINIMUM", "MAXIMUM",
                                       "MISSING", "NOMISSING");
                  goto done;
                }
            }

          free (frq.pie);
          frq.pie = xmalloc (sizeof *frq.pie);
          *frq.pie = (struct frq_chart) {
            .x_min = pie_min,
            .x_max = pie_max,
            .include_missing = pie_missing,
          };
        }
      else if (lex_match_id (lexer, "BARCHART"))
        {
          double bar_min = -DBL_MAX;
          double bar_max = DBL_MAX;
          bool bar_freq = true;

          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "MINIMUM"))
                {
                  if (!lex_force_match (lexer, T_LPAREN)
                      || !lex_force_num_range_closed (lexer, "MINIMUM",
                                                      -DBL_MAX, bar_max))
                    goto done;
                  bar_min = lex_number (lexer);
                  lex_get (lexer);
                  if (!lex_force_match (lexer, T_RPAREN))
                    goto done;
                }
              else if (lex_match_id (lexer, "MAXIMUM"))
                {
                  if (!lex_force_match (lexer, T_LPAREN)
                      || !lex_force_num_range_closed (lexer, "MAXIMUM",
                                                      bar_min, DBL_MAX))
                    goto done;
                  bar_max = lex_number (lexer);
                  lex_get (lexer);
                   if (!lex_force_match (lexer, T_RPAREN))
                    goto done;
                }
              else if (lex_match_id (lexer, "FREQ"))
                {
                  if (lex_match (lexer, T_LPAREN))
                    {
                      if (!lex_force_num_range_open (lexer, "FREQ", 0, DBL_MAX))
                        goto done;
                      /* XXX TODO */
                      lex_get (lexer);
                      if (!lex_force_match (lexer, T_RPAREN))
                        goto done;
                    }
                  bar_freq = true;
                }
              else if (lex_match_id (lexer, "PERCENT"))
                {
                  if (lex_match (lexer, T_LPAREN))
                    {
                      if (!lex_force_num_range_open (lexer, "PERCENT",
                                                     0, DBL_MAX))
                        goto done;
                      /* XXX TODO */
                      lex_get (lexer);
                      if (!lex_force_match (lexer, T_RPAREN))
                        goto done;
                    }
                  bar_freq = false;
                }
              else
                {
                  lex_error_expecting (lexer, "MINIMUM", "MAXIMUM",
                                       "FREQ", "PERCENT");
                  goto done;
                }
            }

          free (frq.bar);
          frq.bar = xmalloc (sizeof *frq.bar);
          *frq.bar = (struct frq_chart) {
            .x_min = bar_min,
            .x_max = bar_max,
            .include_missing = false,
            .y_scale = bar_freq ? FRQ_FREQ : FRQ_PERCENT,
          };
        }
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);

          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "EXCLUDE"))
                {
                  /* XXX TODO */
                }
              else if (lex_match_id (lexer, "INCLUDE"))
                {
                  /* XXX TODO */
                }
              else
                {
                  lex_error_expecting (lexer, "EXCLUDE", "INCLUDE");
                  goto done;
                }
            }
        }
      else if (lex_match_id (lexer, "ORDER"))
        {
          lex_match (lexer, T_EQUALS);
          /* XXX TODO */
          if (!lex_match_id (lexer, "ANALYSIS")
              && !lex_match_id (lexer, "VARIABLE"))
            {
              lex_error_expecting (lexer, "ANALYSIS", "VARIABLE");
              goto done;
            }
        }
      else
        {
          lex_error_expecting (lexer, "STATISTICS", "PERCENTILES", "FORMAT",
                               "NTILES", "ALGORITHM", "HISTOGRAM", "PIECHART",
                               "BARCHART", "MISSING", "ORDER");
          goto done;
        }
    }

  if (frq.stats & BIT_INDEX (FRQ_ST_MEDIAN))
    add_percentile (&frq, .5, false, &allocated_percentiles);

  if (frq.n_percentiles > 0)
    {
      qsort (frq.percentiles, frq.n_percentiles, sizeof *frq.percentiles,
             percentile_compare_3way);

      /* Combine equal percentiles. */
      size_t o = 1;
      for (int i = 1; i < frq.n_percentiles; ++i)
        {
          struct percentile *prev = &frq.percentiles[o - 1];
          struct percentile *this = &frq.percentiles[i];
          if (this->p != prev->p)
            frq.percentiles[o++] = *this;
          else if (this->show)
            prev->show = true;
        }
      frq.n_percentiles = o;

      for (size_t i = 0; i < frq.n_percentiles; i++)
        if (frq.percentiles[i].p == 0.5)
          {
            frq.median_idx = i;
            break;
          }
    }

  frq_run (&frq, ds);
  ok = true;

done:
  free (vars);
  for (size_t i = 0; i < frq.n_vars; i++)
    free (frq.vars[i].percentiles);
  free (frq.vars);
  free (frq.bar);
  free (frq.pie);
  free (frq.hist);
  free (frq.percentiles);

  return ok ? CMD_SUCCESS : CMD_FAILURE;
}

static double
calculate_iqr (const struct frq_proc *frq, const struct var_freqs *vf)
{
  double q1 = SYSMIS;
  double q3 = SYSMIS;

  /* This cannot work unless the 25th and 75th percentile are calculated */
  assert (frq->n_percentiles >= 2);
  for (int i = 0; i < frq->n_percentiles; i++)
    {
      struct percentile *pc = &frq->percentiles[i];

      if (fabs (0.25 - pc->p) < DBL_EPSILON)
        q1 = vf->percentiles[i];
      else if (fabs (0.75 - pc->p) < DBL_EPSILON)
        q3 = vf->percentiles[i];
    }

  return q1 == SYSMIS || q3 == SYSMIS ? SYSMIS : q3 - q1;
}

static bool
chart_includes_value (const struct frq_chart *chart,
                      const struct variable *var,
                      const union value *value)
{
  if (!chart->include_missing && var_is_value_missing (var, value))
    return false;

  if (var_is_numeric (var)
      && ((chart->x_min != SYSMIS && value->f < chart->x_min)
          || (chart->x_max != SYSMIS && value->f > chart->x_max)))
    return false;

  return true;
}

/* Create a gsl_histogram from a freq_tab */
static struct histogram *
freq_tab_to_hist (const struct frq_proc *frq, const struct var_freqs *vf)
{
  /* Find out the extremes of the x value, within the range to be included in
     the histogram, and sum the total frequency of those values. */
  double x_min = DBL_MAX;
  double x_max = -DBL_MAX;
  double valid_freq = 0;
  for (size_t i = 0; i < vf->tab.n_valid; i++)
    {
      const struct freq *f = &vf->tab.valid[i];
      if (chart_includes_value (frq->hist, vf->var, f->values))
        {
          x_min = MIN (x_min, f->values[0].f);
          x_max = MAX (x_max, f->values[0].f);
          valid_freq += f->count;
        }
    }

  if (valid_freq <= 0)
    return NULL;

  double iqr = calculate_iqr (frq, vf);

  double bin_width =
    (iqr > 0
     ? 2 * iqr / pow (valid_freq, 1.0 / 3.0)       /* Freedman-Diaconis. */
     : (x_max - x_min) / (1 + log2 (valid_freq))); /* Sturges */

  struct histogram *histogram = histogram_create (bin_width, x_min, x_max);
  if (histogram == NULL)
    return NULL;

  for (size_t i = 0; i < vf->tab.n_valid; i++)
    {
      const struct freq *f = &vf->tab.valid[i];
      if (chart_includes_value (frq->hist, vf->var, f->values))
        histogram_add (histogram, f->values[0].f, f->count);
    }

  return histogram;
}


/* Allocate an array of struct freqs and fill them from the data in FRQ_TAB,
   according to the parameters of CATCHART
   N_SLICES will contain the number of slices allocated.
   The caller is responsible for freeing slices
*/
static struct freq *
pick_cat_counts (const struct frq_chart *catchart,
                 const struct freq_tab *frq_tab,
                 int *n_slicesp)
{
  int n_slices = 0;
  struct freq *slices = xnmalloc (frq_tab->n_valid + frq_tab->n_missing, sizeof *slices);

  for (size_t i = 0; i < frq_tab->n_valid; i++)
    {
      struct freq *f = &frq_tab->valid[i];
      if (f->count >= catchart->x_min && f->count <= catchart->x_max)
        slices[n_slices++] = *f;
    }


  if (catchart->include_missing)
    {
      for (size_t i = 0; i < frq_tab->n_missing; i++)
        {
          const struct freq *f = &frq_tab->missing[i];
          slices[n_slices].count += f->count;

          if (i == 0)
            slices[n_slices].values[0] = f->values[0];
        }

      if (frq_tab->n_missing > 0)
        n_slices++;
    }

  *n_slicesp = n_slices;
  return slices;
}


/* Allocate an array of struct freqs and fill them from the data in FRQ_TAB,
   according to the parameters of CATCHART
   N_SLICES will contain the number of slices allocated.
   The caller is responsible for freeing slices
*/
static struct freq **
pick_cat_counts_ptr (const struct frq_chart *catchart,
                     const struct freq_tab *frq_tab,
                     int *n_slicesp)
{
  int n_slices = 0;
  struct freq **slices = xnmalloc (frq_tab->n_valid + frq_tab->n_missing, sizeof *slices);

  for (size_t i = 0; i < frq_tab->n_valid; i++)
    {
      struct freq *f = &frq_tab->valid[i];
      if (f->count >= catchart->x_min && f->count <= catchart->x_max)
        slices[n_slices++] = f;
    }

  if (catchart->include_missing)
    for (size_t i = 0; i < frq_tab->n_missing; i++)
      {
        const struct freq *f = &frq_tab->missing[i];
        if (i == 0)
          {
            slices[n_slices] = xmalloc (sizeof *slices[n_slices]);
            slices[n_slices]->values[0] = f->values[0];
          }

        slices[n_slices]->count += f->count;
      }

  *n_slicesp = n_slices;
  return slices;
}

static void
do_piechart (const struct frq_chart *pie, const struct variable *var,
             const struct freq_tab *frq_tab)
{
  int n_slices;
  struct freq *slices = pick_cat_counts (pie, frq_tab, &n_slices);

  if (n_slices < 2)
    msg (SW, _("Omitting pie chart for %s, which has only %d unique values."),
         var_get_name (var), n_slices);
  else if (n_slices > 50)
    msg (SW, _("Omitting pie chart for %s, which has over 50 unique values."),
         var_get_name (var));
  else
    chart_submit (piechart_create (var, slices, n_slices));

  free (slices);
}

static void
do_barchart (const struct frq_chart *bar, const struct variable **var,
             const struct freq_tab *frq_tab)
{
  int n_slices;
  struct freq **slices = pick_cat_counts_ptr (bar, frq_tab, &n_slices);

  if (n_slices < 1)
    msg (SW, _("Omitting bar chart, which has no values."));
  else
    chart_submit (barchart_create (
                    var, 1,
                    bar->y_scale == FRQ_FREQ ? _("Count") : _("Percent"),
                    bar->y_scale == FRQ_PERCENT,
                    slices, n_slices));
  free (slices);
}

/* Calculates all the pertinent statistics for VF, putting them in array
   D[]. */
static void
calc_stats (const struct frq_proc *frq, const struct var_freqs *vf,
            double d[FRQ_ST_count])
{
  const struct freq_tab *ft = &vf->tab;

  /* Calculate the mode.  If there is more than one mode, we take the
     smallest. */
  int most_often = -1;
  double X_mode = SYSMIS;
  for (const struct freq *f = ft->valid; f < ft->missing; f++)
    if (most_often < f->count)
      {
        most_often = f->count;
        X_mode = f->values[0].f;
      }

  /* Calculate moments. */
  struct moments *m = moments_create (MOMENT_KURTOSIS);
  for (const struct freq *f = ft->valid; f < ft->missing; f++)
    moments_pass_one (m, f->values[0].f, f->count);
  for (const struct freq *f = ft->valid; f < ft->missing; f++)
    moments_pass_two (m, f->values[0].f, f->count);
  moments_calculate (m, NULL, &d[FRQ_ST_MEAN], &d[FRQ_ST_VARIANCE],
                     &d[FRQ_ST_SKEWNESS], &d[FRQ_ST_KURTOSIS]);
  moments_destroy (m);

  /* Formulae below are taken from _SPSS Statistical Algorithms_. */
  double W = ft->valid_cases;
  if (ft->n_valid > 0)
    {
      d[FRQ_ST_MINIMUM] = ft->valid[0].values[0].f;
      d[FRQ_ST_MAXIMUM] = ft->valid[ft->n_valid - 1].values[0].f;
      d[FRQ_ST_RANGE] = d[FRQ_ST_MAXIMUM] - d[FRQ_ST_MINIMUM];
    }
  else
    {
      d[FRQ_ST_MINIMUM] = SYSMIS;
      d[FRQ_ST_MAXIMUM] = SYSMIS;
      d[FRQ_ST_RANGE] = SYSMIS;
    }
  d[FRQ_ST_MODE] = X_mode;
  d[FRQ_ST_SUM] = d[FRQ_ST_MEAN] * W;
  d[FRQ_ST_STDDEV] = sqrt (d[FRQ_ST_VARIANCE]);
  d[FRQ_ST_SEMEAN] = d[FRQ_ST_STDDEV] / sqrt (W);
  d[FRQ_ST_SESKEWNESS] = calc_seskew (W);
  d[FRQ_ST_SEKURTOSIS] = calc_sekurt (W);
  d[FRQ_ST_MEDIAN] = (frq->median_idx != SIZE_MAX
                      ? vf->percentiles[frq->median_idx]
                      : SYSMIS);
}

static bool
all_string_variables (const struct frq_proc *frq)
{
  for (size_t i = 0; i < frq->n_vars; i++)
    if (var_is_numeric (frq->vars[i].var))
      return false;

  return true;
}

struct frq_stats_table
  {
    struct pivot_table *table;
    struct pivot_splits *splits;
  };

/* Displays a table of all the statistics requested. */
static struct frq_stats_table *
frq_stats_table_create (const struct frq_proc *frq,
                        const struct dictionary *dict,
                        const struct variable *wv)
{
  if (all_string_variables (frq))
    return NULL;

  struct pivot_table *table = pivot_table_create (N_("Statistics"));
  pivot_table_set_weight_var (table, wv);

  struct pivot_dimension *variables
    = pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Variables"));
  for (size_t i = 0; i < frq->n_vars; i++)
    if (!var_is_alpha (frq->vars[i].var))
      pivot_category_create_leaf (variables->root,
                                  pivot_value_new_variable (frq->vars[i].var));

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Statistics"));
  struct pivot_category *n = pivot_category_create_group (
    statistics->root, N_("N"));
  pivot_category_create_leaves (n,
                                N_("Valid"), PIVOT_RC_COUNT,
                                N_("Missing"), PIVOT_RC_COUNT);
  for (int i = 0; i < FRQ_ST_count; i++)
    if (frq->stats & BIT_INDEX (i))
      pivot_category_create_leaf (statistics->root,
                                  pivot_value_new_text (st_names[i]));
  struct pivot_category *percentiles = NULL;
  for (size_t i = 0; i < frq->n_percentiles; i++)
    {
      const struct percentile *pc = &frq->percentiles[i];

      if (!pc->show)
        continue;

      if (!percentiles)
        percentiles = pivot_category_create_group (
          statistics->root, N_("Percentiles"));
      pivot_category_create_leaf (percentiles, pivot_value_new_integer (
                                    pc->p * 100.0));
    }

  struct pivot_splits *splits = pivot_splits_create (table, PIVOT_AXIS_COLUMN,
                                                     dict);

  struct frq_stats_table *fst = xmalloc (sizeof *fst);
  *fst = (struct frq_stats_table) { .table = table, .splits = splits };
  return fst;
}

static struct frq_stats_table *
frq_stats_table_submit (struct frq_stats_table *fst,
                        const struct frq_proc *frq,
                        const struct dictionary *dict,
                        const struct variable *wv,
                        const struct ccase *example)
{
  if (!fst)
    {
      fst = frq_stats_table_create (frq, dict, wv);
      if (!fst)
        return NULL;
    }
  pivot_splits_new_split (fst->splits, example);

  int var_idx = 0;
  for (size_t i = 0; i < frq->n_vars; i++)
    {
      struct var_freqs *vf = &frq->vars[i];
      if (var_is_alpha (vf->var))
        continue;

      const struct freq_tab *ft = &vf->tab;

      int row = 0;
      pivot_splits_put2 (fst->splits, fst->table, var_idx, row++,
                        pivot_value_new_number (ft->valid_cases));
      pivot_splits_put2 (fst->splits, fst->table, var_idx, row++,
                        pivot_value_new_number (
                          ft->total_cases - ft->valid_cases));

      double stat_values[FRQ_ST_count];
      calc_stats (frq, vf, stat_values);
      for (int j = 0; j < FRQ_ST_count; j++)
        {
          if (!(frq->stats & BIT_INDEX (j)))
            continue;

          union value v = { .f = vf->tab.n_valid ? stat_values[j] : SYSMIS };
          struct pivot_value *pv
            = (j == FRQ_ST_MODE || j == FRQ_ST_MINIMUM || j == FRQ_ST_MAXIMUM
               ? pivot_value_new_var_value (vf->var, &v)
               : pivot_value_new_number (v.f));
          pivot_splits_put2 (fst->splits, fst->table, var_idx, row++, pv);
        }

      for (size_t j = 0; j < frq->n_percentiles; j++)
        {
          const struct percentile *pc = &frq->percentiles[j];
          if (!pc->show)
            continue;

          union value v = {
            .f = vf->tab.n_valid ? vf->percentiles[j] : SYSMIS
          };
          pivot_splits_put2 (fst->splits, fst->table, var_idx, row++,
                             pivot_value_new_var_value (vf->var, &v));
        }

      var_idx++;
    }

  if (!fst->splits)
    {
      frq_stats_table_destroy (fst);
      return NULL;
    }
  return fst;
}

static void
frq_stats_table_destroy (struct frq_stats_table *fst)
{
  if (!fst)
    return;

  pivot_table_submit (fst->table);
  pivot_splits_destroy (fst->splits);
  free (fst);
}
