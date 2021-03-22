/*
  PSPP - a program for statistical analysis.
  Copyright (C) 2012, 2013, 2016, 2019  Free Software Foundation, Inc.

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

#include <math.h>
#include <gsl/gsl_cdf.h>

#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/pool.h"


#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/caseproto.h"
#include "data/subcase.h"


#include "data/format.h"

#include "math/interaction.h"
#include "math/box-whisker.h"
#include "math/categoricals.h"
#include "math/chart-geometry.h"
#include "math/histogram.h"
#include "math/moments.h"
#include "math/np.h"
#include "math/sort.h"
#include "math/order-stats.h"
#include "math/percentiles.h"
#include "math/shapiro-wilk.h"
#include "math/tukey-hinges.h"
#include "math/trimmed-mean.h"

#include "output/charts/boxplot.h"
#include "output/charts/np-plot.h"
#include "output/charts/spreadlevel-plot.h"
#include "output/charts/plot-hist.h"

#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"

#include "output/pivot-table.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void
append_value_name (const struct variable *var, const union value *val, struct string *str)
{
  var_append_value_name (var, val, str);
  if (var_is_value_missing (var, val, MV_ANY))
    ds_put_cstr (str, _(" (missing)"));
}

enum bp_mode
  {
    BP_GROUPS,
    BP_VARIABLES
  };


/* Indices for the ex_proto member (below) */
enum
  {
    EX_VAL,  /* value */
    EX_ID,   /* identity */
    EX_WT    /* weight */
  };


#define PLOT_HISTOGRAM      0x1
#define PLOT_BOXPLOT        0x2
#define PLOT_NPPLOT         0x4
#define PLOT_SPREADLEVEL    0x8

struct examine
{
  struct pool *pool;

  /* A caseproto used to contain the data subsets under examination,
     see (enum above)   */
  struct caseproto *ex_proto;

  size_t n_dep_vars;
  const struct variable **dep_vars;

  size_t n_iacts;
  struct interaction **iacts;

  enum mv_class dep_excl;
  enum mv_class fctr_excl;

  const struct dictionary *dict;

  struct categoricals *cats;

  /* how many extremities to display */
  int disp_extremes;
  int calc_extremes;
  bool descriptives;

  double conf;

  bool missing_pw;

  /* The case index of the ID value (or -1) if not applicable */
  size_t id_idx;
  int id_width;

  enum pc_alg pc_alg;
  double *ptiles;
  size_t n_percentiles;

  unsigned int plot;
  float sl_power;

  enum bp_mode boxplot_mode;

  const struct variable *id_var;

  const struct variable *wv;
};

struct extremity
{
  /* The value of this extremity */
  double val;

  /* Either the casenumber or the value of the variable specified
     by the /ID subcommand which corresponds to this extremity */
  union value identity;
};

struct exploratory_stats
{
  double missing;
  double non_missing;

  struct moments *mom;

  /* Most operations need a sorted reader/writer */
  struct casewriter *sorted_writer;
  struct casereader *sorted_reader;

  struct extremity *minima;
  struct extremity *maxima;

  /*
     Minimum should alway equal mimima[0].val.
     Likewise, maximum should alway equal maxima[0].val.
     This redundancy exists as an optimisation effort.
     Some statistics (eg histogram) require early calculation
     of the min and max
  */
  double minimum;
  double maximum;

  struct trimmed_mean *trimmed_mean;
  struct percentile *quartiles[3];
  struct percentile **percentiles;
  struct shapiro_wilk *shapiro_wilk;

  struct tukey_hinges *hinges;

  /* The data for the NP Plots */
  struct np *np;

  struct histogram *histogram;

  /* The data for the box plots */
  struct box_whisker *box_whisker;

  /* Total weight */
  double cc;

  /* The minimum weight */
  double cmin;
};

static void
show_boxplot_grouped (const struct examine *cmd, int iact_idx)
{
  int v;

  const struct interaction *iact = cmd->iacts[iact_idx];
  const size_t n_cats =  categoricals_n_count (cmd->cats, iact_idx);

  for (v = 0; v < cmd->n_dep_vars; ++v)
    {
      double y_min = DBL_MAX;
      double y_max = -DBL_MAX;
      int grp;
      struct boxplot *boxplot;
      struct string title;
      ds_init_empty (&title);

      if (iact->n_vars > 0)
        {
          struct string istr;
          ds_init_empty (&istr);
          interaction_to_string (iact, &istr);
          ds_put_format (&title, _("Boxplot of %s vs. %s"),
                         var_to_string (cmd->dep_vars[v]),
                         ds_cstr (&istr));
          ds_destroy (&istr);
        }
      else
        ds_put_format (&title, _("Boxplot of %s"), var_to_string (cmd->dep_vars[v]));

      for (grp = 0; grp < n_cats; ++grp)
        {
          const struct exploratory_stats *es =
            categoricals_get_user_data_by_category_real (cmd->cats, iact_idx, grp);

          if (y_min > es[v].minimum)
            y_min = es[v].minimum;

          if (y_max < es[v].maximum)
            y_max = es[v].maximum;
        }

      boxplot = boxplot_create (y_min, y_max, ds_cstr (&title));

      ds_destroy (&title);

      for (grp = 0; grp < n_cats; ++grp)
        {
          int ivar_idx;
          struct string label;

          const struct ccase *c =
            categoricals_get_case_by_category_real (cmd->cats,  iact_idx, grp);

          struct exploratory_stats *es =
            categoricals_get_user_data_by_category_real (cmd->cats, iact_idx, grp);

          ds_init_empty (&label);
          for (ivar_idx = 0; ivar_idx < iact->n_vars; ++ivar_idx)
            {
              struct string l;
              const struct variable *ivar = iact->vars[ivar_idx];
              const union value *val = case_data (c, ivar);
              ds_init_empty (&l);

              append_value_name (ivar, val, &l);
              ds_ltrim (&l, ss_cstr (" "));

              ds_put_substring (&label, l.ss);
              if (ivar_idx < iact->n_vars - 1)
                ds_put_cstr (&label, "; ");

              ds_destroy (&l);
            }

          boxplot_add_box (boxplot, es[v].box_whisker, ds_cstr (&label));
          es[v].box_whisker = NULL;

          ds_destroy (&label);
        }

      boxplot_submit (boxplot);
    }
}

static void
show_boxplot_variabled (const struct examine *cmd, int iact_idx)
{
  int grp;
  const struct interaction *iact = cmd->iacts[iact_idx];
  const size_t n_cats =  categoricals_n_count (cmd->cats, iact_idx);

  for (grp = 0; grp < n_cats; ++grp)
    {
      struct boxplot *boxplot;
      int v;
      double y_min = DBL_MAX;
      double y_max = -DBL_MAX;

      const struct ccase *c =
	categoricals_get_case_by_category_real (cmd->cats,  iact_idx, grp);

      struct string title;
      ds_init_empty (&title);

      for (v = 0; v < cmd->n_dep_vars; ++v)
        {
          const struct exploratory_stats *es =
            categoricals_get_user_data_by_category_real (cmd->cats, iact_idx, grp);

          if (y_min > es[v].minimum)
            y_min = es[v].minimum;

          if (y_max < es[v].maximum)
            y_max = es[v].maximum;
        }

      if (iact->n_vars == 0)
        ds_put_format (&title, _("Boxplot"));
      else
        {
          int ivar_idx;
          struct string label;
          ds_init_empty (&label);
          for (ivar_idx = 0; ivar_idx < iact->n_vars; ++ivar_idx)
            {
              const struct variable *ivar = iact->vars[ivar_idx];
              const union value *val = case_data (c, ivar);

              ds_put_cstr (&label, var_to_string (ivar));
              ds_put_cstr (&label, " = ");
              append_value_name (ivar, val, &label);
              ds_put_cstr (&label, "; ");
            }

          ds_put_format (&title, _("Boxplot of %s"),
                         ds_cstr (&label));

          ds_destroy (&label);
        }

      boxplot = boxplot_create (y_min, y_max, ds_cstr (&title));

      ds_destroy (&title);

      for (v = 0; v < cmd->n_dep_vars; ++v)
        {
          struct exploratory_stats *es =
            categoricals_get_user_data_by_category_real (cmd->cats, iact_idx, grp);

          boxplot_add_box (boxplot, es[v].box_whisker,
                           var_to_string (cmd->dep_vars[v]));
          es[v].box_whisker = NULL;
        }

      boxplot_submit (boxplot);
    }
}


static void
show_npplot (const struct examine *cmd, int iact_idx)
{
  const struct interaction *iact = cmd->iacts[iact_idx];
  const size_t n_cats =  categoricals_n_count (cmd->cats, iact_idx);

  int v;

  for (v = 0; v < cmd->n_dep_vars; ++v)
    {
      int grp;
      for (grp = 0; grp < n_cats; ++grp)
        {
          struct chart *npp, *dnpp;
          struct casereader *reader;
          struct np *np;

          int ivar_idx;
          const struct ccase *c =
            categoricals_get_case_by_category_real (cmd->cats,
                                                    iact_idx, grp);

          const struct exploratory_stats *es =
            categoricals_get_user_data_by_category_real (cmd->cats, iact_idx, grp);

          struct string label;
          ds_init_cstr (&label,
                        var_to_string (cmd->dep_vars[v]));

          if (iact->n_vars > 0)
            {
              ds_put_cstr (&label, " (");
              for (ivar_idx = 0; ivar_idx < iact->n_vars; ++ivar_idx)
                {
                  const struct variable *ivar = iact->vars[ivar_idx];
                  const union value *val = case_data (c, ivar);

                  ds_put_cstr (&label, var_to_string (ivar));
                  ds_put_cstr (&label, " = ");
                  append_value_name (ivar, val, &label);
                  ds_put_cstr (&label, "; ");

                }
              ds_put_cstr (&label, ")");
            }

          np = es[v].np;
          reader = casewriter_make_reader (np->writer);
          np->writer = NULL;

          npp = np_plot_create (np, reader, ds_cstr (&label));
          dnpp = dnp_plot_create (np, reader, ds_cstr (&label));

          if (npp == NULL || dnpp == NULL)
            {
              msg (MW, _("Not creating NP plot because data set is empty."));
              chart_unref (npp);
              chart_unref (dnpp);
            }
          else
            {
              chart_submit (npp);
              chart_submit (dnpp);
            }
	  casereader_destroy (reader);

          ds_destroy (&label);
        }
    }
}

static void
show_spreadlevel (const struct examine *cmd, int iact_idx)
{
  const struct interaction *iact = cmd->iacts[iact_idx];
  const size_t n_cats =  categoricals_n_count (cmd->cats, iact_idx);

  int v;

  /* Spreadlevel when there are no levels is not useful */
  if (iact->n_vars == 0)
    return;

  for (v = 0; v < cmd->n_dep_vars; ++v)
    {
      int grp;
      struct chart *sl;

      struct string label;
      ds_init_cstr (&label,
		    var_to_string (cmd->dep_vars[v]));

      if (iact->n_vars > 0)
	{
	  ds_put_cstr (&label, " (");
	  interaction_to_string (iact, &label);
	  ds_put_cstr (&label, ")");
	}

      sl = spreadlevel_plot_create (ds_cstr (&label), cmd->sl_power);

      for (grp = 0; grp < n_cats; ++grp)
        {
          const struct exploratory_stats *es =
            categoricals_get_user_data_by_category_real (cmd->cats, iact_idx, grp);

	  double median = percentile_calculate (es[v].quartiles[1], cmd->pc_alg);

	  double iqr = percentile_calculate (es[v].quartiles[2], cmd->pc_alg) -
	    percentile_calculate (es[v].quartiles[0], cmd->pc_alg);

	  spreadlevel_plot_add (sl, iqr, median);
	}

      if (sl == NULL)
	msg (MW, _("Not creating spreadlevel chart for %s"), ds_cstr (&label));
      else
	chart_submit (sl);

      ds_destroy (&label);
    }
}


static void
show_histogram (const struct examine *cmd, int iact_idx)
{
  const struct interaction *iact = cmd->iacts[iact_idx];
  const size_t n_cats =  categoricals_n_count (cmd->cats, iact_idx);

  int v;

  for (v = 0; v < cmd->n_dep_vars; ++v)
    {
      int grp;
      for (grp = 0; grp < n_cats; ++grp)
        {
          double n, mean, var;
          int ivar_idx;
          const struct ccase *c =
            categoricals_get_case_by_category_real (cmd->cats,
                                                    iact_idx, grp);

          const struct exploratory_stats *es =
            categoricals_get_user_data_by_category_real (cmd->cats, iact_idx, grp);

          struct string label;

	  if (es[v].histogram == NULL)
	    continue;

          ds_init_cstr (&label,
                        var_to_string (cmd->dep_vars[v]));

          if (iact->n_vars > 0)
            {
              ds_put_cstr (&label, " (");
              for (ivar_idx = 0; ivar_idx < iact->n_vars; ++ivar_idx)
                {
                  const struct variable *ivar = iact->vars[ivar_idx];
                  const union value *val = case_data (c, ivar);

                  ds_put_cstr (&label, var_to_string (ivar));
                  ds_put_cstr (&label, " = ");
                  append_value_name (ivar, val, &label);
                  ds_put_cstr (&label, "; ");

                }
              ds_put_cstr (&label, ")");
            }


          moments_calculate (es[v].mom, &n, &mean, &var, NULL, NULL);

          chart_submit
            (histogram_chart_create (es[v].histogram->gsl_hist,
                                      ds_cstr (&label), n, mean,
                                      sqrt (var), false));


          ds_destroy (&label);
        }
    }
}

static struct pivot_value *
new_value_with_missing_footnote (const struct variable *var,
                                 const union value *value,
                                 struct pivot_footnote *missing_footnote)
{
  struct pivot_value *pv = pivot_value_new_var_value (var, value);
  if (var_is_value_missing (var, value, MV_USER))
    pivot_value_add_footnote (pv, missing_footnote);
  return pv;
}

static void
create_interaction_dimensions (struct pivot_table *table,
                               const struct categoricals *cats,
                               const struct interaction *iact,
                               struct pivot_footnote *missing_footnote)
{
  for (size_t i = iact->n_vars; i-- > 0;)
    {
      const struct variable *var = iact->vars[i];
      struct pivot_dimension *d = pivot_dimension_create__ (
        table, PIVOT_AXIS_ROW, pivot_value_new_variable (var));
      d->root->show_label = true;

      size_t n;
      union value *values = categoricals_get_var_values (cats, var, &n);
      for (size_t j = 0; j < n; j++)
        pivot_category_create_leaf (
          d->root, new_value_with_missing_footnote (var, &values[j],
                                                    missing_footnote));
    }
}

static struct pivot_footnote *
create_missing_footnote (struct pivot_table *table)
{
  return pivot_table_create_footnote (
    table, pivot_value_new_text (N_("User-missing value.")));
}

static void
percentiles_report (const struct examine *cmd, int iact_idx)
{
  struct pivot_table *table = pivot_table_create (N_("Percentiles"));

  struct pivot_dimension *percentiles = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Percentiles"));
  percentiles->root->show_label = true;
  for (int i = 0; i < cmd->n_percentiles; ++i)
    pivot_category_create_leaf (
      percentiles->root,
      pivot_value_new_user_text_nocopy (xasprintf ("%g", cmd->ptiles[i])));

  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Statistics"),
                          N_("Weighted Average"), N_("Tukey's Hinges"));

  const struct interaction *iact = cmd->iacts[iact_idx];
  struct pivot_footnote *missing_footnote = create_missing_footnote (table);
  create_interaction_dimensions (table, cmd->cats, iact, missing_footnote);

  struct pivot_dimension *dep_dim = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Dependent Variables"));

  size_t *indexes = xnmalloc (table->n_dimensions, sizeof *indexes);

  size_t n_cats = categoricals_n_count (cmd->cats, iact_idx);
  for (size_t v = 0; v < cmd->n_dep_vars; ++v)
    {
      indexes[table->n_dimensions - 1] = pivot_category_create_leaf (
        dep_dim->root, pivot_value_new_variable (cmd->dep_vars[v]));

      for (size_t i = 0; i < n_cats; ++i)
        {
          for (size_t j = 0; j < iact->n_vars; j++)
            {
              int idx = categoricals_get_value_index_by_category_real (
                cmd->cats, iact_idx, i, j);
              indexes[table->n_dimensions - 2 - j] = idx;
            }

          const struct exploratory_stats *ess
            = categoricals_get_user_data_by_category_real (
              cmd->cats, iact_idx, i);
          const struct exploratory_stats *es = ess + v;

          double hinges[3];
          tukey_hinges_calculate (es->hinges, hinges);

          for (size_t pc_idx = 0; pc_idx < cmd->n_percentiles; ++pc_idx)
            {
              indexes[0] = pc_idx;

              indexes[1] = 0;
              double value = percentile_calculate (es->percentiles[pc_idx],
                                                   cmd->pc_alg);
              pivot_table_put (table, indexes, table->n_dimensions,
                               pivot_value_new_number (value));

              double hinge = (cmd->ptiles[pc_idx] == 25.0 ? hinges[0]
                              : cmd->ptiles[pc_idx] == 50.0 ? hinges[1]
                              : cmd->ptiles[pc_idx] == 75.0 ? hinges[2]
                              : SYSMIS);
              if (hinge != SYSMIS)
                {
                  indexes[1] = 1;
                  pivot_table_put (table, indexes, table->n_dimensions,
                                   pivot_value_new_number (hinge));
                }
            }
        }

    }
  free (indexes);

  pivot_table_submit (table);
}

static void
normality_report (const struct examine *cmd, int iact_idx)
{
  struct pivot_table *table = pivot_table_create (N_("Tests of Normality"));

  struct pivot_dimension *test =
    pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Shapiro-Wilk"),
			    N_("Statistic"),
			    N_("df"), PIVOT_RC_COUNT,
			    N_("Sig."));

  test->root->show_label = true;

  const struct interaction *iact = cmd->iacts[iact_idx];
  struct pivot_footnote *missing_footnote = create_missing_footnote (table);
  create_interaction_dimensions (table, cmd->cats, iact, missing_footnote);

  struct pivot_dimension *dep_dim = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Dependent Variables"));

  size_t *indexes = xnmalloc (table->n_dimensions, sizeof *indexes);

  size_t n_cats = categoricals_n_count (cmd->cats, iact_idx);
  for (size_t v = 0; v < cmd->n_dep_vars; ++v)
    {
      indexes[table->n_dimensions - 1] =
	pivot_category_create_leaf (dep_dim->root, pivot_value_new_variable (cmd->dep_vars[v]));

      for (size_t i = 0; i < n_cats; ++i)
        {
	  indexes[1] = i;

          const struct exploratory_stats *es
            = categoricals_get_user_data_by_category_real (
              cmd->cats, iact_idx, i);

	  struct shapiro_wilk *sw =  es[v].shapiro_wilk;

	  if (sw == NULL)
	    continue;

	  double w = shapiro_wilk_calculate (sw);

	  int j = 0;
	  indexes[0] = j;

	  pivot_table_put (table, indexes, table->n_dimensions,
			   pivot_value_new_number (w));

	  indexes[0] = ++j;
	  pivot_table_put (table, indexes, table->n_dimensions,
			   pivot_value_new_number (sw->n));

	  indexes[0] = ++j;
	  pivot_table_put (table, indexes, table->n_dimensions,
			   pivot_value_new_number (shapiro_wilk_significance (sw->n, w)));
	}
    }

  free (indexes);

  pivot_table_submit (table);
}


static void
descriptives_report (const struct examine *cmd, int iact_idx)
{
  struct pivot_table *table = pivot_table_create (N_("Descriptives"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Aspect"),
                          N_("Statistic"), N_("Std. Error"));

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Statistics"), N_("Mean"));
  struct pivot_category *interval = pivot_category_create_group__ (
    statistics->root,
    pivot_value_new_text_format (N_("%g%% Confidence Interval for Mean"),
                                 cmd->conf * 100.0));
  pivot_category_create_leaves (interval, N_("Lower Bound"),
                                N_("Upper Bound"));
  pivot_category_create_leaves (
    statistics->root, N_("5% Trimmed Mean"), N_("Median"), N_("Variance"),
    N_("Std. Deviation"), N_("Minimum"), N_("Maximum"), N_("Range"),
    N_("Interquartile Range"), N_("Skewness"), N_("Kurtosis"));

  const struct interaction *iact = cmd->iacts[iact_idx];
  struct pivot_footnote *missing_footnote = create_missing_footnote (table);
  create_interaction_dimensions (table, cmd->cats, iact, missing_footnote);

  struct pivot_dimension *dep_dim = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Dependent Variables"));

  size_t *indexes = xnmalloc (table->n_dimensions, sizeof *indexes);

  size_t n_cats = categoricals_n_count (cmd->cats, iact_idx);
  for (size_t v = 0; v < cmd->n_dep_vars; ++v)
    {
      indexes[table->n_dimensions - 1] = pivot_category_create_leaf (
        dep_dim->root, pivot_value_new_variable (cmd->dep_vars[v]));

      for (size_t i = 0; i < n_cats; ++i)
        {
          for (size_t j = 0; j < iact->n_vars; j++)
            {
              int idx = categoricals_get_value_index_by_category_real (
                cmd->cats, iact_idx, i, j);
              indexes[table->n_dimensions - 2 - j] = idx;
            }

          const struct exploratory_stats *ess
            = categoricals_get_user_data_by_category_real (cmd->cats,
                                                           iact_idx, i);
          const struct exploratory_stats *es = ess + v;

          double m0, m1, m2, m3, m4;
          moments_calculate (es->mom, &m0, &m1, &m2, &m3, &m4);
          double tval = gsl_cdf_tdist_Qinv ((1.0 - cmd->conf) / 2.0, m0 - 1.0);

          struct entry
            {
              int stat_idx;
              int aspect_idx;
              double x;
            }
          entries[] = {
            { 0, 0, m1 },
            { 0, 1, calc_semean (m2, m0) },
            { 1, 0, m1 - tval * calc_semean (m2, m0) },
            { 2, 0, m1 + tval * calc_semean (m2, m0) },
            { 3, 0, trimmed_mean_calculate (es->trimmed_mean) },
            { 4, 0, percentile_calculate (es->quartiles[1], cmd->pc_alg) },
            { 5, 0, m2 },
            { 6, 0, sqrt (m2) },
            { 7, 0, es->minima[0].val },
            { 8, 0, es->maxima[0].val },
            { 9, 0, es->maxima[0].val - es->minima[0].val },
            { 10, 0, (percentile_calculate (es->quartiles[2], cmd->pc_alg) -
                      percentile_calculate (es->quartiles[0], cmd->pc_alg)) },
            { 11, 0, m3 },
            { 11, 1, calc_seskew (m0) },
            { 12, 0, m4 },
            { 12, 1, calc_sekurt (m0) },
          };
          for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
            {
              const struct entry *e = &entries[j];
              indexes[0] = e->aspect_idx;
              indexes[1] = e->stat_idx;
              pivot_table_put (table, indexes, table->n_dimensions,
                               pivot_value_new_number (e->x));
            }
        }
    }

  free (indexes);

  pivot_table_submit (table);
}


static void
extremes_report (const struct examine *cmd, int iact_idx)
{
  struct pivot_table *table = pivot_table_create (N_("Extreme Values"));

  struct pivot_dimension *statistics = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Statistics"));
  pivot_category_create_leaf (statistics->root,
                              (cmd->id_var
                               ? pivot_value_new_variable (cmd->id_var)
                               : pivot_value_new_text (N_("Case Number"))));
  pivot_category_create_leaves (statistics->root, N_("Value"));

  struct pivot_dimension *order = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Order"));
  for (size_t i = 0; i < cmd->disp_extremes; i++)
    pivot_category_create_leaf (order->root, pivot_value_new_integer (i + 1));

  pivot_dimension_create (table, PIVOT_AXIS_ROW,
			  /* TRANSLATORS: This is a noun, not an adjective.  */
			  N_("Extreme"),
                          N_("Highest"), N_("Lowest"));

  const struct interaction *iact = cmd->iacts[iact_idx];
  struct pivot_footnote *missing_footnote = create_missing_footnote (table);
  create_interaction_dimensions (table, cmd->cats, iact, missing_footnote);

  struct pivot_dimension *dep_dim = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Dependent Variables"));

  size_t *indexes = xnmalloc (table->n_dimensions, sizeof *indexes);

  size_t n_cats = categoricals_n_count (cmd->cats, iact_idx);
  for (size_t v = 0; v < cmd->n_dep_vars; ++v)
    {
      indexes[table->n_dimensions - 1] = pivot_category_create_leaf (
        dep_dim->root, pivot_value_new_variable (cmd->dep_vars[v]));

      for (size_t i = 0; i < n_cats; ++i)
        {
          for (size_t j = 0; j < iact->n_vars; j++)
            {
              int idx = categoricals_get_value_index_by_category_real (
                cmd->cats, iact_idx, i, j);
              indexes[table->n_dimensions - 2 - j] = idx;
            }

          const struct exploratory_stats *ess
            = categoricals_get_user_data_by_category_real (cmd->cats,
                                                           iact_idx, i);
          const struct exploratory_stats *es = ess + v;

          for (int e = 0 ; e < cmd->disp_extremes; ++e)
            {
              indexes[1] = e;

              for (size_t j = 0; j < 2; j++)
                {
                  const struct extremity *extremity
                    = j ? &es->minima[e] : &es->maxima[e];
                  indexes[2] = j;

                  indexes[0] = 0;
                  pivot_table_put (
                    table, indexes, table->n_dimensions,
                    (cmd->id_var
                     ? new_value_with_missing_footnote (cmd->id_var,
                                                        &extremity->identity,
                                                        missing_footnote)
                     : pivot_value_new_integer (extremity->identity.f)));

                  indexes[0] = 1;
                  union value val = { .f = extremity->val };
                  pivot_table_put (
                    table, indexes, table->n_dimensions,
                    new_value_with_missing_footnote (cmd->dep_vars[v], &val,
                                                     missing_footnote));
                }
            }
        }
    }
  free (indexes);

  pivot_table_submit (table);
}


static void
summary_report (const struct examine *cmd, int iact_idx)
{
  struct pivot_table *table = pivot_table_create (
    N_("Case Processing Summary"));
  pivot_table_set_weight_var (table, dict_get_weight (cmd->dict));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("N"), PIVOT_RC_COUNT,
                          N_("Percent"), PIVOT_RC_PERCENT);
  struct pivot_dimension *cases = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Cases"), N_("Valid"), N_("Missing"),
    N_("Total"));
  cases->root->show_label = true;

  const struct interaction *iact = cmd->iacts[iact_idx];
  struct pivot_footnote *missing_footnote = create_missing_footnote (table);
  create_interaction_dimensions (table, cmd->cats, iact, missing_footnote);

  struct pivot_dimension *dep_dim = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Dependent Variables"));

  size_t *indexes = xnmalloc (table->n_dimensions, sizeof *indexes);

  size_t n_cats = categoricals_n_count (cmd->cats, iact_idx);
  for (size_t v = 0; v < cmd->n_dep_vars; ++v)
    {
      indexes[table->n_dimensions - 1] = pivot_category_create_leaf (
        dep_dim->root, pivot_value_new_variable (cmd->dep_vars[v]));

      for (size_t i = 0; i < n_cats; ++i)
        {
          for (size_t j = 0; j < iact->n_vars; j++)
            {
              int idx = categoricals_get_value_index_by_category_real (
                cmd->cats, iact_idx, i, j);
              indexes[table->n_dimensions - 2 - j] = idx;
            }

          const struct exploratory_stats *es
            = categoricals_get_user_data_by_category_real (
              cmd->cats, iact_idx, i);

          double total = es[v].missing + es[v].non_missing;
          struct entry
            {
              int stat_idx;
              int case_idx;
              double x;
            }
          entries[] = {
            { 0, 0, es[v].non_missing },
            { 1, 0, 100.0 * es[v].non_missing / total },
            { 0, 1, es[v].missing },
            { 1, 1, 100.0 * es[v].missing / total },
            { 0, 2, total },
            { 1, 2, 100.0 },
          };
          for (size_t j = 0; j < sizeof entries / sizeof *entries; j++)
            {
              const struct entry *e = &entries[j];
              indexes[0] = e->stat_idx;
              indexes[1] = e->case_idx;
              pivot_table_put (table, indexes, table->n_dimensions,
                               pivot_value_new_number (e->x));
            }
        }
    }

  free (indexes);

  pivot_table_submit (table);
}

/* Attempt to parse an interaction from LEXER */
static struct interaction *
parse_interaction (struct lexer *lexer, struct examine *ex)
{
  const struct variable *v = NULL;
  struct interaction *iact = NULL;

  if (lex_match_variable (lexer, ex->dict, &v))
    {
      iact = interaction_create (v);

      while (lex_match (lexer, T_BY))
        {
          if (!lex_match_variable (lexer, ex->dict, &v))
            {
              interaction_destroy (iact);
              return NULL;
            }
          interaction_add_variable (iact, v);
        }
      lex_match (lexer, T_COMMA);
    }

  return iact;
}


static void *
create_n (const void *aux1, void *aux2 UNUSED)
{
  int v;

  const struct examine *examine = aux1;
  struct exploratory_stats *es = pool_calloc (examine->pool, examine->n_dep_vars, sizeof (*es));
  struct subcase ordering;
  subcase_init (&ordering, 0, 0, SC_ASCEND);

  for (v = 0; v < examine->n_dep_vars; v++)
    {
      es[v].sorted_writer = sort_create_writer (&ordering, examine->ex_proto);
      es[v].sorted_reader = NULL;

      es[v].mom = moments_create (MOMENT_KURTOSIS);
      es[v].cmin = DBL_MAX;

      es[v].maximum = -DBL_MAX;
      es[v].minimum =  DBL_MAX;
    }

  subcase_destroy (&ordering);
  return es;
}

static void
update_n (const void *aux1, void *aux2 UNUSED, void *user_data,
          const struct ccase *c, double weight)
{
  int v;
  const struct examine *examine = aux1;
  struct exploratory_stats *es = user_data;

  bool this_case_is_missing = false;
  /* LISTWISE missing must be dealt with here */
  if (!examine->missing_pw)
    {
      for (v = 0; v < examine->n_dep_vars; v++)
	{
	  const struct variable *var = examine->dep_vars[v];

	  if (var_is_value_missing (var, case_data (c, var), examine->dep_excl))
	    {
	      es[v].missing += weight;
	      this_case_is_missing = true;
	    }
	}
    }

  if (this_case_is_missing)
    return;

  for (v = 0; v < examine->n_dep_vars; v++)
    {
      struct ccase *outcase ;
      const struct variable *var = examine->dep_vars[v];
      const double x = case_data (c, var)->f;

      if (var_is_value_missing (var, case_data (c, var), examine->dep_excl))
        {
          es[v].missing += weight;
          continue;
        }

      outcase = case_create (examine->ex_proto);

      if (x > es[v].maximum)
        es[v].maximum = x;

      if (x < es[v].minimum)
        es[v].minimum =  x;

      es[v].non_missing += weight;

      moments_pass_one (es[v].mom, x, weight);

      /* Save the value and the ID to the writer */
      assert (examine->id_idx != -1);
      case_data_rw_idx (outcase, EX_VAL)->f = x;
      value_copy (case_data_rw_idx (outcase, EX_ID),
                  case_data_idx (c, examine->id_idx), examine->id_width);

      case_data_rw_idx (outcase, EX_WT)->f = weight;

      es[v].cc += weight;

      if (es[v].cmin > weight)
        es[v].cmin = weight;

      casewriter_write (es[v].sorted_writer, outcase);
    }
}

static void
calculate_n (const void *aux1, void *aux2 UNUSED, void *user_data)
{
  int v;
  const struct examine *examine = aux1;
  struct exploratory_stats *es = user_data;

  for (v = 0; v < examine->n_dep_vars; v++)
    {
      int i;
      casenumber imin = 0;
      casenumber imax;
      struct casereader *reader;
      struct ccase *c;

      if (examine->plot & PLOT_HISTOGRAM && es[v].non_missing > 0)
        {
          /* Sturges Rule */
          double bin_width = fabs (es[v].minimum - es[v].maximum)
            / (1 + log2 (es[v].cc))
            ;

          es[v].histogram =
            histogram_create (bin_width, es[v].minimum, es[v].maximum);
        }

      es[v].sorted_reader = casewriter_make_reader (es[v].sorted_writer);
      es[v].sorted_writer = NULL;

      imax = casereader_get_case_cnt (es[v].sorted_reader);

      es[v].maxima = pool_calloc (examine->pool, examine->calc_extremes, sizeof (*es[v].maxima));
      es[v].minima = pool_calloc (examine->pool, examine->calc_extremes, sizeof (*es[v].minima));
      for (i = 0; i < examine->calc_extremes; ++i)
        {
          value_init_pool (examine->pool, &es[v].maxima[i].identity, examine->id_width) ;
          value_init_pool (examine->pool, &es[v].minima[i].identity, examine->id_width) ;
        }

      bool warn = true;
      for (reader = casereader_clone (es[v].sorted_reader);
           (c = casereader_read (reader)) != NULL; case_unref (c))
        {
          const double val = case_data_idx (c, EX_VAL)->f;
          double wt = case_data_idx (c, EX_WT)->f;
	  wt = var_force_valid_weight (examine->wv, wt, &warn);

          moments_pass_two (es[v].mom, val, wt);

          if (es[v].histogram)
            histogram_add (es[v].histogram, val, wt);

          if (imin < examine->calc_extremes)
            {
              int x;
              for (x = imin; x < examine->calc_extremes; ++x)
                {
                  struct extremity *min = &es[v].minima[x];
                  min->val = val;
                  value_copy (&min->identity, case_data_idx (c, EX_ID), examine->id_width);
                }
              imin ++;
            }

          imax --;
          if (imax < examine->calc_extremes)
            {
              int x;

              for (x = imax; x < imax + 1; ++x)
                {
                  struct extremity *max;

                  if (x >= examine->calc_extremes)
                    break;

                  max = &es[v].maxima[x];
                  max->val = val;
                  value_copy (&max->identity, case_data_idx (c, EX_ID), examine->id_width);
                }
            }
        }
      casereader_destroy (reader);

      if (examine->calc_extremes > 0 && es[v].non_missing > 0)
        {
          assert (es[v].minima[0].val == es[v].minimum);
	  assert (es[v].maxima[0].val == es[v].maximum);
        }

      {
	const int n_os = 5 + examine->n_percentiles;
	struct order_stats **os ;
	es[v].percentiles = pool_calloc (examine->pool, examine->n_percentiles, sizeof (*es[v].percentiles));

	es[v].trimmed_mean = trimmed_mean_create (es[v].cc, 0.05);
	es[v].shapiro_wilk = NULL;

	os = xcalloc (n_os, sizeof *os);
	os[0] = &es[v].trimmed_mean->parent;

	es[v].quartiles[0] = percentile_create (0.25, es[v].cc);
	es[v].quartiles[1] = percentile_create (0.5,  es[v].cc);
	es[v].quartiles[2] = percentile_create (0.75, es[v].cc);

	os[1] = &es[v].quartiles[0]->parent;
	os[2] = &es[v].quartiles[1]->parent;
	os[3] = &es[v].quartiles[2]->parent;

	es[v].hinges = tukey_hinges_create (es[v].cc, es[v].cmin);
	os[4] = &es[v].hinges->parent;

	for (i = 0; i < examine->n_percentiles; ++i)
	  {
	    es[v].percentiles[i] = percentile_create (examine->ptiles[i] / 100.00, es[v].cc);
	    os[5 + i] = &es[v].percentiles[i]->parent;
	  }

	order_stats_accumulate_idx (os, n_os,
				    casereader_clone (es[v].sorted_reader),
				    EX_WT, EX_VAL);

	free (os);
      }

      if (examine->plot & PLOT_BOXPLOT)
        {
          struct order_stats *os;

          es[v].box_whisker = box_whisker_create (es[v].hinges,
                                                  EX_ID, examine->id_var);

          os = &es[v].box_whisker->parent;
	  order_stats_accumulate_idx (&os, 1,
				      casereader_clone (es[v].sorted_reader),
				      EX_WT, EX_VAL);
        }

      if (examine->plot)
        {
	  double mean;

	  moments_calculate (es[v].mom, NULL, &mean, NULL, NULL, NULL);

          es[v].shapiro_wilk = shapiro_wilk_create (es[v].non_missing, mean);

	  if (es[v].shapiro_wilk)
	    {
	      struct order_stats *os = &es[v].shapiro_wilk->parent;
	      order_stats_accumulate_idx (&os, 1,
					  casereader_clone (es[v].sorted_reader),
					  EX_WT, EX_VAL);
	    }
        }

      if (examine->plot & PLOT_NPPLOT)
        {
          double n, mean, var;
          struct order_stats *os;

          moments_calculate (es[v].mom, &n, &mean, &var, NULL, NULL);

          es[v].np = np_create (n, mean, var);

          os = &es[v].np->parent;

          order_stats_accumulate_idx (&os, 1,
				      casereader_clone (es[v].sorted_reader),
				      EX_WT, EX_VAL);
        }

    }
}

static void
cleanup_exploratory_stats (struct examine *cmd)
{
  int i;
  for (i = 0; i < cmd->n_iacts; ++i)
    {
      int v;
      const size_t n_cats =  categoricals_n_count (cmd->cats, i);

      for (v = 0; v < cmd->n_dep_vars; ++v)
	{
	  int grp;
	  for (grp = 0; grp < n_cats; ++grp)
	    {
	      int q;
	      const struct exploratory_stats *es =
		categoricals_get_user_data_by_category_real (cmd->cats, i, grp);

	      struct order_stats *os = &es[v].hinges->parent;
	      struct statistic  *stat = &os->parent;
	      stat->destroy (stat);

	      for (q = 0; q < 3 ; q++)
		{
		  os = &es[v].quartiles[q]->parent;
		  stat = &os->parent;
		  stat->destroy (stat);
		}

	      for (q = 0; q < cmd->n_percentiles ; q++)
		{
		  os = &es[v].percentiles[q]->parent;
		  stat = &os->parent;
		  stat->destroy (stat);
		}

              if (es[v].shapiro_wilk)
                {
                  stat = &es[v].shapiro_wilk->parent.parent;
                  stat->destroy (stat);
                }

	      os = &es[v].trimmed_mean->parent;
	      stat = &os->parent;
	      stat->destroy (stat);

	      os = &es[v].np->parent;
	      if (os)
		{
		  stat = &os->parent;
		  stat->destroy (stat);
		}

	      statistic_destroy (&es[v].histogram->parent);
	      moments_destroy (es[v].mom);

              if (es[v].box_whisker)
                {
                  stat = &es[v].box_whisker->parent.parent;
                  stat->destroy (stat);
                }

	      casereader_destroy (es[v].sorted_reader);
	    }
	}
    }
}


static void
run_examine (struct examine *cmd, struct casereader *input)
{
  int i;
  struct ccase *c;
  struct casereader *reader;

  struct payload payload;
  payload.create = create_n;
  payload.update = update_n;
  payload.calculate = calculate_n;
  payload.destroy = NULL;

  cmd->wv = dict_get_weight (cmd->dict);

  cmd->cats
    = categoricals_create (cmd->iacts, cmd->n_iacts, cmd->wv, cmd->fctr_excl);

  categoricals_set_payload (cmd->cats, &payload, cmd, NULL);

  if (cmd->id_var == NULL)
    {
      struct ccase *c = casereader_peek (input,  0);

      cmd->id_idx = case_get_value_cnt (c);
      input = casereader_create_arithmetic_sequence (input, 1.0, 1.0);

      case_unref (c);
    }

  for (reader = input;
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      categoricals_update (cmd->cats, c);
    }
  casereader_destroy (reader);
  categoricals_done (cmd->cats);

  for (i = 0; i < cmd->n_iacts; ++i)
    {
      summary_report (cmd, i);

      const size_t n_cats =  categoricals_n_count (cmd->cats, i);
      if (n_cats == 0)
	continue;

      if (cmd->disp_extremes > 0)
        extremes_report (cmd, i);

      if (cmd->n_percentiles > 0)
        percentiles_report (cmd, i);

      if (cmd->plot & PLOT_BOXPLOT)
        {
          switch (cmd->boxplot_mode)
            {
            case BP_GROUPS:
              show_boxplot_grouped (cmd, i);
              break;
            case BP_VARIABLES:
              show_boxplot_variabled (cmd, i);
              break;
            default:
              NOT_REACHED ();
              break;
            }
        }

      if (cmd->plot & PLOT_HISTOGRAM)
        show_histogram (cmd, i);

      if (cmd->plot & PLOT_NPPLOT)
        show_npplot (cmd, i);

      if (cmd->plot & PLOT_SPREADLEVEL)
        show_spreadlevel (cmd, i);

      if (cmd->descriptives)
        descriptives_report (cmd, i);

      if (cmd->plot)
	normality_report (cmd, i);
    }

  cleanup_exploratory_stats (cmd);
  categoricals_destroy (cmd->cats);
}


int
cmd_examine (struct lexer *lexer, struct dataset *ds)
{
  int i;
  bool nototals_seen = false;
  bool totals_seen = false;

  struct interaction **iacts_mem = NULL;
  struct examine examine;
  bool percentiles_seen = false;

  examine.missing_pw = false;
  examine.disp_extremes = 0;
  examine.calc_extremes = 0;
  examine.descriptives = false;
  examine.conf = 0.95;
  examine.pc_alg = PC_HAVERAGE;
  examine.ptiles = NULL;
  examine.n_percentiles = 0;
  examine.id_idx = -1;
  examine.id_width = 0;
  examine.id_var = NULL;
  examine.boxplot_mode = BP_GROUPS;

  examine.ex_proto = caseproto_create ();

  examine.pool = pool_create ();

  /* Allocate space for the first interaction.
     This is interaction is an empty one (for the totals).
     If no totals are requested, we will simply ignore this
     interaction.
  */
  examine.n_iacts = 1;
  examine.iacts = iacts_mem = pool_zalloc (examine.pool, sizeof (struct interaction *));
  examine.iacts[0] = interaction_create (NULL);

  examine.dep_excl = MV_ANY;
  examine.fctr_excl = MV_ANY;
  examine.plot = 0;
  examine.sl_power = 0;
  examine.dep_vars = NULL;
  examine.n_dep_vars = 0;
  examine.dict = dataset_dict (ds);

  /* Accept an optional, completely pointless "/VARIABLES=" */
  lex_match (lexer, T_SLASH);
  if (lex_match_id  (lexer, "VARIABLES"))
    {
      if (! lex_force_match (lexer, T_EQUALS))
        goto error;
    }

  if (!parse_variables_const (lexer, examine.dict,
			      &examine.dep_vars, &examine.n_dep_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    goto error;

  if (lex_match (lexer, T_BY))
    {
      struct interaction *iact = NULL;
      do
        {
          iact = parse_interaction (lexer, &examine);
          if (iact)
            {
              examine.n_iacts++;
              iacts_mem =
                pool_nrealloc (examine.pool, iacts_mem,
			       examine.n_iacts,
			       sizeof (*iacts_mem));

              iacts_mem[examine.n_iacts - 1] = iact;
            }
        }
      while (iact);
    }


  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "STATISTICS"))
	{
	  lex_match (lexer, T_EQUALS);

	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
              if (lex_match_id (lexer, "DESCRIPTIVES"))
                {
                  examine.descriptives = true;
                }
              else if (lex_match_id (lexer, "EXTREME"))
                {
                  int extr = 5;
                  if (lex_match (lexer, T_LPAREN))
                    {
                      if (!lex_force_int_range (lexer, "EXTREME", 0, INT_MAX))
                        goto error;
                      extr = lex_integer (lexer);

                      lex_get (lexer);
                      if (! lex_force_match (lexer, T_RPAREN))
                        goto error;
                    }
                  examine.disp_extremes  = extr;
                }
              else if (lex_match_id (lexer, "NONE"))
                {
                }
              else if (lex_match (lexer, T_ALL))
                {
                  if (examine.disp_extremes == 0)
                    examine.disp_extremes = 5;
                }
              else
                {
                  lex_error (lexer, NULL);
                  goto error;
                }
            }
        }
      else if (lex_match_id (lexer, "PERCENTILES"))
        {
          percentiles_seen = true;
          if (lex_match (lexer, T_LPAREN))
            {
              while (lex_is_number (lexer))
                {
                  double p = lex_number (lexer);

                  if (p <= 0 || p >= 100.0)
                    {
                      lex_error (lexer,
                                 _("Percentiles must lie in the range (0, 100)"));
                      goto error;
                    }

                  examine.n_percentiles++;
                  examine.ptiles =
                    xrealloc (examine.ptiles,
                              sizeof (*examine.ptiles) *
                              examine.n_percentiles);

                  examine.ptiles[examine.n_percentiles - 1] = p;

                  lex_get (lexer);
                  lex_match (lexer, T_COMMA);
                }
              if (!lex_force_match (lexer, T_RPAREN))
                goto error;
            }

	  lex_match (lexer, T_EQUALS);

	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
              if (lex_match_id (lexer, "HAVERAGE"))
                {
                  examine.pc_alg = PC_HAVERAGE;
                }
              else if (lex_match_id (lexer, "WAVERAGE"))
                {
                  examine.pc_alg = PC_WAVERAGE;
                }
              else if (lex_match_id (lexer, "ROUND"))
                {
                  examine.pc_alg = PC_ROUND;
                }
              else if (lex_match_id (lexer, "EMPIRICAL"))
                {
                  examine.pc_alg = PC_EMPIRICAL;
                }
              else if (lex_match_id (lexer, "AEMPIRICAL"))
                {
                  examine.pc_alg = PC_AEMPIRICAL;
                }
              else if (lex_match_id (lexer, "NONE"))
                {
                  examine.pc_alg = PC_NONE;
                }
              else
                {
                  lex_error (lexer, NULL);
                  goto error;
                }
            }
        }
      else if (lex_match_id (lexer, "TOTAL"))
        {
          totals_seen = true;
        }
      else if (lex_match_id (lexer, "NOTOTAL"))
        {
          nototals_seen = true;
        }
      else if (lex_match_id (lexer, "MISSING"))
        {
	  lex_match (lexer, T_EQUALS);

	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
              if (lex_match_id (lexer, "LISTWISE"))
                {
                  examine.missing_pw = false;
                }
              else if (lex_match_id (lexer, "PAIRWISE"))
                {
                  examine.missing_pw = true;
                }
              else if (lex_match_id (lexer, "EXCLUDE"))
                {
                  examine.dep_excl = MV_ANY;
                }
              else if (lex_match_id (lexer, "INCLUDE"))
                {
                  examine.dep_excl = MV_SYSTEM;
                }
              else if (lex_match_id (lexer, "REPORT"))
                {
                  examine.fctr_excl = MV_NEVER;
                }
              else if (lex_match_id (lexer, "NOREPORT"))
                {
                  examine.fctr_excl = MV_ANY;
                }
              else
                {
                  lex_error (lexer, NULL);
                  goto error;
                }
            }
        }
      else if (lex_match_id (lexer, "COMPARE"))
        {
	  lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "VARIABLES"))
            {
              examine.boxplot_mode = BP_VARIABLES;
            }
          else if (lex_match_id (lexer, "GROUPS"))
            {
              examine.boxplot_mode = BP_GROUPS;
            }
          else
            {
              lex_error (lexer, NULL);
              goto error;
            }
        }
      else if (lex_match_id (lexer, "PLOT"))
        {
	  lex_match (lexer, T_EQUALS);

	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
              if (lex_match_id (lexer, "BOXPLOT"))
                {
                  examine.plot |= PLOT_BOXPLOT;
                }
              else if (lex_match_id (lexer, "NPPLOT"))
                {
                  examine.plot |= PLOT_NPPLOT;
                }
              else if (lex_match_id (lexer, "HISTOGRAM"))
                {
                  examine.plot |= PLOT_HISTOGRAM;
                }
              else if (lex_match_id (lexer, "SPREADLEVEL"))
                {
                  examine.plot |= PLOT_SPREADLEVEL;
		  examine.sl_power = 0;
		  if (lex_match (lexer, T_LPAREN) && lex_force_num (lexer))
		    {
                      examine.sl_power = lex_number (lexer);

                      lex_get (lexer);
                      if (! lex_force_match (lexer, T_RPAREN))
                        goto error;
		    }
                }
              else if (lex_match_id (lexer, "NONE"))
                {
                  examine.plot = 0;
                }
              else if (lex_match (lexer, T_ALL))
                {
                  examine.plot = ~0;
                }
              else
                {
                  lex_error (lexer, NULL);
                  goto error;
                }
              lex_match (lexer, T_COMMA);
            }
        }
      else if (lex_match_id (lexer, "CINTERVAL"))
        {
          if (!lex_force_num (lexer))
            goto error;

          examine.conf = lex_number (lexer);
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "ID"))
        {
          lex_match (lexer, T_EQUALS);

          examine.id_var = parse_variable_const (lexer, examine.dict);
        }
      else
        {
          lex_error (lexer, NULL);
          goto error;
        }
    }


  if (totals_seen && nototals_seen)
    {
      msg (SE, _("%s and %s are mutually exclusive"), "TOTAL", "NOTOTAL");
      goto error;
    }

  /* If totals have been requested or if there are no factors
     in this analysis, then the totals need to be included. */
  if (!nototals_seen || examine.n_iacts == 1)
    {
      examine.iacts = &iacts_mem[0];
    }
  else
    {
      examine.n_iacts--;
      examine.iacts = &iacts_mem[1];
      interaction_destroy (iacts_mem[0]);
    }


  if (examine.id_var)
    {
      examine.id_idx = var_get_case_index (examine.id_var);
      examine.id_width = var_get_width (examine.id_var);
    }

  examine.ex_proto = caseproto_add_width (examine.ex_proto, 0); /* value */
  examine.ex_proto = caseproto_add_width (examine.ex_proto, examine.id_width);   /* id */
  examine.ex_proto = caseproto_add_width (examine.ex_proto, 0); /* weight */


  if (examine.disp_extremes > 0)
    {
      examine.calc_extremes = examine.disp_extremes;
    }

  if (examine.descriptives && examine.calc_extremes == 0)
    {
      /* Descriptives always displays the max and min */
      examine.calc_extremes = 1;
    }

  if (percentiles_seen && examine.n_percentiles == 0)
    {
      examine.n_percentiles = 7;
      examine.ptiles = xcalloc (examine.n_percentiles,
                                sizeof (*examine.ptiles));

      examine.ptiles[0] = 5;
      examine.ptiles[1] = 10;
      examine.ptiles[2] = 25;
      examine.ptiles[3] = 50;
      examine.ptiles[4] = 75;
      examine.ptiles[5] = 90;
      examine.ptiles[6] = 95;
    }

  assert (examine.calc_extremes >= examine.disp_extremes);
  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;

    grouper = casegrouper_create_splits (proc_open (ds), examine.dict);
    while (casegrouper_get_next_group (grouper, &group))
      run_examine (&examine, group);
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  caseproto_unref (examine.ex_proto);

  for (i = 0; i < examine.n_iacts; ++i)
    interaction_destroy (examine.iacts[i]);
  free (examine.ptiles);
  free (examine.dep_vars);
  pool_destroy (examine.pool);

  return CMD_SUCCESS;

 error:
  caseproto_unref (examine.ex_proto);
  examine.iacts = iacts_mem;
  for (i = 0; i < examine.n_iacts; ++i)
    interaction_destroy (examine.iacts[i]);
  free (examine.dep_vars);
  free (examine.ptiles);
  pool_destroy (examine.pool);

  return CMD_FAILURE;
}
