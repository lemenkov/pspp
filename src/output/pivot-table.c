/* PSPP - a program for statistical analysis.
   Copyright (C) 2017, 2018, 2023 Free Software Foundation, Inc.

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

#include "output/pivot-table.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <math.h>
#include <stdlib.h>

#include "data/data-out.h"
#include "data/dictionary.h"
#include "data/settings.h"
#include "data/value.h"
#include "data/variable.h"
#include "data/file-name.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/hash-functions.h"
#include "libpspp/i18n.h"
#include "output/driver.h"
#include "output/spv/spv-table-look.h"

#include "gl/c-ctype.h"
#include "gl/configmake.h"
#include "gl/intprops.h"
#include "gl/minmax.h"
#include "gl/relocatable.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"
#include "gl/xsize.h"
#include "time.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void pivot_table_use_rc (const struct pivot_table *, const char *s,
                                struct fmt_spec *, bool *honor_small);

/* Pivot table display styling. */

/* Returns the name of AREA. */
const char *
pivot_area_to_string (enum pivot_area area)
{
  switch (area)
    {
    case PIVOT_AREA_TITLE: return "title";
    case PIVOT_AREA_CAPTION: return "caption";
    case PIVOT_AREA_FOOTER: return "footer";
    case PIVOT_AREA_CORNER: return "corner";
    case PIVOT_AREA_COLUMN_LABELS: return "column labels";
    case PIVOT_AREA_ROW_LABELS: return "row labels";
    case PIVOT_AREA_DATA: return "data";
    case PIVOT_AREA_LAYERS: return "layers";
    case PIVOT_N_AREAS: default: return "**error**";
    }
}

/* Returns the name of BORDER. */
const char *
pivot_border_to_string (enum pivot_border border)
{
  switch (border)
    {
    case PIVOT_BORDER_TITLE:
      return "title";

    case PIVOT_BORDER_OUTER_LEFT:
      return "left outer frame";
    case PIVOT_BORDER_OUTER_TOP:
      return "top outer frame";
    case PIVOT_BORDER_OUTER_RIGHT:
      return "right outer frame";
    case PIVOT_BORDER_OUTER_BOTTOM:
      return "bottom outer frame";

    case PIVOT_BORDER_INNER_LEFT:
      return "left inner frame";
    case PIVOT_BORDER_INNER_TOP:
      return "top inner frame";
    case PIVOT_BORDER_INNER_RIGHT:
      return "right inner frame";
    case PIVOT_BORDER_INNER_BOTTOM:
      return "bottom inner frame";

    case PIVOT_BORDER_DATA_LEFT:
      return "data area left";
    case PIVOT_BORDER_DATA_TOP:
      return "data area top";

    case PIVOT_BORDER_DIM_ROW_HORZ:
      return "row label horizontal dimension border";
    case PIVOT_BORDER_DIM_ROW_VERT:
      return "row label vertical dimension border";
    case PIVOT_BORDER_DIM_COL_HORZ:
      return "column label horizontal dimension border";
    case PIVOT_BORDER_DIM_COL_VERT:
      return "column label vertical dimension border";

    case PIVOT_BORDER_CAT_ROW_HORZ:
      return "row label horizontal category border";
    case PIVOT_BORDER_CAT_ROW_VERT:
      return "row label vertical category border";
    case PIVOT_BORDER_CAT_COL_HORZ:
      return "column label horizontal category border";
    case PIVOT_BORDER_CAT_COL_VERT:
      return "column label vertical category border";

    case PIVOT_N_BORDERS:
    default:
      return "**error**";
    }
}

void
pivot_table_sizing_uninit (struct pivot_table_sizing *sizing)
{
  if (sizing)
    {
      free (sizing->widths);
      free (sizing->breaks);
      free (sizing->keeps);
    }
}

/* Pivot table looks. */

static const struct pivot_table_look *
default_look (const struct pivot_table_look *new)
{
  static struct pivot_table_look *look;
  if (new)
    {
      pivot_table_look_unref (look);
      look = pivot_table_look_ref (new);
    }
  else if (!look)
    {
      char *error = pivot_table_look_read ("default.stt", &look);
      if (error)
        {
          free (error);
          look = pivot_table_look_ref (pivot_table_look_builtin_default ());
        }
    }
  return look;
}

const struct pivot_table_look *
pivot_table_look_get_default (void)
{
  return default_look (NULL);
}

void
pivot_table_look_set_default (const struct pivot_table_look *look)
{
  default_look (look);
}

char * WARN_UNUSED_RESULT
pivot_table_look_read (const char *name, struct pivot_table_look **lookp)
{
  *lookp = NULL;

  /* Construct search path. */
  const char *path[4];
  size_t n = 0;
  path[n++] = ".";
  const char *home = getenv ("HOME");
  char *allocated = NULL;
  if (home != NULL)
    path[n++] = allocated = xasprintf ("%s/.pspp/looks", home);
  char *allocated2;
  path[n++] = relocate2 (PKGDATADIR "/looks", &allocated2);
  path[n++] = NULL;

  /* Search path. */
  char *file = fn_search_path (name, (char **) path);
  if (!file)
    {
      char *name2 = xasprintf ("%s.stt", name);
      file = fn_search_path (name2, (char **) path);
      free (name2);
    }
  free (allocated);
  free (allocated2);
  if (!file)
    return xasprintf ("%s: not found", name);

  /* Read file. */
  char *error = spv_table_look_read (file, lookp);
  free (file);
  return error;
}

const struct pivot_table_look *
pivot_table_look_builtin_default (void)
{
  static struct pivot_table_look look = {
    .ref_cnt = 1,

    .omit_empty = true,
    .row_labels_in_corner = true,
    .col_heading_width_range = { 36, 72 },
    .row_heading_width_range = { 36, 120 },

    .areas = {
#define AREA(BOLD, H, V, L, R, T, B) {                         \
    .cell_style = {                                             \
      .halign = TABLE_HALIGN_##H,                               \
      .valign = TABLE_VALIGN_##V,                               \
      .margin = { [TABLE_HORZ][0] = L, [TABLE_HORZ][1] = R,     \
                  [TABLE_VERT][0] = T, [TABLE_VERT][1] = B },   \
    },                                                          \
    .font_style = {                                             \
      .bold = BOLD,                                             \
      .fg = { [0] = CELL_COLOR_BLACK, [1] = CELL_COLOR_BLACK},  \
      .bg = { [0] = CELL_COLOR_WHITE, [1] = CELL_COLOR_WHITE},  \
      .size = 9,                                                \
      .typeface = (char *) "Sans Serif",                        \
    },                                                          \
  }
      [PIVOT_AREA_TITLE]         = AREA(true,  CENTER, CENTER,  8,11,1,8),
      [PIVOT_AREA_CAPTION]       = AREA(false, LEFT,   TOP,     8,11,1,1),
      [PIVOT_AREA_FOOTER]        = AREA(false, LEFT,   TOP,    11, 8,2,3),
      [PIVOT_AREA_CORNER]        = AREA(false, LEFT,   BOTTOM,  8,11,1,1),
      [PIVOT_AREA_COLUMN_LABELS] = AREA(false, CENTER, BOTTOM,  8,11,1,3),
      [PIVOT_AREA_ROW_LABELS]    = AREA(false, LEFT,   TOP,     8,11,1,3),
      [PIVOT_AREA_DATA]          = AREA(false, MIXED,  TOP,     8,11,1,1),
      [PIVOT_AREA_LAYERS]        = AREA(false, LEFT,   BOTTOM,  8,11,1,3),
#undef AREA
    },

    .borders = {
#define BORDER(STROKE) { .stroke = STROKE, .color = CELL_COLOR_BLACK }
      [PIVOT_BORDER_TITLE]        = BORDER(TABLE_STROKE_NONE),
      [PIVOT_BORDER_OUTER_LEFT]   = BORDER(TABLE_STROKE_NONE),
      [PIVOT_BORDER_OUTER_TOP]    = BORDER(TABLE_STROKE_NONE),
      [PIVOT_BORDER_OUTER_RIGHT]  = BORDER(TABLE_STROKE_NONE),
      [PIVOT_BORDER_OUTER_BOTTOM] = BORDER(TABLE_STROKE_NONE),
      [PIVOT_BORDER_INNER_LEFT]   = BORDER(TABLE_STROKE_THICK),
      [PIVOT_BORDER_INNER_TOP]    = BORDER(TABLE_STROKE_THICK),
      [PIVOT_BORDER_INNER_RIGHT]  = BORDER(TABLE_STROKE_THICK),
      [PIVOT_BORDER_INNER_BOTTOM] = BORDER(TABLE_STROKE_THICK),
      [PIVOT_BORDER_DATA_LEFT]    = BORDER(TABLE_STROKE_THICK),
      [PIVOT_BORDER_DATA_TOP]     = BORDER(TABLE_STROKE_THICK),
      [PIVOT_BORDER_DIM_ROW_HORZ] = BORDER(TABLE_STROKE_SOLID),
      [PIVOT_BORDER_DIM_ROW_VERT] = BORDER(TABLE_STROKE_NONE),
      [PIVOT_BORDER_DIM_COL_HORZ] = BORDER(TABLE_STROKE_SOLID),
      [PIVOT_BORDER_DIM_COL_VERT] = BORDER(TABLE_STROKE_SOLID),
      [PIVOT_BORDER_CAT_ROW_HORZ] = BORDER(TABLE_STROKE_NONE),
      [PIVOT_BORDER_CAT_ROW_VERT] = BORDER(TABLE_STROKE_NONE),
      [PIVOT_BORDER_CAT_COL_HORZ] = BORDER(TABLE_STROKE_SOLID),
      [PIVOT_BORDER_CAT_COL_VERT] = BORDER(TABLE_STROKE_SOLID),
    },
  };

  return &look;
}

struct pivot_table_look *
pivot_table_look_new_builtin_default (void)
{
  return pivot_table_look_unshare (
    pivot_table_look_ref (pivot_table_look_builtin_default ()));
}

struct pivot_table_look *
pivot_table_look_ref (const struct pivot_table_look *look_)
{
  assert (look_->ref_cnt > 0);

  struct pivot_table_look *look = CONST_CAST (struct pivot_table_look *, look_);
  look->ref_cnt++;
  return look;
}

static char *
xstrdup_if_nonempty (const char *s)
{
  return s && s[0] ? xstrdup (s) : NULL;
}

struct pivot_table_look *
pivot_table_look_unshare (struct pivot_table_look *old)
{
  assert (old->ref_cnt > 0);
  if (old->ref_cnt == 1)
    return old;

  pivot_table_look_unref (old);

  struct pivot_table_look *new = xmemdup (old, sizeof *old);
  new->ref_cnt = 1;
  new->name = xstrdup_if_nonempty (old->name);
  new->file_name = xstrdup_if_nonempty (old->name);
  for (size_t i = 0; i < PIVOT_N_AREAS; i++)
    table_area_style_copy (NULL, &new->areas[i], &old->areas[i]);
  new->continuation = xstrdup_if_nonempty (old->continuation);

  return new;
}

void
pivot_table_look_unref (struct pivot_table_look *look)
{
  if (look)
    {
      assert (look->ref_cnt > 0);
      if (!--look->ref_cnt)
        {
          free (look->name);
          free (look->file_name);
          for (size_t i = 0; i < PIVOT_N_AREAS; i++)
            table_area_style_uninit (&look->areas[i]);
          free (look->continuation);
          free (look);
        }
    }
}

/* Axes. */

/* Returns the name of AXIS_TYPE. */
const char *
pivot_axis_type_to_string (enum pivot_axis_type axis_type)
{
  switch (axis_type)
    {
    case PIVOT_AXIS_LAYER:
      return "layer";

    case PIVOT_AXIS_ROW:
      return "row";

    case PIVOT_AXIS_COLUMN:
      return "column";

    default:
      return "<error>";
    }
}

static enum pivot_axis_type
pivot_axis_type_transpose (enum pivot_axis_type axis_type)
{
  assert (axis_type == PIVOT_AXIS_ROW || axis_type == PIVOT_AXIS_COLUMN);
  return (axis_type == PIVOT_AXIS_ROW ? PIVOT_AXIS_COLUMN : PIVOT_AXIS_ROW);
}

/* Implementation of PIVOT_AXIS_FOR_EACH. */
size_t *
pivot_axis_iterator_next (size_t *indexes, const struct pivot_axis *axis)
{
  if (!indexes)
    {
      if (axis->n_dimensions)
        for (size_t i = 0; i < axis->n_dimensions; i++)
          if (axis->dimensions[i]->n_leaves == 0)
            return NULL;

      size_t size = axis->n_dimensions * sizeof *indexes;
      return xzalloc (MAX (size, 1));
    }

  for (size_t i = 0; i < axis->n_dimensions; i++)
    {
      const struct pivot_dimension *d = axis->dimensions[i];
      if (++indexes[i] < d->n_leaves)
        return indexes;

      indexes[i] = 0;
    }

  free (indexes);
  return NULL;
}

/* Dimensions. */

static void
pivot_category_set_rc (struct pivot_category *category, const char *s)
{
  if (!s)
    return;

  pivot_table_use_rc (category->dimension->table, s,
                      &category->format, &category->honor_small);

  /* Ensure that the category itself, in addition to the cells within it, takes
     the format.  (It's kind of rare for a category to have a numeric format
     though.) */
  struct pivot_value *name = category->name;
  if (name->type == PIVOT_VALUE_NUMERIC && !name->numeric.format.w)
    pivot_table_use_rc (category->dimension->table, s,
                        &name->numeric.format, &name->numeric.honor_small);
}

static void
pivot_category_create_leaves_valist (struct pivot_category *parent,
                                     va_list args)
{
  const char *s;
  while ((s = va_arg (args, const char *)))
    {
      if (!strncmp (s, "RC_", 3))
        {
          assert (parent->n_subs);
          pivot_category_set_rc (parent->subs[parent->n_subs - 1], s);
        }
      else
        pivot_category_create_leaf (parent, pivot_value_new_text (s));
    }
}

/* Creates a new dimension with the given NAME in TABLE and returns it.  The
   dimension is added to axis AXIS_TYPE, becoming the outermost dimension on
   that axis.

   NAME should be a translatable name, but not actually translated yet,
   e.g. enclosed in N_().  To use a different kind of value for a name, use
   pivot_dimension_create__() instead.

   The optional varargs parameters may be used to add an initial set of
   categories to the dimension.  Each string should be a translatable category
   name, but not actually translated yet, e.g. enclosed in N_().  Each string
   may optionally be followod by a PIVOT_RC_* string that specifies the default
   numeric format for cells in this category. */
struct pivot_dimension * SENTINEL (0)
(pivot_dimension_create) (struct pivot_table *table,
                          enum pivot_axis_type axis_type,
                          const char *name, ...)
{
  struct pivot_dimension *d = pivot_dimension_create__ (
    table, axis_type, pivot_value_new_text (name));

  va_list args;
  va_start (args, name);
  pivot_category_create_leaves_valist (d->root, args);
  va_end (args);

  return d;
}

/* Creates a new dimension with the given NAME in TABLE and returns it.  The
   dimension is added to axis AXIS_TYPE, becoming the outermost dimension on
   that axis. */
struct pivot_dimension *
pivot_dimension_create__ (struct pivot_table *table,
                          enum pivot_axis_type axis_type,
                          struct pivot_value *name)
{
  assert (pivot_table_is_empty (table));

  struct pivot_dimension *d = xmalloc (sizeof *d);
  *d = (struct pivot_dimension) {
    .table = table,
    .axis_type = axis_type,
    .level = table->axes[axis_type].n_dimensions,
    .top_index = table->n_dimensions,
    .root = xmalloc (sizeof *d->root),
  };

  struct pivot_category *root = d->root;
  *root = (struct pivot_category) {
    .name = name,
    .parent = NULL,
    .dimension = d,
    .show_label = false,
    .data_index = SIZE_MAX,
    .presentation_index = SIZE_MAX,
  };

  table->dimensions = xrealloc (
    table->dimensions, (table->n_dimensions + 1) * sizeof *table->dimensions);
  table->dimensions[table->n_dimensions++] = d;

  struct pivot_axis *axis = &table->axes[axis_type];
  axis->dimensions = xrealloc (
    axis->dimensions, (axis->n_dimensions + 1) * sizeof *axis->dimensions);
  axis->dimensions[axis->n_dimensions++] = d;

  if (axis_type == PIVOT_AXIS_LAYER)
    {
      free (table->current_layer);
      table->current_layer = xcalloc (axis[PIVOT_AXIS_LAYER].n_dimensions,
                                      sizeof *table->current_layer);
    }

  /* axis->extent and axis->label_depth will be calculated later. */

  return d;
}

void
pivot_dimension_destroy (struct pivot_dimension *d)
{
  if (!d)
    return;

  pivot_category_destroy (d->root);
  free (d->data_leaves);
  free (d->presentation_leaves);
  free (d);
}

/* Returns the first leaf node in an in-order traversal that is a child of
   CAT. */
static const struct pivot_category * UNUSED
pivot_category_first_leaf (const struct pivot_category *cat)
{
  if (pivot_category_is_leaf (cat))
    return cat;

  for (size_t i = 0; i < cat->n_subs; i++)
    {
      const struct pivot_category *first
        = pivot_category_first_leaf (cat->subs[i]);
      if (first)
        return first;
    }

  return NULL;
}

/* Returns the next leaf node in an in-order traversal starting at CAT, which
   must be a leaf. */
static const struct pivot_category * UNUSED
pivot_category_next_leaf (const struct pivot_category *cat)
{
  assert (pivot_category_is_leaf (cat));

  for (;;)
    {
      const struct pivot_category *parent = cat->parent;
      if (!parent)
        return NULL;
      for (size_t i = cat->group_index + 1; i < parent->n_subs; i++)
        {
          const struct pivot_category *next
            = pivot_category_first_leaf (parent->subs[i]);
          if (next)
            return next;
        }

      cat = cat->parent;
    }
}

static void
pivot_category_add_child (struct pivot_category *child)
{
  struct pivot_category *parent = child->parent;

  assert (pivot_category_is_group (parent));
  if (parent->n_subs >= parent->allocated_subs)
    parent->subs = x2nrealloc (parent->subs, &parent->allocated_subs,
                               sizeof *parent->subs);
  parent->subs[parent->n_subs++] = child;
}

/* Adds leaf categories as a child of PARENT.  To create top-level categories
   within dimension 'd', pass 'd->root' for PARENT.

   Each of the varargs parameters should be a string, each of which should be a
   translatable category name, but not actually translated yet, e.g. enclosed
   in N_().  Each string may optionally be followod by a PIVOT_RC_* string that
   specifies the default numeric format for cells in this category.

   Returns the category index, which is just a 0-based array index, for the
   first new category.

   Leaves have to be created in in-order, that is, don't create a group and add
   some leaves, then add leaves outside the group and try to add more leaves
   inside it. */
int SENTINEL (0)
(pivot_category_create_leaves) (struct pivot_category *parent, ...)
{
  int retval = parent->dimension->n_leaves;

  va_list args;
  va_start (args, parent);
  pivot_category_create_leaves_valist (parent, args);
  va_end (args);

  return retval;
}

/* Creates a new leaf category with the given NAME as a child of PARENT.  To
   create a top-level category within dimension 'd', pass 'd->root' for PARENT.
   Returns the category index, which is just a 0-based array index, for the new
   category.

   Leaves have to be created in in-order, that is, don't create a group and add
   some leaves, then add leaves outside the group and try to add more leaves
   inside it. */
int
pivot_category_create_leaf (struct pivot_category *parent,
                            struct pivot_value *name)
{
  return pivot_category_create_leaf_rc (parent, name, NULL);
}

/* Creates a new leaf category with the given NAME as a child of PARENT.  To
   create a top-level category within dimension 'd', pass 'd->root' for PARENT.
   Returns the category index, which is just a 0-based array index, for the new
   category.

   If RC is nonnull and the name of a result category, the category is assigned
   that result category.

   Leaves have to be created in in-order, that is, don't create a group and add
   some leaves, then add leaves outside the group and try to add more leaves
   inside it. */
int
pivot_category_create_leaf_rc (struct pivot_category *parent,
                               struct pivot_value *name, const char *rc)
{
  struct pivot_dimension *d = parent->dimension;

  struct pivot_category *leaf = xmalloc (sizeof *leaf);
  *leaf = (struct pivot_category) {
    .name = name,
    .parent = parent,
    .dimension = d,
    .group_index = parent->n_subs,
    .data_index = d->n_leaves,
    .presentation_index = d->n_leaves,
  };

  if (d->n_leaves >= d->allocated_leaves)
    {
      d->data_leaves = x2nrealloc (d->data_leaves, &d->allocated_leaves,
                                   sizeof *d->data_leaves);
      d->presentation_leaves = xrealloc (
        d->presentation_leaves,
        d->allocated_leaves * sizeof *d->presentation_leaves);
    }

  d->data_leaves[d->n_leaves] = leaf;
  d->presentation_leaves[d->n_leaves] = leaf;
  d->n_leaves++;

  pivot_category_add_child (leaf);

  /* Make sure that the new child is the last in in-order. */
  assert (!pivot_category_next_leaf (leaf));

  pivot_category_set_rc (leaf, rc);

  return leaf->data_index;
}

/* Adds a new category group named NAME as a child of PARENT.  To create a
   top-level group within dimension 'd', pass 'd->root' for PARENT.

   NAME should be a translatable name, but not actually translated yet,
   e.g. enclosed in N_().  To use a different kind of value for a name, use
   pivot_category_create_group__() instead.

   The optional varargs parameters may be used to add an initial set of
   categories to the group.  Each string should be a translatable category
   name, but not actually translated yet, e.g. enclosed in N_().  Each string
   may optionally be followod by a PIVOT_RC_* string that specifies the default
   numeric format for cells in this category.

   Returns the new group. */
struct pivot_category * SENTINEL (0)
(pivot_category_create_group) (struct pivot_category *parent,
                               const char *name, ...)
{
  struct pivot_category *group = pivot_category_create_group__ (
    parent, pivot_value_new_text (name));

  va_list args;
  va_start (args, name);
  pivot_category_create_leaves_valist (group, args);
  va_end (args);

  return group;
}

/* Adds a new category group named NAME as a child of PARENT.  To create a
   top-level group within dimension 'd', pass 'd->root' for PARENT.  Returns
   the new group. */
struct pivot_category *
pivot_category_create_group__ (struct pivot_category *parent,
                               struct pivot_value *name)
{
  struct pivot_dimension *d = parent->dimension;

  struct pivot_category *group = xmalloc (sizeof *group);
  *group = (struct pivot_category) {
    .name = name,
    .parent = parent,
    .dimension = d,
    .show_label = true,
    .group_index = parent->n_subs,
    .data_index = SIZE_MAX,
    .presentation_index = SIZE_MAX,
  };

  pivot_category_add_child (group);

  return group;
}

void
pivot_category_destroy (struct pivot_category *c)
{
  if (!c)
    return;

  pivot_value_destroy (c->name);
  for (size_t i = 0; i < c->n_subs; i++)
    pivot_category_destroy (c->subs[i]);
  free (c->subs);
  free (c);
}

/* Result classes.

   These are usually the easiest way to control the formatting of numeric data
   in a pivot table.  See pivot_dimension_create() for an explanation of their
   use.  */
struct result_class
  {
    const char *name;           /* "RC_*". */
    struct fmt_spec format;
  };

/* Formats for most of the result classes. */
static struct result_class result_classes[] =
  {
    { PIVOT_RC_INTEGER,      { .type = FMT_F,   .w = 40, .d = 0 } },
    { PIVOT_RC_PERCENT,      { .type = FMT_PCT, .w = 40, .d = 1 } },
    { PIVOT_RC_CORRELATION,  { .type = FMT_F,   .w = 40, .d = 3 } },
    { PIVOT_RC_SIGNIFICANCE, { .type = FMT_F,   .w = 40, .d = 3 } },
    { PIVOT_RC_RESIDUAL,     { .type = FMT_F,   .w = 40, .d = 2 } },
    { PIVOT_RC_COUNT,        { 0, 0, 0 } },
    { PIVOT_RC_OTHER,        { 0, 0, 0 } },
  };

/* Has PIVOT_RC_COUNT been overridden by the user? */
static bool overridden_count_format;

static struct result_class *
pivot_result_class_find (const char *s)
{
  for (size_t i = 0; i < sizeof result_classes / sizeof *result_classes; i++)
    if (!strcmp (s, result_classes[i].name))
      return &result_classes[i];
  return NULL;
}

static void
pivot_table_use_rc (const struct pivot_table *table, const char *s,
                    struct fmt_spec *format, bool *honor_small)
{
  if (s)
    {
      if (!strcmp (s, PIVOT_RC_OTHER))
        {
          *format = settings_get_format ();
          *honor_small = true;
        }
      else if (!strcmp (s, PIVOT_RC_COUNT) && !overridden_count_format)
        {
          *format = table->weight_format;
          *honor_small = false;
        }
      else
        {
          const struct result_class *rc = pivot_result_class_find (s);
          if (rc)
            {
              *format = rc->format;
              *honor_small = false;
            }
          else
            {
              printf ("unknown class %s\n", s);
            }
        }
    }
}

/* Sets the format specification for the result class named S (which should not
   include the RC_ prefix) to *FORMAT.  Returns true if successful, false if S
   does not name a known result class. */
bool
pivot_result_class_change (const char *s_, struct fmt_spec format)
{
  char *s = xasprintf ("RC_%s", s_);
  struct result_class *rc = pivot_result_class_find (s);
  if (rc)
    {
      rc->format = format;
      if (!strcmp (s, PIVOT_RC_COUNT))
        overridden_count_format = true;
    }
  free (s);

  return rc != NULL;
}

bool
is_pivot_result_class (const char *s)
{
  return pivot_result_class_find (s) != NULL;
}

/* Pivot tables. */

static struct pivot_cell *pivot_table_insert_cell (struct pivot_table *,
                                                   const size_t *dindexes);
static void pivot_table_delete_cell (struct pivot_table *,
                                     struct pivot_cell *);

/* Creates and returns a new pivot table with the given TITLE.  TITLE should be
   a text string marked for translation but not actually translated yet,
   e.g. N_("Descriptive Statistics").  The un-translated text string is used as
   the pivot table's subtype.

   This function is a shortcut for pivot_table_create__() for the most common
   case.  Use pivot_table_create__() directly if the title should be some kind
   of value other than an ordinary text string, or if the subtype should be
   different from the title.

   See the large comment at the top of pivot-table.h for general advice on
   creating pivot tables. */
struct pivot_table *
pivot_table_create (const char *title)
{
  return pivot_table_create__ (pivot_value_new_text (title), title);
}

static const char *mtable[12] =
  {"jan", "feb", "mar", "apr", "may", "jun",
   "jul", "aug", "sep", "oct", "nov", "dec"};

/* Expand S replacing expressions as necessary */
static char *summary_expansion (const char *s)
{
  if (!s || !s[0])
    return NULL;

  struct string comment;
  ds_init_empty (&comment);
  time_t now = time (NULL);
  struct tm *lt = localtime (&now);
  while (*s)
    {
      switch (*s)
        {
        case ')':
          if (0 == strncmp (s + 1, "DATE", 4))
            {
              s += 4;
              if (lt)
                ds_put_c_format (&comment, "%02d-%s-%04d",
                                 lt->tm_mday, mtable[lt->tm_mon], lt->tm_year + 1900);
            }
          else if (0 == strncmp (s + 1, "ADATE", 5))
            {
              s += 5;
              if (lt)
                ds_put_c_format (&comment, "%02d/%02d/%04d",
                                 lt->tm_mon + 1, lt->tm_mday, lt->tm_year + 1900);
            }
          else if (0 == strncmp (s + 1, "SDATE", 5))
            {
              s += 5;
              if (lt)
                ds_put_c_format (&comment, "%04d/%02d/%02d",
                                 lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
            }
          else if (0 == strncmp (s + 1, "EDATE", 5))
            {
              s += 5;
              if (lt)
                ds_put_c_format (&comment, "%02d.%02d.%04d",
                                 lt->tm_mday, lt->tm_mon + 1, lt->tm_year + 1900);
            }
          else if (0 == strncmp (s + 1, "TIME", 4))
            {
              s += 4;
              if (lt)
                {
                  /* 12 hour time format */
                  int hour = lt->tm_hour % 12;
                  if (hour == 0)
                    hour = 12;
                  ds_put_c_format (&comment, "%02d:%02d:%02d",
                                   hour, lt->tm_min, lt->tm_sec);
                }
            }
          else if (0 == strncmp (s + 1, "ETIME", 5))
            {
              s += 5;
              if (lt)
                ds_put_c_format (&comment, "%02d:%02d:%02d",
                                 lt->tm_hour, lt->tm_min, lt->tm_sec);
            }
          break;
        case '\\':
          if (s[1] == 'n')
            {
              s++;
              ds_put_byte (&comment, '\n');
            }
          break;
        default:
          ds_put_byte (&comment, *s);
          break;
        }
      s++;
    }

  char *string = ds_steal_cstr (&comment);

  ds_destroy (&comment);

  return string;
}


/* Creates and returns a new pivot table with the given TITLE, and takes
   ownership of TITLE.  The new pivot table's subtype is SUBTYPE, which should
   be an untranslated English string that describes the contents of the table
   at a high level without being specific about the variables or other context
   involved.

   TITLE and SUBTYPE may be NULL, but in that case the client must add them
   later because they are both mandatory for a pivot table.

   See the large comment at the top of pivot-table.h for general advice on
   creating pivot tables. */
struct pivot_table *
pivot_table_create__ (struct pivot_value *title, const char *subtype)
{
  struct pivot_table *table = xmalloc (sizeof *table);
  *table = (struct pivot_table) {
    .ref_cnt = 1,
    .show_title = true,
    .show_caption = true,
    .weight_format = (struct fmt_spec) { .type = FMT_F, .w = 40 },
    .title = title,
    .notes = summary_expansion (settings_get_summary ()),
    .subtype = subtype ? pivot_value_new_text (subtype) : NULL,
    .command_c = xstrdup_if_nonempty (output_get_command_name ()),
    .look = pivot_table_look_ref (pivot_table_look_get_default ()),
    .settings = fmt_settings_copy (settings_get_fmt_settings ()),
    .small = settings_get_small (),
    .cells = HMAP_INITIALIZER (table->cells),
  };
  return table;
}

/* Creates and returns a new pivot table with the given TITLE and a single cell
   with the given CONTENT.

   This is really just for error handling. */
struct pivot_table *
pivot_table_create_for_text (struct pivot_value *title,
                             struct pivot_value *content)
{
  struct pivot_table *table = pivot_table_create__ (title, "Error");

  struct pivot_dimension *d = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Error"));
  d->hide_all_labels = true;
  pivot_category_create_leaf (d->root, pivot_value_new_text ("null"));

  pivot_table_put1 (table, 0, content);

  return table;
}

/* Increases TABLE's reference count, indicating that it has an additional
   owner.  A pivot table that is shared among multiple owners must not be
   modified. */
struct pivot_table *
pivot_table_ref (const struct pivot_table *table_)
{
  struct pivot_table *table = CONST_CAST (struct pivot_table *, table_);
  table->ref_cnt++;
  return table;
}

static struct pivot_table_sizing
clone_sizing (const struct pivot_table_sizing *s)
{
  return (struct pivot_table_sizing) {
    .widths = (s->n_widths
               ? xmemdup (s->widths, s->n_widths * sizeof *s->widths)
               : NULL),
    .n_widths = s->n_widths,

    .breaks = (s->n_breaks
               ? xmemdup (s->breaks, s->n_breaks * sizeof *s->breaks)
               : NULL),
    .n_breaks = s->n_breaks,

    .keeps = (s->n_keeps
              ? xmemdup (s->keeps, s->n_keeps * sizeof *s->keeps)
              : NULL),
    .n_keeps = s->n_keeps,
  };
}

static struct pivot_footnote **
clone_footnotes (struct pivot_footnote **old, size_t n)
{
  if (!n)
    return NULL;

  struct pivot_footnote **new = xmalloc (n * sizeof *new);
  for (size_t i = 0; i < n; i++)
    {
      new[i] = xmalloc (sizeof *new[i]);
      *new[i] = (struct pivot_footnote) {
        .idx = old[i]->idx,
        .content = pivot_value_clone (old[i]->content),
        .marker = pivot_value_clone (old[i]->marker),
        .show = old[i]->show,
      };
    }
  return new;
}

static struct pivot_category *
clone_category (struct pivot_category *old,
                struct pivot_dimension *new_dimension,
                struct pivot_category *new_parent)
{
  struct pivot_category *new = xmalloc (sizeof *new);
  *new = (struct pivot_category) {
    .name = pivot_value_clone (old->name),
    .parent = new_parent,
    .dimension = new_dimension,
    .label_depth = old->label_depth,
    .extra_depth = old->extra_depth,

    .subs = (old->n_subs
             ? xcalloc (old->n_subs, sizeof *new->subs)
             : NULL),
    .n_subs = old->n_subs,
    .allocated_subs = old->n_subs,

    .show_label = old->show_label,
    .show_label_in_corner = old->show_label_in_corner,

    .format = old->format,
    .group_index = old->group_index,
    .data_index = old->data_index,
    .presentation_index = old->presentation_index,
  };

  if (pivot_category_is_leaf (old))
    {
      assert (new->data_index < new_dimension->n_leaves);
      new->dimension->data_leaves[new->data_index] = new;

      assert (new->presentation_index < new_dimension->n_leaves);
      new->dimension->presentation_leaves[new->presentation_index] = new;
    }

  for (size_t i = 0; i < new->n_subs; i++)
    new->subs[i] = clone_category (old->subs[i], new_dimension, new);

  return new;
}

static struct pivot_dimension *
clone_dimension (struct pivot_dimension *old, struct pivot_table *new_pt)
{
  struct pivot_dimension *new = xmalloc (sizeof *new);
  *new = (struct pivot_dimension) {
    .table = new_pt,
    .axis_type = old->axis_type,
    .level = old->level,
    .top_index = old->top_index,
    .data_leaves = xcalloc (old->n_leaves , sizeof *new->data_leaves),
    .presentation_leaves = xcalloc (old->n_leaves
                                    , sizeof *new->presentation_leaves),
    .n_leaves = old->n_leaves,
    .allocated_leaves = old->n_leaves,
    .hide_all_labels = old->hide_all_labels,
    .label_depth = old->label_depth,
  };

  new->root = clone_category (old->root, new, NULL);

  return new;
}

static struct pivot_dimension **
clone_dimensions (struct pivot_dimension **old, size_t n,
                  struct pivot_table *new_pt)
{
  if (!n)
    return NULL;

  struct pivot_dimension **new = xmalloc (n * sizeof *new);
  for (size_t i = 0; i < n; i++)
    new[i] = clone_dimension (old[i], new_pt);
  return new;
}

struct pivot_table *
pivot_table_unshare (struct pivot_table *old)
{
  assert (old->ref_cnt > 0);
  if (old->ref_cnt == 1)
    return old;

  pivot_table_unref (old);

  struct pivot_table *new = xmalloc (sizeof *new);
  *new = (struct pivot_table) {
    .ref_cnt = 1,

    .look = pivot_table_look_ref (old->look),

    .rotate_inner_column_labels = old->rotate_inner_column_labels,
    .rotate_outer_row_labels = old->rotate_outer_row_labels,
    .show_grid_lines = old->show_grid_lines,
    .show_title = old->show_title,
    .show_caption = old->show_caption,
    .current_layer = (old->current_layer
                      ? xmemdup (old->current_layer,
                                 old->axes[PIVOT_AXIS_LAYER].n_dimensions
                                 * sizeof *new->current_layer)
                      : NULL),
    .show_values = old->show_values,
    .show_variables = old->show_variables,
    .weight_format = old->weight_format,

    .sizing = {
      [TABLE_HORZ] = clone_sizing (&old->sizing[TABLE_HORZ]),
      [TABLE_VERT] = clone_sizing (&old->sizing[TABLE_VERT]),
    },

    .settings = fmt_settings_copy (&old->settings),
    .grouping = old->grouping,
    .small = old->small,

    .command_local = xstrdup_if_nonnull (old->command_local),
    .command_c = xstrdup_if_nonnull (old->command_c),
    .language = xstrdup_if_nonnull (old->language),
    .locale = xstrdup_if_nonnull (old->locale),

    .dataset = xstrdup_if_nonnull (old->dataset),
    .datafile = xstrdup_if_nonnull (old->datafile),
    .date = old->date,

    .footnotes = clone_footnotes (old->footnotes, old->n_footnotes),
    .n_footnotes = old->n_footnotes,
    .allocated_footnotes = old->n_footnotes,

    .title = pivot_value_clone (old->title),
    .subtype = pivot_value_clone (old->subtype),
    .corner_text = pivot_value_clone (old->corner_text),
    .caption = pivot_value_clone (old->caption),
    .notes = xstrdup_if_nonnull (old->notes),

    .dimensions = clone_dimensions (old->dimensions, old->n_dimensions, new),
    .n_dimensions = old->n_dimensions,

    .cells = HMAP_INITIALIZER (new->cells),
  };

  for (size_t i = 0; i < PIVOT_N_AXES; i++)
    {
      struct pivot_axis *new_axis = &new->axes[i];
      const struct pivot_axis *old_axis = &old->axes[i];

      *new_axis = (struct pivot_axis) {
        .dimensions = xmalloc (old_axis->n_dimensions
                               * sizeof *new_axis->dimensions),
        .n_dimensions = old_axis->n_dimensions,
        .extent = old_axis->extent,
        .label_depth = old_axis->label_depth,
      };

      for (size_t i = 0; i < new_axis->n_dimensions; i++)
        new_axis->dimensions[i] = new->dimensions[
          old_axis->dimensions[i]->top_index];
    }

  const struct pivot_cell *old_cell;
  size_t *dindexes = xmalloc (old->n_dimensions * sizeof *dindexes);
  HMAP_FOR_EACH (old_cell, struct pivot_cell, hmap_node, &old->cells)
    {
      for (size_t i = 0; i < old->n_dimensions; i++)
        dindexes[i] = old_cell->idx[i];
      struct pivot_cell *new_cell
        = pivot_table_insert_cell (new, dindexes);
      new_cell->value = pivot_value_clone (old_cell->value);
    }
  free (dindexes);

  return new;
}

/* Decreases TABLE's reference count, indicating that it has one fewer owner.
   If TABLE no longer has any owners, it is freed. */
void
pivot_table_unref (struct pivot_table *table)
{
  if (!table)
    return;
  assert (table->ref_cnt > 0);
  if (--table->ref_cnt)
    return;

  free (table->current_layer);
  pivot_table_look_unref (table->look);

  for (int i = 0; i < TABLE_N_AXES; i++)
    pivot_table_sizing_uninit (&table->sizing[i]);

  fmt_settings_uninit (&table->settings);

  free (table->command_local);
  free (table->command_c);
  free (table->language);
  free (table->locale);

  free (table->dataset);
  free (table->datafile);

  for (size_t i = 0; i < table->n_footnotes; i++)
    pivot_footnote_destroy (table->footnotes[i]);
  free (table->footnotes);

  pivot_value_destroy (table->title);
  pivot_value_destroy (table->subtype);
  pivot_value_destroy (table->corner_text);
  pivot_value_destroy (table->caption);
  free (table->notes);

  for (size_t i = 0; i < table->n_dimensions; i++)
    pivot_dimension_destroy (table->dimensions[i]);
  free (table->dimensions);

  for (size_t i = 0; i < PIVOT_N_AXES; i++)
    free (table->axes[i].dimensions);

  struct pivot_cell *cell, *next_cell;
  HMAP_FOR_EACH_SAFE (cell, next_cell, struct pivot_cell, hmap_node,
                      &table->cells)
    pivot_table_delete_cell (table, cell);

  hmap_destroy (&table->cells);

  free (table);
}

/* Returns true if TABLE has more than one owner.  A pivot table that is shared
   among multiple owners must not be modified. */
bool
pivot_table_is_shared (const struct pivot_table *table)
{
  return table->ref_cnt > 1;
}

static void
pivot_table_set_value__ (struct pivot_value **dstp, struct pivot_value *src)
{
  pivot_value_destroy (*dstp);
  *dstp = src;
}

/* Changes the title of TABLE to TITLE.  Takes ownership of TITLE. */
void
pivot_table_set_title (struct pivot_table *table, struct pivot_value *title)
{
  pivot_table_set_value__ (&table->title, title);
}

/* Changes the subtype of TABLE to SUBTYPE.  Takes ownership of SUBTYPE. */
void
pivot_table_set_subtype (struct pivot_table *table, struct pivot_value *subtype)
{
  pivot_table_set_value__ (&table->subtype, subtype);
}

/* Changes the corner text of TABLE to CORNER_TEXT.  Takes ownership of
   CORNER_TEXT. */
void
pivot_table_set_corner_text (struct pivot_table *table,
                             struct pivot_value *corner_text)
{
  pivot_table_set_value__ (&table->corner_text, corner_text);
}

/* Changes the caption of TABLE to CAPTION.  Takes ownership of CAPTION. */
void
pivot_table_set_caption (struct pivot_table *table, struct pivot_value *caption)
{
  pivot_table_set_value__ (&table->caption, caption);
}

/* Swaps axes A and B in TABLE. */
void
pivot_table_swap_axes (struct pivot_table *table,
                       enum pivot_axis_type a, enum pivot_axis_type b)
{
  if (a == b)
    return;

  struct pivot_axis tmp = table->axes[a];
  table->axes[a] = table->axes[b];
  table->axes[b] = tmp;

  for (int a = 0; a < PIVOT_N_AXES; a++)
    {
      struct pivot_axis *axis = &table->axes[a];
      for (size_t d = 0; d < axis->n_dimensions; d++)
        axis->dimensions[d]->axis_type = a;
    }

  if (a == PIVOT_AXIS_LAYER || b == PIVOT_AXIS_LAYER)
    {
      free (table->current_layer);
      table->current_layer = xzalloc (
        table->axes[PIVOT_AXIS_LAYER].n_dimensions
        * sizeof *table->current_layer);
    }
}

/* Swaps the row and column axes in TABLE. */
void
pivot_table_transpose (struct pivot_table *table)
{
  pivot_table_swap_axes (table, PIVOT_AXIS_ROW, PIVOT_AXIS_COLUMN);
}

static void
pivot_table_update_axes (struct pivot_table *table)
{
  for (int a = 0; a < PIVOT_N_AXES; a++)
    {
      struct pivot_axis *axis = &table->axes[a];

      for (size_t d = 0; d < axis->n_dimensions; d++)
        {
          struct pivot_dimension *dim = axis->dimensions[d];
          dim->axis_type = a;
          dim->level = d;
        }
    }
}

/* Moves DIM from its current location in TABLE to POS within AXIS.  POS of 0
   is the innermost dimension, 1 is the next one out, and so on. */
void
pivot_table_move_dimension (struct pivot_table *table,
                            struct pivot_dimension *dim,
                            enum pivot_axis_type axis, size_t pos)
{
  assert (dim->table == table);

  struct pivot_axis *old_axis = &table->axes[dim->axis_type];
  struct pivot_axis *new_axis = &table->axes[axis];
  pos = MIN (pos, new_axis->n_dimensions);

  if (old_axis == new_axis && pos == dim->level)
    {
      /* No change. */
      return;
    }

  /* Update the current layer, if necessary.  If we're moving within the layer
     axis, preserve the current layer. */
  if (dim->axis_type == PIVOT_AXIS_LAYER)
    {
      if (axis == PIVOT_AXIS_LAYER)
        {
          /* Rearranging the layer axis. */
          move_element (table->current_layer, old_axis->n_dimensions,
                        sizeof *table->current_layer,
                        dim->level, pos);
        }
      else
        {
          /* A layer is becoming a row or column. */
          remove_element (table->current_layer, old_axis->n_dimensions,
                          sizeof *table->current_layer, dim->level);
        }
    }
  else if (axis == PIVOT_AXIS_LAYER)
    {
      /* A row or column is becoming a layer. */
      table->current_layer = xrealloc (
        table->current_layer,
        (new_axis->n_dimensions + 1) * sizeof *table->current_layer);
      insert_element (table->current_layer, new_axis->n_dimensions,
                      sizeof *table->current_layer, pos);
      table->current_layer[pos] = 0;
    }

  /* Remove DIM from its current axis. */
  remove_element (old_axis->dimensions, old_axis->n_dimensions,
                  sizeof *old_axis->dimensions, dim->level);
  old_axis->n_dimensions--;

  /* Insert DIM into its new axis. */
  new_axis->dimensions = xrealloc (
    new_axis->dimensions,
    (new_axis->n_dimensions + 1) * sizeof *new_axis->dimensions);
  insert_element (new_axis->dimensions, new_axis->n_dimensions,
                  sizeof *new_axis->dimensions, pos);
  new_axis->dimensions[pos] = dim;
  new_axis->n_dimensions++;

  pivot_table_update_axes (table);
}


const struct pivot_table_look *
pivot_table_get_look (const struct pivot_table *table)
{
  return table->look;
}

void
pivot_table_set_look (struct pivot_table *table,
                      const struct pivot_table_look *look)
{
  pivot_table_look_unref (table->look);
  table->look = pivot_table_look_ref (look);
}

/* Sets the format used for PIVOT_RC_COUNT cells to the one used for variable
   WV, which should be the weight variable for the dictionary whose data or
   statistics are being put into TABLE.

   This has no effect if WV is NULL. */
void
pivot_table_set_weight_var (struct pivot_table *table,
                            const struct variable *wv)
{
  if (wv)
    pivot_table_set_weight_format (table, var_get_print_format (wv));
}

/* Sets the format used for PIVOT_RC_COUNT cells to WFMT, which should be the
   format for the dictionary whose data or statistics are being put into
   TABLE. */
void
pivot_table_set_weight_format (struct pivot_table *table, struct fmt_spec wfmt)
{
  wfmt.w = 40;
  table->weight_format = wfmt;
}

/* Returns true if TABLE has no cells, false otherwise. */
bool
pivot_table_is_empty (const struct pivot_table *table)
{
  return hmap_is_empty (&table->cells);
}

static unsigned int
pivot_cell_hash_indexes (const size_t *indexes, size_t n_idx)
{
  return hash_bytes (indexes, n_idx * sizeof *indexes, 0);
}

static bool
equal_indexes (const size_t *a, const unsigned int *b, size_t n)
{
  for (size_t i = 0; i < n; i++)
    if (a[i] != b[i])
      return false;

  return true;
}

static struct pivot_cell *
pivot_table_lookup_cell__ (const struct pivot_table *table,
                            const size_t *dindexes, unsigned int hash)
{
  struct pivot_cell *cell;
  HMAP_FOR_EACH_WITH_HASH (cell, struct pivot_cell, hmap_node, hash,
                           &table->cells)
    if (equal_indexes (dindexes, cell->idx, table->n_dimensions))
      return cell;
  return false;
}

static struct pivot_cell *
pivot_cell_allocate (size_t n_idx)
{
  struct pivot_cell *cell UNUSED;
  return xmalloc (sizeof *cell + n_idx * sizeof *cell->idx);
}

static struct pivot_cell *
pivot_table_insert_cell (struct pivot_table *table, const size_t *dindexes)
{
  unsigned int hash = pivot_cell_hash_indexes (dindexes, table->n_dimensions);
  struct pivot_cell *cell = pivot_table_lookup_cell__ (table, dindexes, hash);
  if (!cell)
    {
      cell = pivot_cell_allocate (table->n_dimensions);
      for (size_t i = 0; i < table->n_dimensions; i++)
        cell->idx[i] = dindexes[i];
      cell->value = NULL;
      hmap_insert (&table->cells, &cell->hmap_node, hash);
    }
  return cell;
}

/* Puts VALUE in the cell in TABLE whose indexes are given by the N indexes in
   DINDEXES.  The order of the indexes is the same as the order in which the
   dimensions were created.  N must be the number of dimensions in TABLE.
   Takes ownership of VALUE.

   If VALUE is a numeric value without a specified format, this function checks
   each of the categories designated by DINDEXES[] and takes the format from
   the first category with a result class.  If none has a result class, uses
   the overall default numeric format. */
void
pivot_table_put (struct pivot_table *table, const size_t *dindexes, size_t n,
                 struct pivot_value *value)
{
  assert (n == table->n_dimensions);
  for (size_t i = 0; i < n; i++)
    assert (dindexes[i] < table->dimensions[i]->n_leaves);

  if (value->type == PIVOT_VALUE_NUMERIC && !value->numeric.format.w)
    {
      for (size_t i = 0; i < table->n_dimensions; i++)
        {
          const struct pivot_dimension *d = table->dimensions[i];
          if (dindexes[i] < d->n_leaves)
            {
              const struct pivot_category *c = d->data_leaves[dindexes[i]];
              if (c->format.w)
                {
                  value->numeric.format = c->format;
                  value->numeric.honor_small = c->honor_small;
                  goto done;
                }
            }
        }
      value->numeric.format = settings_get_format ();
      value->numeric.honor_small = true;

    done:;
    }

  struct pivot_cell *cell = pivot_table_insert_cell (table, dindexes);
  pivot_value_destroy (cell->value);
  cell->value = value;
}

/* Puts VALUE in the cell in TABLE with index IDX1.  TABLE must have 1
   dimension.  Takes ownership of VALUE.  */
void
pivot_table_put1 (struct pivot_table *table, size_t idx1,
                  struct pivot_value *value)
{
  size_t dindexes[] = { idx1 };
  pivot_table_put (table, dindexes, sizeof dindexes / sizeof *dindexes, value);
}

/* Puts VALUE in the cell in TABLE with index (IDX1, IDX2).  TABLE must have 2
   dimensions.  Takes ownership of VALUE.  */
void
pivot_table_put2 (struct pivot_table *table, size_t idx1, size_t idx2,
                  struct pivot_value *value)
{
  size_t dindexes[] = { idx1, idx2 };
  pivot_table_put (table, dindexes, sizeof dindexes / sizeof *dindexes, value);
}

/* Puts VALUE in the cell in TABLE with index (IDX1, IDX2, IDX3).  TABLE must
   have 3 dimensions.  Takes ownership of VALUE.  */
void
pivot_table_put3 (struct pivot_table *table, size_t idx1, size_t idx2,
                  size_t idx3, struct pivot_value *value)
{
  size_t dindexes[] = { idx1, idx2, idx3 };
  pivot_table_put (table, dindexes, sizeof dindexes / sizeof *dindexes, value);
}

/* Puts VALUE in the cell in TABLE with index (IDX1, IDX2, IDX3, IDX4).  TABLE
   must have 4 dimensions.  Takes ownership of VALUE.  */
void
pivot_table_put4 (struct pivot_table *table, size_t idx1, size_t idx2,
                  size_t idx3, size_t idx4, struct pivot_value *value)
{
  size_t dindexes[] = { idx1, idx2, idx3, idx4 };
  pivot_table_put (table, dindexes, sizeof dindexes / sizeof *dindexes, value);
}

/* Creates and returns a new footnote in TABLE with the given CONTENT and an
   automatically assigned marker.

   The footnote will only appear in output if it is referenced.  Use
   pivot_value_add_footnote() to add a reference to the footnote. */
struct pivot_footnote *
pivot_table_create_footnote (struct pivot_table *table,
                             struct pivot_value *content)
{
  return pivot_table_create_footnote__ (table, table->n_footnotes,
                                        NULL, content);
}

void
pivot_footnote_format_marker (const struct pivot_footnote *f,
                              const struct pivot_table *pt,
                              struct string *s)
{
  if (f->marker)
    pivot_value_format_body (f->marker, pt, s);
  else if (pt->look->show_numeric_markers)
    ds_put_format (s, "%zu", f->idx + 1);
  else
    {
      char text[F26ADIC_STRLEN_MAX + 1];
      str_format_26adic (f->idx + 1, false, text, sizeof text);
      ds_put_cstr (s, text);
    }
}

char *
pivot_footnote_marker_string (const struct pivot_footnote *f,
                              const struct pivot_table *pt)
{
  struct string s = DS_EMPTY_INITIALIZER;
  pivot_footnote_format_marker (f, pt, &s);
  return ds_steal_cstr (&s);
}

/* Creates or modifies a footnote in TABLE with 0-based number IDX (and creates
   all lower indexes as a side effect).  If MARKER is nonnull, sets the
   footnote's marker; if CONTENT is nonnull, sets the footnote's content. */
struct pivot_footnote *
pivot_table_create_footnote__ (struct pivot_table *table, size_t idx,
                               struct pivot_value *marker,
                               struct pivot_value *content)
{
  if (idx >= table->n_footnotes)
    {
      while (idx >= table->allocated_footnotes)
        table->footnotes = x2nrealloc (table->footnotes,
                                       &table->allocated_footnotes,
                                       sizeof *table->footnotes);
      while (idx >= table->n_footnotes)
        {
          struct pivot_footnote *f = xmalloc (sizeof *f);
          *f = (struct pivot_footnote) {
            .idx = table->n_footnotes,
            .show = true,
          };
          table->footnotes[table->n_footnotes++] = f;
        }
    }

  struct pivot_footnote *f = table->footnotes[idx];
  if (marker)
    {
      pivot_value_destroy (f->marker);
      f->marker = marker;
    }
  if (content)
    {
      pivot_value_destroy (f->content);
      f->content = content;
    }
  return f;
}

/* Frees the data owned by F. */
void
pivot_footnote_destroy (struct pivot_footnote *f)
{
  if (f)
    {
      pivot_value_destroy (f->content);
      pivot_value_destroy (f->marker);
      free (f);
    }
}

/* Converts per-axis presentation-order indexes, given in PINDEXES, into data
   indexes for each dimension in TABLE in DINDEXES[]. */
void
pivot_table_convert_indexes_ptod (const struct pivot_table *table,
                                  const size_t *pindexes[PIVOT_N_AXES],
                                  size_t dindexes[/* table->n_dimensions */])
{
  for (size_t i = 0; i < PIVOT_N_AXES; i++)
    {
      const struct pivot_axis *axis = &table->axes[i];

      for (size_t j = 0; j < axis->n_dimensions; j++)
        {
          const struct pivot_dimension *d = axis->dimensions[j];
          size_t pindex = pindexes[i][j];
          dindexes[d->top_index] = d->presentation_leaves[pindex]->data_index;
        }
    }
}

size_t *
pivot_table_enumerate_axis (const struct pivot_table *table,
                            enum pivot_axis_type axis_type,
                            const size_t *layer_indexes, bool omit_empty,
                            size_t *n)
{
  const struct pivot_axis *axis = &table->axes[axis_type];
  if (!axis->n_dimensions)
    {
      size_t *enumeration = xnmalloc (2, sizeof *enumeration);
      enumeration[0] = 0;
      enumeration[1] = SIZE_MAX;
      if (n)
        *n = 1;
      return enumeration;
    }
  else if (!axis->extent)
    {
      size_t *enumeration = xmalloc (sizeof *enumeration);
      *enumeration = SIZE_MAX;
      if (n)
        *n = 0;
      return enumeration;
    }

  size_t *enumeration = xnmalloc (xsum (xtimes (axis->extent,
                                                axis->n_dimensions), 1),
                                  sizeof *enumeration);
  size_t *p = enumeration;
  size_t *dindexes = XCALLOC (table->n_dimensions, size_t);

  size_t *axis_indexes;
  PIVOT_AXIS_FOR_EACH (axis_indexes, axis)
    {
      if (omit_empty)
        {
          enum pivot_axis_type axis2_type
            = pivot_axis_type_transpose (axis_type);

          size_t *axis2_indexes;
          PIVOT_AXIS_FOR_EACH (axis2_indexes, &table->axes[axis2_type])
            {
              const size_t *pindexes[PIVOT_N_AXES];
              pindexes[PIVOT_AXIS_LAYER] = layer_indexes;
              pindexes[axis_type] = axis_indexes;
              pindexes[axis2_type] = axis2_indexes;
              pivot_table_convert_indexes_ptod (table, pindexes, dindexes);
              if (pivot_table_get (table, dindexes))
                goto found;
            }
          continue;

        found:
          free (axis2_indexes);
        }

      memcpy (p, axis_indexes, axis->n_dimensions * sizeof *p);
      p += axis->n_dimensions;
    }
  if (omit_empty && p == enumeration)
    {
      PIVOT_AXIS_FOR_EACH (axis_indexes, axis)
        {
          memcpy (p, axis_indexes, axis->n_dimensions * sizeof *p);
          p += axis->n_dimensions;
        }
    }
  *p = SIZE_MAX;
  if (n)
    *n = (p - enumeration) / axis->n_dimensions;

  free (dindexes);
  return enumeration;
}

static struct pivot_cell *
pivot_table_lookup_cell (const struct pivot_table *table,
                         const size_t *dindexes)
{
  unsigned int hash = pivot_cell_hash_indexes (dindexes, table->n_dimensions);
  return pivot_table_lookup_cell__ (table, dindexes, hash);
}

const struct pivot_value *
pivot_table_get (const struct pivot_table *table, const size_t *dindexes)
{
  const struct pivot_cell *cell = pivot_table_lookup_cell (table, dindexes);
  return cell ? cell->value : NULL;
}

struct pivot_value *
pivot_table_get_rw (struct pivot_table *table, const size_t *dindexes)
{
  struct pivot_cell *cell = pivot_table_insert_cell (table, dindexes);
  if (!cell->value)
    cell->value = pivot_value_new_user_text ("", -1);
  return cell->value;
}

static void
pivot_table_delete_cell (struct pivot_table *table, struct pivot_cell *cell)
{
  hmap_delete (&table->cells, &cell->hmap_node);
  pivot_value_destroy (cell->value);
  free (cell);
}

bool
pivot_table_delete (struct pivot_table *table, const size_t *dindexes)
{
  struct pivot_cell *cell = pivot_table_lookup_cell (table, dindexes);
  if (cell)
    {
      pivot_table_delete_cell (table, cell);
      return true;
    }
  else
    return false;
}

static void
distribute_extra_depth (struct pivot_category *category, size_t extra_depth)
{
  if (pivot_category_is_group (category) && category->n_subs)
    for (size_t i = 0; i < category->n_subs; i++)
      distribute_extra_depth (category->subs[i], extra_depth);
  else
    category->extra_depth += extra_depth;
}

static void
pivot_category_assign_label_depth (struct pivot_category *category,
                                   bool dimension_labels_in_corner)
{
  category->extra_depth = 0;

  if (pivot_category_is_group (category))
    {
      size_t depth = 0;
      for (size_t i = 0; i < category->n_subs; i++)
        {
          pivot_category_assign_label_depth (category->subs[i], false);
          depth = MAX (depth, category->subs[i]->label_depth);
        }

      for (size_t i = 0; i < category->n_subs; i++)
        {
          struct pivot_category *sub = category->subs[i];

          size_t extra_depth = depth - sub->label_depth;
          if (extra_depth)
            distribute_extra_depth (sub, extra_depth);

          sub->label_depth = depth;
        }

      category->show_label_in_corner = (category->show_label
                                        && dimension_labels_in_corner);
      category->label_depth
        = (category->show_label && !category->show_label_in_corner
           ? depth + 1 : depth);
    }
  else
    category->label_depth = 1;
}

static bool
pivot_axis_assign_label_depth (struct pivot_table *table,
                             enum pivot_axis_type axis_type,
                             bool dimension_labels_in_corner)
{
  struct pivot_axis *axis = &table->axes[axis_type];
  bool any_label_shown_in_corner = false;
  axis->label_depth = 0;
  axis->extent = 1;
  for (size_t i = 0; i < axis->n_dimensions; i++)
    {
      struct pivot_dimension *d = axis->dimensions[i];
      pivot_category_assign_label_depth (d->root, dimension_labels_in_corner);
      d->label_depth = d->hide_all_labels ? 0 : d->root->label_depth;
      axis->label_depth += d->label_depth;
      axis->extent *= d->n_leaves;

      if (d->root->show_label_in_corner)
        any_label_shown_in_corner = true;
    }
  return any_label_shown_in_corner;
}

void
pivot_table_assign_label_depth (struct pivot_table *table)
{
  pivot_axis_assign_label_depth (table, PIVOT_AXIS_COLUMN, false);
  if (pivot_axis_assign_label_depth (
        table, PIVOT_AXIS_ROW, (table->look->row_labels_in_corner
                                && !table->corner_text))
      && table->axes[PIVOT_AXIS_COLUMN].label_depth == 0)
    table->axes[PIVOT_AXIS_COLUMN].label_depth = 1;
  pivot_axis_assign_label_depth (table, PIVOT_AXIS_LAYER, false);
}

static void
indent (int indentation)
{
  for (int i = 0; i < indentation * 2; i++)
    putchar (' ');
}

static void
pivot_value_dump (const struct pivot_value *value,
                  const struct pivot_table *pt)
{
  char *s = pivot_value_to_string (value, pt);
  fputs (s, stdout);
  free (s);
}

static void
pivot_table_dump_value (const struct pivot_value *value, const char *name,
                        const struct pivot_table *pt, int indentation)
{
  if (value)
    {
      indent (indentation);
      printf ("%s: ", name);
      pivot_value_dump (value, pt);
      putchar ('\n');
    }
}

static void
pivot_table_dump_string (const char *string, const char *name, int indentation)
{
  if (string)
    {
      indent (indentation);
      printf ("%s: %s\n", name, string);
    }
}

void
pivot_category_dump (const struct pivot_category *c,
                     const struct pivot_table *pt, int indentation)
{
  indent (indentation);
  printf ("%s \"", pivot_category_is_leaf (c) ? "leaf" : "group");
  pivot_value_dump (c->name, pt);
  printf ("\" ");

  if (pivot_category_is_leaf (c))
    printf ("data_index=%zu\n", c->data_index);
  else
    {
      printf (" (label %s)", c->show_label ? "shown" : "hidden");
      printf ("\n");

      for (size_t i = 0; i < c->n_subs; i++)
        pivot_category_dump (c->subs[i], pt, indentation + 1);
    }
}

void
pivot_dimension_dump (const struct pivot_dimension *d,
                      const struct pivot_table *pt, int indentation)
{
  indent (indentation);
  printf ("%s dimension %zu (where 0=innermost), label_depth=%d:\n",
          pivot_axis_type_to_string (d->axis_type), d->level, d->label_depth);

  pivot_category_dump (d->root, pt, indentation + 1);
}

static void
table_area_style_dump (enum pivot_area area, const struct table_area_style *a,
                       int indentation)
{
  indent (indentation);
  printf ("%s: ", pivot_area_to_string (area));
  font_style_dump (&a->font_style);
  putchar (' ');
  cell_style_dump (&a->cell_style);
  putchar ('\n');
}

static void
table_border_style_dump (enum pivot_border border,
                         const struct table_border_style *b, int indentation)
{
  indent (indentation);
  printf ("%s: %s ", pivot_border_to_string (border),
          table_stroke_to_string (b->stroke));
  cell_color_dump (&b->color);
  putchar ('\n');
}

static char ***
compose_headings (const struct pivot_table *pt,
                  const struct pivot_axis *axis,
                  const size_t *column_enumeration)
{
  if (!axis->n_dimensions || !axis->extent || !axis->label_depth)
    return NULL;

  char ***headings = xnmalloc (axis->label_depth, sizeof *headings);
  for (size_t i = 0; i < axis->label_depth; i++)
    headings[i] = xcalloc (axis->extent, sizeof **headings);

  const size_t *indexes;
  size_t column = 0;
  PIVOT_ENUMERATION_FOR_EACH (indexes, column_enumeration, axis)
    {
      int row = axis->label_depth - 1;
      for (int dim_index = 0; dim_index < axis->n_dimensions; dim_index++)
        {
          const struct pivot_dimension *d = axis->dimensions[dim_index];
          if (d->hide_all_labels)
            continue;
          for (const struct pivot_category *c
                 = d->presentation_leaves[indexes[dim_index]];
               c;
               c = c->parent)
            {
              if (pivot_category_is_leaf (c) || (c->show_label
                                                 && !c->show_label_in_corner))
                {
                  headings[row][column] = pivot_value_to_string (c->name, pt);
                  if (!*headings[row][column])
                    {
                      free (headings[row][column]);
                      headings[row][column] = xstrdup ("<blank>");
                    }
                  row--;
                }
            }
        }
      column++;
    }

  return headings;
}

static void
free_headings (const struct pivot_axis *axis, char ***headings)
{
  if (!headings)
    return;
  for (size_t i = 0; i < axis->label_depth; i++)
    {
      for (size_t j = 0; j < axis->extent; j++)
        free (headings[i][j]);
      free (headings[i]);
    }
  free (headings);
}

static void
pivot_table_sizing_dump (const char *name,
                         const int width_ranges[2],
                         const struct pivot_table_sizing *s,
                         int indentation)
{
  indent (indentation);
  printf ("%ss: min=%d, max=%d\n", name, width_ranges[0], width_ranges[1]);
  if (s->n_widths)
    {
      indent (indentation + 1);
      printf ("%s widths:", name);
      for (size_t i = 0; i < s->n_widths; i++)
        printf (" %d", s->widths[i]);
      printf ("\n");
    }
  if (s->n_breaks)
    {
      indent (indentation + 1);
      printf ("break after %ss:", name);
      for (size_t i = 0; i < s->n_breaks; i++)
        printf (" %zu", s->breaks[i]);
      printf ("\n");
    }
  if (s->n_keeps)
    {
      indent (indentation + 1);
      printf ("keep %ss together:", name);
      for (size_t i = 0; i < s->n_keeps; i++)
        printf (" [%zu,%zu]",
                s->keeps[i].ofs,
                s->keeps[i].ofs + s->keeps[i].n - 1);
      printf ("\n");
    }
}

static void
dump_leaf (const struct pivot_table *table, const struct pivot_category *c)
{
  if (c)
    {
      dump_leaf (table, c->parent);
      if (pivot_category_is_leaf (c) || c->show_label)
        {
          putchar (' ');
          pivot_value_dump (c->name, table);
        }
    }
}

void
pivot_table_dump (const struct pivot_table *table, int indentation)
{
  if (!table)
    return;

  pivot_table_assign_label_depth (CONST_CAST (struct pivot_table *, table));

  pivot_table_dump_value (table->title, "title", table, indentation);
  pivot_table_dump_value (table->subtype, "subtype", table, indentation);
  pivot_table_dump_string (table->command_c, "command", indentation);
  pivot_table_dump_string (table->dataset, "dataset", indentation);
  pivot_table_dump_string (table->datafile, "datafile", indentation);
  pivot_table_dump_string (table->notes, "notes", indentation);
  pivot_table_dump_string (table->look->name, "table-look", indentation);
  if (table->date)
    {
      indent (indentation);

      struct tm *tm = localtime (&table->date);
      printf ("date: %d-%02d-%02d %d:%02d:%02d\n", tm->tm_year + 1900,
              tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min,
              tm->tm_sec);
    }

  indent (indentation);
  printf ("sizing:\n");
  pivot_table_sizing_dump ("column", table->look->col_heading_width_range,
                           &table->sizing[TABLE_HORZ], indentation + 1);
  pivot_table_sizing_dump ("row", table->look->row_heading_width_range,
                           &table->sizing[TABLE_VERT], indentation + 1);

  indent (indentation);
  printf ("areas:\n");
  for (enum pivot_area area = 0; area < PIVOT_N_AREAS; area++)
    table_area_style_dump (area, &table->look->areas[area], indentation + 1);

  indent (indentation);
  printf ("borders:\n");
  for (enum pivot_border border = 0; border < PIVOT_N_BORDERS; border++)
    table_border_style_dump (border, &table->look->borders[border],
                             indentation + 1);

  for (size_t i = 0; i < table->n_dimensions; i++)
    pivot_dimension_dump (table->dimensions[i], table, indentation);

  /* Presentation and data indexes. */
  size_t *dindexes = XCALLOC (table->n_dimensions, size_t);

  const struct pivot_axis *layer_axis = &table->axes[PIVOT_AXIS_LAYER];
  if (layer_axis->n_dimensions)
    {
      indent (indentation);
      printf ("current layer:");

      for (size_t i = 0; i < layer_axis->n_dimensions; i++)
        {
          const struct pivot_dimension *d = layer_axis->dimensions[i];
          char *name = pivot_value_to_string (d->root->name, table);
          printf (" %s", name);
          free (name);

          size_t ofs = table->current_layer[i];
          if (ofs < d->n_leaves)
            {
              char *value = pivot_value_to_string (d->data_leaves[ofs]->name,
                                                   table);
              printf ("=%s", value);
              free (value);
            }
        }

      putchar ('\n');
    }

  size_t *layer_indexes;
  size_t layer_iteration = 0;
  PIVOT_AXIS_FOR_EACH (layer_indexes, &table->axes[PIVOT_AXIS_LAYER])
    {
      indent (indentation);
      printf ("layer %zu:", layer_iteration++);

      const struct pivot_axis *layer_axis = &table->axes[PIVOT_AXIS_LAYER];
      for (size_t i = 0; i < layer_axis->n_dimensions; i++)
        {
          const struct pivot_dimension *d = layer_axis->dimensions[i];

          fputs (i == 0 ? " " : ", ", stdout);
          pivot_value_dump (d->root->name, table);
          fputs (" =", stdout);

          dump_leaf (table, d->presentation_leaves[layer_indexes[i]]);
        }
      putchar ('\n');

      size_t *column_enumeration = pivot_table_enumerate_axis (
        table, PIVOT_AXIS_COLUMN, layer_indexes, table->look->omit_empty, NULL);
      size_t *row_enumeration = pivot_table_enumerate_axis (
        table, PIVOT_AXIS_ROW, layer_indexes, table->look->omit_empty, NULL);

      /* Print column headings.

         Ordinarily the test for nonnull 'column_headings' would be
         unnecessary, because 'column_headings' is null only if the axis's
         label_depth is 0, but there is a special case for the column axis only
         in pivot_table_assign_label_depth(). */
      char ***column_headings = compose_headings (
        table, &table->axes[PIVOT_AXIS_COLUMN], column_enumeration);
      if (column_headings)
        {
          for (size_t y = 0; y < table->axes[PIVOT_AXIS_COLUMN].label_depth; y++)
            {
              indent (indentation + 1);
              for (size_t x = 0; x < table->axes[PIVOT_AXIS_COLUMN].extent; x++)
                {
                  if (x)
                    fputs ("; ", stdout);
                  if (column_headings && column_headings[y] && column_headings[y][x])
                    fputs (column_headings[y][x], stdout);
                }
              putchar ('\n');
            }
          free_headings (&table->axes[PIVOT_AXIS_COLUMN], column_headings);
        }

      indent (indentation + 1);
      printf ("-----------------------------------------------\n");

      char ***row_headings = compose_headings (
        table, &table->axes[PIVOT_AXIS_ROW], row_enumeration);

      size_t x = 0;
      const size_t *pindexes[PIVOT_N_AXES]
        = { [PIVOT_AXIS_LAYER] = layer_indexes };
      PIVOT_ENUMERATION_FOR_EACH (pindexes[PIVOT_AXIS_ROW], row_enumeration,
                                  &table->axes[PIVOT_AXIS_ROW])
        {
          indent (indentation + 1);

          size_t i = 0;
          for (size_t y = 0; y < table->axes[PIVOT_AXIS_ROW].label_depth; y++)
            {
              if (i++)
                fputs ("; ", stdout);
              if (row_headings[y][x])
                fputs (row_headings[y][x], stdout);
            }

          printf (" | ");

          i = 0;
          PIVOT_ENUMERATION_FOR_EACH (pindexes[PIVOT_AXIS_COLUMN],
                                      column_enumeration,
                                      &table->axes[PIVOT_AXIS_COLUMN])
            {
              if (i++)
                printf ("; ");

              pivot_table_convert_indexes_ptod (table, pindexes, dindexes);
              const struct pivot_value *value = pivot_table_get (
                table, dindexes);
              if (value)
                pivot_value_dump (value, table);
            }
          printf ("\n");

          x++;
        }

      free (column_enumeration);
      free (row_enumeration);
      free_headings (&table->axes[PIVOT_AXIS_ROW], row_headings);
    }

  pivot_table_dump_value (table->caption, "caption", table, indentation);

  for (size_t i = 0; i < table->n_footnotes; i++)
    {
      const struct pivot_footnote *f = table->footnotes[i];
      indent (indentation);
      putchar ('[');
      if (f->marker)
        pivot_value_dump (f->marker, table);
      else
        printf ("%zu", f->idx);
      putchar (']');
      pivot_value_dump (f->content, table);
      putchar ('\n');
    }

  free (dindexes);
}

static const char *
consume_int (const char *p, size_t *n)
{
  *n = 0;
  while (c_isdigit (*p))
    *n = *n * 10 + (*p++ - '0');
  return p;
}

static size_t
pivot_format_inner_template (struct string *out, const char *template,
                             char escape,
                             struct pivot_value **values, size_t n_values,
                             const struct pivot_table *pt)
{
  size_t args_consumed = 0;
  while (*template && *template != ':')
    {
      if (*template == '\\' && template[1])
        {
          ds_put_byte (out, template[1] == 'n' ? '\n' : template[1]);
          template += 2;
        }
      else if (*template == escape)
        {
          size_t index;
          template = consume_int (template + 1, &index);
          if (index >= 1 && index <= n_values)
            {
              pivot_value_format (values[index - 1], pt, out);
              args_consumed = MAX (args_consumed, index);
            }
        }
      else
        ds_put_byte (out, *template++);
    }
  return args_consumed;
}

static const char *
pivot_extract_inner_template (const char *template, const char **p)
{
  *p = template;

  for (;;)
    {
      if (*template == '\\' && template[1] != '\0')
        template += 2;
      else if (*template == ':')
        return template + 1;
      else if (*template == '\0')
        return template;
      else
        template++;
    }
}

static void
pivot_format_template (struct string *out, const char *template,
                       const struct pivot_argument *args, size_t n_args,
                       const struct pivot_table *pt)
{
  while (*template)
    {
      if (*template == '\\' && template[1] != '\0')
        {
          ds_put_byte (out, template[1] == 'n' ? '\n' : template[1]);
          template += 2;
        }
      else if (*template == '^')
        {
          size_t index;
          template = consume_int (template + 1, &index);
          if (index >= 1 && index <= n_args && args[index - 1].n > 0)
            pivot_value_format (args[index - 1].values[0], pt, out);
        }
      else if (*template == '[')
        {
          const char *tmpl[2];
          template = pivot_extract_inner_template (template + 1, &tmpl[0]);
          template = pivot_extract_inner_template (template, &tmpl[1]);
          template += *template == ']';

          size_t index;
          template = consume_int (template, &index);
          if (index < 1 || index > n_args)
            continue;

          const struct pivot_argument *arg = &args[index - 1];
          size_t left = arg->n;
          while (left)
            {
              struct pivot_value **values = arg->values + (arg->n - left);
              int tmpl_idx = left == arg->n && *tmpl[0] != ':' ? 0 : 1;
              char escape = "%^"[tmpl_idx];
              size_t used = pivot_format_inner_template (
                out, tmpl[tmpl_idx], escape, values, left, pt);
              if (!used || used > left)
                break;
              left -= used;
            }
        }
      else
        ds_put_byte (out, *template++);
    }
}

static enum settings_value_show
interpret_show (enum settings_value_show global_show,
                enum settings_value_show table_show,
                enum settings_value_show value_show,
                bool has_label)
{
  return (!has_label ? SETTINGS_VALUE_SHOW_VALUE
          : value_show != SETTINGS_VALUE_SHOW_DEFAULT ? value_show
          : table_show != SETTINGS_VALUE_SHOW_DEFAULT ? table_show
          : global_show);
}

/* Appends to OUT the actual text content from the given Pango MARKUP. */
static void
get_text_from_markup (const char *markup, struct string *out)
{
  xmlParserCtxt *parser = xmlCreatePushParserCtxt (NULL, NULL, NULL, 0, NULL);
  if (!parser)
    {
      ds_put_cstr (out, markup);
      return;
    }

  xmlParseChunk (parser, "<xml>", strlen ("<xml>"), false);
  xmlParseChunk (parser, markup, strlen (markup), false);
  xmlParseChunk (parser, "</xml>", strlen ("</xml>"), true);

  if (parser->wellFormed)
    {
      xmlChar *s = xmlNodeGetContent (xmlDocGetRootElement (parser->myDoc));
      ds_put_cstr (out, CHAR_CAST (char *, s));
      xmlFree (s);
    }
  else
    ds_put_cstr (out, markup);
  xmlFreeDoc (parser->myDoc);
  xmlFreeParserCtxt (parser);
}

static const struct pivot_table pivot_value_format_defaults = {
  .show_values = SETTINGS_VALUE_SHOW_DEFAULT,
  .show_variables = SETTINGS_VALUE_SHOW_DEFAULT,
  .settings = FMT_SETTINGS_INIT,
};

/* Appends a text representation of the body of VALUE to OUT.  Settings on PT
   control whether variable and value labels are included (pass NULL for PT to
   get default formatting in the absence of a pivot table).

   The "body" omits subscripts and superscripts and footnotes.

   Returns true if OUT is a number (or a number plus a value label), false
   otherwise.  */
bool
pivot_value_format_body (const struct pivot_value *value,
                         const struct pivot_table *pt_,
                         struct string *out)
{
  const struct pivot_table *pt = pt_ ? pt_ : &pivot_value_format_defaults;
  enum settings_value_show show;
  bool numeric = false;

  switch (value->type)
    {
    case PIVOT_VALUE_NUMERIC:
      show = interpret_show (settings_get_show_values (),
                             pt->show_values,
                             value->numeric.show,
                             value->numeric.value_label != NULL);
      if (show & SETTINGS_VALUE_SHOW_VALUE)
        {
          struct fmt_spec f = value->numeric.format;
          const struct fmt_spec format
            = (f.type == FMT_F
               && value->numeric.honor_small
               && value->numeric.x != 0
               && fabs (value->numeric.x) < pt->small
               ? (struct fmt_spec) { .type = FMT_E, .w = 40, .d = f.d }
               : f);

          char *s = data_out (&(union value) { .f = value->numeric.x },
                              "UTF-8", format, &pt->settings);
          ds_put_cstr (out, s + strspn (s, " "));
          free (s);
        }
      if (show & SETTINGS_VALUE_SHOW_LABEL)
        {
          if (show & SETTINGS_VALUE_SHOW_VALUE)
            ds_put_byte (out, ' ');
          ds_put_cstr (out, value->numeric.value_label);
        }
      numeric = !(show & SETTINGS_VALUE_SHOW_LABEL);
      break;

    case PIVOT_VALUE_STRING:
      show = interpret_show (settings_get_show_values (),
                             pt->show_values,
                             value->string.show,
                             value->string.value_label != NULL);
      if (show & SETTINGS_VALUE_SHOW_VALUE)
        {
          if (value->string.hex)
            {
              for (const uint8_t *p = CHAR_CAST (uint8_t *, value->string.s);
                   *p; p++)
                ds_put_format (out, "%02X", *p);
            }
          else
            ds_put_cstr (out, value->string.s);
        }
      if (show & SETTINGS_VALUE_SHOW_LABEL)
        {
          if (show & SETTINGS_VALUE_SHOW_VALUE)
            ds_put_byte (out, ' ');
          ds_put_cstr (out, value->string.value_label);
        }
      break;

    case PIVOT_VALUE_VARIABLE:
      show = interpret_show (settings_get_show_variables (),
                             pt->show_variables,
                             value->variable.show,
                             value->variable.var_label != NULL);
      if (show & SETTINGS_VALUE_SHOW_VALUE)
        ds_put_cstr (out, value->variable.var_name);
      if (show & SETTINGS_VALUE_SHOW_LABEL)
        {
          if (show & SETTINGS_VALUE_SHOW_VALUE)
            ds_put_byte (out, ' ');
          ds_put_cstr (out, value->variable.var_label);
        }
      break;

    case PIVOT_VALUE_TEXT:
      if (value->ex && value->ex->font_style && value->ex->font_style->markup)
        get_text_from_markup (value->text.local, out);
      else
        ds_put_cstr (out, value->text.local);
      break;

    case PIVOT_VALUE_TEMPLATE:
      pivot_format_template (out, value->template.local, value->template.args,
                             value->template.n_args, pt);
      break;
    }

  return numeric;
}

/* Appends a text representation of VALUE to OUT.  Settings on PT control
   whether variable and value labels are included (pass NULL for PT to get
   default formatting in the absence of a pivot table).

   Subscripts and footnotes are included.

   Returns true if OUT is a number (or a number plus a value label), false
   otherwise.  */
bool
pivot_value_format (const struct pivot_value *value,
                    const struct pivot_table *pt_,
                    struct string *out)
{
  const struct pivot_table *pt = pt_ ? pt_ : &pivot_value_format_defaults;
  bool numeric = pivot_value_format_body (value, pt, out);

  const struct pivot_value_ex *ex = value->ex;
  if (ex)
    {
      if (ex->n_subscripts)
        {
          for (size_t i = 0; i < ex->n_subscripts; i++)
            ds_put_format (out, "%c%s", i ? ',' : '_', ex->subscripts[i]);
        }

      for (size_t i = 0; i < ex->n_footnotes; i++)
        {
          ds_put_byte (out, '[');

          size_t idx = ex->footnote_indexes[i];
          const struct pivot_footnote *f = pt->footnotes[idx];
          pivot_footnote_format_marker (f, pt, out);

          ds_put_byte (out, ']');
        }
    }

  return numeric;
}

/* Returns a text representation of VALUE.  The caller must free the string,
   with free().  Settings on PT control whether variable and value labels are
   included (pass NULL for PT to get default formatting in the absence of a
   pivot table). */
char *
pivot_value_to_string (const struct pivot_value *value,
                       const struct pivot_table *pt)
{
  struct string s = DS_EMPTY_INITIALIZER;
  pivot_value_format (value, pt, &s);
  return ds_steal_cstr (&s);
}

struct pivot_value *
pivot_value_clone (const struct pivot_value *old)
{
  if (!old)
    return NULL;

  struct pivot_value *new = xmemdup (old, sizeof *new);
  if (old->ex)
    new->ex = pivot_value_ex_clone (old->ex);

  switch (new->type)
    {
    case PIVOT_VALUE_NUMERIC:
      new->numeric.var_name = xstrdup_if_nonnull (new->numeric.var_name);
      new->numeric.value_label = xstrdup_if_nonnull (new->numeric.value_label);
      break;

    case PIVOT_VALUE_STRING:
      new->string.s = xstrdup (new->string.s);
      new->string.var_name = xstrdup_if_nonnull (new->string.var_name);
      new->string.value_label = xstrdup_if_nonnull (new->string.value_label);
      break;

    case PIVOT_VALUE_VARIABLE:
      new->variable.var_name = xstrdup_if_nonnull (new->variable.var_name);
      new->variable.var_label = xstrdup_if_nonnull (new->variable.var_label);
      break;

    case PIVOT_VALUE_TEXT:
      new->text.local = xstrdup (old->text.local);
      new->text.c = (old->text.c == old->text.local ? new->text.local
                     : xstrdup_if_nonnull (old->text.c));
      new->text.id = (old->text.id == old->text.local ? new->text.local
                      : old->text.id == old->text.c ? new->text.c
                      : xstrdup_if_nonnull (old->text.id));
      break;

    case PIVOT_VALUE_TEMPLATE:
      new->template.local = xstrdup (old->template.local);
      new->template.id = (old->template.id == old->template.local
                          ? new->template.local
                          : xstrdup (old->template.id));
      new->template.args = xmalloc (new->template.n_args
                                    * sizeof *new->template.args);
      for (size_t i = 0; i < old->template.n_args; i++)
        pivot_argument_copy (&new->template.args[i],
                             &old->template.args[i]);
      break;

    default:
      NOT_REACHED ();
    }
  return new;
}

/* Frees the data owned by V. */
void
pivot_value_destroy (struct pivot_value *value)
{
  if (value)
    {
      pivot_value_ex_destroy (value->ex);
      switch (value->type)
        {
        case PIVOT_VALUE_NUMERIC:
          free (value->numeric.var_name);
          free (value->numeric.value_label);
          break;

        case PIVOT_VALUE_STRING:
          free (value->string.s);
          free (value->string.var_name);
          free (value->string.value_label);
          break;

        case PIVOT_VALUE_VARIABLE:
          free (value->variable.var_name);
          free (value->variable.var_label);
          break;

        case PIVOT_VALUE_TEXT:
          free (value->text.local);
          if (value->text.c != value->text.local)
            free (value->text.c);
          if (value->text.id != value->text.local
              && value->text.id != value->text.c)
            free (value->text.id);
          break;

        case PIVOT_VALUE_TEMPLATE:
          free (value->template.local);
          if (value->template.id != value->template.local)
            free (value->template.id);
          for (size_t i = 0; i < value->template.n_args; i++)
            pivot_argument_uninit (&value->template.args[i]);
          free (value->template.args);
          break;

        default:
          NOT_REACHED ();
        }
      free (value);
    }
}

/* Sets AREA to the style to use for VALUE, with defaults coming from
   DEFAULT_STYLE for the parts of the style that VALUE doesn't override. */
void
pivot_value_get_style (struct pivot_value *value,
                       const struct font_style *base_font_style,
                       const struct cell_style *base_cell_style,
                       struct table_area_style *area)
{
  const struct pivot_value_ex *ex = pivot_value_ex (value);
  font_style_copy (NULL, &area->font_style,
                   ex->font_style ? ex->font_style : base_font_style);
  area->cell_style = *(ex->cell_style ? ex->cell_style : base_cell_style);
}

/* Copies AREA into VALUE's style. */
void
pivot_value_set_style (struct pivot_value *value,
                       const struct table_area_style *area)
{
  pivot_value_set_font_style (value, &area->font_style);
  pivot_value_set_cell_style (value, &area->cell_style);
}

void
pivot_value_set_font_style (struct pivot_value *value,
                            const struct font_style *font_style)
{
  struct pivot_value_ex *ex = pivot_value_ex_rw (value);
  if (ex->font_style)
    font_style_uninit (ex->font_style);
  else
    ex->font_style = xmalloc (sizeof *ex->font_style);
  font_style_copy (NULL, ex->font_style, font_style);
}

void
pivot_value_set_cell_style (struct pivot_value *value,
                            const struct cell_style *cell_style)
{
  struct pivot_value_ex *ex = pivot_value_ex_rw (value);
  if (!ex->cell_style)
    ex->cell_style = xmalloc (sizeof *ex->cell_style);
  *ex->cell_style = *cell_style;
}

void
pivot_argument_copy (struct pivot_argument *dst,
                     const struct pivot_argument *src)
{
  *dst = (struct pivot_argument) {
    .n = src->n,
    .values = xmalloc (src->n * sizeof *dst->values),
  };

  for (size_t i = 0; i < src->n; i++)
    dst->values[i] = pivot_value_clone (src->values[i]);
}

/* Frees the data owned by ARG (but not ARG itself). */
void
pivot_argument_uninit (struct pivot_argument *arg)
{
  if (arg)
    {
      for (size_t i = 0; i < arg->n; i++)
        pivot_value_destroy (arg->values[i]);
      free (arg->values);
    }
}

/* Creates and returns a new pivot_value whose contents is the null-terminated
   string TEXT.  Takes ownership of TEXT.

   This function is for text strings provided by the user (with the exception
   that pivot_value_new_variable() should be used for variable names).  For
   strings that are part of the PSPP user interface, such as names of
   procedures, statistics, annotations, error messages, etc., use
   pivot_value_new_text(). */
struct pivot_value *
pivot_value_new_user_text_nocopy (char *text)
{
  struct pivot_value *value = xmalloc (sizeof *value);
  *value = (struct pivot_value) {
    .text = {
      .type = PIVOT_VALUE_TEXT,
      .local = text,
      .c = text,
      .id = text,
      .user_provided = true,
    }
  };
  return value;
}

/* Creates and returns a new pivot_value whose contents is the LENGTH bytes of
   TEXT.  Use SIZE_MAX if TEXT is null-teriminated and its length is not known
   in advance.

   This function is for text strings provided by the user (with the exception
   that pivot_value_new_variable() should be used for variable names).  For
   strings that are part of the PSPP user interface, such as names of
   procedures, statistics, annotations, error messages, etc., use
   pivot_value_new_text().

   The caller retains ownership of TEXT. */
struct pivot_value *
pivot_value_new_user_text (const char *text, size_t length)
{
  return pivot_value_new_user_text_nocopy (
    xmemdup0 (text, length != SIZE_MAX ? length : strlen (text)));
}

/* Creates and returns new pivot_value whose contents is TEXT, which should be
   a translatable string, but not actually translated yet, e.g. enclosed in
   N_().  This function is for text strings that are part of the PSPP user
   interface, such as names of procedures, statistics, annotations, error
   messages, etc.  For strings that come from the user, use
   pivot_value_new_user_text(). */
struct pivot_value *
pivot_value_new_text (const char *text)
{
  char *c = xstrdup (text);
  char *local = xstrdup (gettext (c));

  struct pivot_value *value = xmalloc (sizeof *value);
  *value = (struct pivot_value) {
    .text = {
      .type = PIVOT_VALUE_TEXT,
      .local = local,
      .c = c,
      .id = c,
      .user_provided = false,
    }
  };
  return value;
}

/* Same as pivot_value_new_text() but its argument is a printf()-like format
   string.  The format string should generally be enclosed in N_(). */
struct pivot_value * PRINTF_FORMAT (1, 2)
pivot_value_new_text_format (const char *format, ...)
{
  va_list args;
  va_start (args, format);
  char *c = xvasprintf (format, args);
  va_end (args);

  va_start (args, format);
  char *local = xvasprintf (gettext (format), args);
  va_end (args);

  struct pivot_value *value = xmalloc (sizeof *value);
  *value = (struct pivot_value) {
    .text = {
      .type = PIVOT_VALUE_TEXT,
      .local = local,
      .c = c,
      .id = xstrdup (c),
      .user_provided = false,
    }
  };
  return value;
}

/* Returns a new pivot_value that represents X.

   The format to use for X is unspecified.  Usually the easiest way to specify
   a format is through assigning a result class to one of the categories that
   the pivot_value will end up in.  If that is not suitable, then the caller
   can use pivot_value_set_rc() or assign directly to value->numeric.format. */
struct pivot_value *
pivot_value_new_number (double x)
{
  struct pivot_value *value = xmalloc (sizeof *value);
  *value = (struct pivot_value) {
    .numeric = {
      .type = PIVOT_VALUE_NUMERIC,
      .x = x
    },
  };
  return value;
}

/* Returns a new pivot_value that represents X, formatted as an integer. */
struct pivot_value *
pivot_value_new_integer (double x)
{
  struct pivot_value *value = pivot_value_new_number (x);
  value->numeric.format = (struct fmt_spec) { .type = FMT_F, .w = 40 };
  return value;
}

/* Returns a new pivot_value that represents VALUE, formatted as for
   VARIABLE. */
struct pivot_value *
pivot_value_new_var_value (const struct variable *variable,
                           const union value *value)
{
  struct pivot_value *pv = pivot_value_new_value (
    value, var_get_width (variable), var_get_print_format (variable),
    var_get_encoding (variable));

  char *var_name = xstrdup (var_get_name (variable));
  if (var_is_alpha (variable))
    pv->string.var_name = var_name;
  else
    pv->numeric.var_name = var_name;

  const char *label = var_lookup_value_label (variable, value);
  if (label)
    {
      if (var_is_alpha (variable))
        pv->string.value_label = xstrdup (label);
      else
        pv->numeric.value_label = xstrdup (label);
    }

  return pv;
}

/* Returns a new pivot_value that represents VALUE, with the given WIDTH,
   formatted with FORMAT.  For a string value, ENCODING must be its character
   encoding. */
struct pivot_value *
pivot_value_new_value (const union value *value, int width,
                       struct fmt_spec format, const char *encoding)
{
  struct pivot_value *pv = XZALLOC (struct pivot_value);
  if (width > 0)
    {
      char *s = recode_string (UTF8, encoding, CHAR_CAST (char *, value->s),
                               width);
      size_t n = strlen (s);
      while (n > 0 && s[n - 1] == ' ')
        s[--n] = '\0';

      pv->type = PIVOT_VALUE_STRING;
      pv->string.s = s;
      pv->string.hex = format.type == FMT_AHEX;
    }
  else
    {
      pv->type = PIVOT_VALUE_NUMERIC;
      pv->numeric.x = value->f;
      pv->numeric.format = format;
    }

  return pv;
}

/* Returns a new pivot_value for VARIABLE. */
struct pivot_value *
pivot_value_new_variable (const struct variable *variable)
{
  return pivot_value_new_variable__ (var_get_name (variable),
                                     var_get_label (variable));
}

/* Returns a new pivot_value for a variable with the given NAME and optional
   LABEL. */
struct pivot_value *
pivot_value_new_variable__ (const char *name, const char *label)
{
  struct pivot_value *value = xmalloc (sizeof *value);
  *value = (struct pivot_value) {
    .variable = {
      .type = PIVOT_VALUE_VARIABLE,
      .var_name = xstrdup (name),
      .var_label = xstrdup_if_nonnull (label),
    },
  };
  return value;
}

/* Attaches a reference to FOOTNOTE to V. */
void
pivot_value_add_footnote (struct pivot_value *v,
                          const struct pivot_footnote *footnote)
{
  struct pivot_value_ex *ex = pivot_value_ex_rw (v);

  /* Some legacy tables include numerous duplicate footnotes.  Suppress
     them. */
  for (size_t i = 0; i < ex->n_footnotes; i++)
    if (ex->footnote_indexes[i] == footnote->idx)
      return;

  ex->footnote_indexes = xrealloc (
    ex->footnote_indexes,
    (ex->n_footnotes + 1) * sizeof *ex->footnote_indexes);
  ex->footnote_indexes[ex->n_footnotes++] = footnote->idx;
  pivot_value_sort_footnotes (v);
}

static int
compare_footnote_indexes (const void *a_, const void *b_)
{
  const size_t *ap = a_;
  const size_t *bp = b_;
  size_t a = *ap;
  size_t b = *bp;
  return a < b ? -1 : a > b;
}

/* Sorts the footnote references in V in the standard ascending order.

   This is only necessary if code adds (plural) footnotes to a pivot_value by
   itself, because pivot_value_add_footnote() does it automatically. */
void
pivot_value_sort_footnotes (struct pivot_value *v)
{
  if (v->ex && v->ex->n_footnotes > 1)
    qsort (v->ex->footnote_indexes, v->ex->n_footnotes,
           sizeof *v->ex->footnote_indexes, compare_footnote_indexes);
}

/* If VALUE is a numeric value, and RC is a result class such as
   PIVOT_RC_COUNT, changes VALUE's format to the result class's. */
void
pivot_value_set_rc (const struct pivot_table *table, struct pivot_value *value,
                    const char *rc)
{
  if (value->type == PIVOT_VALUE_NUMERIC)
    pivot_table_use_rc (table, rc,
                        &value->numeric.format, &value->numeric.honor_small);
}

/* pivot_value_ex. */

struct pivot_value_ex *
pivot_value_ex_rw (struct pivot_value *value)
{
  if (!value->ex)
    value->ex = xzalloc (sizeof *value->ex);
  return value->ex;
}

struct pivot_value_ex *
pivot_value_ex_clone (const struct pivot_value_ex *old)
{
  struct font_style *font_style = NULL;
  if (old->font_style)
    {
      font_style = xmalloc (sizeof *font_style);
      font_style_copy (NULL, font_style, old->font_style);
    }

  char **subscripts = NULL;
  if (old->n_subscripts)
    {
      subscripts = xnmalloc (old->n_subscripts, sizeof *subscripts);
      for (size_t i = 0; i < old->n_subscripts; i++)
        subscripts[i] = xstrdup (old->subscripts[i]);
    }

  struct pivot_value_ex *new = xmalloc (sizeof *new);
  *new = (struct pivot_value_ex) {
    .font_style = font_style,
    .cell_style = (old->cell_style
                   ? xmemdup (old->cell_style, sizeof *new->cell_style)
                   : NULL),
    .subscripts = subscripts,
    .n_subscripts = old->n_subscripts,
    .footnote_indexes = (
      old->n_footnotes
      ? xmemdup (old->footnote_indexes,
                 old->n_footnotes * sizeof *new->footnote_indexes)
      : NULL),
    .n_footnotes = old->n_footnotes
  };
  return new;
}

void
pivot_value_ex_destroy (struct pivot_value_ex *ex)
{
  if (ex)
    {
      font_style_uninit (ex->font_style);
      free (ex->font_style);
      free (ex->cell_style);
      free (ex->footnote_indexes);

      for (size_t i = 0; i < ex->n_subscripts; i++)
        free (ex->subscripts[i]);
      free (ex->subscripts);
      free (ex);
    }
}

/* pivot_splits */

struct pivot_splits_value
  {
    struct hmap_node hmap_node;
    union value value;
    int leaf;
  };

struct pivot_splits_var
  {
    struct pivot_dimension *dimension;
    const struct variable *var;
    int width;
    struct hmap values;
  };

struct pivot_splits_dup
  {
    struct hmap_node hmap_node;
    union value *values;
  };

struct pivot_splits
  {
    struct pivot_splits_var *vars;
    size_t n;
    char *encoding;

    struct hmap dups;

    size_t dindexes[MAX_SPLITS];

    int warnings_left;
  };

/* Adds a dimension for each layered split file variable in DICT to PT on AXIS.
   These dimensions should be the last dimensions added to PT (the
   pivot_splits_put*() functions rely on this).  Returns a new pivot_splits
   structure if any dimensions were added, otherwise a null pointer.

   See the large comment on split file handling in pivot-table.h for more
   information. */
struct pivot_splits *
pivot_splits_create (struct pivot_table *pt,
                     enum pivot_axis_type axis,
                     const struct dictionary *dict)
{
  if (dict_get_split_type (dict) != SPLIT_LAYERED)
    return NULL;

  size_t n = dict_get_n_splits (dict);
  assert (n <= MAX_SPLITS);

  const struct variable *const *vars = dict_get_split_vars (dict);
  struct pivot_splits_var *psvars = xnmalloc (n, sizeof *psvars);
  for (size_t i = n - 1; i < n; i--)
    {
      const struct variable *var = vars[i];
      struct pivot_splits_var *psvar = &psvars[i];

      struct pivot_dimension *d = pivot_dimension_create__ (
        pt, axis, pivot_value_new_variable (var));
      d->root->show_label = true;

      *psvar = (struct pivot_splits_var) {
        .width = var_get_width (var),
        .values = HMAP_INITIALIZER (psvar->values),
        .dimension = d,
        .var = var,
      };
    }

  struct pivot_splits *ps = xmalloc (sizeof *ps);
  *ps = (struct pivot_splits) {
    .vars = psvars,
    .n = n,
    .encoding = xstrdup (dict_get_encoding (dict)),
    .dups = HMAP_INITIALIZER (ps->dups),
    .dindexes = { [0] = SIZE_MAX },
    .warnings_left = 5,
  };
  return ps;
}

/* Destroys PS. */
void
pivot_splits_destroy (struct pivot_splits *ps)
{
  if (!ps)
    return;

  if (ps->warnings_left < 0)
    msg (SW, ngettext ("Suppressed %d additional warning about duplicate "
                       "split values.",
                       "Suppressed %d additional warnings about duplicate "
                       "split values.", -ps->warnings_left),
         -ps->warnings_left);

  struct pivot_splits_dup *dup, *next_dup;
  HMAP_FOR_EACH_SAFE (dup, next_dup, struct pivot_splits_dup, hmap_node,
                      &ps->dups)
    {
      for (size_t i = 0; i < ps->n; i++)
        value_destroy (&dup->values[i], ps->vars[i].width);
      free (dup->values);
      free (dup);
    }
  hmap_destroy (&ps->dups);

  for (size_t i = 0; i < ps->n; i++)
    {
      struct pivot_splits_var *psvar = &ps->vars[i];
      struct pivot_splits_value *psval, *next;
      HMAP_FOR_EACH_SAFE (psval, next, struct pivot_splits_value, hmap_node,
                          &psvar->values)
        {
          value_destroy (&psval->value, psvar->width);
          hmap_delete (&psvar->values, &psval->hmap_node);
          free (psval);
        }
      hmap_destroy (&psvar->values);
    }
  free (ps->vars);

  free (ps->encoding);
  free (ps);
}

static struct pivot_splits_value *
pivot_splits_value_find (struct pivot_splits_var *psvar,
                         const union value *value)
{
  struct pivot_splits_value *psval;
  HMAP_FOR_EACH_WITH_HASH (psval, struct pivot_splits_value, hmap_node,
                           value_hash (value, psvar->width, 0), &psvar->values)
    if (value_equal (&psval->value, value, psvar->width))
      return psval;
  return NULL;
}

static bool
pivot_splits_find_dup (struct pivot_splits *ps, const struct ccase *example)
{
  unsigned int hash = 0;
  for (size_t i = 0; i < ps->n; i++)
    {
      struct pivot_splits_var *psvar = &ps->vars[i];
      const union value *value = case_data (example, psvar->var);
      hash = value_hash (value, psvar->width, hash);
    }
  struct pivot_splits_dup *dup;
  HMAP_FOR_EACH_WITH_HASH (dup, struct pivot_splits_dup, hmap_node, hash,
                           &ps->dups)
    {
      bool equal = true;
      for (size_t i = 0; i < ps->n && equal; i++)
        {
          struct pivot_splits_var *psvar = &ps->vars[i];
          const union value *value = case_data (example, psvar->var);
          equal = value_equal (value, &dup->values[i], psvar->width);
        }
      if (equal)
        return true;
    }

  union value *values = xmalloc (ps->n * sizeof *values);
  for (size_t i = 0; i < ps->n; i++)
    {
      struct pivot_splits_var *psvar = &ps->vars[i];
      const union value *value = case_data (example, psvar->var);
      value_clone (&values[i], value, psvar->width);
    }

  dup = xmalloc (sizeof *dup);
  dup->values = values;
  hmap_insert (&ps->dups, &dup->hmap_node, hash);
  return false;
}

/* Begins adding data for a new split file group to the pivot table associated
   with PS.  EXAMPLE should be a case from the new split file group.

   This is a no-op if PS is NULL.

   See the large comment on split file handling in pivot-table.h for more
   information. */
void
pivot_splits_new_split (struct pivot_splits *ps, const struct ccase *example)
{
  if (!ps)
    return;

  for (size_t i = 0; i < ps->n; i++)
    {
      struct pivot_splits_var *psvar = &ps->vars[i];
      const union value *value = case_data (example, psvar->var);
      struct pivot_splits_value *psval = pivot_splits_value_find (psvar, value);
      if (!psval)
        {
          psval = xmalloc (sizeof *psval);
          hmap_insert (&psvar->values, &psval->hmap_node,
                       value_hash (value, psvar->width, 0));
          value_clone (&psval->value, value, psvar->width);
          psval->leaf = pivot_category_create_leaf (
            psvar->dimension->root,
            pivot_value_new_var_value (psvar->var, value));
        }

      ps->dindexes[i] = psval->leaf;
    }

  if (pivot_splits_find_dup (ps, example))
    {
      if (ps->warnings_left-- > 0)
        {
          struct string s = DS_EMPTY_INITIALIZER;
          for (size_t i = 0; i < ps->n; i++)
            {
              if (i > 0)
                ds_put_cstr (&s, ", ");

              struct pivot_splits_var *psvar = &ps->vars[i];
              const union value *value = case_data (example, psvar->var);
              ds_put_format (&s, "%s = ", var_get_name (psvar->var));

              char *s2 = data_out (value, ps->encoding,
                                   var_get_print_format (psvar->var),
                                   settings_get_fmt_settings ());
              ds_put_cstr (&s, s2 + strspn (s2, " "));
              free (s2);
            }
          msg (SW, _("When SPLIT FILE is in effect, the input data must be "
                     "sorted by the split variables (for example, using SORT "
                     "CASES), but multiple runs of cases with the same split "
                     "values were found separated by cases with different "
                     "values.  Each run will be analyzed separately.  The "
                     "duplicate split values are: %s"), ds_cstr (&s));
          ds_destroy (&s);
        }

      struct pivot_splits_var *psvar = &ps->vars[0];
      const union value *value = case_data (example, psvar->var);
      ps->dindexes[0] = pivot_category_create_leaf (
        psvar->dimension->root,
        pivot_value_new_var_value (psvar->var, value));
    }
}

static size_t
pivot_splits_get_dindexes (const struct pivot_splits *ps, size_t *dindexes)
{
  if (!ps)
    return 0;

  assert (ps->dindexes[0] != SIZE_MAX);
  for (size_t i = 0; i < ps->n; i++)
    dindexes[ps->n - i - 1] = ps->dindexes[i];
  return ps->n;
}

/* Puts VALUE in the cell in TABLE with index IDX1.  TABLE must have 1
   dimension plus the split file dimensions from PS (if nonnull).  Takes
   ownership of VALUE.

   See the large comment on split file handling in pivot-table.h for more
   information. */
void
pivot_splits_put1 (struct pivot_splits *ps, struct pivot_table *table,
                   size_t idx1, struct pivot_value *value)
{
  size_t dindexes[1 + MAX_SPLITS];
  size_t *p = dindexes;
  *p++ = idx1;
  p += pivot_splits_get_dindexes (ps, p);
  pivot_table_put (table, dindexes, p - dindexes, value);
}

/* Puts VALUE in the cell in TABLE with index (IDX1, IDX2).  TABLE must have 2
   dimensions plus the split file dimensions from PS (if nonnull).  Takes
   ownership of VALUE.

   See the large comment on split file handling in pivot-table.h for more
   information. */
void
pivot_splits_put2 (struct pivot_splits *ps, struct pivot_table *table,
                   size_t idx1, size_t idx2, struct pivot_value *value)
{
  size_t dindexes[2 + MAX_SPLITS];
  size_t *p = dindexes;
  *p++ = idx1;
  *p++ = idx2;
  p += pivot_splits_get_dindexes (ps, p);
  pivot_table_put (table, dindexes, p - dindexes, value);
}

/* Puts VALUE in the cell in TABLE with index (IDX1, IDX2, IDX3).  TABLE must
   have 3 dimensions plus the split file dimensions from PS (if nonnull).
   Takes ownership of VALUE.

   See the large comment on split file handling in pivot-table.h for more
   information. */
void
pivot_splits_put3 (struct pivot_splits *ps, struct pivot_table *table,
                   size_t idx1, size_t idx2, size_t idx3,
                   struct pivot_value *value)
{
  size_t dindexes[3 + MAX_SPLITS];
  size_t *p = dindexes;
  *p++ = idx1;
  *p++ = idx2;
  *p++ = idx3;
  p += pivot_splits_get_dindexes (ps, p);
  pivot_table_put (table, dindexes, p - dindexes, value);
}

/* Puts VALUE in the cell in TABLE with index (IDX1, IDX2, IDX3, IDX4).  TABLE
   must have 4 dimensions plus the split file dimensions from PS (if nonnull).
   Takes ownership of VALUE.

   See the large comment on split file handling in pivot-table.h for more
   information. */
void
pivot_splits_put4 (struct pivot_splits *ps, struct pivot_table *table,
                   size_t idx1, size_t idx2, size_t idx3, size_t idx4,
                   struct pivot_value *value)
{
  size_t dindexes[4 + MAX_SPLITS];
  size_t *p = dindexes;
  *p++ = idx1;
  *p++ = idx2;
  *p++ = idx3;
  *p++ = idx4;
  p += pivot_splits_get_dindexes (ps, p);
  pivot_table_put (table, dindexes, p - dindexes, value);
}
