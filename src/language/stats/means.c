/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012, 2013, 2019 Free Software Foundation, Inc.

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

#include "data/case.h"
#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/variable.h"

#include "libpspp/hmap.h"
#include "libpspp/bt.h"

#include "output/pivot-table.h"

#include "means.h"


#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

/* A "cell" in this procedure represents a distinct value of the
   procedure's categorical variables,  and a set of summary statistics
   of all cases which whose categorical variables have that set of
   values.   For example,  the dataset

   v1    v2    cat1     cat2
   100   202      0     1
   100   202      0     2
   100   202      1     0
   100   202      0     1


   has three cells in layer 0 and two cells in layer 1  in addition
   to a "grand summary" cell to which all (non-missing) cases
   contribute.

   The cells form a n-ary tree structure with the "grand summary"
   cell at the root.
  */
struct cell
{
  struct hmap_node hmap_node; /* Element in hash table. */
  struct bt_node  bt_node;    /* Element in binary tree */

  struct cell_container children;

  /* The level of the subtotal to which this cell pertains, or
     -1 if it does not pertain to a subtotal.  */
  int subtotal;

  /* The statistics to be calculated for the cell.  */
  struct per_var_data **stat;

  /* The parent of this cell, or NULL if this is the root cell.  */
  struct cell *parent_cell;

  int n_subtotals;
  struct cell_container *subtotals;

  int n_values;
  union value values[1];         /* The value(s). */
};

static void
dump_cell (const struct cell *cell, int level)
{
  printf ("%p: ", cell);
  for (int i = 0; i < cell->n_values; ++i)
    printf ("%g; ", cell->values[i].f);
  //  printf ("--- Count: %g", cell->count);
  // printf ("--- N Subtotals: %d", cell->n_subtotals);
  printf ("--- Level: %d", level);
  printf ("--- Subtotal: %d", cell->subtotal);
  printf ("--- Parent: %p", cell->parent_cell);
  printf ("\n");
}

static void
dump_tree (const struct cell *cell, int level)
{
  struct cell *sub_cell;
  BT_FOR_EACH (sub_cell, struct cell, bt_node, &cell->children.bt)
    {
      dump_tree (sub_cell, level + 1);
    }

  for (int i = 0; i < cell->n_subtotals; ++i)
    {
      struct cell_container *container = cell->subtotals + i;
      struct cell *scell;
      BT_FOR_EACH (scell, struct cell, bt_node, &container->bt)
	{
	  dump_cell (scell, level);
	}
    }

  dump_cell (cell, level);
}

struct instance
{
  struct hmap_node hmap_node; /* Element in hash table. */
  struct bt_node  bt_node;    /* Element in binary tree */

  /* A unique, consecutive, zero based index identifying this
     instance.  */
  int index;

  /* The top level value of this instance.  */
  union value value;
};

static void
dump_layer (const struct layer *layer)
{
  printf ("Layer: %p; fv0: %s; N %ld\n", layer,
	  layer->n_factor_vars
	  ? var_get_name (layer->factor_vars[0])
	  : "(null)",
	  hmap_count (&layer->instances.map)
	  );

  struct instance *inst;
  BT_FOR_EACH (inst, struct instance, bt_node, &layer->instances.bt)
    {
      printf ("Val %g; Index %d\n", inst->value.f, inst->index);
    }
  printf ("\n");
}


/* Generate a hash based on the values of the N variables in
   the array VARS which are taken from the case C.  */
static size_t
generate_hash (const struct ccase *c, int n,
	       const struct variable * const * vars)
{
  size_t hash = 0;
  for (int i = 0; i < n; ++i)
    {
      const struct variable *var = vars[i];
      const union value *vv = case_data (c, var);
      int width = var_get_width (var);
      hash = value_hash (vv, width, hash);
    }

  return hash;
}

/* Create a cell based on the N variables in the array VARS,
   which are indeces into the case C.
   The caller is responsible for destroying this cell when
   no longer needed. */
static struct cell *
generate_cell (const struct means *means, const struct ccase *c, int n,
	       const struct variable * const * vars)
{
  struct cell *cell = xzalloc ((sizeof *cell)
			       + (n - 1) * sizeof (union value));
  cell->subtotal = -1;
  cell->n_values = n;
  for (int i = 0; i < n; ++i)
    {
      const struct variable *var = vars[i];
      const union value *vv = case_data (c, var);
      int width = var_get_width (var);
      value_clone (&cell->values[i], vv, width);
    }

  hmap_init (&cell->children.map);

  cell->stat = xcalloc (means->n_statistics, sizeof *cell->stat);
  for (int stat = 0; stat < means->n_statistics; ++stat)
    {
      stat_create *sc = cell_spec[means->statistics[stat]].sc;

      cell->stat[stat] = sc (means->pool);
    }

  return cell;
}


/* If a  cell based on the N variables in the array VARS,
   which are indeces into the case C and whose hash is HASH,
   exists in HMAP, then return that cell.
   Otherwise, return NULL.  */
static struct cell *
lookup_cell (struct hmap *hmap,  size_t hash,
	     const struct ccase *c,
	     int n, const struct variable * const * vars)
{
  struct cell *fcol = NULL;
  HMAP_FOR_EACH_WITH_HASH (fcol, struct cell, hmap_node, hash, hmap)
    {
      bool match = true;
      for (int i = 0; i < n; ++i)
	{
	  const struct variable *var = vars[i];
	  const union value *vv = case_data (c, var);
	  int width = var_get_width (var);
	  if (!value_equal (vv, &fcol->values[i], width))
	    {
	      match = false;
	      break;
	    }
	}
      if (match)
	return fcol;
    }
  return NULL;
}


/*  A comparison function used to sort cells in a binary tree.  */
static int
my_compare_func (const struct bt_node *a,
		 const struct bt_node *b,
		 const void *aux)
{
  const struct cell *fa = BT_DATA (a, struct cell, bt_node);
  const struct cell *fb = BT_DATA (b, struct cell, bt_node);

  const struct variable *const *cv = aux;

  int vidx = fa->n_values - 1;
  assert (fa->n_values == fb->n_values);

  // FIXME: Consider whether other layers need to be compared.
  int r = value_compare_3way (&fa->values[vidx],
			      &fb->values[vidx],
			      var_get_width (cv[vidx]));
  return r;
}

/*  A comparison function used to sort cells in a binary tree.  */
static int
my_other_compare_func (const struct bt_node *a,
		 const struct bt_node *b,
		 const void *aux)
{
  const struct instance *fa = BT_DATA (a, struct instance, bt_node);
  const struct instance *fb = BT_DATA (b, struct instance, bt_node);

  const struct variable *var = aux;

  int r = value_compare_3way (&fa->value,
			      &fb->value,
			      var_get_width (var));
  return r;
}


static void arrange_cells (struct cell *cell, const struct mtable *table);


/* Walk the tree starting at CELL, creating and populating the BT for
   each  cell.  Also assigns  the "rank", "parent_cell" and "subtotal" members
   of each cell.*/
static void
arrange_cell (struct cell_container *container,
	      struct cell *cell, const struct mtable *table,
	      int subtotal)
{
  const struct variable **control_vars = table->control_vars;
  struct bt *bt = &container->bt;
  struct hmap *map = &container->map;
  bt_init (bt, my_compare_func, control_vars);

  struct cell *scell;
  HMAP_FOR_EACH (scell, struct cell, hmap_node, map)
    {
      scell->parent_cell = cell;
      scell->subtotal = subtotal;
      bt_insert (bt, &scell->bt_node);
      arrange_cells (scell, table);
    }

  if (cell->n_values > 0 && cell->subtotal == -1)
    {
      struct layer *layer = table->layers[cell->n_values - 1];

      const struct variable *var = control_vars[cell->n_values - 1];
      int width = var_get_width (var);
      unsigned int hash
  	= value_hash (&cell->values[cell->n_values - 1], width, 0);


      struct instance *inst = NULL;
      struct instance *next = NULL;
      HMAP_FOR_EACH_WITH_HASH_SAFE (inst, next, struct instance, hmap_node,
      			       hash, &layer->instances.map)
      	{
      	  if (value_equal (&inst->value,
      	  		   &cell->values[cell->n_values - 1],
      	  		   width))
      	    {
      	      break;
      	    }
      	}

      if (!inst)
      	{
	  inst = xzalloc (sizeof *inst);
	  inst->index = -1;
	  value_copy (&inst->value, &cell->values[cell->n_values -1],
		      width);
      	  hmap_insert (&layer->instances.map, &inst->hmap_node, hash);
      	}
    }
}

/* Arrange the children and then all the subtotals.  */
static void
arrange_cells (struct cell *cell, const struct mtable *table)
{
  arrange_cell (&cell->children, cell, table, -1);

  for (int s = 0; s < cell->n_subtotals; ++ s)
    {
      arrange_cell (cell->subtotals + s, cell, table, s);
    }
}




/*  If the top level value in CELL, has an instance in LAYER, then
    return that instance.  Otherwise return NULL.  */
static const struct instance *
lookup_instance (const struct layer *layer, const struct cell *cell)
{
  const struct variable *var = layer->factor_vars[0];
  const union value *val = cell->values + cell->n_values - 1;
  int width = var_get_width (var);
  unsigned int hash = value_hash (val, width, 0);
  struct instance *inst = NULL;
  struct instance *next;
  HMAP_FOR_EACH_WITH_HASH_SAFE (inst, next,
				struct instance, hmap_node,
				hash, &layer->instances.map)
    {
      if (value_equal (val, &inst->value, width))
	break;
    }
  return inst;
}

static void
populate_table (const struct cell *cell, const struct means *means,
	     struct pivot_table *table, int level)
{
  /* It is easier to overallocate this table by 2, than to adjust the
     logic when assigning its contents, and it is cheap to do so.  */
  size_t *indexes = xcalloc (table->n_dimensions + 2, sizeof *indexes);
  for (int s = 0; s < means->n_statistics; ++s)
    {
      bool kludge = false;   // FIXME: Remove this for production.
      indexes[0] = s;
      int stat = means->statistics[s];
      if (cell->subtotal != -1)
	{
	  //	  for (int i = 1; i < table->n_dimensions; ++i)
	  {
	    int i = 1;
	    const struct layer *layer
	      = means->table->layers[table->n_dimensions - 1 - i];
	    assert (layer);
	    const struct instance *inst = lookup_instance (layer, cell);
	    assert (inst);
	    indexes[i] = inst->index;
	  }
	  int i = 2;
	  const struct layer *layer
	    = means->table->layers[table->n_dimensions - 1 - i];
	  indexes[i] = hmap_count (&layer->instances.map);
	  kludge = true;
	}
      else if (hmap_is_empty (&cell->children.map))
	{
	  const struct cell *pc = cell;
	  for (int i = 1; i < table->n_dimensions; ++i)
	    {
	      const struct layer *layer
		= means->table->layers[table->n_dimensions - 1 - i];

	      const struct instance *inst = lookup_instance (layer, pc);
	      assert (inst);
	      indexes[i] = inst->index;
	      pc = pc->parent_cell;
	    }
	  kludge = true;
	}
      else
	{
	  const struct layer *layer;
	  int i = 0;
	  for (int st = 0; st < cell->n_subtotals; ++st)
	    {
	      layer = means->table->layers[table->n_dimensions - i - 2];
	      indexes[++i] = hmap_count (&layer->instances.map);
	    }
	  layer = means->table->layers[table->n_dimensions - i - 2];
	  indexes[++i] = hmap_count (&layer->instances.map);
	  if (++i < table->n_dimensions)
	    {
	      layer = means->table->layers[table->n_dimensions - i - 1];
	      const struct instance *inst = lookup_instance (layer, cell);
	      assert (inst);
	      indexes[i] = inst->index;
	    }
	  kludge = true;
	}

      if (kludge)
	{
	  stat_get *sg = cell_spec[stat].sd;
	  pivot_table_put (table, indexes, table->n_dimensions,
			   pivot_value_new_number (sg (cell->stat[s])));
	}
    }
  free (indexes);

  const struct bt *bt = &cell->children.bt;
  struct cell *child = NULL;
  BT_FOR_EACH (child, struct cell, bt_node, bt)
    {
      populate_table (child, means, table, level + 1);
    }

  //  printf ("There are %d subtotals\n", cell->n_subtotals);
  for (int i = 0; i < cell->n_subtotals; ++i)
    {
      //      printf ("aa\n");
      const struct cell_container *st = cell->subtotals + i;
      struct cell *scell;
      HMAP_FOR_EACH (scell, struct cell, hmap_node, &st->map)
	{
	  //	  printf ("%s:%d xxx\n", __FILE__, __LINE__);
	  /* dump_cell (scell, 0); */
	  populate_table (scell, means, table, level + 1);
	}
      //      printf ("zz\n");
    }
  //  printf ("ooo\n");
}

static void
ann_dim (struct pivot_table *t, int d, size_t *indeces)
{
  if (d < 0)
    return;
  char label[10];

  const struct pivot_dimension *dim = t->dimensions[d];
  for (int l = 0; l < dim->n_leaves; ++l)
    {
      indeces[d] = l;
      int x;
      for (x = 0; x < t->n_dimensions; ++x)
	{
	  label[x] = '0' + indeces[x];
	}
      label[x] = '\0';
      pivot_table_put (t, indeces, t->n_dimensions,
		       pivot_value_new_user_text (label, x));
      ann_dim (t, d - 1, indeces);
    }
}

static void
annotate_table (struct pivot_table *t)
{
  size_t *indeces = xcalloc (t->n_dimensions, sizeof *indeces);

  for (int d = 0; d < t->n_dimensions; ++d)
    {
      ann_dim (t, d, indeces);
    }
  free (indeces);
}

static void
create_table_structure (const struct mtable *mt, struct pivot_table *table)
{
  for (int l = mt->n_layers -1; l >=0 ; --l)
    {
      const struct layer *layer = mt->layers[l];
      assert (layer->n_factor_vars > 0);
      const struct variable *var = layer->factor_vars[0];
      struct pivot_dimension *dim_layer
	= pivot_dimension_create (table, PIVOT_AXIS_ROW,
				  var_to_string (var));
      dim_layer->root->show_label = true;

      {
	struct instance *inst = NULL;
	BT_FOR_EACH (inst, struct instance, bt_node, &layer->instances.bt)
	  {
	    struct substring space = SS_LITERAL_INITIALIZER ("\t ");
	    struct string str;
	    ds_init_empty (&str);
	    var_append_value_name (var,
				   &inst->value,
				   &str);

	    ds_ltrim (&str, space);

	    pivot_category_create_leaf
	      (dim_layer->root,
	       pivot_value_new_text (ds_cstr (&str)));

	    ds_destroy (&str);
	  }
      }

      pivot_category_create_leaf
	(dim_layer->root,
	 pivot_value_new_text ("Total"));
    }
}

void
means_shipout (const struct mtable *mt, const struct means *means)
{
  struct pivot_table *table = pivot_table_create (N_("Report"));

  struct pivot_dimension *dim_cells =
    pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Statistics"));

  for (int i = 0; i < means->n_statistics; ++i)
    {
      const struct cell_spec *cs = cell_spec + means->statistics[i];
      pivot_category_create_leaf
	(dim_cells->root,
	 pivot_value_new_text (gettext (cs->title)));
    }

  create_table_structure (mt, table);

  populate_table (mt->root_cell, means, table, 0);
  //  pivot_table_dump (table, 0);
  // annotate_table (table);

  pivot_table_submit (table);
}



static bool
missing_for_layer (const struct layer *layer, const struct ccase *c)
{
  if (layer->n_factor_vars == 0)
    return false;

  const struct variable *var = layer->factor_vars[0];
  const union value *vv = case_data (c, var);

  return var_is_value_missing (var, vv, MV_ANY);
}


static bool
missing_for_any_layer (const struct mtable *table,
		       int startx,
		       const struct ccase *c)
{
  bool miss = false;
  for (int l = startx; l < table->n_layers; ++l)
    {
      miss = missing_for_layer (table->layers[l], c);
    }

  if (miss)
    return true;

  return false;
}

static struct cell *
update_map_from_data (const struct means *means,
		      const struct mtable *table,
		      int startx,
		      struct hmap *map,
		      const struct ccase *c,
		      int start_var,
		      int n_vars,
		      double weight)
{
  const struct variable **cv = table->control_vars + start_var;
  if (! missing_for_any_layer (table, startx, c))
    {
      const struct variable *dep_var = table->dep_vars[0];

      size_t hash = generate_hash (c, n_vars, cv);

      struct cell *fcol = lookup_cell (map, hash,
				       c, n_vars, cv);
      if (!fcol)
	{
	  fcol = generate_cell (means, c, n_vars, cv);
	  fcol->n_subtotals = table->n_layers - 2 - start_var;
	  if (fcol->n_subtotals < 0)
	    fcol->n_subtotals = 0;
	  fcol->subtotals = xcalloc (fcol->n_subtotals,
				     sizeof *fcol->subtotals);
	  for (int i = 0; i < fcol->n_subtotals; ++i)
	    {
	      struct cell_container *c = fcol->subtotals + i;
	      hmap_init (&c->map);
	    }

	  hmap_insert (map, &fcol->hmap_node, hash);
	}

      for (int stat = 0; stat < means->n_statistics; ++stat)
	{
	  stat_update *su = cell_spec[means->statistics[stat]].su;
	  su (fcol->stat[stat], weight, case_data (c, dep_var)->f);
	}

      return fcol;
    }
  return NULL;
}


void
run_means (struct means *cmd, struct casereader *input,
	   const struct dataset *ds UNUSED)
{
  struct mtable *table = cmd->table + 0;
  struct ccase *c;
  struct casereader *reader;

  table->root_cell = generate_cell (cmd, 0, 0, 0);
  table->root_cell->n_subtotals = table->n_layers - 1;
  if (table->root_cell->n_subtotals < 0)
    table->root_cell->n_subtotals = 0;
  table->root_cell->subtotals
    = xcalloc (table->root_cell->n_subtotals,
	       sizeof *table->root_cell->subtotals);
  for (int i = 0; i < table->root_cell->n_subtotals; ++i)
    {
      struct cell_container *c = table->root_cell->subtotals + i;
      hmap_init (&c->map);
    }

  table->control_vars
    = calloc (table->n_layers, sizeof *table->control_vars);
  for (int l = 0; l  < table->n_layers; ++l)
    {
      const struct layer *layer = table->layers[l];
      if (layer->n_factor_vars > 0)
	table->control_vars[l] = layer->factor_vars[0];
    }

  struct cell *cell = table->root_cell;
  const struct variable *dep_var = table->dep_vars[0];
  for (reader = input;
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      const double weight = dict_get_case_weight (cmd->dict, c, NULL);
      if (! missing_for_any_layer (table, 0, c))
	{
	  for (int stat = 0; stat < cmd->n_statistics; ++stat)
	    {
	      stat_update *su = cell_spec[cmd->statistics[stat]].su;
	      su (cell->stat[stat], weight, case_data (c, dep_var)->f);
	    }
	}

      for (int i = 0; i < cell->n_subtotals; ++i)
	{
	  struct cell_container *container = cell->subtotals + i;
	  update_map_from_data (cmd, table, 1, &container->map, c, 1, 1,
				weight);
	}

      struct hmap *map = &cell->children.map;
      for (int l = 0; l < table->n_layers; ++l)
	{
	  struct cell *cell
	    = update_map_from_data (cmd, table, l, map, c,
				    0, l + 1, weight);

	  if (cell)
	    map = &cell->children.map;
	}
    }
  casereader_destroy (reader);

  arrange_cells (table->root_cell, table);
  for (int l = 0; l < table->n_layers; ++l)
    {
      struct layer *layer = table->layers[l];
      bt_init (&layer->instances.bt, my_other_compare_func,
	       table->control_vars[l]);

      /* Iterate the instance hash table, and insert each instance
	 into the binary tree BT.  */
      struct instance *inst;
      HMAP_FOR_EACH (inst, struct instance, hmap_node,
		     &layer->instances.map)
      	{
	  bt_insert (&layer->instances.bt, &inst->bt_node);
      	}

      /* Iterate the binary tree (in order) and assign the index
	 member accordingly.  */
      int index = 0;
      BT_FOR_EACH (inst, struct instance, bt_node, &layer->instances.bt)
      	{
      	  inst->index = index++;
      	}
    }

  /*  The root cell should have no parent.  */
  assert (table->root_cell->parent_cell == 0);
  dump_tree (table->root_cell, 0);
}
