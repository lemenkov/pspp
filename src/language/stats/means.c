/* PSPP - a program for statistical analysis.
   Copyright (C) 2019 Free Software Foundation, Inc.

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
#include "libpspp/hash-functions.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"

#include "language/command.h"

#include "count-one-bits.h"
#include "count-leading-zeros.h"

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

  int n_children;
  struct cell_container *children;

  /* The statistics to be calculated for the cell.  */
  struct statistic **stat;

  /* The parent of this cell, or NULL if this is the root cell.  */
  const struct cell *parent_cell;

  /* A bit-field variable which indicates which control variables
     are allocated a fixed value (for this cell),  and which are
     "wildcards".

     A one indicates a fixed value.  A zero indicates a wildcard.
     Wildcard values are used to calculate totals and sub-totals.
  */
  unsigned int not_wild;

  /* The value(s). */
  union value *values;

  /* The variables corresponding to the above values.  */
  const struct variable **vars;
};

/*  A structure used to find the union of all values used
    within a layer, and to sort those values.  */
struct instance
{
  struct hmap_node hmap_node; /* Element in hash table. */
  struct bt_node  bt_node;    /* Element in binary tree */

  /* A unique, consecutive, zero based index identifying this
     instance.  */
  int index;

  /* The top level value of this instance.  */
  union value value;
  const struct variable *var;
};


static void
destroy_workspace (const struct mtable *mt, struct workspace *ws)
{
  for (int l = 0; l < mt->n_layers; ++l)
    {
      struct cell_container *instances = ws->instances + l;
      struct instance *inst;
      struct instance *next;
      HMAP_FOR_EACH_SAFE (inst, next, struct instance, hmap_node,
			  &instances->map)
	{
	  int width = var_get_width (inst->var);
	  value_destroy (&inst->value, width);
	  free (inst);
	}
      hmap_destroy (&instances->map);
    }
  free (ws->control_idx);
  free (ws->instances);
}

/* Destroy CELL.  */
static void
destroy_cell (const struct means *means,
	      const struct mtable *mt, struct cell *cell)
{
  int idx = 0;
  for (int i = 0; i < mt->n_layers; ++i)
    {
      if (0 == ((cell->not_wild >> i) & 0x1))
	continue;

      const struct layer *layer = mt->layers[i];
      for (int cmb = 0; cmb < mt->n_combinations; ++cmb)
      {
        struct workspace *ws = mt->ws + cmb;
        const struct variable *var
          = layer->factor_vars[ws->control_idx[i]];

        int width = var_get_width (var);
        value_destroy (&cell->values[idx++], width);
      }
    }
  for (int i = 0; i < cell->n_children; ++i)
    {
      struct cell_container *container = cell->children + i;
      hmap_destroy (&container->map);
    }

  for (int v = 0; v < mt->n_dep_vars; ++v)
    {
      for (int s = 0; s < means->n_statistics; ++s)
        {
          stat_destroy *des = cell_spec[means->statistics[s]].sf;
          des (cell->stat[s + v * means->n_statistics]);
        }
    }
  free (cell->stat);

  free (cell->children);
  free (cell->values);
  free (cell->vars);
  free (cell);
}


/* Walk the tree in postorder starting from CELL and destroy all the
   cells.  */
static void
means_destroy_cells (const struct means *means, struct cell *cell,
		     const struct mtable *table)
{
  for (int i = 0; i < cell->n_children; ++i)
    {
      struct cell_container *container = cell->children + i;
      struct cell *sub_cell;
      struct cell *next;
      HMAP_FOR_EACH_SAFE (sub_cell,  next, struct cell, hmap_node,
  			  &container->map)
  	{
  	  means_destroy_cells (means, sub_cell, table);
  	}
    }

  destroy_cell (means, table, cell);
}

#if 0

static void
dump_cell (const struct cell *cell, const struct mtable *mt, int level)
{
  for (int l = 0; l < level; ++l)
    putchar (' ');
  printf ("%p: ", cell);
  for (int i = 0; i < mt->n_layers; ++i)
    {
      putchar (((cell->not_wild >> i) & 0x1) ? 'w' : '.');
    }
  printf (" - ");
  int x = 0;
  for (int i = 0; i < mt->n_layers; ++i)
    {
      if ((cell->not_wild >> i) & 0x1)
	{
	  printf ("%s: ", var_get_name (cell->vars[x]));
	  printf ("%g ", cell->values[x++].f);
	}
      else
	printf ("x ");
    }
  stat_get *sg = cell_spec[MEANS_N].sd;
  printf ("--- S1: %g", sg (cell->stat[0]));

  printf ("--- N Children: %d", cell->n_children);
  //  printf ("--- Level: %d", level);
  printf ("--- Parent: %p", cell->parent_cell);
  printf ("\n");
}

static void
dump_indeces (const size_t *indexes, int n)
{
  for (int i = 0 ; i < n; ++i)
    {
      printf ("%ld; ", indexes[i]);
    }
  printf ("\n");
}

/* Dump the tree in pre-order.  */
static void
dump_tree (const struct cell *cell, const struct mtable *table,
	   int level, const struct cell *parent)
{
  assert (cell->parent_cell == parent);
  dump_cell (cell, table, level);

  for (int i = 0; i < cell->n_children; ++i)
    {
      struct cell_container *container = cell->children + i;
      struct cell *sub_cell;
      BT_FOR_EACH (sub_cell, struct cell, bt_node, &container->bt)
	{
	  dump_tree (sub_cell, table, level + 1, cell);
	}
    }
}

#endif

/* Generate a hash based on the values of the N variables in
   the array VARS which are taken from the case C.  */
static unsigned int
generate_hash (const struct mtable *mt,
	       const struct ccase *c,
	       unsigned int not_wild,
	       const struct workspace *ws)
{
  unsigned int hash = 0;
  for (int i = 0; i < mt->n_layers; ++i)
    {
      if (0 == ((not_wild >> i) & 0x1))
	continue;

      const struct layer *layer = mt->layers[i];
      const struct variable *var = layer->factor_vars[ws->control_idx[i]];
      const union value *vv = case_data (c, var);
      int width = var_get_width (var);
      hash = hash_int (i, hash);
      hash = value_hash (vv, width, hash);
    }

  return hash;
}

/* Create a cell based on the N variables in the array VARS,
   which are indeces into the case C.
   The caller is responsible for destroying this cell when
   no longer needed. */
static struct cell *
generate_cell (const struct means *means,
	       const struct mtable *mt,
	       const struct ccase *c,
               unsigned int not_wild,
	       const struct cell *pcell,
	       const struct workspace *ws)
{
  int n_vars = count_one_bits (not_wild);
  struct cell *cell = xzalloc ((sizeof *cell));
  cell->values = xcalloc (n_vars, sizeof *cell->values);
  cell->vars = xcalloc (n_vars, sizeof *cell->vars);
  cell->not_wild = not_wild;

  cell->parent_cell = pcell;
  cell->n_children = mt->n_layers -
    (sizeof (cell->not_wild) * CHAR_BIT) +
    count_leading_zeros (cell->not_wild);

  int idx = 0;
  for (int i = 0; i < mt->n_layers; ++i)
    {
      if (0 == ((not_wild >> i) & 0x1))
	continue;

      const struct layer *layer = mt->layers[i];
      const struct variable *var = layer->factor_vars[ws->control_idx[i]];
      const union value *vv = case_data (c, var);
      int width = var_get_width (var);
      cell->vars[idx] = var;
      value_clone (&cell->values[idx++], vv, width);
    }
  assert (idx == n_vars);

  cell->children = xcalloc (cell->n_children, sizeof *cell->children);
  for (int i = 0; i < cell->n_children; ++i)
    {
      struct cell_container *container = cell->children + i;
      hmap_init (&container->map);
    }

  cell->stat = xcalloc (means->n_statistics * mt->n_dep_vars, sizeof *cell->stat);
  for (int v = 0; v < mt->n_dep_vars; ++v)
    {
      for (int stat = 0; stat < means->n_statistics; ++stat)
        {
          stat_create *sc = cell_spec[means->statistics[stat]].sc;

          cell->stat[stat + v * means->n_statistics] = sc (means->pool);
        }
    }
  return cell;
}


/* If a  cell based on the N variables in the array VARS,
   which are indeces into the case C and whose hash is HASH,
   exists in HMAP, then return that cell.
   Otherwise, return NULL.  */
static struct cell *
lookup_cell (const struct mtable *mt,
	     struct hmap *hmap,  unsigned int hash,
	     const struct ccase *c,
	     unsigned int not_wild,
	     const struct workspace *ws)
{
  struct cell *cell = NULL;
  HMAP_FOR_EACH_WITH_HASH (cell, struct cell, hmap_node, hash, hmap)
    {
      bool match = true;
      int idx = 0;
      if (cell->not_wild != not_wild)
      	continue;
      for (int i = 0; i < mt->n_layers; ++i)
	{
	  if (0 == ((cell->not_wild >> i) & 0x1))
	    continue;

	  const struct layer *layer = mt->layers[i];
	  const struct variable *var = layer->factor_vars[ws->control_idx[i]];
	  const union value *vv = case_data (c, var);
	  int width = var_get_width (var);
	  assert (var == cell->vars[idx]);
	  if (!value_equal (vv, &cell->values[idx++], width))
	    {
	      match = false;
	      break;
	    }
	}
      if (match)
	return cell;
    }
  return NULL;
}


/*  A comparison function used to sort cells in a binary tree.
    Only the innermost value needs to be compared, because no
    two cells with similar outer values will appear in the same
    tree/map.   */
static int
cell_compare_3way (const struct bt_node *a,
		   const struct bt_node *b,
		   const void *aux UNUSED)
{
  const struct cell *fa = BT_DATA (a, struct cell, bt_node);
  const struct cell *fb = BT_DATA (b, struct cell, bt_node);

  assert (fa->not_wild == fb->not_wild);
  int vidx = count_one_bits (fa->not_wild) - 1;
  assert (fa->vars[vidx] == fb->vars[vidx]);

  return value_compare_3way (&fa->values[vidx],
			     &fb->values[vidx],
			     var_get_width (fa->vars[vidx]));
}

/*  A comparison function used to sort cells in a binary tree.  */
static int
compare_instance_3way (const struct bt_node *a,
		       const struct bt_node *b,
		       const void *aux UNUSED)
{
  const struct instance *fa = BT_DATA (a, struct instance, bt_node);
  const struct instance *fb = BT_DATA (b, struct instance, bt_node);

  assert (fa->var == fb->var);

  return  value_compare_3way (&fa->value,
			      &fb->value,
			      var_get_width (fa->var));
}


static void arrange_cells (struct workspace *ws,
			   struct cell *cell, const struct mtable *table);


/* Iterate CONTAINER's map inserting a copy of its elements into
   CONTAINER's binary tree.    Also, for each layer in TABLE, create
   an instance container, containing the union of all elements in
   CONTAINER.  */
static void
arrange_cell (struct workspace *ws, struct cell_container *container,
	      const struct mtable *mt)
{
  struct bt *bt = &container->bt;
  struct hmap *map = &container->map;
  bt_init (bt, cell_compare_3way, NULL);

  struct cell *cell;
  HMAP_FOR_EACH (cell, struct cell, hmap_node, map)
    {
      bt_insert (bt, &cell->bt_node);

      int idx = 0;
      for (int i = 0; i < mt->n_layers; ++i)
	{
	  if (0 == ((cell->not_wild >> i) & 0x1))
	    continue;

	  struct cell_container *instances = ws->instances + i;
	  const struct variable *var = cell->vars[idx];
	  int width = var_get_width (var);
	  unsigned int hash
	    = value_hash (&cell->values[idx], width, 0);

	  struct instance *inst = NULL;
	  struct instance *next = NULL;
	  HMAP_FOR_EACH_WITH_HASH_SAFE (inst, next, struct instance,
					hmap_node,
					hash, &instances->map)
	    {
	      assert (cell->vars[idx] == var);
	      if (value_equal (&inst->value,
			       &cell->values[idx],
			       width))
		{
		  break;
		}
	    }

	  if (!inst)
	    {
	      inst = xzalloc (sizeof *inst);
	      inst->index = -1;
	      inst->var = var;
	      value_clone (&inst->value, &cell->values[idx],
			   width);
	      hmap_insert (&instances->map, &inst->hmap_node, hash);
	    }

	  idx++;
	}

      arrange_cells (ws, cell, mt);
    }
}

/* Arrange the children and then all the subtotals.  */
static void
arrange_cells (struct workspace *ws, struct cell *cell,
	       const struct mtable *table)
{
  for (int i = 0; i < cell->n_children; ++i)
    {
      struct cell_container *container = cell->children + i;
      arrange_cell (ws, container, table);
    }
}




/*  If the top level value in CELL, has an instance in the L_IDX'th layer,
    then return that instance.  Otherwise return NULL.  */
static const struct instance *
lookup_instance (const struct mtable *mt, const struct workspace *ws,
		 int l_idx, const struct cell *cell)
{
  const struct layer *layer = mt->layers[l_idx];
  int n_vals = count_one_bits (cell->not_wild);
  const struct variable *var = layer->factor_vars[ws->control_idx[l_idx]];
  const union value *val = cell->values + n_vals - 1;
  int width = var_get_width (var);
  unsigned int hash = value_hash (val, width, 0);
  const struct cell_container *instances = ws->instances + l_idx;
  struct instance *inst = NULL;
  struct instance *next;
  HMAP_FOR_EACH_WITH_HASH_SAFE (inst, next,
				struct instance, hmap_node,
				hash, &instances->map)
    {
      if (value_equal (val, &inst->value, width))
	break;
    }
  return inst;
}

/* Enter the values into PT.  */
static void
populate_table (const struct means *means, const struct mtable *mt,
		const struct workspace *ws,
                const struct cell *cell,
                struct pivot_table *pt)
{
  size_t *indexes = XCALLOC (pt->n_dimensions, size_t);
  for (int v = 0; v < mt->n_dep_vars; ++v)
    {
      for (int s = 0; s < means->n_statistics; ++s)
        {
          int i = 0;
          if (mt->n_dep_vars > 1)
            indexes[i++] = v;
          indexes[i++] = s;
          int stat = means->statistics[s];
          stat_get *sg = cell_spec[stat].sd;
          {
            const struct cell *pc = cell;
            for (; i < pt->n_dimensions; ++i)
              {
                int l_idx = pt->n_dimensions - i - 1;
		const struct cell_container *instances = ws->instances + l_idx;
                if (0 == (cell->not_wild >> l_idx & 0x1U))
                  {
                    indexes [i] = hmap_count (&instances->map);
                  }
                else
                  {
                    assert (pc);
                    const struct instance *inst
		      = lookup_instance (mt, ws, l_idx, pc);
                    assert (inst);
                    indexes [i] = inst->index;
                    pc = pc->parent_cell;
                  }
              }
          }

	  int idx = s + v * means->n_statistics;
	  struct pivot_value *pv
	    = pivot_value_new_number (sg (cell->stat[idx]));
	  if (NULL == cell_spec[stat].rc)
	    {
	      const struct variable *dv = mt->dep_vars[v];
	      pv->numeric.format = * var_get_print_format (dv);
	    }
          pivot_table_put (pt, indexes, pt->n_dimensions, pv);
        }
    }
  free (indexes);

  for (int i = 0; i < cell->n_children; ++i)
    {
      struct cell_container *container = cell->children + i;
      struct cell *child = NULL;
      BT_FOR_EACH (child, struct cell, bt_node, &container->bt)
	{
          populate_table (means, mt, ws, child, pt);
	}
    }
}

static void
create_table_structure (const struct mtable *mt, struct pivot_table *pt,
			const struct workspace *ws)
{
  int * lindexes = ws->control_idx;
  /* The inner layers are situated rightmost in the table.
     So this iteration is in reverse order.  */
  for (int l = mt->n_layers -1; l >=0 ; --l)
    {
      const struct layer *layer = mt->layers[l];
      const struct cell_container *instances = ws->instances + l;
      const struct variable *var = layer->factor_vars[lindexes[l]];
      struct pivot_dimension *dim_layer
	= pivot_dimension_create (pt, PIVOT_AXIS_ROW,
				  var_to_string (var));
      dim_layer->root->show_label = true;

      /* Place the values of the control variables as table headings.  */
      {
	struct instance *inst = NULL;
	BT_FOR_EACH (inst, struct instance, bt_node, &instances->bt)
	  {
	    struct substring space = SS_LITERAL_INITIALIZER ("\t ");
	    struct string str;
	    ds_init_empty (&str);
	    var_append_value_name (var,
				   &inst->value,
				   &str);

	    ds_ltrim (&str, space);

	    pivot_category_create_leaf (dim_layer->root,
                                        pivot_value_new_text (ds_cstr (&str)));

	    ds_destroy (&str);
	  }
      }

      pivot_category_create_leaf (dim_layer->root,
                                  pivot_value_new_text ("Total"));
    }
}

/* Initialise C_DES with a string describing the control variable
   relating to MT, LINDEXES.  */
static void
layers_to_string (const struct mtable *mt, const int *lindexes,
		  struct string *c_des)
{
  for (int l = 0; l < mt->n_layers; ++l)
    {
      const struct layer *layer = mt->layers[l];
      const struct variable *ctrl_var = layer->factor_vars[lindexes[l]];
      if (l > 0)
	ds_put_cstr (c_des, " * ");
      ds_put_cstr (c_des, var_get_name (ctrl_var));
    }
}

static void
populate_case_processing_summary (struct pivot_category *pc,
				  const struct mtable *mt,
				  const int *lindexes)
{
  struct string ds;
  ds_init_empty (&ds);
  int l = 0;
  for (l = 0; l < mt->n_layers; ++l)
    {
      const struct layer *layer = mt->layers[l];
      const struct variable *ctrl_var = layer->factor_vars[lindexes[l]];
      if (l > 0)
	ds_put_cstr (&ds, " * ");
      ds_put_cstr (&ds, var_get_name (ctrl_var));
    }
  for (int dv = 0; dv < mt->n_dep_vars; ++dv)
    {
      struct string dss;
      ds_init_empty (&dss);
      ds_put_cstr (&dss, var_get_name (mt->dep_vars[dv]));
      if (mt->n_layers > 0)
	{
	  ds_put_cstr (&dss, " * ");
	  ds_put_substring (&dss, ds.ss);
	}
      pivot_category_create_leaf (pc,
				  pivot_value_new_text (ds_cstr (&dss)));
      ds_destroy (&dss);
    }

  ds_destroy (&ds);
}

/* Create the "Case Processing Summary" table.  */
static void
means_case_processing_summary (const struct mtable *mt)
{
  struct pivot_table *pt = pivot_table_create (N_("Case Processing Summary"));

  struct pivot_dimension *dim_cases =
    pivot_dimension_create (pt, PIVOT_AXIS_COLUMN, N_("Cases"));
  dim_cases->root->show_label = true;

  struct pivot_category *cats[3];
  cats[0] = pivot_category_create_group (dim_cases->root,
					 N_("Included"), NULL);
  cats[1] = pivot_category_create_group (dim_cases->root,
					 N_("Excluded"), NULL);
  cats[2] = pivot_category_create_group (dim_cases->root,
					 N_("Total"), NULL);
  for (int i = 0; i < 3; ++i)
    {
      pivot_category_create_leaf_rc (cats[i],
                                     pivot_value_new_text (N_("N")),
				     PIVOT_RC_COUNT);
      pivot_category_create_leaf_rc (cats[i],
                                     pivot_value_new_text (N_("Percent")),
				     PIVOT_RC_PERCENT);
    }

  struct pivot_dimension *rows =
    pivot_dimension_create (pt, PIVOT_AXIS_ROW, N_("Variables"));

  for (int cmb = 0; cmb < mt->n_combinations; ++cmb)
    {
      const struct workspace *ws = mt->ws + cmb;
      populate_case_processing_summary (rows->root, mt, ws->control_idx);
      for (int dv = 0; dv < mt->n_dep_vars; ++dv)
        {
          int idx = cmb * mt->n_dep_vars + dv;
          const struct summary *summ = mt->summ + idx;
          double n_included = summ->n_total - summ->n_missing;
          pivot_table_put2 (pt, 5, idx,
                            pivot_value_new_number (100.0 * summ->n_total / summ->n_total));
          pivot_table_put2 (pt, 4, idx,
                            pivot_value_new_number (summ->n_total));

          pivot_table_put2 (pt, 3, idx,
                            pivot_value_new_number (100.0 * summ->n_missing / summ->n_total));
          pivot_table_put2 (pt, 2, idx,
                            pivot_value_new_number (summ->n_missing));

          pivot_table_put2 (pt, 1, idx,
                            pivot_value_new_number (100.0 * n_included / summ->n_total));
          pivot_table_put2 (pt, 0, idx,
                            pivot_value_new_number (n_included));
        }
    }

  pivot_table_submit (pt);
}

static void
means_shipout_single (const struct mtable *mt, const struct means *means,
		      const struct workspace *ws)
{
  struct pivot_table *pt = pivot_table_create (N_("Report"));
  pt->look.omit_empty = true;

  struct pivot_dimension *dim_cells =
    pivot_dimension_create (pt, PIVOT_AXIS_COLUMN, N_("Statistics"));

  /* Set the statistics headings, eg "Mean", "Std. Dev" etc.  */
  for (int i = 0; i < means->n_statistics; ++i)
    {
      const struct cell_spec *cs = cell_spec + means->statistics[i];
      pivot_category_create_leaf_rc
	(dim_cells->root,
	 pivot_value_new_text (gettext (cs->title)), cs->rc);
    }

  create_table_structure (mt, pt, ws);
  populate_table (means, mt, ws, ws->root_cell, pt);
  pivot_table_submit (pt);
}


static void
means_shipout_multivar (const struct mtable *mt, const struct means *means,
			const struct workspace *ws)
{
  struct string dss;
  ds_init_empty (&dss);
  for (int dv = 0; dv < mt->n_dep_vars; ++dv)
    {
      if (dv > 0)
	ds_put_cstr (&dss, " * ");
      ds_put_cstr (&dss, var_get_name (mt->dep_vars[dv]));
    }

  for (int l = 0; l < mt->n_layers; ++l)
    {
      ds_put_cstr (&dss, " * ");
      const struct layer *layer = mt->layers[l];
      const struct variable *var = layer->factor_vars[ws->control_idx[l]];
      ds_put_cstr (&dss, var_get_name (var));
    }

  struct pivot_table *pt = pivot_table_create (ds_cstr (&dss));
  pt->look.omit_empty = true;
  ds_destroy (&dss);

  struct pivot_dimension *dim_cells =
    pivot_dimension_create (pt, PIVOT_AXIS_COLUMN, N_("Variables"));

  for (int i = 0; i < mt->n_dep_vars; ++i)
    {
      pivot_category_create_leaf
	(dim_cells->root,
	 pivot_value_new_variable (mt->dep_vars[i]));
    }

  struct pivot_dimension *dim_stats
    = pivot_dimension_create (pt, PIVOT_AXIS_ROW,
  			      N_ ("Statistics"));
  dim_stats->root->show_label = false;

  for (int i = 0; i < means->n_statistics; ++i)
    {
      const struct cell_spec *cs = cell_spec + means->statistics[i];
      pivot_category_create_leaf_rc
	(dim_stats->root,
	 pivot_value_new_text (gettext (cs->title)), cs->rc);
    }

  create_table_structure (mt, pt, ws);
  populate_table (means, mt, ws, ws->root_cell, pt);
  pivot_table_submit (pt);
}

static void
means_shipout (const struct mtable *mt, const struct means *means)
{
  for (int cmb = 0; cmb < mt->n_combinations; ++cmb)
    {
      const struct workspace *ws = mt->ws + cmb;
      if (ws->root_cell == NULL)
	{
	  struct string des;
	  ds_init_empty (&des);
	  layers_to_string (mt, ws->control_idx, &des);
	  msg (MW, _("The table \"%s\" has no non-empty control variables."
		     "  No result for this table will be displayed."),
	       ds_cstr (&des));
	  ds_destroy (&des);
	  continue;
	}
      if (mt->n_dep_vars > 1)
	means_shipout_multivar (mt, means, ws);
      else
	means_shipout_single (mt, means, ws);
    }
}




static bool
control_var_missing (const struct means *means,
		     const struct mtable *mt,
		     unsigned int not_wild UNUSED,
		     const struct ccase *c,
		     const struct workspace *ws)
{
  bool miss = false;
  for (int l = 0; l < mt->n_layers; ++l)
    {
      /* if (0 == ((not_wild >> l) & 0x1)) */
      /* { */
      /*   continue; */
      /* } */

      const struct layer *layer = mt->layers[l];
      const struct variable *var = layer->factor_vars[ws->control_idx[l]];
      const union value *vv = case_data (c, var);

      miss = var_is_value_missing (var, vv, means->ctrl_exclude);
      if (miss)
	break;
    }

  return miss;
}

/* Lookup the set of control variables described by MT, C and NOT_WILD,
   in the hash table MAP.  If there is no such entry, then create a
   cell with these paremeters and add is to MAP.
   If the generated cell has childen, repeat for all the children.
   Returns the root cell.
*/
static struct cell *
service_cell_map (const struct means *means, const struct mtable *mt,
		 const struct ccase *c,
                 unsigned int not_wild,
		 struct hmap *map,
		 const struct cell *pcell,
                 int level,
		 const struct workspace *ws)
{
  struct cell *cell = NULL;
  if (map)
    {
      if (!control_var_missing (means, mt, not_wild, c, ws))
	{
	  /* Lookup this set of values in the cell's hash table.  */
	  unsigned int hash = generate_hash (mt, c, not_wild, ws);
	  cell = lookup_cell (mt, map, hash, c, not_wild, ws);

	  /* If it has not been seen before, then create a new
	     subcell, with this set of values, and insert it
	     into the table.  */
	  if (cell == NULL)
	    {
              cell = generate_cell (means, mt, c, not_wild, pcell, ws);
	      hmap_insert (map, &cell->hmap_node, hash);
	    }
	}
    }
  else
    {
      /* This condition should only happen in the root node case. */
      cell = ws->root_cell;
      if (cell == NULL &&
	  !control_var_missing (means, mt, not_wild, c, ws))
	cell = generate_cell (means, mt, c, not_wild, pcell, ws);
    }

  if (cell)
    {
      /* Here is where the business really happens!   After
	 testing for missing values, the cell's statistics
	 are accumulated.  */
      if (!control_var_missing (means, mt, not_wild, c, ws))
        {
          for (int v = 0; v < mt->n_dep_vars; ++v)
            {
              const struct variable *dep_var = mt->dep_vars[v];
	      const union value *vv = case_data (c, dep_var);
	      if (var_is_value_missing (dep_var, vv, means->dep_exclude))
		continue;

              for (int stat = 0; stat < means->n_statistics; ++stat)
                {
                  const double weight = dict_get_case_weight (means->dict, c,
                                                              NULL);
                  stat_update *su = cell_spec[means->statistics[stat]].su;
                  su (cell->stat[stat + v * means->n_statistics], weight,
		      case_data (c, dep_var)->f);
                }
            }
        }

      /* Recurse into all the children (if there are any).  */
      for (int i = 0; i < cell->n_children; ++i)
	{
	  struct cell_container *cc = cell->children + i;
	  service_cell_map (means, mt, c,
                           not_wild | (0x1U << (i + level)),
			   &cc->map, cell, level + i + 1, ws);
	}
    }

  return cell;
}

/*  Do all the necessary preparation and pre-calculation that
    needs to be done before iterating the data.  */
static void
prepare_means (struct means *cmd)
{
  for (int t = 0; t < cmd->n_tables; ++t)
    {
      struct mtable *mt = cmd->table + t;

      for (int i = 0; i < mt->n_combinations; ++i)
        {
          struct workspace *ws = mt->ws + i;
	  ws->root_cell = NULL;
          ws->control_idx = xzalloc (mt->n_layers
					 * sizeof *ws->control_idx);
          ws->instances = xzalloc (mt->n_layers
					 * sizeof *ws->instances);
          int cmb = i;
          for (int l = mt->n_layers - 1; l >= 0; --l)
            {
	      struct cell_container *instances = ws->instances + l;
              const struct layer *layer = mt->layers[l];
              ws->control_idx[l] = cmb % layer->n_factor_vars;
              cmb /= layer->n_factor_vars;
	      hmap_init (&instances->map);
            }
        }
    }
}


/* Do all the necessary calculations that occur AFTER iterating
   the data.  */
static void
post_means (struct means *cmd)
{
  for (int t = 0; t < cmd->n_tables; ++t)
    {
      struct mtable *mt = cmd->table + t;
      for (int cmb = 0; cmb < mt->n_combinations; ++cmb)
	{
	  struct workspace *ws = mt->ws + cmb;
	  if (ws->root_cell == NULL)
	    continue;
	  arrange_cells (ws, ws->root_cell, mt);
	  /*  The root cell should have no parent.  */
	  assert (ws->root_cell->parent_cell == 0);

	  for (int l = 0; l < mt->n_layers; ++l)
	    {
	      struct cell_container *instances = ws->instances + l;
	      bt_init (&instances->bt, compare_instance_3way, NULL);

	      /* Iterate the instance hash table, and insert each instance
		 into the binary tree BT.  */
	      struct instance *inst;
	      HMAP_FOR_EACH (inst, struct instance, hmap_node,
			     &instances->map)
		{
		  bt_insert (&instances->bt, &inst->bt_node);
		}

	      /* Iterate the binary tree (in order) and assign the index
		 member accordingly.  */
	      int index = 0;
	      BT_FOR_EACH (inst, struct instance, bt_node, &instances->bt)
		{
		  inst->index = index++;
		}
	    }
	}
    }
}


/* Update the summary information (the missings and the totals).  */
static void
update_summaries (const struct means *means, struct mtable *mt,
		  const struct ccase *c, double weight)
{
  for (int dv = 0; dv < mt->n_dep_vars; ++dv)
    {
      for (int cmb = 0; cmb < mt->n_combinations; ++cmb)
	{
	  struct workspace *ws = mt->ws + cmb;
	  struct summary *summ = mt->summ
	    + cmb * mt->n_dep_vars + dv;

	  summ->n_total += weight;
	  const struct variable *var = mt->dep_vars[dv];
	  const union value *vv = case_data (c, var);
	  /* First check if the dependent variable is missing.  */
	  if (var_is_value_missing (var, vv, means->dep_exclude))
	    summ->n_missing += weight;
	  /* If the dep var is not missing, then check each
	     control variable.  */
	  else
	    for (int l = 0; l < mt->n_layers; ++l)
	      {
		const struct layer *layer = mt->layers [l];
		const struct variable *var
		  = layer->factor_vars[ws->control_idx[l]];
		const union value *vv = case_data (c, var);
		if (var_is_value_missing (var, vv, means->ctrl_exclude))
		  {
		    summ->n_missing += weight;
		    break;
		  }
	      }
	}
    }
}


void
run_means (struct means *cmd, struct casereader *input,
	   const struct dataset *ds UNUSED)
{
  struct ccase *c = NULL;
  struct casereader *reader;

  prepare_means (cmd);

  for (reader = input;
       (c = casereader_read (reader)) != NULL; case_unref (c))
    {
      const double weight
	= dict_get_case_weight (cmd->dict, c, NULL);
      for (int t = 0; t < cmd->n_tables; ++t)
	{
	  struct mtable *mt = cmd->table + t;
	  update_summaries (cmd, mt, c, weight);

	  for (int cmb = 0; cmb < mt->n_combinations; ++cmb)
	    {
	      struct workspace *ws = mt->ws + cmb;

	      ws->root_cell = service_cell_map (cmd, mt, c,
						0U, NULL, NULL, 0, ws);
	    }
	}
    }
  casereader_destroy (reader);

  post_means (cmd);
}

struct lexer;

int
cmd_means (struct lexer *lexer, struct dataset *ds)
{
  struct means means;
  means.pool = pool_create ();

  means.ctrl_exclude = MV_ANY;
  means.dep_exclude = MV_ANY;
  means.table = NULL;
  means.n_tables = 0;

  means.dict = dataset_dict (ds);

  means.n_statistics = 3;
  means.statistics = pool_calloc (means.pool, 3, sizeof *means.statistics);
  means.statistics[0] = MEANS_MEAN;
  means.statistics[1] = MEANS_N;
  means.statistics[2] = MEANS_STDDEV;

  if (! means_parse (lexer, &means))
    goto error;

  /* Calculate some constant data for each table.  */
  for (int t = 0; t < means.n_tables; ++t)
    {
      struct mtable *mt = means.table + t;
      mt->n_combinations = 1;
      for (int l = 0; l < mt->n_layers; ++l)
	mt->n_combinations *= mt->layers[l]->n_factor_vars;
    }

  {
    struct casegrouper *grouper;
    struct casereader *group;
    bool ok;

    grouper = casegrouper_create_splits (proc_open (ds), means.dict);
    while (casegrouper_get_next_group (grouper, &group))
      {
	/* Allocate the workspaces.  */
	for (int t = 0; t < means.n_tables; ++t)
	{
	  struct mtable *mt = means.table + t;
	  mt->summ = xzalloc (mt->n_combinations * mt->n_dep_vars
			      * sizeof (*mt->summ));
	  mt->ws = xzalloc (mt->n_combinations * sizeof (*mt->ws));
	}
      	run_means (&means, group, ds);
	for (int t = 0; t < means.n_tables; ++t)
	  {
	    const struct mtable *mt = means.table + t;

	    means_case_processing_summary (mt);
	    means_shipout (mt, &means);

	    for (int i = 0; i < mt->n_combinations; ++i)
	      {
		struct workspace *ws = mt->ws + i;
		if (ws->root_cell == NULL)
		  continue;

		means_destroy_cells (&means, ws->root_cell, mt);
	      }
	  }

	/* Destroy the workspaces.  */
	for (int t = 0; t < means.n_tables; ++t)
	  {
	    struct mtable *mt = means.table + t;
	    free (mt->summ);
	    for (int i = 0; i < mt->n_combinations; ++i)
	      {
		struct workspace *ws = mt->ws + i;
		destroy_workspace (mt, ws);
	      }
	    free (mt->ws);
	  }
      }
    ok = casegrouper_destroy (grouper);
    ok = proc_commit (ds) && ok;
  }

  pool_destroy (means.pool);
  return CMD_SUCCESS;

 error:

  pool_destroy (means.pool);
  return CMD_FAILURE;
}
