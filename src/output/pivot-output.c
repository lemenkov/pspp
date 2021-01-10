/* PSPP - a program for statistical analysis.
   Copyright (C) 2018 Free Software Foundation, Inc.

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

#include <stdlib.h>

#include "output/pivot-output.h"

#include "data/settings.h"
#include "libpspp/assertion.h"
#include "libpspp/pool.h"
#include "output/page-eject-item.h"
#include "output/pivot-table.h"
#include "output/table-item.h"
#include "output/table-provider.h"
#include "output/table.h"
#include "output/text-item.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#define H TABLE_HORZ
#define V TABLE_VERT

size_t *
pivot_output_next_layer (const struct pivot_table *pt, size_t *indexes,
                         bool print)
{
  const struct pivot_axis *layer_axis = &pt->axes[PIVOT_AXIS_LAYER];
  if (print && pt->look->print_all_layers)
    return pivot_axis_iterator_next (indexes, layer_axis);
  else if (!indexes)
    {
      size_t size = layer_axis->n_dimensions * sizeof *pt->current_layer;
      return size ? xmemdup (pt->current_layer, size) : xmalloc (1);
    }
  else
    {
      free (indexes);
      return NULL;
    }
}

static const struct pivot_category *
find_category (const struct pivot_dimension *d, int dim_index,
               const size_t *indexes, int row_ofs)
{
  size_t index = indexes[dim_index];
  assert (index < d->n_leaves);
  for (const struct pivot_category *c = d->presentation_leaves[index];
       c; c = c->parent)
    {
      /* A category can cover multiple rows.  Only return the category for its
         top row. */
      if (row_ofs == c->extra_depth)
        return c;

      row_ofs -= 1 + c->extra_depth;
      if (row_ofs < 0)
        return NULL;
    }
  return NULL;
}

static struct table_area_style *
table_area_style_override (struct pool *pool,
                           const struct table_area_style *in,
                           const struct cell_style *cell_,
                           const struct font_style *font_,
                           bool rotate_label)
{
  const struct cell_style *cell = cell_ ? cell_ : &in->cell_style;
  const struct font_style *font = font_ ? font_ : &in->font_style;

  struct table_area_style *out = (pool
                            ? pool_alloc (pool, sizeof *out)
                            : xmalloc (sizeof *out));
  *out = (struct table_area_style) {
    .cell_style.halign = rotate_label ? TABLE_HALIGN_CENTER : cell->halign,
    .cell_style.valign = rotate_label ? TABLE_VALIGN_CENTER : cell->valign,
    .cell_style.decimal_offset = cell->decimal_offset,
    .cell_style.margin[H][0] = cell->margin[H][0],
    .cell_style.margin[H][1] = cell->margin[H][1],
    .cell_style.margin[V][0] = cell->margin[V][0],
    .cell_style.margin[V][1] = cell->margin[V][1],
    .font_style.fg[0] = font->fg[0],
    .font_style.fg[1] = font->fg[1],
    .font_style.bg[0] = font->bg[0],
    .font_style.bg[1] = font->bg[1],
    .font_style.typeface = (font->typeface
                            ? pool_strdup (pool, font->typeface)
                            : NULL),
    .font_style.size = font->size,
    .font_style.bold = font->bold,
    .font_style.italic = font->italic,
    .font_style.underline = font->underline,
    .font_style.markup = font->markup,
  };
  return out;
}

static void
fill_cell (struct table *t, int x1, int y1, int x2, int y2,
           int style_idx, const struct pivot_value *value,
           bool rotate_label)
{
  int options = style_idx << TAB_STYLE_SHIFT;
  if (rotate_label)
    options |= TAB_ROTATE;

  table_put (t, x1, y1, x2, y2, options, value);
}

static void
fill_cell_owned (struct table *t, int x1, int y1, int x2, int y2,
                 int style_idx, struct string *s, bool rotate_label)
{
  int options = style_idx << TAB_STYLE_SHIFT;
  if (rotate_label)
    options |= TAB_ROTATE;

  table_put_owned (t, x1, y1, x2, y2, options,
                   pivot_value_new_user_text_nocopy (ds_steal_cstr (s)));
}

static int
get_table_rule (const struct table_border_style *styles,
                enum pivot_border style_idx)
{
  return styles[style_idx].stroke | (style_idx << TAB_RULE_STYLE_SHIFT);
}

static void
draw_line (struct table *t, const struct table_border_style *styles,
           enum pivot_border style_idx,
           enum table_axis axis, int a, int b0, int b1)
{
  int rule = get_table_rule (styles, style_idx);
  if (axis == H)
    table_hline (t, rule, b0, b1, a);
  else
    table_vline (t, rule, a, b0, b1);
}

/* Fills row or column headings into T.

   This function uses terminology and variable names for column headings, but
   it also applies to row headings because it uses variables for the
   differences, e.g. when for column headings it would use the H axis, it
   instead uses 'h', which is set to H for column headings and V for row
   headings.  */
static void
compose_headings (struct table *t,
                  const struct pivot_axis *h_axis, enum table_axis h,
                  const struct pivot_axis *v_axis,
                  const struct table_border_style *borders,
                  enum pivot_border dim_col_horz,
                  enum pivot_border dim_col_vert,
                  enum pivot_border cat_col_horz,
                  enum pivot_border cat_col_vert,
                  const size_t *column_enumeration, size_t n_columns,
                  int label_style_idx,
                  bool rotate_inner_labels, bool rotate_outer_labels)
{
  const enum table_axis v = !h;
  const int v_size = h_axis->label_depth;
  const int h_ofs = v_axis->label_depth;

  if (!h_axis->n_dimensions || !n_columns || !v_size)
    return;

  const int stride = MAX (1, h_axis->n_dimensions);

  /* Below, we're going to iterate through the dimensions.  Each dimension
     occupies one or more rows in the heading.  'top_row' is the top row of
     these (and 'top_row + d->label_depth - 1' is the bottom row). */
  int top_row = 0;

  /* We're going to iterate through dimensions and the rows that label them
     from top to bottom (from outer to inner dimensions).  As we move downward,
     we start drawing vertical rules to separate categories and groups.  After
     we start drawing a vertical rule in a particular horizontal position, it
     continues until the bottom of the heading.  vrules[pos] indicates whether,
     in our current row, we have already started drawing a vertical rule in
     horizontal position 'pos'.  (There are n_columns + 1 horizontal positions.
     We allocate all of them for convenience below but only the inner n_columns
     - 1 of them really matter.)

     Here's an example that shows how vertical rules continue all the way
     downward:

     +-----------------------------------------------------+ __
     |                         bbbb                        |  |
     +-----------------+-----------------+-----------------+  |dimension "bbbb"
     |      bbbb1      |      bbbb2      |      bbbb3      | _|
     +-----------------+-----------------+-----------------+ __
     |       aaaa      |       aaaa      |       aaaa      |  |
     +-----+-----+-----+-----+-----+-----+-----+-----+-----+  |dimension "aaaa"
     |aaaa1|aaaa2|aaaa3|aaaa1|aaaa2|aaaa3|aaaa1|aaaa2|aaaa3| _|
     +-----+-----+-----+-----+-----+-----+-----+-----+-----+

     ^     ^     ^     ^     ^     ^     ^     ^     ^     ^
     |     |     |     |     |     |     |     |     |     |
     0     1     2     3     4     5     6     7     8     9
     |___________________vrules[] indexes__________________|

     Our data structures are more naturally iterated from bottom to top (inner
     to outer dimensions).  A previous version of this code actually worked
     like that, but it didn't draw all of the vertical lines correctly as shown
     above.  It ended up rendering the above heading much like shown below,
     which isn't what users expect.  The "aaaa" label really needs to be shown
     three times for clarity:

     +-----------------------------------------------------+
     |                         bbbb                        |
     +-----------------+-----------------+-----------------+
     |      bbbb1      |      bbbb2      |      bbbb3      |
     +-----------------+-----------------+-----------------+
     |                 |       aaaa      |                 |
     +-----+-----+-----+-----+-----+-----+-----+-----+-----+
     |aaaa1|aaaa2|aaaa3|aaaa1|aaaa2|aaaa3|aaaa1|aaaa2|aaaa3|
     +-----+-----+-----+-----+-----+-----+-----+-----+-----+
  */
  bool *vrules = xzalloc (n_columns + 1);
  vrules[0] = vrules[n_columns] = true;
  for (int dim_index = h_axis->n_dimensions; --dim_index >= 0; )
    {
      const struct pivot_dimension *d = h_axis->dimensions[dim_index];
      if (d->hide_all_labels)
        continue;

      for (int row_ofs = 0; row_ofs < d->label_depth; row_ofs++)
        {
          for (size_t x1 = 0; x1 < n_columns;)
            {
              const struct pivot_category *c = find_category (
                d, dim_index, column_enumeration + x1 * stride,
                d->label_depth - row_ofs - 1);
              if (!c)
                {
                  x1++;
                  continue;
                }

              size_t x2;
              for (x2 = x1 + 1; x2 < n_columns; x2++)
                {
                  if (vrules[x2])
                    break;
                  const struct pivot_category *c2 = find_category (
                    d, dim_index, column_enumeration + x2 * stride,
                    d->label_depth - row_ofs - 1);
                  if (c != c2)
                    break;
                }

              int y1 = top_row + row_ofs;
              int y2 = top_row + row_ofs + c->extra_depth + 1;
              bool is_outer_row = y1 == 0;
              bool is_inner_row = y2 == v_size;
              if (pivot_category_is_leaf (c) || c->show_label)
                {
                  int bb[TABLE_N_AXES][2];
                  bb[h][0] = x1 + h_ofs;
                  bb[h][1] = x2 + h_ofs - 1;
                  bb[v][0] = y1;
                  bb[v][1] = y2 - 1;
                  bool rotate = ((rotate_inner_labels && is_inner_row)
                                 || (rotate_outer_labels && is_outer_row));
                  fill_cell (t, bb[H][0], bb[V][0], bb[H][1], bb[V][1],
                             label_style_idx, c->name, rotate);

                  /* Draw all the vertical lines in our running example, other
                     than the far left and far right ones.  Only the ones that
                     start in the last row of the heading are drawn with the
                     "category" style, the rest with the "dimension" style,
                     e.g. only the # below are category style:

                     +-----------------------------------------------------+
                     |                         bbbb                        |
                     +-----------------+-----------------+-----------------+
                     |      bbbb1      |      bbbb2      |      bbbb3      |
                     +-----------------+-----------------+-----------------+
                     |       aaaa      |       aaaa      |       aaaa      |
                     +-----+-----+-----+-----+-----+-----+-----+-----+-----+
                     |aaaa1#aaaa2#aaaa3|aaaa1#aaaa2#aaaa3|aaaa1#aaaa2#aaaa3|
                     +-----+-----+-----+-----+-----+-----+-----+-----+-----+
                  */
                  enum pivot_border style
                    = (y1 == v_size - 1 ? cat_col_vert : dim_col_vert);
                  if (!vrules[x2])
                    {
                      draw_line (t, borders, style, v, x2 + h_ofs, y1,
                                 t->n[v] - 1);
                      vrules[x2] = true;
                    }
                  if (!vrules[x1])
                    {
                      draw_line (t, borders, style, v, x1 + h_ofs, y1,
                                 t->n[v] - 1);
                      vrules[x1] = true;
                    }
                }

              /* Draws the horizontal lines within a dimension, that is, those
                 that separate a separating a category (or group) from its
                 parent group or dimension's label.  Our running example
                 doesn't have groups but the ==== lines below show the
                 separators between categories and their dimension label:

                 +-----------------------------------------------------+
                 |                         bbbb                        |
                 +=================+=================+=================+
                 |      bbbb1      |      bbbb2      |      bbbb3      |
                 +-----------------+-----------------+-----------------+
                 |       aaaa      |       aaaa      |       aaaa      |
                 +=====+=====+=====+=====+=====+=====+=====+=====+=====+
                 |aaaa1|aaaa2|aaaa3|aaaa1|aaaa2|aaaa3|aaaa1|aaaa2|aaaa3|
                 +-----+-----+-----+-----+-----+-----+-----+-----+-----+
              */
              if (c->parent && c->parent->show_label)
                draw_line (t, borders, cat_col_horz, h, y1,
                           x1 + h_ofs, x2 + h_ofs - 1);
              x1 = x2;
            }
        }

      if (d->root->show_label_in_corner && h_ofs > 0)
        {
          int bb[TABLE_N_AXES][2];
          bb[h][0] = 0;
          bb[h][1] = h_ofs - 1;
          bb[v][0] = top_row;
          bb[v][1] = top_row + d->label_depth - 1;
          fill_cell (t, bb[H][0], bb[V][0], bb[H][1], bb[V][1],
                     PIVOT_AREA_CORNER, d->root->name, false);
        }

      /* Draw the horizontal line between dimensions, e.g. the ===== line here:

         +-----------------------------------------------------+ __
         |                         bbbb                        |  |
         +-----------------+-----------------+-----------------+  |dim "bbbb"
         |      bbbb1      |      bbbb2      |      bbbb3      | _|
         +=================+=================+=================+ __
         |       aaaa      |       aaaa      |       aaaa      |  |
         +-----+-----+-----+-----+-----+-----+-----+-----+-----+  |dim "aaaa"
         |aaaa1|aaaa2|aaaa3|aaaa1|aaaa2|aaaa3|aaaa1|aaaa2|aaaa3| _|
         +-----+-----+-----+-----+-----+-----+-----+-----+-----+
      */
      if (dim_index != h_axis->n_dimensions - 1)
        draw_line (t, borders, dim_col_horz, h, top_row, h_ofs,
                   t->n[h] - 1);
      top_row += d->label_depth;
    }
  free (vrules);
}

static struct table *
create_aux_table (const struct pivot_table *pt, int nc, int nr,
                  int style_idx)
{
  struct table *table = table_create (nc, nr, 0, 0, 0, 0);
  table->styles[style_idx] = table_area_style_override (
      table->container, &pt->look->areas[style_idx], NULL, NULL, false);
  return table;
}


static void
add_references (const struct pivot_table *pt, const struct table *table,
                bool *refs, size_t *n_refs)
{
  if (!table)
    return;

  for (int y = 0; y < table->n[V]; y++)
    for (int x = 0; x < table->n[H]; )
      {
        struct table_cell cell;
        table_get_cell (table, x, y, &cell);

        if (x == cell.d[H][0] && y == cell.d[V][0])
          {
            for (size_t i = 0; i < cell.value->n_footnotes; i++)
              {
                size_t idx = cell.value->footnote_indexes[i];
                assert (idx < pt->n_footnotes);

                if (!refs[idx] && pt->footnotes[idx]->show)
                  {
                    refs[idx] = true;
                    (*n_refs)++;
                  }
              }
          }

        x = cell.d[TABLE_HORZ][1];
      }
}

static struct pivot_footnote **
collect_footnotes (const struct pivot_table *pt,
                   const struct table *title,
                   const struct table *layers,
                   const struct table *body,
                   const struct table *caption,
                   size_t *n_footnotesp)
{
  if (!pt->n_footnotes)
    {
      *n_footnotesp = 0;
      return NULL;
    }

  bool *refs = xzalloc (pt->n_footnotes);
  size_t n_refs = 0;
  add_references (pt, title, refs, &n_refs);
  add_references (pt, layers, refs, &n_refs);
  add_references (pt, body, refs, &n_refs);
  add_references (pt, caption, refs, &n_refs);

  struct pivot_footnote **footnotes = xnmalloc (n_refs, sizeof *footnotes);
  size_t n_footnotes = 0;
  for (size_t i = 0; i < pt->n_footnotes; i++)
    if (refs[i])
      footnotes[n_footnotes++] = pt->footnotes[i];
  assert (n_footnotes == n_refs);

  free (refs);

  *n_footnotesp = n_footnotes;
  return footnotes;
}

void
pivot_output (const struct pivot_table *pt,
              const size_t *layer_indexes,
              bool printing UNUSED,
              struct table **titlep,
              struct table **layersp,
              struct table **bodyp,
              struct table **captionp,
              struct table **footnotesp,
              struct pivot_footnote ***fp, size_t *nfp)
{
  const size_t *pindexes[PIVOT_N_AXES]
    = { [PIVOT_AXIS_LAYER] = layer_indexes };

  size_t data[TABLE_N_AXES];
  size_t *column_enumeration = pivot_table_enumerate_axis (
    pt, PIVOT_AXIS_COLUMN, layer_indexes, pt->look->omit_empty, &data[H]);
  size_t *row_enumeration = pivot_table_enumerate_axis (
    pt, PIVOT_AXIS_ROW, layer_indexes, pt->look->omit_empty, &data[V]);

  int stub[TABLE_N_AXES] = {
    [H] = pt->axes[PIVOT_AXIS_ROW].label_depth,
    [V] = pt->axes[PIVOT_AXIS_COLUMN].label_depth,
  };
  struct table *body = table_create (data[H] + stub[H],
                                     data[V] + stub[V],
                                     stub[H], 0, stub[V], 0);
  for (size_t i = 0; i < PIVOT_N_AREAS; i++)
    body->styles[i] = table_area_style_override (
      body->container, &pt->look->areas[i], NULL, NULL, false);

  struct table_border_style borders[PIVOT_N_BORDERS];
  memcpy (borders, pt->look->borders, sizeof borders);
  if (!printing && pt->show_grid_lines)
    for (int b = 0; b < PIVOT_N_BORDERS; b++)
      if (borders[b].stroke == TABLE_STROKE_NONE)
        borders[b].stroke = TABLE_STROKE_DASHED;

  for (size_t i = 0; i < PIVOT_N_BORDERS; i++)
    {
      const struct table_border_style *in = &pt->look->borders[i];
      body->rule_colors[i] = pool_alloc (body->container,
                                         sizeof *body->rule_colors[i]);
      *body->rule_colors[i] = in->color;
    }

  compose_headings (body,
                    &pt->axes[PIVOT_AXIS_COLUMN], H, &pt->axes[PIVOT_AXIS_ROW],
                    borders,
                    PIVOT_BORDER_DIM_COL_HORZ,
                    PIVOT_BORDER_DIM_COL_VERT,
                    PIVOT_BORDER_CAT_COL_HORZ,
                    PIVOT_BORDER_CAT_COL_VERT,
                    column_enumeration, data[H],
                    PIVOT_AREA_COLUMN_LABELS,
                    pt->rotate_outer_row_labels, false);

  compose_headings (body,
                    &pt->axes[PIVOT_AXIS_ROW], V, &pt->axes[PIVOT_AXIS_COLUMN],
                    borders,
                    PIVOT_BORDER_DIM_ROW_VERT,
                    PIVOT_BORDER_DIM_ROW_HORZ,
                    PIVOT_BORDER_CAT_ROW_VERT,
                    PIVOT_BORDER_CAT_ROW_HORZ,
                    row_enumeration, data[V],
                    PIVOT_AREA_ROW_LABELS,
                    false, pt->rotate_inner_column_labels);

  size_t *dindexes = XCALLOC (pt->n_dimensions, size_t);
  size_t y = 0;
  PIVOT_ENUMERATION_FOR_EACH (pindexes[PIVOT_AXIS_ROW], row_enumeration,
                              &pt->axes[PIVOT_AXIS_ROW])
    {
      size_t x = 0;
      PIVOT_ENUMERATION_FOR_EACH (pindexes[PIVOT_AXIS_COLUMN],
                                  column_enumeration,
                                  &pt->axes[PIVOT_AXIS_COLUMN])
        {
          pivot_table_convert_indexes_ptod (pt, pindexes, dindexes);
          const struct pivot_value *value = pivot_table_get (pt, dindexes);
          fill_cell (body, x + stub[H], y + stub[V], x + stub[H], y + stub[V],
                     PIVOT_AREA_DATA, value, false);

          x++;
        }

      y++;
    }
  free (dindexes);

  if ((pt->corner_text || !pt->look->row_labels_in_corner)
      && stub[H] && stub[V])
    fill_cell (body, 0, 0, stub[H] - 1, stub[V] - 1,
               PIVOT_AREA_CORNER, pt->corner_text, false);

  if (body->n[H] && body->n[V])
    {
      table_hline (
        body, get_table_rule (borders, PIVOT_BORDER_INNER_TOP),
        0, body->n[H] - 1, 0);
      table_hline (
        body, get_table_rule (borders, PIVOT_BORDER_INNER_BOTTOM),
        0, body->n[H] - 1, body->n[V]);
      table_vline (
        body, get_table_rule (borders, PIVOT_BORDER_INNER_LEFT),
        0, 0, body->n[V] - 1);
      table_vline (
        body, get_table_rule (borders, PIVOT_BORDER_INNER_RIGHT),
        body->n[H], 0, body->n[V] - 1);

      if (stub[V])
        table_hline (
          body, get_table_rule (borders, PIVOT_BORDER_DATA_TOP),
          0, body->n[H] - 1, stub[V]);
      if (stub[H])
        table_vline (
          body, get_table_rule (borders, PIVOT_BORDER_DATA_LEFT),
          stub[H], 0, body->n[V] - 1);

    }
  free (column_enumeration);
  free (row_enumeration);

  /* Title. */
  struct table *title;
  if (pt->title && pt->show_title && titlep)
    {
      title = create_aux_table (pt, 1, 1, PIVOT_AREA_TITLE);
      fill_cell (title, 0, 0, 0, 0, PIVOT_AREA_TITLE, pt->title, false);
    }
  else
    title = NULL;

  /* Layers. */
  const struct pivot_axis *layer_axis = &pt->axes[PIVOT_AXIS_LAYER];
  int n_layers = 0;
  if (layersp)
    for (size_t i = 0; i < layer_axis->n_dimensions; i++)
      {
        const struct pivot_dimension *d = layer_axis->dimensions[i];
        if (d->n_leaves)
          n_layers++;
      }

  struct table *layers;
  if (n_layers > 0)
    {
      layers = create_aux_table (pt, 1, n_layers, PIVOT_AREA_LAYERS);
      size_t y = 0;
      for (size_t i = 0; i < layer_axis->n_dimensions; i++)
        {
          const struct pivot_dimension *d = layer_axis->dimensions[i];
          if (!d->n_leaves)
            continue;

          struct string s = DS_EMPTY_INITIALIZER;
          pivot_value_format (d->root->name, pt, &s);
          ds_put_cstr (&s, ": ");
          pivot_value_format (d->data_leaves[layer_indexes[i]]->name, pt, &s);
          fill_cell_owned (layers, 0, y, 0, y, PIVOT_AREA_LAYERS, &s, false);
          y++;
        }
    }
  else
    layers = NULL;

  /* Caption. */
  struct table *caption;
  if (pt->caption && pt->show_caption && captionp)
    {
      caption = create_aux_table (pt, 1, 1, PIVOT_AREA_CAPTION);
      fill_cell (caption, 0, 0, 0, 0, PIVOT_AREA_CAPTION, pt->caption, false);
    }
  else
    caption = NULL;

  /* Footnotes. */
  size_t nf;
  struct pivot_footnote **f = collect_footnotes (pt, title, layers, body,
                                                 caption, &nf);
  struct table *footnotes;
  if (nf && footnotesp)
    {
      footnotes = create_aux_table (pt, 1, nf, PIVOT_AREA_FOOTER);

      for (size_t i = 0; i < nf; i++)
        {
          struct string s = DS_EMPTY_INITIALIZER;
          pivot_footnote_format_marker (f[i], pt, &s);
          ds_put_cstr (&s, ". ");
          pivot_value_format (f[i]->content, pt, &s);
          fill_cell_owned (footnotes, 0, i, 0, i, PIVOT_AREA_FOOTER, &s,
                           false);
        }
    }
  else
    footnotes = NULL;

  *titlep = title;
  if (layersp)
    *layersp = layers;
  *bodyp = body;
  if (captionp)
    *captionp = caption;
  if (footnotesp)
    *footnotesp = footnotes;
  if (fp)
    {
      *fp = f;
      *nfp = nf;
    }
  else
    free (f);
}

void
pivot_table_submit (struct pivot_table *pt)
{
  table_item_submit (table_item_create (pt));
}
