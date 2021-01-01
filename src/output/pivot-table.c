/* PSPP - a program for statistical analysis.
   Copyright (C) 2017, 2018 Free Software Foundation, Inc.

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

#include <stdlib.h>

#include "data/data-out.h"
#include "data/settings.h"
#include "data/value.h"
#include "data/variable.h"
#include "data/file-name.h"
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

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static const struct fmt_spec *pivot_table_get_format (
  const struct pivot_table *, const char *s);

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
    .width_ranges = {
      [TABLE_HORZ] = { 36, 72 },
      [TABLE_VERT] = { 36, 120 },
    },

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

      return xcalloc (axis->n_dimensions, sizeof *indexes);
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
  const struct fmt_spec *format = pivot_table_get_format (
    category->dimension->table, s);
  if (format)
    category->format = *format;
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
    { PIVOT_RC_INTEGER,      { FMT_F,   40, 0 } },
    { PIVOT_RC_PERCENT,      { FMT_PCT, 40, 1 } },
    { PIVOT_RC_CORRELATION,  { FMT_F,   40, 3 } },
    { PIVOT_RC_SIGNIFICANCE, { FMT_F,   40, 3 } },
    { PIVOT_RC_RESIDUAL,     { FMT_F,   40, 2 } },
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

static const struct fmt_spec *
pivot_table_get_format (const struct pivot_table *table, const char *s)
{
  if (!s)
    return NULL;
  else if (!strcmp (s, PIVOT_RC_OTHER))
    return settings_get_format ();
  else if (!strcmp (s, PIVOT_RC_COUNT) && !overridden_count_format)
    return &table->weight_format;
  else
    {
      const struct result_class *rc = pivot_result_class_find (s);
      return rc ? &rc->format : NULL;
    }
}

/* Sets the format specification for the result class named S (which should not
   include the RC_ prefix) to *FORMAT.  Returns true if successful, false if S
   does not name a known result class. */
bool
pivot_result_class_change (const char *s_, const struct fmt_spec *format)
{
  char *s = xasprintf ("RC_%s", s_);
  struct result_class *rc = pivot_result_class_find (s);
  if (rc)
    {
      rc->format = *format;
      if (!strcmp (s, PIVOT_RC_COUNT))
        overridden_count_format = true;
    }
  free (s);

  return rc != NULL;
}

/* Pivot tables. */

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
  struct pivot_table *table = xzalloc (sizeof *table);
  table->ref_cnt = 1;
  table->show_title = true;
  table->show_caption = true;
  table->weight_format = (struct fmt_spec) { FMT_F, 40, 0 };
  table->title = title;
  table->subtype = subtype ? pivot_value_new_text (subtype) : NULL;
  table->command_c = output_get_command_name ();
  table->look = pivot_table_look_ref (pivot_table_look_get_default ());

  hmap_init (&table->cells);

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

  for (int i = 0; i < sizeof table->ccs / sizeof *table->ccs; i++)
    free (table->ccs[i]);

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

  for (size_t i = 0; i < table->n_dimensions; i++)
    pivot_dimension_destroy (table->dimensions[i]);
  free (table->dimensions);

  for (size_t i = 0; i < PIVOT_N_AXES; i++)
    free (table->axes[i].dimensions);

  struct pivot_cell *cell, *next_cell;
  HMAP_FOR_EACH_SAFE (cell, next_cell, struct pivot_cell, hmap_node,
                      &table->cells)
    {
      hmap_delete (&table->cells, &cell->hmap_node);
      pivot_value_destroy (cell->value);
      free (cell);
    }
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
   format for the dictionary whose data or statistics are being put into TABLE.

   This has no effect if WFMT is NULL. */
void
pivot_table_set_weight_format (struct pivot_table *table,
                               const struct fmt_spec *wfmt)
{
  if (wfmt)
    table->weight_format = *wfmt;
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
   DINDEXES.  N must be the number of dimensions in TABLE.  Takes ownership of
   VALUE.

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
                  goto done;
                }
            }
        }
      value->numeric.format = *settings_get_format ();

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

static struct pivot_value *
pivot_make_default_footnote_marker (int idx, bool show_numeric_markers)
{
  char text[INT_BUFSIZE_BOUND (size_t)];
  if (show_numeric_markers)
    snprintf (text, sizeof text, "%d", idx + 1);
  else
    str_format_26adic (idx + 1, false, text, sizeof text);
  return pivot_value_new_user_text (text, -1);
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
          f->idx = table->n_footnotes;
          f->marker = pivot_make_default_footnote_marker (
            f->idx, table->look->show_numeric_markers);
          f->content = NULL;
          f->show = true;

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
          dindexes[d->top_index]
            = d->presentation_leaves[pindexes[i][j]]->data_index;
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

static const struct pivot_cell *
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
pivot_value_dump (const struct pivot_value *value)
{
  char *s = pivot_value_to_string (value, SETTINGS_VALUE_SHOW_DEFAULT,
                                   SETTINGS_VALUE_SHOW_DEFAULT);
  fputs (s, stdout);
  free (s);
}

static void
pivot_table_dump_value (const struct pivot_value *value, const char *name,
                      int indentation)
{
  if (value)
    {
      indent (indentation);
      printf ("%s: ", name);
      pivot_value_dump (value);
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

static void
pivot_category_dump (const struct pivot_category *c, int indentation)
{
  indent (indentation);
  printf ("%s \"", pivot_category_is_leaf (c) ? "leaf" : "group");
  pivot_value_dump (c->name);
  printf ("\" ");

  if (pivot_category_is_leaf (c))
    printf ("data_index=%zu\n", c->data_index);
  else
    {
      printf (" (label %s)", c->show_label ? "shown" : "hidden");
      printf ("\n");

      for (size_t i = 0; i < c->n_subs; i++)
        pivot_category_dump (c->subs[i], indentation + 1);
    }
}

void
pivot_dimension_dump (const struct pivot_dimension *d, int indentation)
{
  indent (indentation);
  printf ("%s dimension %zu (where 0=innermost), label_depth=%d:\n",
          pivot_axis_type_to_string (d->axis_type), d->level, d->label_depth);

  pivot_category_dump (d->root, indentation + 1);
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
compose_headings (const struct pivot_axis *axis,
                  const size_t *column_enumeration,
                  enum settings_value_show show_values,
                  enum settings_value_show show_variables)
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
                  headings[row][column] = pivot_value_to_string (
                    c->name, show_values, show_variables);
                  if (!*headings[row][column])
                    headings[row][column] = xstrdup ("<blank>");
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

void
pivot_table_dump (const struct pivot_table *table, int indentation)
{
  if (!table)
    return;

  int old_decimal = settings_get_decimal_char (FMT_COMMA);
  if (table->decimal == '.' || table->decimal == ',')
    settings_set_decimal_char (table->decimal);

  pivot_table_dump_value (table->title, "title", indentation);
  pivot_table_dump_value (table->subtype, "subtype", indentation);
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
  pivot_table_sizing_dump ("column", table->look->width_ranges[TABLE_HORZ],
                           &table->sizing[TABLE_HORZ], indentation + 1);
  pivot_table_sizing_dump ("row", table->look->width_ranges[TABLE_VERT],
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
    pivot_dimension_dump (table->dimensions[i], indentation);

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
          char *name = pivot_value_to_string (d->root->name,
                                              table->show_values,
                                              table->show_variables);
          char *value = pivot_value_to_string (
            d->data_leaves[table->current_layer[i]]->name,
            table->show_values, table->show_variables);
          printf (" %s=%s", name, value);
          free (value);
          free (name);
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
          pivot_value_dump (d->root->name);
          fputs (" =", stdout);

          struct pivot_value **names = xnmalloc (d->n_leaves, sizeof *names);
          size_t n_names = 0;
          for (const struct pivot_category *c
                 = d->presentation_leaves[layer_indexes[i]];
               c;
               c = c->parent)
            {
              if (pivot_category_is_leaf (c) || c->show_label)
                names[n_names++] = c->name;
            }

          for (size_t i = n_names; i-- > 0;)
            {
              putchar (' ');
              pivot_value_dump (names[i]);
            }
          free (names);
        }
      putchar ('\n');

      size_t *column_enumeration = pivot_table_enumerate_axis (
        table, PIVOT_AXIS_COLUMN, layer_indexes, table->look->omit_empty, NULL);
      size_t *row_enumeration = pivot_table_enumerate_axis (
        table, PIVOT_AXIS_ROW, layer_indexes, table->look->omit_empty, NULL);

      char ***column_headings = compose_headings (
        &table->axes[PIVOT_AXIS_COLUMN], column_enumeration,
        table->show_values, table->show_variables);
      for (size_t y = 0; y < table->axes[PIVOT_AXIS_COLUMN].label_depth; y++)
        {
          indent (indentation + 1);
          for (size_t x = 0; x < table->axes[PIVOT_AXIS_COLUMN].extent; x++)
            {
              if (x)
                fputs ("; ", stdout);
              if (column_headings[y][x])
                fputs (column_headings[y][x], stdout);
            }
          putchar ('\n');
        }
      free_headings (&table->axes[PIVOT_AXIS_COLUMN], column_headings);

      indent (indentation + 1);
      printf ("-----------------------------------------------\n");

      char ***row_headings = compose_headings (
        &table->axes[PIVOT_AXIS_ROW], row_enumeration,
        table->show_values, table->show_variables);

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
                pivot_value_dump (value);
            }
          printf ("\n");

          x++;
        }

      free (column_enumeration);
      free (row_enumeration);
      free_headings (&table->axes[PIVOT_AXIS_ROW], row_headings);
    }

  pivot_table_dump_value (table->caption, "caption", indentation);

  for (size_t i = 0; i < table->n_footnotes; i++)
    {
      const struct pivot_footnote *f = table->footnotes[i];
      indent (indentation);
      putchar ('[');
      if (f->marker)
        pivot_value_dump (f->marker);
      else
        printf ("%zu", f->idx);
      putchar (']');
      pivot_value_dump (f->content);
      putchar ('\n');
    }

  free (dindexes);
  settings_set_decimal_char (old_decimal);
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
                             enum settings_value_show show_values,
                             enum settings_value_show show_variables)
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
              pivot_value_format (values[index - 1], show_values,
                                  show_variables, out);
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
                       enum settings_value_show show_values,
                       enum settings_value_show show_variables)
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
            pivot_value_format (args[index - 1].values[0],
                                show_values, show_variables, out);
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
                out, tmpl[tmpl_idx], escape, values, left,
                show_values, show_variables);
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

/* Appends a text representation of the body of VALUE to OUT.  SHOW_VALUES and
   SHOW_VARIABLES control whether variable and value labels are included.

   The "body" omits subscripts and superscripts and footnotes. */
bool
pivot_value_format_body (const struct pivot_value *value,
                         enum settings_value_show show_values,
                         enum settings_value_show show_variables,
                         struct string *out)
{
  enum settings_value_show show;
  bool numeric = false;

  switch (value->type)
    {
    case PIVOT_VALUE_NUMERIC:
      show = interpret_show (settings_get_show_values (),
                             show_values,
                             value->numeric.show,
                             value->numeric.value_label != NULL);
      if (show & SETTINGS_VALUE_SHOW_VALUE)
        {
          char *s = data_out (&(union value) { .f = value->numeric.x },
                              "UTF-8", &value->numeric.format);
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
                             show_values,
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
                             show_variables,
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
      ds_put_cstr (out, value->text.local);
      break;

    case PIVOT_VALUE_TEMPLATE:
      pivot_format_template (out, value->template.local, value->template.args,
                             value->template.n_args, show_values,
                             show_variables);
      break;
    }

  return numeric;
}

/* Appends a text representation of VALUE to OUT.  SHOW_VALUES and
   SHOW_VARIABLES control whether variable and value labels are included.

   Subscripts and footnotes are included. */
void
pivot_value_format (const struct pivot_value *value,
                    enum settings_value_show show_values,
                    enum settings_value_show show_variables,
                    struct string *out)
{
  pivot_value_format_body (value, show_values, show_variables, out);

  if (value->n_subscripts)
    {
      for (size_t i = 0; i < value->n_subscripts; i++)
        ds_put_format (out, "%c%s", i ? ',' : '_', value->subscripts[i]);
    }

  for (size_t i = 0; i < value->n_footnotes; i++)
    {
      ds_put_byte (out, '^');
      pivot_value_format (value->footnotes[i]->marker,
                          show_values, show_variables, out);
    }
}

/* Returns a text representation of VALUE.  The caller must free the string,
   with free(). */
char *
pivot_value_to_string (const struct pivot_value *value,
                       enum settings_value_show show_values,
                       enum settings_value_show show_variables)
{
  struct string s = DS_EMPTY_INITIALIZER;
  pivot_value_format (value, show_values, show_variables, &s);
  return ds_steal_cstr (&s);
}

/* Frees the data owned by V. */
void
pivot_value_destroy (struct pivot_value *value)
{
  if (value)
    {
      font_style_uninit (value->font_style);
      free (value->font_style);
      free (value->cell_style);
      /* Do not free the elements of footnotes because VALUE does not own
         them. */
      free (value->footnotes);

      for (size_t i = 0; i < value->n_subscripts; i++)
        free (value->subscripts[i]);
      free (value->subscripts);

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
  font_style_copy (NULL, &area->font_style, (value->font_style
                                             ? value->font_style
                                             : base_font_style));
  area->cell_style = *(value->cell_style
                       ? value->cell_style
                       : base_cell_style);
}

/* Copies AREA into VALUE's style. */
void
pivot_value_set_style (struct pivot_value *value,
                       const struct table_area_style *area)
{
  if (value->font_style)
    font_style_uninit (value->font_style);
  else
    value->font_style = xmalloc (sizeof *value->font_style);
  font_style_copy (NULL, value->font_style, &area->font_style);

  if (!value->cell_style)
    value->cell_style = xmalloc (sizeof *value->cell_style);
  *value->cell_style = area->cell_style;
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
    .type = PIVOT_VALUE_TEXT,
    .text = {
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
   pivot_value_new_text().j

   The caller retains ownership of TEXT.*/
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
    .type = PIVOT_VALUE_TEXT,
    .text = {
      .local = local,
      .c = c,
      .id = c,
      .user_provided = false,
    }
  };
  return value;
}

/* Same as pivot_value_new_text() but its argument is a printf()-like format
   string. */
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
    .type = PIVOT_VALUE_TEXT,
    .text = {
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
    .type = PIVOT_VALUE_NUMERIC,
    .numeric = { .x = x, },
  };
  return value;
}

/* Returns a new pivot_value that represents X, formatted as an integer. */
struct pivot_value *
pivot_value_new_integer (double x)
{
  struct pivot_value *value = pivot_value_new_number (x);
  value->numeric.format = (struct fmt_spec) { FMT_F, 40, 0 };
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
                       const struct fmt_spec *format, const char *encoding)
{
  struct pivot_value *pv = xzalloc (sizeof *pv);
  if (width > 0)
    {
      char *s = recode_string (UTF8, encoding, CHAR_CAST (char *, value->s),
                               width);
      size_t n = strlen (s);
      while (n > 0 && s[n - 1] == ' ')
        s[--n] = '\0';

      pv->type = PIVOT_VALUE_STRING;
      pv->string.s = s;
      pv->string.hex = format->type == FMT_AHEX;
    }
  else
    {
      pv->type = PIVOT_VALUE_NUMERIC;
      pv->numeric.x = value->f;
      pv->numeric.format = *format;
    }

  return pv;
}

/* Returns a new pivot_value for VARIABLE. */
struct pivot_value *
pivot_value_new_variable (const struct variable *variable)
{
  struct pivot_value *value = xmalloc (sizeof *value);
  *value = (struct pivot_value) {
    .type = PIVOT_VALUE_VARIABLE,
    .variable = {
      .var_name = xstrdup (var_get_name (variable)),
      .var_label = xstrdup_if_nonempty (var_get_label (variable)),
    },
  };
  return value;
}

/* Attaches a reference to FOOTNOTE to V. */
void
pivot_value_add_footnote (struct pivot_value *v,
                          const struct pivot_footnote *footnote)
{
  /* Some legacy tables include numerous duplicate footnotes.  Suppress
     them. */
  for (size_t i = 0; i < v->n_footnotes; i++)
    if (v->footnotes[i] == footnote)
      return;

  v->footnotes = xrealloc (v->footnotes,
                           (v->n_footnotes + 1) * sizeof *v->footnotes);
  v->footnotes[v->n_footnotes++] = footnote;
}

/* If VALUE is a numeric value, and RC is a result class such as
   PIVOT_RC_COUNT, changes VALUE's format to the result class's. */
void
pivot_value_set_rc (const struct pivot_table *table, struct pivot_value *value,
                    const char *rc)
{
  if (value->type == PIVOT_VALUE_NUMERIC)
    {
      const struct fmt_spec *f = pivot_table_get_format (table, rc);
      if (f)
        value->numeric.format = *f;
    }
}

