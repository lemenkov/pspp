/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012, 2015 Free Software Foundation, Inc.

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

#include <gsl/gsl_matrix.h>
#include <gsl/gsl_permutation.h>
#include <gsl/gsl_sort_vector.h>
#include <gsl/gsl_statistics.h>
#include <stdio.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/assertion.h"
#include "libpspp/str.h"
#include "math/random.h"
#include "output/tab.h"
#include "output/text-item.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum missing_type
  {
    MISS_LISTWISE,
    MISS_PAIRWISE,
  };


struct qc
{
  const struct variable **vars;
  size_t n_vars;

  double epsilon;               /* The convergence criterium */

  int ngroups;			/* Number of group. (Given by the user) */
  int maxiter;			/* Maximum iterations (Given by the user) */
  bool print_cluster_membership; /* true => print membership */
  bool print_initial_clusters;   /* true => print initial cluster */
  bool no_initial;              /* true => simplified initial cluster selection */
  bool no_update;               /* true => do not iterate  */

  const struct variable *wv;	/* Weighting variable. */

  enum missing_type missing_type;
  enum mv_class exclude;
};

/* Holds all of the information for the functions.  int n, holds the number of
   observation and its default value is -1.  We set it in
   kmeans_recalculate_centers in first invocation. */
struct Kmeans
{
  gsl_matrix *centers;		/* Centers for groups. */
  gsl_matrix *updated_centers;
  casenumber n;

  gsl_vector_long *num_elements_groups;

  gsl_matrix *initial_centers;	/* Initial random centers. */
  double convergence_criteria;
  gsl_permutation *group_order;	/* Group order for reporting. */
};

static struct Kmeans *kmeans_create (const struct qc *qc);

static void kmeans_get_nearest_group (const struct Kmeans *kmeans, struct ccase *c, const struct qc *, int *, double *, int *, double *);

static void kmeans_order_groups (struct Kmeans *kmeans, const struct qc *);

static void kmeans_cluster (struct Kmeans *kmeans, struct casereader *reader, const struct qc *);

static void quick_cluster_show_centers (struct Kmeans *kmeans, bool initial, const struct qc *);

static void quick_cluster_show_membership (struct Kmeans *kmeans, const struct casereader *reader, const struct qc *);

static void quick_cluster_show_number_cases (struct Kmeans *kmeans, const struct qc *);

static void quick_cluster_show_results (struct Kmeans *kmeans, const struct casereader *reader, const struct qc *);

int cmd_quick_cluster (struct lexer *lexer, struct dataset *ds);

static void kmeans_destroy (struct Kmeans *kmeans);

/* Creates and returns a struct of Kmeans with given casereader 'cs', parsed
   variables 'variables', number of cases 'n', number of variables 'm', number
   of clusters and amount of maximum iterations. */
static struct Kmeans *
kmeans_create (const struct qc *qc)
{
  struct Kmeans *kmeans = xmalloc (sizeof (struct Kmeans));
  kmeans->centers = gsl_matrix_alloc (qc->ngroups, qc->n_vars);
  kmeans->updated_centers = gsl_matrix_alloc (qc->ngroups, qc->n_vars);
  kmeans->num_elements_groups = gsl_vector_long_alloc (qc->ngroups);
  kmeans->group_order = gsl_permutation_alloc (kmeans->centers->size1);
  kmeans->initial_centers = NULL;

  return (kmeans);
}

static void
kmeans_destroy (struct Kmeans *kmeans)
{
  gsl_matrix_free (kmeans->centers);
  gsl_matrix_free (kmeans->updated_centers);
  gsl_matrix_free (kmeans->initial_centers);

  gsl_vector_long_free (kmeans->num_elements_groups);

  gsl_permutation_free (kmeans->group_order);

  free (kmeans);
}

static double
diff_matrix (const gsl_matrix *m1, const gsl_matrix *m2)
{
  int i,j;
  double max_diff = -INFINITY;
  for (i = 0; i < m1->size1; ++i)
    {
      double diff = 0;
      for (j = 0; j < m1->size2; ++j)
	{
	  diff += pow2 (gsl_matrix_get (m1,i,j) - gsl_matrix_get (m2,i,j) );
	}
      if (diff > max_diff)
	max_diff = diff;
    }

  return max_diff;
}



static double 
matrix_mindist (const gsl_matrix *m, int *mn, int *mm)
{
  int i, j;
  double mindist = INFINITY;
  for (i = 0; i < m->size1 - 1; ++i)
    {
      for (j = i + 1; j < m->size1; ++j)
	{
	  int k;
	  double diff_sq = 0;
	  for (k = 0; k < m->size2; ++k)
	    {
	      diff_sq += pow2 (gsl_matrix_get (m, j, k) - gsl_matrix_get (m, i, k));
	    }
	  if (diff_sq < mindist)
	    {
	      mindist = diff_sq;
	      if (mn)
		*mn = i;
	      if (mm)
		*mm = j;
	    }
	}
    }

  return mindist;
}


/* Return the distance of C from the group whose index is WHICH */
static double
dist_from_case (const struct Kmeans *kmeans, const struct ccase *c, const struct qc *qc, int which)
{
  int j;
  double dist = 0;
  for (j = 0; j < qc->n_vars; j++)
    {
      const union value *val = case_data (c, qc->vars[j]);
      if ( var_is_value_missing (qc->vars[j], val, qc->exclude))
	NOT_REACHED ();
      
      dist += pow2 (gsl_matrix_get (kmeans->centers, which, j) - val->f);
    }
 
  return dist;
}

/* Return the minimum distance of the group WHICH and all other groups */
static double
min_dist_from (const struct Kmeans *kmeans, const struct qc *qc, int which)
{
  int j, i;

  double mindist = INFINITY;
  for (i = 0; i < qc->ngroups; i++)
    {
      if (i == which)
	continue;

      double dist = 0;
      for (j = 0; j < qc->n_vars; j++)
	{
	  dist += pow2 (gsl_matrix_get (kmeans->centers, i, j) - gsl_matrix_get (kmeans->centers, which, j));
	}
      
      if (dist < mindist)
	{
	  mindist = dist;
	}
    }

  return mindist;
}



/* Calculate the intial cluster centers. */
static void
kmeans_initial_centers (struct Kmeans *kmeans, const struct casereader *reader, const struct qc *qc)
{
  struct ccase *c;
  int nc = 0, j;

  struct casereader *cs = casereader_clone (reader);
  for (; (c = casereader_read (cs)) != NULL; case_unref (c))
    {
      bool missing = false;
      for (j = 0; j < qc->n_vars; ++j)
	{
	  const union value *val = case_data (c, qc->vars[j]);
	  if ( var_is_value_missing (qc->vars[j], val, qc->exclude))
	    {
	      missing = true;
	      break;
	    }

	  if (nc < qc->ngroups)
	    gsl_matrix_set (kmeans->centers, nc, j, val->f);
	}

      if (missing)
	continue;

      if (nc++ < qc->ngroups)
	continue;

      if (!qc->no_initial)
	{
	  int mq, mp;
	  double delta;

	  int mn, mm;
	  double m = matrix_mindist (kmeans->centers, &mn, &mm);

	  kmeans_get_nearest_group (kmeans, c, qc, &mq, &delta, &mp, NULL);
	  if (delta > m)
	    /* If the distance between C and the nearest group, is greater than the distance
	       between the two  groups which are clostest to each other, then one group must be replaced */
	    {
	      /* Out of mn and mm, which is the clostest of the two groups to C ? */
	      int which = (dist_from_case (kmeans, c, qc, mn) > dist_from_case (kmeans, c, qc, mm)) ? mm : mn;

	      for (j = 0; j < qc->n_vars; ++j)
		{
		  const union value *val = case_data (c, qc->vars[j]);
		  gsl_matrix_set (kmeans->centers, which, j, val->f);
		}
	    }
	  else if (dist_from_case (kmeans, c, qc, mp) > min_dist_from (kmeans, qc, mq))
	    /* If the distance between C and the second nearest group (MP) is greater than the 
	       smallest distance between the nearest group (MQ) and any other group, then replace
	       MQ with C */
	    {
	      for (j = 0; j < qc->n_vars; ++j)
		{
		  const union value *val = case_data (c, qc->vars[j]);
		  gsl_matrix_set (kmeans->centers, mq, j, val->f);
		}
	    }
	}
    }

  casereader_destroy (cs);

  kmeans->convergence_criteria = qc->epsilon * matrix_mindist (kmeans->centers, NULL, NULL);

  /* As it is the first iteration, the variable kmeans->initial_centers is NULL
     and it is created once for reporting issues. */
  kmeans->initial_centers = gsl_matrix_alloc (qc->ngroups, qc->n_vars);
  gsl_matrix_memcpy (kmeans->initial_centers, kmeans->centers);
}


/* Return the index of the group which is nearest to the case C */
static void
kmeans_get_nearest_group (const struct Kmeans *kmeans, struct ccase *c, const struct qc *qc, int *g_q, double *delta_q, int *g_p, double *delta_p)
{
  int result0 = -1;
  int result1 = -1;
  int i, j;
  double mindist0 = INFINITY;
  double mindist1 = INFINITY;
  for (i = 0; i < qc->ngroups; i++)
    {
      double dist = 0;
      for (j = 0; j < qc->n_vars; j++)
	{
	  const union value *val = case_data (c, qc->vars[j]);
	  if ( var_is_value_missing (qc->vars[j], val, qc->exclude))
	    continue;

	  dist += pow2 (gsl_matrix_get (kmeans->centers, i, j) - val->f);
	}

      if (dist < mindist0)
	{
	  mindist1 = mindist0;
	  result1 = result0;

	  mindist0 = dist;
	  result0 = i;
	}
      else if (dist < mindist1)
	{
	  mindist1 = dist;
	  result1 = i;
	}
    }

  if (delta_q)
    *delta_q = mindist0;

  if (g_q)
    *g_q = result0;


  if (delta_p)
    *delta_p = mindist1;

  if (g_p)
    *g_p = result1;
}



static void
kmeans_order_groups (struct Kmeans *kmeans, const struct qc *qc)
{
  gsl_vector *v = gsl_vector_alloc (qc->ngroups);
  gsl_matrix_get_col (v, kmeans->centers, 0);
  gsl_sort_vector_index (kmeans->group_order, v);
  gsl_vector_free (v);
}

/* Main algorithm.
   Does iterations, checks convergency. */
static void
kmeans_cluster (struct Kmeans *kmeans, struct casereader *reader, const struct qc *qc)
{
  int j;

  kmeans_initial_centers (kmeans, reader, qc);

  gsl_matrix_memcpy (kmeans->updated_centers, kmeans->centers);


  for (int xx = 0 ; xx < qc->maxiter ; ++xx)
    {
      gsl_vector_long_set_all (kmeans->num_elements_groups, 0.0);

      kmeans->n = 0;
      if (!qc->no_update)
	{
	  struct casereader *r = casereader_clone (reader);
	  struct ccase *c;
	  for (; (c = casereader_read (r)) != NULL; case_unref (c))
	    {
	      int group = -1;
	      int g;
	      bool missing = false;

	      for (j = 0; j < qc->n_vars; j++)
		{
		  const union value *val = case_data (c, qc->vars[j]);
		  if ( var_is_value_missing (qc->vars[j], val, qc->exclude))
		    missing = true;
		}
	
	      if (missing)
		continue;

	      double mindist = INFINITY;
	      for (g = 0; g < qc->ngroups; ++g)
		{
		  double d = dist_from_case (kmeans, c, qc, g);

		  if (d < mindist)
		    {
		      mindist = d;
		      group = g;
		    }
		}

	      long *n = gsl_vector_long_ptr (kmeans->num_elements_groups, group);
	      *n += qc->wv ? case_data (c, qc->wv)->f : 1.0;
	      kmeans->n++;

	      for (j = 0; j < qc->n_vars; ++j)
		{
		  const union value *val = case_data (c, qc->vars[j]);
		  if ( var_is_value_missing (qc->vars[j], val, qc->exclude))
		    continue;
		  double *x = gsl_matrix_ptr (kmeans->updated_centers, group, j);
		  *x += val->f * (qc->wv ? case_data (c, qc->wv)->f : 1.0);
		}
	    }    

	  casereader_destroy (r);
	}

      int g;

      /* Divide the cluster sums by the number of items in each cluster */
      for (g = 0; g < qc->ngroups; ++g)
	{
	  for (j = 0; j < qc->n_vars; ++j)
	    {
	      long n = gsl_vector_long_get (kmeans->num_elements_groups, g);
	      double *x = gsl_matrix_ptr (kmeans->updated_centers, g, j);
	      *x /= n + 1;  // Plus 1 for the initial centers
	    }
	}
  

      gsl_matrix_memcpy (kmeans->centers, kmeans->updated_centers);

      {
	kmeans->n = 0;
	/* Step 3 */
	gsl_vector_long_set_all (kmeans->num_elements_groups, 0.0);
	gsl_matrix_set_all (kmeans->updated_centers, 0.0);
	struct ccase *c;
	struct casereader *cs = casereader_clone (reader);
	for (; (c = casereader_read (cs)) != NULL; case_unref (c))
	  {
	    int group = -1; 
	    kmeans_get_nearest_group (kmeans, c, qc, &group, NULL, NULL, NULL);

	    for (j = 0; j < qc->n_vars; ++j)
	      {
		const union value *val = case_data (c, qc->vars[j]);
		if ( var_is_value_missing (qc->vars[j], val, qc->exclude))
		  continue;

		double *x = gsl_matrix_ptr (kmeans->updated_centers, group, j);
		*x += val->f;
	      }

	    long *n = gsl_vector_long_ptr (kmeans->num_elements_groups, group);
	    *n += qc->wv ? case_data (c, qc->wv)->f : 1.0;
	    kmeans->n++;
	  }
	casereader_destroy (cs);


	/* Divide the cluster sums by the number of items in each cluster */
	for (g = 0; g < qc->ngroups; ++g)
	  {
	    for (j = 0; j < qc->n_vars; ++j)
	      {
		long n = gsl_vector_long_get (kmeans->num_elements_groups, g);
		double *x = gsl_matrix_ptr (kmeans->updated_centers, g, j);
		*x /= n ;
	      }
	  }

	double d = diff_matrix (kmeans->updated_centers, kmeans->centers);
	if (d < kmeans->convergence_criteria)
	  break;
      }

      if (qc->no_update)
	break;
    }
}

/* Reports centers of clusters.
   Initial parameter is optional for future use.
   If initial is true, initial cluster centers are reported.  Otherwise,
   resulted centers are reported. */
static void
quick_cluster_show_centers (struct Kmeans *kmeans, bool initial, const struct qc *qc)
{
  struct tab_table *t;
  int nc, nr, currow;
  int i, j;
  nc = qc->ngroups + 1;
  nr = qc->n_vars + 4;
  t = tab_create (nc, nr);
  tab_headers (t, 0, nc - 1, 0, 1);
  currow = 0;
  if (!initial)
    {
      tab_title (t, _("Final Cluster Centers"));
    }
  else
    {
      tab_title (t, _("Initial Cluster Centers"));
    }
  tab_box (t, TAL_2, TAL_2, TAL_0, TAL_1, 0, 0, nc - 1, nr - 1);
  tab_joint_text (t, 1, 0, nc - 1, 0, TAB_CENTER, _("Cluster"));
  tab_hline (t, TAL_1, 1, nc - 1, 2);
  currow += 2;

  for (i = 0; i < qc->ngroups; i++)
    {
      tab_text_format (t, (i + 1), currow, TAB_CENTER, "%d", (i + 1));
    }
  currow++;
  tab_hline (t, TAL_1, 1, nc - 1, currow);
  currow++;
  for (i = 0; i < qc->n_vars; i++)
    {
      tab_text (t, 0, currow + i, TAB_LEFT,
		var_to_string (qc->vars[i]));
    }

  for (i = 0; i < qc->ngroups; i++)
    {
      for (j = 0; j < qc->n_vars; j++)
	{
	  if (!initial)
	    {
	      tab_double (t, i + 1, j + 4, TAB_CENTER,
			  gsl_matrix_get (kmeans->centers,
					  kmeans->group_order->data[i], j),
			  var_get_print_format (qc->vars[j]), RC_OTHER);
	    }
	  else
	    {
	      tab_double (t, i + 1, j + 4, TAB_CENTER,
			  gsl_matrix_get (kmeans->initial_centers,
					  kmeans->group_order->data[i], j),
			  var_get_print_format (qc->vars[j]), RC_OTHER);
	    }
	}
    }
  tab_submit (t);
}

/* Reports cluster membership for each case. */
static void
quick_cluster_show_membership (struct Kmeans *kmeans, const struct casereader *reader, const struct qc *qc)
{
  struct tab_table *t;
  int nc, nr, i;

  struct ccase *c;
  struct casereader *cs = casereader_clone (reader);
  nc = 2;
  nr = kmeans->n + 1;
  t = tab_create (nc, nr);
  tab_headers (t, 0, nc - 1, 0, 0);
  tab_title (t, _("Cluster Membership"));
  tab_text (t, 0, 0, TAB_CENTER, _("Case Number"));
  tab_text (t, 1, 0, TAB_CENTER, _("Cluster"));
  tab_box (t, TAL_2, TAL_2, TAL_0, TAL_1, 0, 0, nc - 1, nr - 1);
  tab_hline (t, TAL_1, 0, nc - 1, 1);

  gsl_permutation *ip = gsl_permutation_alloc (qc->ngroups);
  gsl_permutation_inverse (ip, kmeans->group_order);

  for (i = 0; (c = casereader_read (cs)) != NULL; i++, case_unref (c))
    {
      int clust = -1; 
      assert (i < kmeans->n);
      kmeans_get_nearest_group (kmeans, c, qc, &clust, NULL, NULL, NULL);
      clust = ip->data[clust];
      tab_text_format (t, 0, i+1, TAB_CENTER, "%d", (i + 1));
      tab_text_format (t, 1, i+1, TAB_CENTER, "%d", (clust + 1));
    }
  gsl_permutation_free (ip);
  assert (i == kmeans->n);
  tab_submit (t);
  casereader_destroy (cs);
}


/* Reports number of cases of each single cluster. */
static void
quick_cluster_show_number_cases (struct Kmeans *kmeans, const struct qc *qc)
{
  struct tab_table *t;
  int nc, nr;
  int i, numelem;
  long int total;
  nc = 3;
  nr = qc->ngroups + 1;
  t = tab_create (nc, nr);
  tab_headers (t, 0, nc - 1, 0, 0);
  tab_title (t, _("Number of Cases in each Cluster"));
  tab_box (t, TAL_2, TAL_2, TAL_0, TAL_1, 0, 0, nc - 1, nr - 1);
  tab_text (t, 0, 0, TAB_LEFT, _("Cluster"));

  total = 0;
  for (i = 0; i < qc->ngroups; i++)
    {
      tab_text_format (t, 1, i, TAB_CENTER, "%d", (i + 1));
      numelem =
	kmeans->num_elements_groups->data[kmeans->group_order->data[i]];
      tab_text_format (t, 2, i, TAB_CENTER, "%d", numelem);
      total += numelem;
    }

  tab_text (t, 0, qc->ngroups, TAB_LEFT, _("Valid"));
  tab_text_format (t, 2, qc->ngroups, TAB_LEFT, "%ld", total);
  tab_submit (t);
}

/* Reports. */
static void
quick_cluster_show_results (struct Kmeans *kmeans, const struct casereader *reader, const struct qc *qc)
{
  kmeans_order_groups (kmeans, qc); /* what does this do? */
  
  if( qc->print_initial_clusters )
    quick_cluster_show_centers (kmeans, true, qc);
  quick_cluster_show_centers (kmeans, false, qc);
  quick_cluster_show_number_cases (kmeans, qc);
  if( qc->print_cluster_membership )
    quick_cluster_show_membership(kmeans, reader, qc);
}

int
cmd_quick_cluster (struct lexer *lexer, struct dataset *ds)
{
  struct qc qc;
  struct Kmeans *kmeans;
  bool ok;
  const struct dictionary *dict = dataset_dict (ds);
  qc.ngroups = 2;
  qc.maxiter = 10;
  qc.epsilon = DBL_EPSILON;
  qc.missing_type = MISS_LISTWISE;
  qc.exclude = MV_ANY;
  qc.print_cluster_membership = false; /* default = do not output case cluster membership */
  qc.print_initial_clusters = false;   /* default = do not print initial clusters */
  qc.no_initial = false;               /* default = use well separated initial clusters */
  qc.no_update = false;               /* default = iterate until convergence or max iterations */

  if (!parse_variables_const (lexer, dict, &qc.vars, &qc.n_vars,
			      PV_NO_DUPLICATE | PV_NUMERIC))
    {
      return (CMD_FAILURE);
    }

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "MISSING"))
	{
	  lex_match (lexer, T_EQUALS);
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "LISTWISE") || lex_match_id (lexer, "DEFAULT"))
		{
		  qc.missing_type = MISS_LISTWISE;
		}
	      else if (lex_match_id (lexer, "PAIRWISE"))
		{
		  qc.missing_type = MISS_PAIRWISE;
		}
	      else if (lex_match_id (lexer, "INCLUDE"))
		{
		  qc.exclude = MV_SYSTEM;
		}
	      else if (lex_match_id (lexer, "EXCLUDE"))
		{
		  qc.exclude = MV_ANY;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }	  
	}
      else if (lex_match_id (lexer, "PRINT"))
	{
	  lex_match (lexer, T_EQUALS);
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "CLUSTER"))
                qc.print_cluster_membership = true;
	      else if (lex_match_id (lexer, "INITIAL"))
	        qc.print_initial_clusters = true;
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "CRITERIA"))
	{
	  lex_match (lexer, T_EQUALS);
	  while (lex_token (lexer) != T_ENDCMD
		 && lex_token (lexer) != T_SLASH)
	    {
	      if (lex_match_id (lexer, "CLUSTERS"))
		{
		  if (lex_force_match (lexer, T_LPAREN) &&
		      lex_force_int (lexer))
		    {
		      qc.ngroups = lex_integer (lexer);
		      if (qc.ngroups <= 0)
			{
			  lex_error (lexer, _("The number of clusters must be positive"));
			  goto error;
			}
		      lex_get (lexer);
		      if (!lex_force_match (lexer, T_RPAREN))
			goto error;
		    }
		}
	      else if (lex_match_id (lexer, "CONVERGE"))
		{
		  if (lex_force_match (lexer, T_LPAREN) &&
		      lex_force_num (lexer))
		    {
		      qc.epsilon = lex_number (lexer);
		      if (qc.epsilon <= 0)
			{
			  lex_error (lexer, _("The convergence criterium must be positive"));
			  goto error;
			}
		      lex_get (lexer);
		      if (!lex_force_match (lexer, T_RPAREN))
			goto error;
		    }
		}
	      else if (lex_match_id (lexer, "MXITER"))
		{
		  if (lex_force_match (lexer, T_LPAREN) &&
		      lex_force_int (lexer))
		    {
		      qc.maxiter = lex_integer (lexer);
		      if (qc.maxiter <= 0)
			{
			  lex_error (lexer, _("The number of iterations must be positive"));
			  goto error;
			}
		      lex_get (lexer);
		      if (!lex_force_match (lexer, T_RPAREN))
			goto error;
		    }
		}
	      else if (lex_match_id (lexer, "NOINITIAL"))
		{
		  qc.no_initial = true;
		}
	      else if (lex_match_id (lexer, "NOUPDATE"))
		{
		  qc.no_update = true;
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

  qc.wv = dict_get_weight (dict);

  {
    struct casereader *group;
    struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds), dict);

    while (casegrouper_get_next_group (grouper, &group))
      {
	if ( qc.missing_type == MISS_LISTWISE )
	  {
	    group  = casereader_create_filter_missing (group, qc.vars, qc.n_vars,
						       qc.exclude,
						       NULL,  NULL);
	  }

	kmeans = kmeans_create (&qc);
	kmeans_cluster (kmeans, group, &qc);
	quick_cluster_show_results (kmeans, group, &qc);
	kmeans_destroy (kmeans);
	casereader_destroy (group);
      }
    ok = casegrouper_destroy (grouper);
  }
  ok = proc_commit (ds) && ok;

  free (qc.vars);

  return (ok);

 error:
  free (qc.vars);
  return CMD_FAILURE;
}
