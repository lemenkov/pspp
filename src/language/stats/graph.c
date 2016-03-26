/*
  PSPP - a program for statistical analysis.
  Copyright (C) 2012, 2013, 2015 Free Software Foundation, Inc.
  
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

/*
 * This module implements the graph command
 */

#include <config.h>

#include <math.h>
#include "gl/xalloc.h"
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

#include "math/chart-geometry.h"
#include "math/histogram.h"
#include "math/moments.h"
#include "math/sort.h"
#include "math/order-stats.h"
#include "output/charts/plot-hist.h"
#include "output/charts/scatterplot.h"
#include "output/charts/barchart.h"

#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "language/stats/freq.h"
#include "language/stats/chart-category.h"

#include "output/tab.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum chart_type
  {
    CT_NONE,
    CT_BAR,
    CT_LINE,
    CT_PIE,
    CT_ERRORBAR,
    CT_HILO,
    CT_HISTOGRAM,
    CT_SCATTERPLOT,
    CT_PARETO
  };

enum scatter_type
  {
    ST_BIVARIATE,
    ST_OVERLAY,
    ST_MATRIX,
    ST_XYZ
  };

enum  bar_type
  {
    CBT_SIMPLE,
    CBT_GROUPED,
    CBT_STACKED,
    CBT_RANGE
  };


/* Variable index for histogram case */
enum
  {
    HG_IDX_X,
    HG_IDX_WT
  };

struct exploratory_stats
{
  double missing;
  double non_missing;

  struct moments *mom;

  double minimum;
  double maximum;

  /* Total weight */
  double cc;

  /* The minimum weight */
  double cmin;
};


struct graph
{
  struct pool *pool;

  size_t n_dep_vars;
  const struct variable **dep_vars;
  struct exploratory_stats *es;

  enum mv_class dep_excl;
  enum mv_class fctr_excl;

  const struct dictionary *dict;

  bool missing_pw;

  /* ------------ Graph ---------------- */
  bool normal; /* For histograms, draw the normal curve */

  enum chart_type chart_type;
  enum scatter_type scatter_type;
  enum bar_type bar_type;
  const struct variable *by_var[2];
  size_t n_by_vars;
  
  struct subcase ordering; /* Ordering for aggregation */
  int agr; /* Index into ag_func */
  
  /* A caseproto that contains the plot data */
  struct caseproto *gr_proto;
};




static double
calc_mom1 (double acc, double x, double w)
{
  return acc + x * w;
}

static double
calc_mom0 (double acc, double x UNUSED, double w)
{
  return acc + w;
}

static double
pre_low_extreme (void)
{
  return -DBL_MAX;
}

static double
calc_max (double acc, double x, double w UNUSED)
{
  return (acc > x) ? acc : x;
}

static double
pre_high_extreme (void)
{
  return DBL_MAX;
}

static double
calc_min (double acc, double x, double w UNUSED)
{
  return (acc < x) ? acc : x;
}

static double
post_normalise (double acc, double cc)
{
  return acc / cc;
}

static double
post_percentage (double acc, double ccc)
{
  return acc / ccc * 100.0;
}


const struct ag_func ag_func[] =
  {
    {"COUNT",   N_("Count"),      0, 0, NULL, calc_mom0, 0, 0},
    {"PCT",     N_("Percentage"), 0, 0, NULL, calc_mom0, 0, post_percentage},
    {"CUFREQ",  N_("Cumulative Count"),   0, 1, NULL, calc_mom0, 0, 0},
    {"CUPCT",   N_("Cumulative Percent"), 0, 1, NULL, calc_mom0, 0, post_percentage},

    {"MEAN",    N_("Mean"),    1, 0, NULL, calc_mom1, post_normalise, 0},
    {"SUM",     N_("Sum"),     1, 0, NULL, calc_mom1, 0, 0},
    {"MAXIMUM", N_("Maximum"), 1, 0, pre_low_extreme, calc_max, 0, 0},
    {"MINIMUM", N_("Minimum"), 1, 0, pre_high_extreme, calc_min, 0, 0},
  };

const int N_AG_FUNCS = sizeof (ag_func) / sizeof (ag_func[0]);

static bool
parse_function (struct lexer *lexer, struct graph *graph)
{
  int i;
  for (i = 0 ; i < N_AG_FUNCS; ++i)
    {
      if (lex_match_id (lexer, ag_func[i].name))
	{
	  graph->agr = i;
	  break;
	}
    }
  if (i == N_AG_FUNCS)
    {
      goto error;
    }

  graph->n_dep_vars = ag_func[i].arity;
  if (ag_func[i].arity > 0)
    {
      int v;
      if (!lex_force_match (lexer, T_LPAREN))
	goto error;

      graph->dep_vars = xzalloc (sizeof (graph->dep_vars) * graph->n_dep_vars);
      for (v = 0; v < ag_func[i].arity; ++v)
	{
	  graph->dep_vars[v] = parse_variable (lexer, graph->dict);
	  if (! graph->dep_vars[v])
	    goto error;
	}

      if (!lex_force_match (lexer, T_RPAREN))
	goto error;
    }

  if (!lex_force_match (lexer, T_BY))
    goto error;

  graph->by_var[0] = parse_variable (lexer, graph->dict);
  if (!graph->by_var[0])
    {
      goto error;
    }
  subcase_add_var (&graph->ordering, graph->by_var[0], SC_ASCEND);
  graph->n_by_vars++;

  if (lex_match (lexer, T_BY))
    {
      graph->by_var[1] = parse_variable (lexer, graph->dict);
      if (!graph->by_var[1])
	{
	  goto error;
	}
      subcase_add_var (&graph->ordering, graph->by_var[1], SC_ASCEND);
      graph->n_by_vars++;
    }

  return true;
  
 error:
  lex_error (lexer, NULL);
  return false;
}


static void
show_scatterplot (const struct graph *cmd, struct casereader *input)
{
  struct string title;
  struct scatterplot_chart *scatterplot;
  bool byvar_overflow = false;

  ds_init_empty (&title);

  if (cmd->n_by_vars > 0)
    {
      ds_put_format (&title, _("%s vs. %s by %s"),
			   var_to_string (cmd->dep_vars[1]),
			   var_to_string (cmd->dep_vars[0]),
			   var_to_string (cmd->by_var[0]));
    }
  else
    {
      ds_put_format (&title, _("%s vs. %s"),
		     var_to_string (cmd->dep_vars[1]),
		     var_to_string (cmd->dep_vars[0]));
    }

  scatterplot = scatterplot_create (input,
				    var_to_string(cmd->dep_vars[0]),
				    var_to_string(cmd->dep_vars[1]),
				    (cmd->n_by_vars > 0) ? cmd->by_var[0] : NULL,
				    &byvar_overflow,
				    ds_cstr (&title),
				    cmd->es[0].minimum, cmd->es[0].maximum,
				    cmd->es[1].minimum, cmd->es[1].maximum);
  scatterplot_chart_submit (scatterplot);
  ds_destroy (&title);

  if (byvar_overflow)
    {
      msg (MW, _("Maximum number of scatterplot categories reached. "
		 "Your BY variable has too many distinct values. "
		 "The coloring of the plot will not be correct."));
    }
}

static void
show_histogr (const struct graph *cmd, struct casereader *input)
{
  struct histogram *histogram;
  struct ccase *c;
  
  if (cmd->es[0].cc <= 0)
    {
      casereader_destroy (input);
      return;
    }
  
  {
    /* Sturges Rule */
    double bin_width = fabs (cmd->es[0].minimum - cmd->es[0].maximum)
      / (1 + log2 (cmd->es[0].cc))
      ;

    histogram =
      histogram_create (bin_width, cmd->es[0].minimum, cmd->es[0].maximum);
  }

  if (NULL == histogram)
    {
      casereader_destroy (input);
      return;
    }

  for (;(c = casereader_read (input)) != NULL; case_unref (c))
    {
      const double x      = case_data_idx (c, HG_IDX_X)->f;
      const double weight = case_data_idx (c, HG_IDX_WT)->f;
      moments_pass_two (cmd->es[0].mom, x, weight);
      histogram_add (histogram, x, weight);
    }
  casereader_destroy (input);


  {
    double n, mean, var;

    struct string label;

    ds_init_cstr (&label, 
		  var_to_string (cmd->dep_vars[0]));

    moments_calculate (cmd->es[0].mom, &n, &mean, &var, NULL, NULL);

    chart_item_submit
      ( histogram_chart_create (histogram->gsl_hist,
				ds_cstr (&label), n, mean,
				sqrt (var), cmd->normal));

    statistic_destroy (&histogram->parent);      
    ds_destroy (&label);
  }
}

static void
cleanup_exploratory_stats (struct graph *cmd)
{ 
  int v;

  for (v = 0; v < cmd->n_dep_vars; ++v)
    {
      moments_destroy (cmd->es[v].mom);
    }
}


static void
run_barchart (struct graph *cmd, struct casereader *input)
{
  struct casegrouper *grouper;
  struct casereader *group;
  double ccc = 0.0;

  if ( cmd->missing_pw == false) 
    input = casereader_create_filter_missing (input,
                                              cmd->dep_vars,
                                              cmd->n_dep_vars,
                                              cmd->dep_excl,
                                              NULL,
                                              NULL);


  input = sort_execute (input, &cmd->ordering);

  struct freq **freqs = NULL;
  int n_freqs = 0;

  for (grouper = casegrouper_create_vars (input, cmd->by_var,
                                          cmd->n_by_vars);
       casegrouper_get_next_group (grouper, &group);
       casereader_destroy (group))
    {
      int v;
      struct ccase *c = casereader_peek (group, 0);

      /* Deal with missing values in the categorical variables */
      for (v = 0; v < cmd->n_by_vars; ++v)
	{
	  if (var_is_value_missing (cmd->by_var[v], case_data (c, cmd->by_var[v]), cmd->fctr_excl) )
	    break;
	}

      if (v < cmd->n_by_vars)
	{
	  case_unref (c);
	  continue;
	}

      freqs = xrealloc (freqs, sizeof (*freqs) * ++n_freqs);
      freqs[n_freqs - 1] = xzalloc (sizeof (**freqs) +
				    sizeof (union value) * (cmd->n_by_vars - 1) );

      if (ag_func[cmd->agr].cumulative && n_freqs >= 2)
	freqs[n_freqs - 1]->count = freqs[n_freqs - 2]->count;
      else
	freqs[n_freqs - 1]->count = 0;
      if (ag_func[cmd->agr].pre)
	freqs[n_freqs - 1]->count = ag_func[cmd->agr].pre();


      for (v = 0; v < cmd->n_by_vars; ++v)
	{
	  value_clone (&freqs[n_freqs - 1]->values[v], case_data (c, cmd->by_var[v]),
		       var_get_width (cmd->by_var[v])
		       );
	}
      case_unref (c);

      double cc = 0;
      for (;(c = casereader_read (group)) != NULL; case_unref (c))
	{
	  const double weight = dict_get_case_weight (cmd->dict,c,NULL);
	  const double x =  (cmd->n_dep_vars > 0) ? case_data (c, cmd->dep_vars[0])->f : SYSMIS;

	  cc += weight;
	  
	  freqs[n_freqs - 1]->count
	    = ag_func[cmd->agr].calc (freqs[n_freqs - 1]->count, x, weight);
	}

      if (ag_func[cmd->agr].post)
      	freqs[n_freqs - 1]->count
      	  = ag_func[cmd->agr].post (freqs[n_freqs - 1]->count, cc);

      ccc += cc;
    }

  casegrouper_destroy (grouper);

  for (int i = 0; i < n_freqs; ++i)
    {
      if (ag_func[cmd->agr].ppost)
      	freqs[i]->count = ag_func[cmd->agr].ppost (freqs[i]->count, ccc);
    }


  {
    struct string label;
    ds_init_empty (&label);

    if (cmd->n_dep_vars > 0)
      ds_put_format (&label, _("%s of %s"),
		     ag_func[cmd->agr].description,
		     var_get_name (cmd->dep_vars[0]));
    else
      ds_put_cstr (&label, 
		     ag_func[cmd->agr].description);
      
    chart_item_submit (barchart_create (cmd->by_var, cmd->n_by_vars,
					ds_cstr (&label), false,
					freqs, n_freqs));

    ds_destroy (&label);
  }

  for (int i = 0; i < n_freqs; ++i)
    free (freqs[i]);
  
  free (freqs);
}


static void
run_graph (struct graph *cmd, struct casereader *input)
{
  struct ccase *c;
  struct casereader *reader;
  struct casewriter *writer;

  cmd->es = pool_calloc (cmd->pool,cmd->n_dep_vars,sizeof(struct exploratory_stats));
  for(int v=0;v<cmd->n_dep_vars;v++)
    {
      cmd->es[v].mom = moments_create (MOMENT_KURTOSIS);
      cmd->es[v].cmin = DBL_MAX;
      cmd->es[v].maximum = -DBL_MAX;
      cmd->es[v].minimum =  DBL_MAX;
    }
  /* Always remove cases listwise. This is correct for */
  /* the histogram because there is only one variable  */
  /* and a simple bivariate scatterplot                */
  /* if ( cmd->missing_pw == false)                    */
    input = casereader_create_filter_missing (input,
                                              cmd->dep_vars,
                                              cmd->n_dep_vars,
                                              cmd->dep_excl,
                                              NULL,
                                              NULL);

  writer = autopaging_writer_create (cmd->gr_proto);

  /* The case data is copied to a new writer        */
  /* The setup of the case depends on the Charttype */
  /* For Scatterplot x is assumed in dep_vars[0]    */
  /*                 y is assumed in dep_vars[1]    */
  /* For Histogram   x is assumed in dep_vars[0]    */
  assert(SP_IDX_X == 0 && SP_IDX_Y == 1 && HG_IDX_X == 0);

  for (;(c = casereader_read (input)) != NULL; case_unref (c))
    {
      struct ccase *outcase = case_create (cmd->gr_proto);
      const double weight = dict_get_case_weight (cmd->dict,c,NULL);
      if (cmd->chart_type == CT_HISTOGRAM)
	case_data_rw_idx (outcase, HG_IDX_WT)->f = weight;
      if (cmd->chart_type == CT_SCATTERPLOT && cmd->n_by_vars > 0)
	value_copy (case_data_rw_idx (outcase, SP_IDX_BY),
		    case_data (c, cmd->by_var[0]),
		    var_get_width (cmd->by_var[0]));
      for(int v=0;v<cmd->n_dep_vars;v++)
	{
	  const struct variable *var = cmd->dep_vars[v];
	  const double x = case_data (c, var)->f;

	  if (var_is_value_missing (var, case_data (c, var), cmd->dep_excl))
	    {
	      cmd->es[v].missing += weight;
	      continue;
	    }
	  /* Magically v value fits to SP_IDX_X, SP_IDX_Y, HG_IDX_X */
	  case_data_rw_idx (outcase, v)->f = x;

	  if (x > cmd->es[v].maximum)
	    cmd->es[v].maximum = x;

	  if (x < cmd->es[v].minimum)
	    cmd->es[v].minimum =  x;

	  cmd->es[v].non_missing += weight;

	  moments_pass_one (cmd->es[v].mom, x, weight);

	  cmd->es[v].cc += weight;

	  if (cmd->es[v].cmin > weight)
	    cmd->es[v].cmin = weight;
	}
      casewriter_write (writer,outcase);
    }

  reader = casewriter_make_reader (writer);

  switch (cmd->chart_type)
    {
    case CT_HISTOGRAM:
      show_histogr (cmd,reader);
      break;
    case CT_SCATTERPLOT:
      show_scatterplot (cmd,reader);
      break;
    default:
      NOT_REACHED ();
      break;
    };

  casereader_destroy (input);
  cleanup_exploratory_stats (cmd);
}


int
cmd_graph (struct lexer *lexer, struct dataset *ds)
{
  struct graph graph;

  graph.missing_pw = false;
  
  graph.pool = pool_create ();

  graph.dep_excl = MV_ANY;
  graph.fctr_excl = MV_ANY;
  
  graph.dict = dataset_dict (ds);
  
  graph.dep_vars = NULL;
  graph.chart_type = CT_NONE;
  graph.scatter_type = ST_BIVARIATE;
  graph.n_by_vars = 0;
  graph.gr_proto = caseproto_create ();

  subcase_init_empty (&graph.ordering);
  
  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "HISTOGRAM"))
	{
	  if (graph.chart_type != CT_NONE)
	    {
	      lex_error (lexer, _("Only one chart type is allowed."));
	      goto error;
	    }
          graph.normal = false;
          if (lex_match (lexer, T_LPAREN))
            {
              if (!lex_force_match_id (lexer, "NORMAL"))
                goto error;
              
              if (!lex_force_match (lexer, T_RPAREN))
                goto error;

              graph.normal = true;
            }
	  if (!lex_force_match (lexer, T_EQUALS))
	    goto error;
	  graph.chart_type = CT_HISTOGRAM;
	  if (!parse_variables_const (lexer, graph.dict,
				      &graph.dep_vars, &graph.n_dep_vars,
				      PV_NO_DUPLICATE | PV_NUMERIC))
	    goto error;
	  if (graph.n_dep_vars > 1)
	    {
	      lex_error (lexer, _("Only one variable is allowed."));
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "BAR"))
	{
	  if (graph.chart_type != CT_NONE)
	    {
	      lex_error (lexer, _("Only one chart type is allowed."));
	      goto error;
	    }
	  graph.chart_type = CT_BAR;
	  graph.bar_type = CBT_SIMPLE;
	  
	  if (lex_match (lexer, T_LPAREN)) 
	    {
	      if (lex_match_id (lexer, "SIMPLE"))
		{
		  /* This is the default anyway */
		}
	      else if (lex_match_id (lexer, "GROUPED"))  
		{
		  graph.bar_type = CBT_GROUPED; 
		  goto error;
		}
	      else if (lex_match_id (lexer, "STACKED"))  
		{
		  graph.bar_type = CBT_STACKED; 
		  lex_error (lexer, _("%s is not yet implemented."), "STACKED");
		  goto error;
		}
	      else if (lex_match_id (lexer, "RANGE"))  
		{
		  graph.bar_type = CBT_RANGE; 
		  lex_error (lexer, _("%s is not yet implemented."), "RANGE");
		  goto error;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	      if (!lex_force_match (lexer, T_RPAREN))
		goto error;
	    }
	  
	  if (!lex_force_match (lexer, T_EQUALS))
	    goto error;

	  if (! parse_function (lexer, &graph))
	    goto error;
	}
      else if (lex_match_id (lexer, "SCATTERPLOT"))
	{
	  if (graph.chart_type != CT_NONE)
	    {
	      lex_error (lexer, _("Only one chart type is allowed."));
	      goto error;
	    }
	  graph.chart_type = CT_SCATTERPLOT;
	  if (lex_match (lexer, T_LPAREN)) 
	    {
	      if (lex_match_id (lexer, "BIVARIATE"))
		{
		  /* This is the default anyway */
		}
	      else if (lex_match_id (lexer, "OVERLAY"))  
		{
		  lex_error (lexer, _("%s is not yet implemented."),"OVERLAY");
		  goto error;
		}
	      else if (lex_match_id (lexer, "MATRIX"))  
		{
		  lex_error (lexer, _("%s is not yet implemented."),"MATRIX");
		  goto error;
		}
	      else if (lex_match_id (lexer, "XYZ"))  
		{
		  lex_error(lexer, _("%s is not yet implemented."),"XYZ");
		  goto error;
		}
	      else
		{
		  lex_error_expecting (lexer, "BIVARIATE", NULL);
		  goto error;
		}
	      if (!lex_force_match (lexer, T_RPAREN))
		goto error;
	    }
	  if (!lex_force_match (lexer, T_EQUALS))
	    goto error;

	  if (!parse_variables_const (lexer, graph.dict,
				      &graph.dep_vars, &graph.n_dep_vars,
				      PV_NO_DUPLICATE | PV_NUMERIC))
	    goto error;
	 
	  if (graph.scatter_type == ST_BIVARIATE && graph.n_dep_vars != 1)
	    {
	      lex_error(lexer, _("Only one variable is allowed."));
	      goto error;
	    }

	  if (!lex_force_match (lexer, T_WITH))
	    goto error;

	  if (!parse_variables_const (lexer, graph.dict,
				      &graph.dep_vars, &graph.n_dep_vars,
				      PV_NO_DUPLICATE | PV_NUMERIC | PV_APPEND))
	    goto error;

	  if (graph.scatter_type == ST_BIVARIATE && graph.n_dep_vars != 2)
	    {
	      lex_error (lexer, _("Only one variable is allowed."));
	      goto error;
	    }
	  
	  if (lex_match (lexer, T_BY))
	    {
	      const struct variable *v = NULL;
	      if (!lex_match_variable (lexer,graph.dict,&v))
		{
		  lex_error (lexer, _("Variable expected"));
		  goto error;
		}
	      graph.by_var[0] = v;
              graph.n_by_vars = 1;
	    }
	}
      else if (lex_match_id (lexer, "LINE"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"LINE");
	  goto error;
	}
      else if (lex_match_id (lexer, "PIE"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"PIE");
	  goto error;
	}
      else if (lex_match_id (lexer, "ERRORBAR"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"ERRORBAR");
	  goto error;
	}
      else if (lex_match_id (lexer, "PARETO"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"PARETO");
	  goto error;
	}
      else if (lex_match_id (lexer, "TITLE"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"TITLE");
	  goto error;
	}
      else if (lex_match_id (lexer, "SUBTITLE"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"SUBTITLE");
	  goto error;
	}
      else if (lex_match_id (lexer, "FOOTNOTE"))
	{
	  lex_error (lexer, _("%s is not yet implemented."),"FOOTNOTE");
	  lex_error (lexer, _("FOOTNOTE is not implemented yet for GRAPH"));
	  goto error;
	}
      else if (lex_match_id (lexer, "MISSING"))
        {
	  lex_match (lexer, T_EQUALS);

	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
              if (lex_match_id (lexer, "LISTWISE"))
                {
                  graph.missing_pw = false;
                }
              else if (lex_match_id (lexer, "VARIABLE"))
                {
                  graph.missing_pw = true;
                }
              else if (lex_match_id (lexer, "EXCLUDE"))
                {
                  graph.dep_excl = MV_ANY;
                }
              else if (lex_match_id (lexer, "INCLUDE"))
                {
                  graph.dep_excl = MV_SYSTEM;
                }
              else if (lex_match_id (lexer, "REPORT"))
                {
                  graph.fctr_excl = MV_NEVER;
                }
              else if (lex_match_id (lexer, "NOREPORT"))
                {
                  graph.fctr_excl = MV_ANY;
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

  switch (graph.chart_type)
    {
    case CT_SCATTERPLOT:
      /* See scatterplot.h for the setup of the case prototype */
      graph.gr_proto = caseproto_add_width (graph.gr_proto, 0); /* x value - SP_IDX_X*/
      graph.gr_proto = caseproto_add_width (graph.gr_proto, 0); /* y value - SP_IDX_Y*/
      /* The by_var contains the plot categories for the different xy plot colors */
      if (graph.n_by_vars > 0) /* SP_IDX_BY */
	graph.gr_proto = caseproto_add_width (graph.gr_proto, var_get_width(graph.by_var[0]));
      break;
    case CT_HISTOGRAM:
      graph.gr_proto = caseproto_add_width (graph.gr_proto, 0); /* x value      */
      graph.gr_proto = caseproto_add_width (graph.gr_proto, 0); /* weight value */
      break;
    case CT_BAR:
      break;
    case CT_NONE:
      lex_error_expecting (lexer, "HISTOGRAM", "SCATTERPLOT", "BAR", NULL);
      goto error;
    default:
      NOT_REACHED ();
      break;
    };

  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;
    
    grouper = casegrouper_create_splits (proc_open (ds), graph.dict);
    while (casegrouper_get_next_group (grouper, &group))
      {
	if (graph.chart_type == CT_BAR)
	  run_barchart (&graph, group);
	else
	  run_graph (&graph, group);
      }
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  subcase_destroy (&graph.ordering);
  free (graph.dep_vars);
  pool_destroy (graph.pool);
  caseproto_unref (graph.gr_proto);

  return CMD_SUCCESS;

 error:
  subcase_destroy (&graph.ordering);
  caseproto_unref (graph.gr_proto);
  free (graph.dep_vars);
  pool_destroy (graph.pool);

  return CMD_FAILURE;
}
