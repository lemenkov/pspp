/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012, 2015, 2019 Free Software Foundation, Inc.

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
#include "output/pivot-table.h"
#include "output/output-item.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum missing_type
  {
    MISS_LISTWISE,
    MISS_PAIRWISE,
  };


struct save_trans_data
  {
    /* A writer which contains the values (if any) to be appended to
       each case in the active dataset   */
    struct casewriter *writer;

    /* A reader created from the writer above. */
    struct casereader *appending_reader;

    /* The indices to be used to access values in the above,
       reader/writer  */
    int membership_case_idx;
    int distance_case_idx;

    /* The variables created to hold the values appended to the dataset  */
    struct variable *membership;
    struct variable *distance;
  };


struct qc
  {
    struct dataset *dataset;
    struct dictionary *dict;

    const struct variable **vars;
    size_t n_vars;

    double epsilon;               /* The convergence criterion */

    int ngroups;                        /* Number of group. (Given by the user) */
    int maxiter;                        /* Maximum iterations (Given by the user) */
    bool print_cluster_membership; /* true => print membership */
    bool print_initial_clusters;   /* true => print initial cluster */
    bool initial;             /* false => simplified initial cluster selection */
    bool update;               /* false => do not iterate  */

    const struct variable *wv;        /* Weighting variable. */

    enum missing_type missing_type;
    enum mv_class exclude;

    /* Which values are to be saved?  */
    bool save_membership;
    bool save_distance;

    /* The name of the new variable to contain the cluster of each case.  */
    char *var_membership;

    /* The name of the new variable to contain the distance of each case
       from its cluster centre.  */
    char *var_distance;

    struct save_trans_data *save_trans_data;
  };

/* Holds all of the information for the functions.  int n, holds the number of
   observation and its default value is -1.  We set it in
   kmeans_recalculate_centers in first invocation. */
struct Kmeans
  {
    gsl_matrix *centers;                /* Centers for groups. */
    gsl_matrix *updated_centers;
    casenumber n;

    gsl_vector_long *num_elements_groups;

    gsl_matrix *initial_centers;        /* Initial random centers. */
    double convergence_criteria;
    gsl_permutation *group_order;        /* Group order for reporting. */
  };

static struct Kmeans *kmeans_create (const struct qc *);

static void kmeans_get_nearest_group (const struct Kmeans *,
                                      struct ccase *, const struct qc *,
                                      int *, double *, int *, double *);

static void kmeans_order_groups (struct Kmeans *, const struct qc *);

static void kmeans_cluster (struct Kmeans *, struct casereader *,
                            const struct qc *);

static void quick_cluster_show_centers (struct Kmeans *, bool initial,
                                        const struct qc *);

static void quick_cluster_show_membership (struct Kmeans *,
                                           const struct casereader *,
                                           struct qc *);

static void quick_cluster_show_number_cases (struct Kmeans *,
                                             const struct qc *);

static void quick_cluster_show_results (struct Kmeans *,
                                        const struct casereader *,
                                        struct qc *);

int cmd_quick_cluster (struct lexer *, struct dataset *);

static void kmeans_destroy (struct Kmeans *);

/* Creates and returns a struct of Kmeans with given casereader 'cs', parsed
   variables 'variables', number of cases 'n', number of variables 'm', number
   of clusters and amount of maximum iterations. */
static struct Kmeans *
kmeans_create (const struct qc *qc)
{
  struct Kmeans *kmeans = xmalloc (sizeof *kmeans);
  *kmeans = (struct Kmeans) {
    .centers = gsl_matrix_alloc (qc->ngroups, qc->n_vars),
    .updated_centers = gsl_matrix_alloc (qc->ngroups, qc->n_vars),
    .num_elements_groups = gsl_vector_long_alloc (qc->ngroups),
    .group_order = gsl_permutation_alloc (qc->ngroups),
  };
  return kmeans;
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
  double max_diff = -INFINITY;
  for (size_t i = 0; i < m1->size1; ++i)
    {
      double diff = 0;
      for (size_t j = 0; j < m1->size2; ++j)
        diff += pow2 (gsl_matrix_get (m1,i,j) - gsl_matrix_get (m2,i,j));
      if (diff > max_diff)
        max_diff = diff;
    }

  return max_diff;
}



static double
matrix_mindist (const gsl_matrix *m, int *mn, int *mm)
{
  double mindist = INFINITY;
  for (size_t i = 0; i + 1 < m->size1; ++i)
    for (size_t j = i + 1; j < m->size1; ++j)
      {
        double diff_sq = 0;
        for (size_t k = 0; k < m->size2; ++k)
          diff_sq += pow2 (gsl_matrix_get (m, j, k) - gsl_matrix_get (m, i, k));
        if (diff_sq < mindist)
          {
            mindist = diff_sq;
            if (mn)
              *mn = i;
            if (mm)
              *mm = j;
          }
      }
  return mindist;
}

/* Return the distance of C from the group whose index is WHICH */
static double
dist_from_case (const struct Kmeans *kmeans, const struct ccase *c,
                const struct qc *qc, int which)
{
  double dist = 0;
  for (size_t j = 0; j < qc->n_vars; j++)
    {
      const union value *val = case_data (c, qc->vars[j]);
      assert (!(var_is_value_missing (qc->vars[j], val) & qc->exclude));
      dist += pow2 (gsl_matrix_get (kmeans->centers, which, j) - val->f);
    }

  return dist;
}

/* Return the minimum distance of the group WHICH and all other groups */
static double
min_dist_from (const struct Kmeans *kmeans, const struct qc *qc, int which)
{
   double mindist = INFINITY;
  for (size_t i = 0; i < qc->ngroups; i++)
    {
      if (i == which)
        continue;

      double dist = 0;
      for (size_t j = 0; j < qc->n_vars; j++)
        dist += pow2 (gsl_matrix_get (kmeans->centers, i, j)
                      - gsl_matrix_get (kmeans->centers, which, j));

      if (dist < mindist)
        mindist = dist;
    }

  return mindist;
}

/* Calculate the initial cluster centers. */
static void
kmeans_initial_centers (struct Kmeans *kmeans,
                        const struct casereader *reader,
                        const struct qc *qc)
{
  int nc = 0;

  struct casereader *cs = casereader_clone (reader);
  struct ccase *c;
  for (; (c = casereader_read (cs)) != NULL; case_unref (c))
    {
      bool missing = false;
      for (size_t j = 0; j < qc->n_vars; ++j)
        {
          const union value *val = case_data (c, qc->vars[j]);
          if (var_is_value_missing (qc->vars[j], val) & qc->exclude)
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

      if (qc->initial)
        {
          int mn, mm;
          double m = matrix_mindist (kmeans->centers, &mn, &mm);

          int mq, mp;
          double delta;
          kmeans_get_nearest_group (kmeans, c, qc, &mq, &delta, &mp, NULL);
          if (delta > m)
            /* If the distance between C and the nearest group, is greater than the distance
               between the two  groups which are clostest to each
               other, then one group must be replaced.  */
            {
              /* Out of mn and mm, which is the clostest of the two groups to C ? */
              int which = (dist_from_case (kmeans, c, qc, mn)
                           > dist_from_case (kmeans, c, qc, mm)) ? mm : mn;

              for (size_t j = 0; j < qc->n_vars; ++j)
                {
                  const union value *val = case_data (c, qc->vars[j]);
                  gsl_matrix_set (kmeans->centers, which, j, val->f);
                }
            }
          else if (dist_from_case (kmeans, c, qc, mp) > min_dist_from (kmeans, qc, mq))
            /* If the distance between C and the second nearest group
               (MP) is greater than the smallest distance between the
               nearest group (MQ) and any other group, then replace
               MQ with C.  */
            {
              for (size_t j = 0; j < qc->n_vars; ++j)
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
kmeans_get_nearest_group (const struct Kmeans *kmeans, struct ccase *c,
                          const struct qc *qc, int *g_q, double *delta_q,
                          int *g_p, double *delta_p)
{
  int result0 = -1;
  int result1 = -1;
  double mindist0 = INFINITY;
  double mindist1 = INFINITY;
  for (size_t i = 0; i < qc->ngroups; i++)
    {
      double dist = 0;
      for (size_t j = 0; j < qc->n_vars; j++)
        {
          const union value *val = case_data (c, qc->vars[j]);
          if (var_is_value_missing (qc->vars[j], val) & qc->exclude)
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
kmeans_cluster (struct Kmeans *kmeans, struct casereader *reader,
                const struct qc *qc)
{
  kmeans_initial_centers (kmeans, reader, qc);

  gsl_matrix_memcpy (kmeans->updated_centers, kmeans->centers);
  for (int xx = 0; xx < qc->maxiter; ++xx)
    {
      gsl_vector_long_set_all (kmeans->num_elements_groups, 0.0);

      kmeans->n = 0;
      if (qc->update)
        {
          struct casereader *r = casereader_clone (reader);
          struct ccase *c;
          for (; (c = casereader_read (r)) != NULL; case_unref (c))
            {
              bool missing = false;
              for (size_t j = 0; j < qc->n_vars; j++)
                {
                  const union value *val = case_data (c, qc->vars[j]);
                  if (var_is_value_missing (qc->vars[j], val) & qc->exclude)
                    missing = true;
                }
              if (missing)
                continue;

              double mindist = INFINITY;
              int group = -1;
              for (size_t g = 0; g < qc->ngroups; ++g)
                {
                  double d = dist_from_case (kmeans, c, qc, g);

                  if (d < mindist)
                    {
                      mindist = d;
                      group = g;
                    }
                }

              long *n = gsl_vector_long_ptr (kmeans->num_elements_groups, group);
              *n += qc->wv ? case_num (c, qc->wv) : 1.0;
              kmeans->n++;

              for (size_t j = 0; j < qc->n_vars; ++j)
                {
                  const union value *val = case_data (c, qc->vars[j]);
                  if (var_is_value_missing (qc->vars[j], val) & qc->exclude)
                    continue;
                  double *x = gsl_matrix_ptr (kmeans->updated_centers, group, j);
                  *x += val->f * (qc->wv ? case_num (c, qc->wv) : 1.0);
                }
            }

          casereader_destroy (r);
        }

      /* Divide the cluster sums by the number of items in each cluster */
      for (size_t g = 0; g < qc->ngroups; ++g)
        for (size_t j = 0; j < qc->n_vars; ++j)
          {
            long n = gsl_vector_long_get (kmeans->num_elements_groups, g);
            double *x = gsl_matrix_ptr (kmeans->updated_centers, g, j);
            *x /= n + 1;  // Plus 1 for the initial centers
          }
      gsl_matrix_memcpy (kmeans->centers, kmeans->updated_centers);

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

          for (size_t j = 0; j < qc->n_vars; ++j)
            {
              const union value *val = case_data (c, qc->vars[j]);
              if (var_is_value_missing (qc->vars[j], val) & qc->exclude)
                continue;

              double *x = gsl_matrix_ptr (kmeans->updated_centers, group, j);
              *x += val->f;
            }

          long *n = gsl_vector_long_ptr (kmeans->num_elements_groups, group);
          *n += qc->wv ? case_num (c, qc->wv) : 1.0;
          kmeans->n++;
        }
      casereader_destroy (cs);

      /* Divide the cluster sums by the number of items in each cluster */
      for (size_t g = 0; g < qc->ngroups; ++g)
        for (size_t j = 0; j < qc->n_vars; ++j)
          {
            long n = gsl_vector_long_get (kmeans->num_elements_groups, g);
            double *x = gsl_matrix_ptr (kmeans->updated_centers, g, j);
            *x /= n;
          }

      double d = diff_matrix (kmeans->updated_centers, kmeans->centers);
      if (d < kmeans->convergence_criteria)
        break;

      if (!qc->update)
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
  struct pivot_table *table
    = pivot_table_create (initial
                          ? N_("Initial Cluster Centers")
                          : N_("Final Cluster Centers"));

  struct pivot_dimension *clusters
    = pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Cluster"));

  clusters->root->show_label = true;
  for (size_t i = 0; i < qc->ngroups; i++)
    pivot_category_create_leaf (clusters->root,
                                pivot_value_new_integer (i + 1));

  struct pivot_dimension *variables
    = pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Variable"));

  for (size_t i = 0; i < qc->n_vars; i++)
    pivot_category_create_leaf (variables->root,
                                pivot_value_new_variable (qc->vars[i]));

  const gsl_matrix *matrix = (initial
                              ? kmeans->initial_centers
                              : kmeans->centers);
  for (size_t i = 0; i < qc->ngroups; i++)
    for (size_t j = 0; j < qc->n_vars; j++)
      {
        double x = gsl_matrix_get (matrix, kmeans->group_order->data[i], j);
        union value v = { .f = x };
        pivot_table_put2 (table, i, j,
                          pivot_value_new_var_value (qc->vars[j], &v));
      }

  pivot_table_submit (table);
}


/* A transformation function which juxtaposes the dataset with the
   (pre-prepared) dataset containing membership and/or distance
   values.  */
static enum trns_result
save_trans_func (void *aux, struct ccase **c, casenumber x UNUSED)
{
  const struct save_trans_data *std = aux;
  struct ccase *ca  = casereader_read (std->appending_reader);
  if (ca == NULL)
    return TRNS_CONTINUE;

  *c = case_unshare (*c);

  if (std->membership_case_idx >= 0)
    *case_num_rw (*c, std->membership) = case_num_idx (ca, std->membership_case_idx);

  if (std->distance_case_idx >= 0)
    *case_num_rw (*c, std->distance) = case_num_idx (ca, std->distance_case_idx);

  case_unref (ca);

  return TRNS_CONTINUE;
}

/* Free the resources of the transformation.  */
static bool
save_trans_destroy (void *aux)
{
  struct save_trans_data *std = aux;
  casereader_destroy (std->appending_reader);
  free (std);
  return true;
}

/* Reports cluster membership for each case, and is requested saves the
   membership and the distance of the case from the cluster centre.  */
static void
quick_cluster_show_membership (struct Kmeans *kmeans,
                               const struct casereader *reader,
                               struct qc *qc)
{
  struct pivot_table *table = NULL;
  struct pivot_dimension *cases = NULL;
  if (qc->print_cluster_membership)
    {
      table = pivot_table_create (N_("Cluster Membership"));

      pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Cluster"),
                              N_("Cluster"));

      cases
        = pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Case Number"));

      cases->root->show_label = true;
    }

  gsl_permutation *ip = gsl_permutation_alloc (qc->ngroups);
  gsl_permutation_inverse (ip, kmeans->group_order);

  struct caseproto *proto = caseproto_create ();
  if (qc->save_membership || qc->save_distance)
    {
      /* Prepare data which may potentially be used in a
         transformation appending new variables to the active
         dataset.  */
      int idx = 0;
      int membership_case_idx = -1;
      if (qc->save_membership)
        {
          proto = caseproto_add_width (proto, 0);
          membership_case_idx = idx++;
        }

      int distance_case_idx = -1;
      if (qc->save_distance)
        {
          proto = caseproto_add_width (proto, 0);
          distance_case_idx = idx++;
        }

      qc->save_trans_data = xmalloc (sizeof *qc->save_trans_data);
      *qc->save_trans_data = (struct save_trans_data) {
        .membership_case_idx = membership_case_idx,
        .distance_case_idx = distance_case_idx,
        .writer = autopaging_writer_create (proto),
      };
    }

  struct casereader *cs = casereader_clone (reader);
  struct ccase *c;
  for (int i = 0; (c = casereader_read (cs)) != NULL; i++, case_unref (c))
    {
      assert (i < kmeans->n);
      int clust;
      kmeans_get_nearest_group (kmeans, c, qc, &clust, NULL, NULL, NULL);
      int cluster = ip->data[clust];

      if (qc->save_trans_data)
        {
          /* Calculate the membership and distance values.  */
          struct ccase *outc = case_create (proto);
          if (qc->save_membership)
            *case_num_rw_idx (outc, qc->save_trans_data->membership_case_idx) = cluster + 1;

          if (qc->save_distance)
            *case_num_rw_idx (outc, qc->save_trans_data->distance_case_idx)
              = sqrt (dist_from_case (kmeans, c, qc, clust));

          casewriter_write (qc->save_trans_data->writer, outc);
        }

      if (qc->print_cluster_membership)
        {
          /* Print the cluster membership to the table.  */
          int case_idx = pivot_category_create_leaf (cases->root,
                                                 pivot_value_new_integer (i + 1));
          pivot_table_put2 (table, 0, case_idx,
                            pivot_value_new_integer (cluster + 1));
        }
    }

  caseproto_unref (proto);
  gsl_permutation_free (ip);

  if (qc->print_cluster_membership)
    pivot_table_submit (table);
  casereader_destroy (cs);
}


/* Reports number of cases of each single cluster. */
static void
quick_cluster_show_number_cases (struct Kmeans *kmeans, const struct qc *qc)
{
  struct pivot_table *table
    = pivot_table_create (N_("Number of Cases in each Cluster"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"),
                          N_("Count"));

  struct pivot_dimension *clusters
    = pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Clusters"));

  struct pivot_category *group
    = pivot_category_create_group (clusters->root, N_("Cluster"));

  long int total = 0;
  for (int i = 0; i < qc->ngroups; i++)
    {
      int cluster_idx
        = pivot_category_create_leaf (group, pivot_value_new_integer (i + 1));
      int count = kmeans->num_elements_groups->data [kmeans->group_order->data[i]];
      pivot_table_put2 (table, 0, cluster_idx, pivot_value_new_integer (count));
      total += count;
    }

  int cluster_idx = pivot_category_create_leaf (clusters->root,
                                                pivot_value_new_text (N_("Valid")));
  pivot_table_put2 (table, 0, cluster_idx, pivot_value_new_integer (total));
  pivot_table_submit (table);
}

/* Reports. */
static void
quick_cluster_show_results (struct Kmeans *kmeans, const struct casereader *reader,
                            struct qc *qc)
{
  kmeans_order_groups (kmeans, qc); /* what does this do? */

  if (qc->print_initial_clusters)
    quick_cluster_show_centers (kmeans, true, qc);
  quick_cluster_show_centers (kmeans, false, qc);
  quick_cluster_show_number_cases (kmeans, qc);

  quick_cluster_show_membership (kmeans, reader, qc);
}

/* Parse the QUICK CLUSTER command and populate QC accordingly.
   Returns false on error.  */
static bool
quick_cluster_parse (struct lexer *lexer, struct qc *qc)
{
  if (!parse_variables_const (lexer, qc->dict, &qc->vars, &qc->n_vars,
                              PV_NO_DUPLICATE | PV_NUMERIC))
    return false;

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "LISTWISE")
                  || lex_match_id (lexer, "DEFAULT"))
                qc->missing_type = MISS_LISTWISE;
              else if (lex_match_id (lexer, "PAIRWISE"))
                qc->missing_type = MISS_PAIRWISE;
              else if (lex_match_id (lexer, "INCLUDE"))
                qc->exclude = MV_SYSTEM;
              else if (lex_match_id (lexer, "EXCLUDE"))
                qc->exclude = MV_ANY;
              else
                {
                  lex_error_expecting (lexer, "LISTWISE", "DEFAULT",
                                       "PAIRWISE", "INCLUDE", "EXCLUDE");
                  return false;
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
                qc->print_cluster_membership = true;
              else if (lex_match_id (lexer, "INITIAL"))
                qc->print_initial_clusters = true;
              else
                {
                  lex_error_expecting (lexer, "CLUSTER", "INITIAL");
                  return false;
                }
            }
        }
      else if (lex_match_id (lexer, "SAVE"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH)
            {
              if (lex_match_id (lexer, "CLUSTER"))
                {
                  qc->save_membership = true;
                  if (lex_match (lexer, T_LPAREN))
                    {
                      if (!lex_force_id (lexer))
                        return false;

                      free (qc->var_membership);
                      qc->var_membership = xstrdup (lex_tokcstr (lexer));
                      if (NULL != dict_lookup_var (qc->dict, qc->var_membership))
                        {
                          lex_error (lexer,
                                     _("A variable called `%s' already exists."),
                                     qc->var_membership);
                          free (qc->var_membership);
                          qc->var_membership = NULL;
                          return false;
                        }

                      lex_get (lexer);

                      if (!lex_force_match (lexer, T_RPAREN))
                        return false;
                    }
                }
              else if (lex_match_id (lexer, "DISTANCE"))
                {
                  qc->save_distance = true;
                  if (lex_match (lexer, T_LPAREN))
                    {
                      if (!lex_force_id (lexer))
                        return false;

                      free (qc->var_distance);
                      qc->var_distance = xstrdup (lex_tokcstr (lexer));
                      if (NULL != dict_lookup_var (qc->dict, qc->var_distance))
                        {
                          lex_error (lexer,
                                     _("A variable called `%s' already exists."),
                                     qc->var_distance);
                          free (qc->var_distance);
                          qc->var_distance = NULL;
                          return false;
                        }

                      lex_get (lexer);

                      if (!lex_force_match (lexer, T_RPAREN))
                        return false;
                    }
                }
              else
                {
                  lex_error_expecting (lexer, "CLUSTER", "DISTANCE");
                  return false;
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
                  if (!lex_force_match (lexer, T_LPAREN)
                      || !lex_force_int_range (lexer, "CLUSTERS", 1, INT_MAX))
                    return false;
                  qc->ngroups = lex_integer (lexer);
                  lex_get (lexer);
                  if (!lex_force_match (lexer, T_RPAREN))
                    return false;
                }
              else if (lex_match_id (lexer, "CONVERGE"))
                {
                  if (!lex_force_match (lexer, T_LPAREN)
                      || !lex_force_num_range_open (lexer, "CONVERGE",
                                                    0, DBL_MAX))
                    return false;
                  qc->epsilon = lex_number (lexer);
                  lex_get (lexer);
                  if (!lex_force_match (lexer, T_RPAREN))
                    return false;
                }
              else if (lex_match_id (lexer, "MXITER"))
                {
                  if (!lex_force_match (lexer, T_LPAREN)
                      || !lex_force_int_range (lexer, "MXITER", 1, INT_MAX))
                    return false;
                  qc->maxiter = lex_integer (lexer);
                  lex_get (lexer);
                  if (!lex_force_match (lexer, T_RPAREN))
                    return false;
                }
              else if (lex_match_id (lexer, "NOINITIAL"))
                qc->initial = false;
              else if (lex_match_id (lexer, "NOUPDATE"))
                qc->update = false;
              else
                {
                  lex_error_expecting (lexer, "CLUSTERS", "CONVERGE", "MXITER",
                                       "NOINITIAL", "NOUPDATE");
                  return false;
                }
            }
        }
      else
        {
          lex_error_expecting (lexer, "MISSING", "PRINT", "SAVE", "CRITERIA");
          return false;
        }
    }
  return true;
}

int
cmd_quick_cluster (struct lexer *lexer, struct dataset *ds)
{
  struct qc qc = {
    .dataset = ds,
    .dict = dataset_dict (ds),
    .ngroups = 2,
    .maxiter = 10,
    .epsilon = DBL_EPSILON,
    .missing_type = MISS_LISTWISE,
    .exclude = MV_ANY,
    .initial = true,
    .update = true,
  };

  if (!quick_cluster_parse (lexer, &qc))
    goto error;

  qc.wv = dict_get_weight (qc.dict);

  struct casegrouper *grouper = casegrouper_create_splits (proc_open (ds), qc.dict);
  struct casereader *group;
  while (casegrouper_get_next_group (grouper, &group))
    {
      if (qc.missing_type == MISS_LISTWISE)
        group = casereader_create_filter_missing (group, qc.vars, qc.n_vars,
                                                  qc.exclude, NULL, NULL);

      struct Kmeans *kmeans = kmeans_create (&qc);
      kmeans_cluster (kmeans, group, &qc);
      quick_cluster_show_results (kmeans, group, &qc);
      kmeans_destroy (kmeans);
      casereader_destroy (group);
    }
  bool ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  /* If requested, set a transformation to append the cluster and
     distance values to the current dataset.  */
  if (qc.save_trans_data)
    {
      struct save_trans_data *std = qc.save_trans_data;

      std->appending_reader = casewriter_make_reader (std->writer);

      if (qc.save_membership)
        {
          /* Invent a variable name if necessary.  */
          int idx = 0;
          struct string name;
          ds_init_empty (&name);
          while (qc.var_membership == NULL)
            {
              ds_clear (&name);
              ds_put_format (&name, "QCL_%d", idx++);

              if (!dict_lookup_var (qc.dict, ds_cstr (&name)))
                {
                  qc.var_membership = strdup (ds_cstr (&name));
                  break;
                }
            }
          ds_destroy (&name);

          std->membership = dict_create_var_assert (qc.dict, qc.var_membership, 0);
        }

      if (qc.save_distance)
        {
          /* Invent a variable name if necessary.  */
          int idx = 0;
          struct string name;
          ds_init_empty (&name);
          while (qc.var_distance == NULL)
            {
              ds_clear (&name);
              ds_put_format (&name, "QCL_%d", idx++);

              if (!dict_lookup_var (qc.dict, ds_cstr (&name)))
                {
                  qc.var_distance = strdup (ds_cstr (&name));
                  break;
                }
            }
          ds_destroy (&name);

          std->distance = dict_create_var_assert (qc.dict, qc.var_distance, 0);
        }

      static const struct trns_class trns_class = {
        .name = "QUICK CLUSTER",
        .execute = save_trans_func,
        .destroy = save_trans_destroy,
      };
      add_transformation (qc.dataset, &trns_class, std);
    }

  free (qc.var_distance);
  free (qc.var_membership);
  free (qc.vars);
  return ok;

 error:
  free (qc.var_distance);
  free (qc.var_membership);
  free (qc.vars);
  return CMD_FAILURE;
}
