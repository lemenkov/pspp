/* PSPP - a program for statistical analysis.
   Copyright (C) 2015 Free Software Foundation, Inc.

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

#include "output/charts/barchart.h"
#include "output/charts/piechart.h"

#include <stdlib.h>

#include "libpspp/cast.h"
#include "libpspp/str.h"
#include "libpspp/array.h"
#include "output/chart-provider.h"

#include "gl/xalloc.h"
#include "data/variable.h"
#include "data/settings.h"
#include "language/stats/freq.h"


static int
compare_category_3way (const void *a_, const void *b_, const void *bc_)
{
  const struct category *const*a = a_;
  const struct category *const*b = b_;
  const struct barchart *bc = bc_;

  return value_compare_3way (&(*a)->val, &(*b)->val, var_get_width (bc->var[1]));
}


static int
compare_category_by_index_3way (const void *a_, const void *b_,
                                const void *unused UNUSED)
{
  const struct category *const*a = a_;
  const struct category *const*b = b_;

  if ( (*a)->idx < (*b)->idx)
    return -1;

  return ((*a)->idx > (*b)->idx);
}

static unsigned int
hash_freq_2level_ptr (const void *a_, const void *bc_)
{
  const struct freq *const *ap = a_;
  const struct barchart *bc = bc_;

  size_t hash = value_hash (&(*ap)->values[0], bc->widths[0], 0);

  if (bc->n_vars > 1)
    hash = value_hash (&(*ap)->values[1], bc->widths[1], hash);

  return hash;
}


static int
compare_freq_2level_ptr_3way (const void *a_, const void *b_, const void *bc_)
{
  const struct freq *const *ap = a_;
  const struct freq *const *bp = b_;
  const struct barchart *bc = bc_;

  const int level0 = value_compare_3way (&(*ap)->values[0], &(*bp)->values[0], bc->widths[0]);

  if (level0 == 0 && bc->n_vars > 1)
    return value_compare_3way (&(*ap)->values[1], &(*bp)->values[1], bc->widths[1]);

  return level0;
}

/* Print out a textual representation of a barchart.
   This is intended only for testing, and not as a means
   of visualising the data.
*/
static void
barchart_dump (const struct barchart *bc, FILE *fp)
{
  fprintf (fp, "Graphic: Barchart\n");
  fprintf (fp, "Percentage: %d\n", bc->percent);
  fprintf (fp, "Total Categories: %d\n", bc->n_nzcats);
  fprintf (fp, "Primary Categories: %d\n", bc->n_pcats);
  fprintf (fp, "Largest Category: %g\n", bc->largest);
  fprintf (fp, "Total Count: %g\n", bc->total_count);

  fprintf (fp, "Y Label: \"%s\"\n", bc->ylabel);

  fprintf (fp, "Categorical Variables:\n");
  for (int i = 0; i < bc->n_vars; ++i)
    {
      fprintf (fp, "  Var: \"%s\"\n", var_get_name (bc->var[i]));
    }

  fprintf (fp, "Categories:\n");
  struct category *cat;
  struct category **cats = XCALLOC (hmap_count (&bc->primaries), struct category *);
  int  i = 0;
  HMAP_FOR_EACH (cat, struct category, node, &bc->primaries)
    {
      cats[i++] = cat;
    }
  /* HMAP_FOR_EACH is not guaranteed to iterate in any particular order.  So
     we must sort here before we output the results.  */
  sort (cats, i, sizeof (struct category *),  compare_category_by_index_3way, bc);
  for (i = 0; i < hmap_count (&bc->primaries); ++i)
    {
      const struct category *c = cats[i];
      fprintf (fp, "  %d \"%s\"\n", c->idx, ds_cstr (&c->label));
    }
  free (cats);

  if (bc->ss)
    {
      fprintf (fp, "Sub-categories:\n");
      for (int i = 0; i < bc->n_nzcats / bc->n_pcats; ++i)
	{
	  const struct category *cat = bc->ss[i];
	  fprintf (fp, "  %d \"%s\"\n", cat->idx, ds_cstr(&cat->label));
	}
    }

  fprintf (fp, "All Categories:\n");
  for (int i = 0; i < bc->n_nzcats; ++i)
    {
      const struct freq *frq = bc->cats[i];
      fprintf (fp, "Count: %g; ", frq->count);

      struct string s = DS_EMPTY_INITIALIZER;
      var_append_value_name (bc->var[0], &frq->values[0], &s);

      fprintf (fp, "Cat: \"%s\"", ds_cstr (&s));
      ds_clear (&s);

      if (bc->ss)
	{
	  var_append_value_name (bc->var[1], &frq->values[1], &s);
	  fprintf (fp, ", \"%s\"", ds_cstr (&s));
	}
      ds_destroy (&s);
      fputc ('\n', fp);
    }

  fputc ('\n', fp);
}


/* Creates and returns a chart that will render a barchart with
   the given TITLE and the N_CATS described in CATS.

   VAR is an array containing the categorical variables, and N_VAR
   the number of them. N_VAR must be exactly 1 or 2.

   CATS are the counts of the values of those variables. N_CATS is the
   number of distinct values.
*/
struct chart *
barchart_create (const struct variable **var, int n_vars,
		 const char *ylabel, bool percent,
		 struct freq *const *cats, int n_cats)
{
  struct barchart *bar;
  int i;

  const int pidx = 0;
  const int sidx = 1;


  int width = var_get_width (var[pidx]);

  assert (n_vars >= 1 && n_vars <= 2);

  bar = xzalloc (sizeof *bar);
  bar->percent = percent;
  bar->var = var;
  bar->n_vars = n_vars;
  bar->n_nzcats = n_cats;
  chart_init (&bar->chart, &barchart_class, var_to_string (var[pidx]));

  bar->largest = -1;
  bar->ylabel = strdup (ylabel);

    {
      int idx = 0;
      hmap_init (&bar->primaries);

      /*
	 Iterate the categories and create a hash table of the primary categories.
	 We need to do this to find out how many there are and to cache the labels.
      */
      for (i = 0; i < n_cats; i++)
	{
	  const struct freq *src = cats[i];
	  size_t hash = value_hash (&src->values[pidx], width, 0);

	  struct category *foo;
	  int flag = 0;
	  HMAP_FOR_EACH_WITH_HASH (foo, struct category, node, hash, &bar->primaries)
	    {
	      if (value_equal (&foo->val, &src->values[pidx], width))
		{
		  flag = 1;
		  break;
		}
	    }

	  if (!flag)
	    {
	      struct category *s = xzalloc (sizeof *s);
	      s->idx = idx++;
	      s->width = var_get_width (var[pidx]);
	      value_init (&s->val, s->width);
	      value_copy (&s->val, &src->values[pidx], s->width);
	      ds_init_empty (&s->label);
	      var_append_value_name (var[pidx], &s->val, &s->label);

	      hmap_insert (&bar->primaries, &s->node, hash);
	    }
	}

      bar->n_pcats = hmap_count (&bar->primaries);
    }

  if (n_vars > 1)
    {
      hmap_init (&bar->secondaries);
      int idx = 0;
      /* Iterate the categories, and create a hash table of secondary categories */
      for (i = 0; i < n_cats; i++)
	{
	  struct freq *src = cats[i];

	  struct category *foo;
	  int flag = 0;
	  size_t hash = value_hash (&src->values[sidx], var_get_width (var[sidx]), 0);
	  HMAP_FOR_EACH_WITH_HASH (foo, struct category, node, hash, &bar->secondaries)
	    {
	      if (value_equal (&foo->val, &src->values[sidx], var_get_width (var[sidx])))
		{
		  flag = 1;
		  break;
		}
	    }

	  if (!flag)
	    {
	      struct category *s = xzalloc (sizeof *s);
	      s->idx = idx++;
	      s->width = var_get_width (var[sidx]);
	      value_init (&s->val, s->width);
	      value_copy (&s->val, &src->values[sidx], var_get_width (var[sidx]));
	      ds_init_empty (&s->label);
	      var_append_value_name (var[sidx], &s->val, &s->label);

	      hmap_insert (&bar->secondaries, &s->node, hash);
	      bar->ss = xrealloc (bar->ss, idx * sizeof *bar->ss);
	      bar->ss[idx - 1] = s;
	    }
	}

      int n_category = hmap_count (&bar->secondaries);

      sort (bar->ss, n_category, sizeof *bar->ss,
	    compare_category_3way, bar);
    }


  /* Deep copy.  Not necessary for cmd line, but essential for the GUI,
     since an expose callback will access these structs which may not
     exist.
   */
  bar->cats = xcalloc (n_cats, sizeof *bar->cats);

  bar->widths[0] = var_get_width (bar->var[0]);
  if (n_vars > 1)
    bar->widths[1] = var_get_width (bar->var[1]);

  {
    struct hmap level2table;
    hmap_init (&level2table);
    int x = 0;

    for (i = 0; i < n_cats; i++)
      {
	struct freq *c = cats[i];

	struct freq *foo;
	bool flag = false;
	size_t hash = hash_freq_2level_ptr (&c, bar);
	HMAP_FOR_EACH_WITH_HASH (foo, struct freq, node, hash, &level2table)
	  {
	    if (0 == compare_freq_2level_ptr_3way (&foo, &c, bar))
	      {
		foo->count += c->count;
		bar->total_count += c->count;

		if (foo->count > bar->largest)
		  bar->largest = foo->count;

		flag = true;
		break;
	      }
	  }

	if (!flag)
	  {
	    struct freq *aggregated_freq = freq_clone (c, n_vars, bar->widths);
	    hmap_insert (&level2table, &aggregated_freq->node, hash);

	    if (c->count > bar->largest)
	      bar->largest = aggregated_freq->count;

	    bar->total_count += c->count;
	    bar->cats[x++] = aggregated_freq;
	  }
      }

    bar->n_nzcats = hmap_count (&level2table);
    hmap_destroy (&level2table);
  }

  sort (bar->cats, bar->n_nzcats, sizeof *bar->cats,
	compare_freq_2level_ptr_3way, bar);

  if (settings_get_testing_mode ())
    barchart_dump (bar, stdout);

  return &bar->chart;
}

static void
destroy_cat_map (struct hmap *m)
{
  struct category *foo = NULL;
  struct category *next = NULL;
  HMAP_FOR_EACH_SAFE (foo, next, struct category, node, m)
    {
      value_destroy (&foo->val, foo->width);

      ds_destroy (&foo->label);
      free (foo);
    }

  hmap_destroy (m);
}

static void
barchart_destroy (struct chart *chart)
{
  struct barchart *bar = to_barchart (chart);

  int i;

  destroy_cat_map (&bar->primaries);
  if (bar->ss)
    {
      destroy_cat_map (&bar->secondaries);
    }

  for (i = 0; i < bar->n_nzcats; i++)
    {
      freq_destroy (bar->cats[i], bar->n_vars, bar->widths);
    }

  free (bar->cats);
  free (bar->ylabel);
  free (bar->ss);
  free (bar);
}

const struct chart_class barchart_class =
  {
    barchart_destroy
  };
