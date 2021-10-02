/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2012, 2014 Free Software Foundation, Inc.

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

#include "math/categoricals.h"
#include "math/interaction.h"

#include <float.h>
#include <stdio.h>

#include "data/case.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/array.h"
#include "libpspp/hmap.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/hash-functions.h"

#include "gl/xalloc.h"

#define CATEGORICALS_DEBUG 0

struct value_node
{
  struct hmap_node node;      /* Node in hash map. */
  union value val;            /* The value */
  int index;                  /* A zero based unique index for this value */
};

static struct value_node *
lookup_value (const struct hmap *map, const union value *val,
              unsigned int hash, int width)
{
  struct value_node *vn;
  HMAP_FOR_EACH_WITH_HASH (vn, struct value_node, node, hash, map)
    if (value_equal (&vn->val, val, width))
      return vn;
  return NULL;
}

/* A variable used in a categoricals object.  */
struct variable_node
  {
    struct hmap_node node;      /* In struct categorical's 'varmap'. */
    const struct variable *var; /* The variable. */
    struct hmap valmap;         /* Contains "struct value_node"s. */
    union value *values;        /* Values in valmap, as a sorted array. */
  };

static int
compare_value_node_3way (const void *vn1_, const void *vn2_, const void *aux)
{
  const struct value_node *const *vn1p = vn1_;
  const struct value_node *const *vn2p = vn2_;

  const struct variable_node *vn = aux;

  return value_compare_3way (&(*vn1p)->val, &(*vn2p)->val,
                             var_get_width (vn->var));
}

static struct variable_node *
lookup_variable (const struct hmap *map, const struct variable *var)
{
  struct variable_node *vn;
  HMAP_FOR_EACH_WITH_HASH (vn, struct variable_node, node,
                           hash_pointer (var, 0), map)
    if (vn->var == var)
      return vn;
  return NULL;
}

struct interact_params
{
  /* The interaction, and an array with iact->n_vars elements such that
     varnodes[x] points to the variable_node for iact->vars[x]. */
  const struct interaction *iact;
  struct variable_node **varnodes;

  /* An example of each interaction that appears in the data, like a frequency
     table for 'iact'.  By construction, the number of elements must be less
     than or equal to 'n_cats'.

     categoricals_update() updates 'ivmap' case-by-case, then
     categoricals_done() dumps 'ivmap' into 'ivs' and sorts it. */
  struct hmap ivmap;            /* Contains "struct interaction_value"s. */
  struct interaction_value **ivs;

  int base_df;
  int base_cats;

  /* Product of hmap_count(&varnodes[*]->valmap), that is, the maximum number
     of distinct values of this interaction. */
  int n_cats;

  /* Product of degrees of freedom of all the variables. */
  int df_prod;

  double *enc_sum;

  /* Sum of ivs[*]->cc. */
  double cc;
};

struct interaction_value
  {
    struct hmap_node node;    /* In struct interact_params's ivmap. */
    struct ccase *ccase;      /* A case representative of the interaction. */
    double cc;                /* Total weight of cases for this interaction. */
    void *user_data;
  };

static int
compare_interaction_value_3way (const void *vn1_, const void *vn2_,
                                const void *aux)
{
  const struct interaction_value *const *vn1p = vn1_;
  const struct interaction_value *const *vn2p = vn2_;

  const struct interact_params *iap = aux;

  return interaction_case_cmp_3way (iap->iact, (*vn1p)->ccase, (*vn2p)->ccase);
}

struct categoricals
{
  /* The weight variable */
  const struct variable *wv;

  struct interact_params *iap;  /* Interaction parameters. */
  size_t n_iap;

  /* Contains a "struct variable_node" for each variable in 'iap'. */
  struct hmap varmap;

  /* A map to enable the lookup of variables indexed by subscript.
     This map considers only the N - 1 of the N variables.
  */
  int *df_to_iact; /* 'df_sum' elements. */
  size_t df_sum;

  /* Like the above, but uses all N variables. */
  int *cat_to_iact; /* 'n_cats_total' elements. */
  size_t n_cats_total;

  struct pool *pool;

  /* Missing values in the factor variables to be excluded */
  enum mv_class fctr_excl;

  const void *aux1;
  void *aux2;

  bool sane;

  const struct payload *payload;
};


bool
categoricals_isbalanced (const struct categoricals *cat)
{
  for (int i = 0 ; i < cat->n_iap; ++i)
    {
      int v;
      const struct interact_params *iap = &cat->iap[i];

      double oval = -1.0;
      for (v = 0; v < hmap_count (&iap->ivmap); ++v)
        {
          const struct interaction_value *iv = iap->ivs[v];
          if (oval == -1.0)
            oval = iv->cc;
          if (oval != iv->cc)
            return false;
        }
    }
  return true;
}


static void
categoricals_dump (const struct categoricals *cat)
{
  if (CATEGORICALS_DEBUG)
    {
      int i;

      printf ("df to interaction map:\n");
      for (i = 0; i < cat->df_sum; ++i)
        printf (" %d", cat->df_to_iact[i]);
      printf ("\n");

      printf ("Category to interaction map:\n");
      for (i = 0; i < cat->n_cats_total; ++i)
        printf (" %d", cat->cat_to_iact[i]);
      printf ("\n");

      printf ("Number of interactions %zu\n", cat->n_iap);
      for (i = 0 ; i < cat->n_iap; ++i)
        {
          int v;
          struct string str;
          const struct interact_params *iap = &cat->iap[i];
          const struct interaction *iact = iap->iact;

          ds_init_empty (&str);
          interaction_to_string (iact, &str);

          printf ("\nInteraction: \"%s\" (number of categories: %d); ", ds_cstr (&str), iap->n_cats);
          ds_destroy (&str);
          printf ("Base index (df/categories): %d/%d\n", iap->base_df, iap->base_cats);

          printf ("\t(");
          for (v = 0; v < hmap_count (&iap->ivmap); ++v)
            {
              int vv;
              const struct interaction_value *iv = iap->ivs[v];

              if (v > 0)  printf ("   ");
              printf ("{");
              for (vv = 0; vv < iact->n_vars; ++vv)
                {
                  const struct variable *var = iact->vars[vv];
                  const union value *val = case_data (iv->ccase, var);
                  struct variable_node *vn = iap->varnodes[vv];
                  const int width = var_get_width (var);
                  unsigned int valhash = value_hash (val, width, 0);
                  struct value_node *valn = lookup_value (&vn->valmap, val, valhash, width);

                  assert (vn->var == var);

                  printf ("%.*g(%d)", DBL_DIG + 1, val->f, valn->index);
                  if (vv < iact->n_vars - 1)
                    printf (", ");
                }
              printf ("}");
            }
          printf (")\n");
        }
    }
}

void
categoricals_destroy (struct categoricals *cat)
{
  if (!cat)
    return;

  for (int i = 0; i < cat->n_iap; ++i)
    {
      /* Unref any cases that we reffed. */
      struct interaction_value *iv;
      HMAP_FOR_EACH (iv, struct interaction_value, node, &cat->iap[i].ivmap)
        {
          if (cat->payload && cat->payload->destroy)
            cat->payload->destroy (cat->aux1, cat->aux2, iv->user_data);
          case_unref (iv->ccase);
        }

      free (cat->iap[i].enc_sum);
      hmap_destroy (&cat->iap[i].ivmap);
    }
  free (cat->iap);

  /* Interate over each variable and delete its value map.

     The values themselves are part of the pool. */
  struct variable_node *vn;
  HMAP_FOR_EACH (vn, struct variable_node, node, &cat->varmap)
    hmap_destroy (&vn->valmap);

  hmap_destroy (&cat->varmap);

  pool_destroy (cat->pool);

  free (cat);
}

static struct interaction_value *
lookup_case (const struct hmap *map, const struct interaction *iact,
             const struct ccase *c)
{
  size_t hash = interaction_case_hash (iact, c, 0);
  struct interaction_value *iv;
  HMAP_FOR_EACH_WITH_HASH (iv, struct interaction_value, node, hash, map)
    if (interaction_case_equal (iact, c, iv->ccase))
      return iv;
  return NULL;
}

/* Returns true iff CAT is sane, that is, if it is complete and has at least
   one value. */
bool
categoricals_sane (const struct categoricals *cat)
{
  return cat->sane;
}

/* Creates and returns a new categoricals object whose variables come from the
   N_INTER interactions objects in the array starting at INTER.  (The INTER
   objects must outlive the categoricals object because it uses them
   internally.)

   FCTR_EXCL determines which cases are listwise ignored by
   categoricals_update(). */
struct categoricals *
categoricals_create (struct interaction *const *inter, size_t n_inter,
                     const struct variable *wv, enum mv_class fctr_excl)
{
  struct categoricals *cat = XZALLOC (struct categoricals);
  cat->iap = pool_calloc (cat->pool, n_inter, sizeof *cat->iap);
  cat->n_iap = n_inter;
  cat->wv = wv;
  cat->pool = pool_create ();
  cat->fctr_excl = fctr_excl;

  hmap_init (&cat->varmap);
  for (size_t i = 0; i < cat->n_iap; ++i)
    {
      struct interact_params *iap = &cat->iap[i];
      hmap_init (&iap->ivmap);
      iap->iact = inter[i];
      iap->cc = 0.0;
      iap->varnodes = pool_nmalloc (cat->pool, iap->iact->n_vars,
                                    sizeof *iap->varnodes);
      for (size_t v = 0; v < inter[i]->n_vars; ++v)
        {
          const struct variable *var = inter[i]->vars[v];
          struct variable_node *vn = lookup_variable (&cat->varmap, var);
          if (!vn)
            {
              vn = pool_malloc (cat->pool, sizeof *vn);
              vn->var = var;
              vn->values = NULL;
              hmap_init (&vn->valmap);
              hmap_insert (&cat->varmap, &vn->node, hash_pointer (var, 0));
            }
          iap->varnodes[v] = vn;
        }
    }

  return cat;
}

void
categoricals_update (struct categoricals *cat, const struct ccase *c)
{
  if (!cat)
    return;
  assert (!cat->df_to_iact);
  assert (!cat->cat_to_iact);

  double weight;
  weight = cat->wv ? case_num (c, cat->wv) : 1.0;
  weight = var_force_valid_weight (cat->wv, weight, NULL);

  /* Update the frequency table for each variable. */
  struct variable_node *vn;
  HMAP_FOR_EACH (vn, struct variable_node, node, &cat->varmap)
    {
      const int width = var_get_width (vn->var);
      const union value *val = case_data (c, vn->var);
      unsigned int hash = value_hash (val, width, 0);

      struct value_node *valn = lookup_value (&vn->valmap, val, hash, width);
      if (valn == NULL)
        {
          valn = pool_malloc (cat->pool, sizeof *valn);
          valn->index = -1;
          value_init_pool (cat->pool, &valn->val, width);
          value_copy (&valn->val, val, width);
          hmap_insert (&vn->valmap, &valn->node, hash);
        }
    }

  /* Update the frequency table for full interactions. */
  for (int i = 0; i < cat->n_iap; ++i)
    {
      struct interact_params *iap = &cat->iap[i];
      const struct interaction *iact = iap->iact;
      if (interaction_case_is_missing (iact, c, cat->fctr_excl))
        continue;

      unsigned int hash = interaction_case_hash (iact, c, 0);
      struct interaction_value *node = lookup_case (&iap->ivmap, iact, c);
      if (!node)
        {
          node = pool_malloc (cat->pool, sizeof *node);
          node->ccase = case_ref (c);
          node->cc = weight;

          hmap_insert (&iap->ivmap, &node->node, hash);

          if (cat->payload)
            node->user_data = cat->payload->create (cat->aux1, cat->aux2);
        }
      else
        node->cc += weight;
      iap->cc += weight;

      if (cat->payload)
        cat->payload->update (cat->aux1, cat->aux2, node->user_data, c,
                              weight);
    }
}

/* Return the number of categories (distinct values) for interaction IDX in
   CAT. */
size_t
categoricals_n_count (const struct categoricals *cat, size_t idx)
{
  return hmap_count (&cat->iap[idx].ivmap);
}

/* Return the total number of categories across all interactions in CAT. */
size_t
categoricals_n_total (const struct categoricals *cat)
{
  return categoricals_is_complete (cat) ? cat->n_cats_total : 0;
}

/* Returns the number of degrees of freedom for interaction IDX within CAT. */
size_t
categoricals_df (const struct categoricals *cat, size_t idx)
{
  const struct interact_params *iap = &cat->iap[idx];
  return iap->df_prod;
}

/* Returns the total degrees of freedom for CAT. */
size_t
categoricals_df_total (const struct categoricals *cat)
{
  return cat ? cat->df_sum : 0;
}

/* Returns true iff categoricals_done() has been called for CAT. */
bool
categoricals_is_complete (const struct categoricals *cat)
{
  return cat->df_to_iact != NULL;
}

/* This function must be called (once) before any call to the *_by_subscript or
  *_by_category functions, but AFTER any calls to categoricals_update.  If this
  function returns false, then no calls to _by_subscript or *_by_category are
  allowed. */
void
categoricals_done (const struct categoricals *cat_)
{
  struct categoricals *cat = CONST_CAST (struct categoricals *, cat_);
  if (!cat || categoricals_is_complete (cat))
    return;

  /* Assign 'index' to each variables' value_nodes, counting up from 0 in
     ascending order by value. */
  struct variable_node *vn;
  HMAP_FOR_EACH (vn, struct variable_node, node, &cat->varmap)
    {
      size_t n_vals = hmap_count (&vn->valmap);
      if (!n_vals)
        {
          cat->sane = false;
          return;
        }

      struct value_node **nodes = XCALLOC (n_vals,  struct value_node *);
      int x = 0;
      struct value_node *valnd;
      HMAP_FOR_EACH (valnd, struct value_node, node, &vn->valmap)
        nodes[x++] = valnd;
      sort (nodes, n_vals, sizeof *nodes, compare_value_node_3way, vn);
      for (x = 0; x < n_vals; ++x)
        nodes[x]->index = x;
      free (nodes);
    }

  /* Calculate the degrees of freedom, and the number of categories. */
  cat->df_sum = 0;
  cat->n_cats_total = 0;
  for (int i = 0 ; i < cat->n_iap; ++i)
    {
      struct interact_params *iap = &cat->iap[i];
      const struct interaction *iact = iap->iact;

      iap->df_prod = 1;
      iap->n_cats = 1;
      for (int v = 0 ; v < iact->n_vars; ++v)
        {
          size_t n_vals = hmap_count (&iap->varnodes[v]->valmap);

          iap->df_prod *= n_vals - 1;
          iap->n_cats *= n_vals;
        }

      if (iact->n_vars > 0)
        cat->df_sum += iap->df_prod;
      cat->n_cats_total += iap->n_cats;
    }


  cat->df_to_iact = pool_calloc (cat->pool, cat->df_sum,
                                 sizeof *cat->df_to_iact);

  cat->cat_to_iact = pool_calloc (cat->pool, cat->n_cats_total,
                                  sizeof *cat->cat_to_iact);

  int idx_df = 0;
  int idx_cat = 0;
  for (int i = 0; i < cat->n_iap; ++i)
    {
      struct interact_params *iap = &cat->iap[i];

      iap->base_df = idx_df;
      iap->base_cats = idx_cat;

      /* For some purposes (eg CONTRASTS in ONEWAY) the values need to be
         sorted. */
      iap->ivs = pool_nmalloc (cat->pool, hmap_count (&iap->ivmap),
                               sizeof *iap->ivs);
      int x = 0;
      struct interaction_value *ivn;
      HMAP_FOR_EACH (ivn, struct interaction_value, node, &iap->ivmap)
        iap->ivs[x++] = ivn;
      sort (iap->ivs, x, sizeof *iap->ivs,
            compare_interaction_value_3way, iap);

      /* Populate the variable maps. */
      if (iap->iact->n_vars)
        for (int j = 0; j < iap->df_prod; ++j)
          cat->df_to_iact[idx_df++] = i;

      for (int j = 0; j < iap->n_cats; ++j)
        cat->cat_to_iact[idx_cat++] = i;
    }

  categoricals_dump (cat);

  /* Tally up the sums for all the encodings */
  for (int i = 0; i < cat->n_iap; ++i)
    {
      struct interact_params *iap = &cat->iap[i];
      const struct interaction *iact = iap->iact;

      const int df = iact->n_vars ? iap->df_prod : 0;

      iap->enc_sum = xcalloc (df, sizeof *iap->enc_sum);

      for (int y = 0; y < hmap_count (&iap->ivmap); ++y)
        {
          struct interaction_value *iv = iap->ivs[y];
          for (int x = iap->base_df;
               x < iap->base_df + df; ++x)
            {
              const double bin = categoricals_get_effects_code_for_case (
                cat, x, iv->ccase);
              iap->enc_sum[x - iap->base_df] += bin * iv->cc;
            }
          if (cat->payload && cat->payload->calculate)
            cat->payload->calculate (cat->aux1, cat->aux2, iv->user_data);
        }
    }

  cat->sane = true;
}

union value *
categoricals_get_var_values (const struct categoricals *cat,
                             const struct variable *var, size_t *np)
{
  struct variable_node *vn = lookup_variable (&cat->varmap, var);
  *np = hmap_count (&vn->valmap);
  if (!vn->values)
    {
      vn->values = pool_nalloc (cat->pool, *np, sizeof *vn->values);

      struct value_node *valnd;
      HMAP_FOR_EACH (valnd, struct value_node, node, &vn->valmap)
        vn->values[valnd->index] = valnd->val;
    }
  return vn->values;
}

static struct interact_params *
df_to_iap (const struct categoricals *cat, int subscript)
{
  assert (subscript >= 0);
  assert (subscript < cat->df_sum);

  return &cat->iap[cat->df_to_iact[subscript]];
}

static struct interact_params *
cat_index_to_iap (const struct categoricals *cat, int cat_index)
{
  assert (cat_index >= 0);
  assert (cat_index < cat->n_cats_total);

  return &cat->iap[cat->cat_to_iact[cat_index]];
}

/* Return the interaction corresponding to SUBSCRIPT. */
const struct interaction *
categoricals_get_interaction_by_subscript (const struct categoricals *cat,
                                           int subscript)
{
  return df_to_iap (cat, subscript)->iact;
}

double
categoricals_get_weight_by_subscript (const struct categoricals *cat,
                                      int subscript)
{
  return df_to_iap (cat, subscript)->cc;
}

double
categoricals_get_sum_by_subscript (const struct categoricals *cat,
                                   int subscript)
{
  const struct interact_params *iap = df_to_iap (cat, subscript);
  return iap->enc_sum[subscript - iap->base_df];
}


/* Returns unity if the value in case C at SUBSCRIPT is equal to the category
   for that subscript */
static double
categoricals_get_code_for_case (const struct categoricals *cat, int subscript,
                                const struct ccase *c,
                                bool effects_coding)
{
  const struct interaction *iact
    = categoricals_get_interaction_by_subscript (cat, subscript);

  const struct interact_params *iap = df_to_iap (cat, subscript);

  double result = 1.0;
  int dfp = 1;
  for (int v = 0; v < iact->n_vars; ++v)
    {
      const struct variable *var = iact->vars[v];

      const union value *val = case_data (c, var);
      const int width = var_get_width (var);

      const unsigned int hash = value_hash (val, width, 0);
      const struct value_node *valn
        = lookup_value (&iap->varnodes[v]->valmap, val, hash, width);

      const int df = hmap_count (&iap->varnodes[v]->valmap) - 1;
      const int dfpn = dfp * df;

      if (effects_coding && valn->index == df)
        result = -result;
      else
        {
          /* Translate subscript into an index for the individual variable. */
          const int index = ((subscript - iap->base_df) % dfpn) / dfp;
          if (valn->index != index)
            return 0.0;
        }
      dfp = dfpn;
    }

  return result;
}


/* Returns unity if the value in case C at SUBSCRIPT is equal to the category
   for that subscript. */
double
categoricals_get_dummy_code_for_case (const struct categoricals *cat,
                                      int subscript, const struct ccase *c)
{
  return categoricals_get_code_for_case (cat, subscript, c, false);
}

/* Returns unity if the value in case C at SUBSCRIPT is equal to the category
   for that subscript.
   Else if it is the last category, return -1.
   Otherwise return 0.
 */
double
categoricals_get_effects_code_for_case (const struct categoricals *cat,
                                        int subscript, const struct ccase *c)
{
  return categoricals_get_code_for_case (cat, subscript, c, true);
}

/* Return a case containing the set of values corresponding to
   the Nth Category of the IACTth interaction */
const struct ccase *
categoricals_get_case_by_category_real (const struct categoricals *cat,
                                        int iact, int n)
{
  const struct interact_params *iap = &cat->iap[iact];
  return n < hmap_count (&iap->ivmap) ? iap->ivs[n]->ccase : NULL;
}

/* Return a the user data corresponding to the Nth Category of the IACTth
   interaction. */
void *
categoricals_get_user_data_by_category_real (const struct categoricals *cat,
                                             int iact, int n)
{
  const struct interact_params *iap = &cat->iap[iact];
  return n < hmap_count (&iap->ivmap) ? iap->ivs[n]->user_data : NULL;
}

int
categoricals_get_value_index_by_category_real (const struct categoricals *cat,
                                               int iact_idx, int cat_idx,
                                               int var_idx)
{
  const struct interact_params *iap = &cat->iap[iact_idx];
  const struct interaction_value *ivn = iap->ivs[cat_idx];
  const struct variable *var = iap->iact->vars[var_idx];
  const struct variable_node *vn = iap->varnodes[var_idx];
  const union value *val = case_data (ivn->ccase, var);
  int width = var_get_width (var);
  unsigned int hash = value_hash (val, width, 0);
  return lookup_value (&vn->valmap, val, hash, width)->index;
}

/* Return a case containing the set of values corresponding to CAT_INDEX. */
const struct ccase *
categoricals_get_case_by_category (const struct categoricals *cat,
                                   int cat_index)
{
  const struct interact_params *iap = cat_index_to_iap (cat, cat_index);
  const struct interaction_value *vn = iap->ivs[cat_index - iap->base_cats];
  return vn->ccase;
}

void *
categoricals_get_user_data_by_category (const struct categoricals *cat,
                                        int cat_index)
{
  const struct interact_params *iap = cat_index_to_iap (cat, cat_index);
  const struct interaction_value *iv = iap->ivs[cat_index - iap->base_cats];
  return iv->user_data;
}

void
categoricals_set_payload (struct categoricals *cat, const struct payload *p,
                          const void *aux1, void *aux2)
{
  cat->payload = p;
  cat->aux1 = aux1;
  cat->aux2 = aux2;
}
