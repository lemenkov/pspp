/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011, 2014, 2016 Free Software Foundation, Inc.

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

#include "output/table.h"
#include "output/table-provider.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>

#include "data/format.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "output/table-item.h"
#include "output/tab.h"

#include "gl/xalloc.h"

/* Increases TABLE's reference count, indicating that it has an additional
   owner.  An table that is shared among multiple owners must not be
   modified. */
struct table *
table_ref (const struct table *table_)
{
  struct table *table = CONST_CAST (struct table *, table_);
  table->ref_cnt++;
  return table;
}

/* Decreases TABLE's reference count, indicating that it has one fewer owner.
   If TABLE no longer has any owners, it is freed. */
void
table_unref (struct table *table)
{
  if (table != NULL)
    {
      assert (table->ref_cnt > 0);
      if (--table->ref_cnt == 0)
        table->klass->destroy (table);
    }
}

/* Returns true if TABLE has more than one owner.  A table item that is shared
   among multiple owners must not be modified. */
bool
table_is_shared (const struct table *table)
{
  return table->ref_cnt > 1;
}

/* Sets the number of left header columns in TABLE to HL. */
void
table_set_hl (struct table *table, int hl)
{
  assert (!table_is_shared (table));
  table->h[TABLE_HORZ][0] = hl;
}

/* Sets the number of right header columns in TABLE to HR. */
void
table_set_hr (struct table *table, int hr)
{
  assert (!table_is_shared (table));
  table->h[TABLE_HORZ][1] = hr;
}

/* Sets the number of top header rows in TABLE to HT. */
void
table_set_ht (struct table *table, int ht)
{
  assert (!table_is_shared (table));
  table->h[TABLE_VERT][0] = ht;
}

/* Sets the number of top header rows in TABLE to HB. */
void
table_set_hb (struct table *table, int hb)
{
  assert (!table_is_shared (table));
  table->h[TABLE_VERT][1] = hb;
}

/* Initializes TABLE as a table of the specified CLASS, initially with a
   reference count of 1.

   TABLE initially has 0 rows and columns and no headers.  The table
   implementation should update the numbers of rows and columns.  The table
   implementation (or its client) may update the header rows and columns.

   A table is an abstract class, that is, a plain struct table is not useful on
   its own.  Thus, this function is normally called from the initialization
   function of some subclass of table. */
void
table_init (struct table *table, const struct table_class *class)
{
  table->klass = class;
  table->n[TABLE_HORZ] = table->n[TABLE_VERT] = 0;
  table->h[TABLE_HORZ][0] = table->h[TABLE_HORZ][1] = 0;
  table->h[TABLE_VERT][0] = table->h[TABLE_VERT][1] = 0;
  table->ref_cnt = 1;
}

/* Sets the number of columns in TABLE to NC. */
void
table_set_nc (struct table *table, int nc)
{
  assert (!table_is_shared (table));
  table->n[TABLE_HORZ] = nc;
}

/* Sets the number of rows in TABLE to NR. */
void
table_set_nr (struct table *table, int nr)
{
  assert (!table_is_shared (table));
  table->n[TABLE_VERT] = nr;
}

struct area_style *
area_style_clone (struct pool *pool, const struct area_style *old)
{
  struct area_style *new = pool_malloc (pool, sizeof *new);
  *new = *old;
  if (new->font_style.typeface)
    new->font_style.typeface = pool_strdup (pool, new->font_style.typeface);
  return new;
}

void
area_style_free (struct area_style *style)
{
  if (style)
    {
      free (style->font_style.typeface);
      free (style);
    }
}

/* Initializes CELL with the contents of the table cell at column X and row Y
   within TABLE.  When CELL is no longer needed, the caller is responsible for
   freeing it by calling table_cell_free(CELL).

   The caller must ensure that CELL is destroyed before TABLE is unref'ed. */
void
table_get_cell (const struct table *table, int x, int y,
                struct table_cell *cell)
{
  assert (x >= 0 && x < table->n[TABLE_HORZ]);
  assert (y >= 0 && y < table->n[TABLE_VERT]);

  static const struct area_style default_style = AREA_STYLE_INITIALIZER;
  cell->style = &default_style;

  table->klass->get_cell (table, x, y, cell);
}

/* Frees CELL, which should have been initialized by calling
   table_get_cell(). */
void
table_cell_free (struct table_cell *cell)
{
  if (cell->destructor != NULL)
    cell->destructor (cell->destructor_aux);
}

/* Returns one of the TAL_* enumeration constants (declared in output/table.h)
   representing a rule running alongside one of the cells in TABLE.

   Suppose NC is the number of columns in TABLE and NR is the number of rows.
   Then, if AXIS is TABLE_HORZ, then 0 <= X <= NC and 0 <= Y < NR.  If (X,Y) =
   (0,0), the return value is the rule that runs vertically on the left side of
   cell (0,0); if (X,Y) = (1,0), it is the vertical rule between that cell and
   cell (1,0); and so on, up to (NC,0), which runs vertically on the right of
   cell (NC-1,0).

   The following diagram illustrates the meaning of (X,Y) for AXIS = TABLE_HORZ
   within a 7x7 table.  The '|' characters at the intersection of the X labels
   and Y labels show the rule whose style would be returned by calling
   table_get_rule with those X and Y values:

                           0  1  2  3  4  5  6  7
                           +--+--+--+--+--+--+--+
                         0 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+
                         1 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+
                         2 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+
                         3 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+
                         4 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+
                         5 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+
                         6 |  |  |  |  |  |  |  |
                           +--+--+--+--+--+--+--+

   Similarly, if AXIS is TABLE_VERT, then 0 <= X < NC and 0 <= Y <= NR.  If
   (X,Y) = (0,0), the return value is the rule that runs horizontally above
   the top of cell (0,0); if (X,Y) = (0,1), it is the horizontal rule
   between that cell and cell (0,1); and so on, up to (0,NR), which runs
   horizontally below cell (0,NR-1). */
int
table_get_rule (const struct table *table, enum table_axis axis, int x, int y,
                struct cell_color *color)
{
  assert (x >= 0 && x < table->n[TABLE_HORZ] + (axis == TABLE_HORZ));
  assert (y >= 0 && y < table->n[TABLE_VERT] + (axis == TABLE_VERT));
  *color = (struct cell_color) CELL_COLOR_BLACK;
  return table->klass->get_rule (table, axis, x, y, color);
}

void
table_cell_format_footnote_markers (const struct table_cell *cell,
                                    struct string *s)
{
  for (size_t i = 0; i < cell->n_footnotes; i++)
    {
      if (i)
        ds_put_byte (s, ',');
      ds_put_cstr (s, cell->footnotes[i]->marker);
    }
}

static const struct footnote **
add_footnotes (const struct footnote **refs, size_t n_refs,
               const struct footnote **footnotes, size_t *allocated, size_t *n)
{
  for (size_t i = 0; i < n_refs; i++)
    {
      const struct footnote *f = refs[i];
      if (f->idx >= *allocated)
        {
          size_t new_allocated = (f->idx + 1) * 2;
          footnotes = xrealloc (footnotes, new_allocated * sizeof *footnotes);
          while (*allocated < new_allocated)
            footnotes[(*allocated)++] = NULL;
        }
      footnotes[f->idx] = f;
      if (f->idx >= *n)
        *n = f->idx + 1;
    }
  return footnotes;
}

size_t
table_collect_footnotes (const struct table_item *item,
                         const struct footnote ***footnotesp)
{
  const struct footnote **footnotes = NULL;
  size_t allocated = 0;
  size_t n = 0;

  struct table *t = item->table;
  for (int y = 0; y < table_nr (t); y++)
    {
      struct table_cell cell;
      for (int x = 0; x < table_nc (t); x = cell.d[TABLE_HORZ][1])
        {
          table_get_cell (t, x, y, &cell);

          if (x == cell.d[TABLE_HORZ][0] && y == cell.d[TABLE_VERT][0])
            footnotes = add_footnotes (cell.footnotes, cell.n_footnotes,
                                       footnotes, &allocated, &n);
          table_cell_free (&cell);
        }
    }

  const struct table_item_text *title = table_item_get_title (item);
  if (title)
    footnotes = add_footnotes (title->footnotes, title->n_footnotes,
                               footnotes, &allocated, &n);

  const struct table_item_text *caption = table_item_get_caption (item);
  if (caption)
    footnotes = add_footnotes (caption->footnotes, caption->n_footnotes,
                               footnotes, &allocated, &n);

  size_t n_nonnull = 0;
  for (size_t i = 0; i < n; i++)
    if (footnotes[i])
      footnotes[n_nonnull++] = footnotes[i];

  *footnotesp = footnotes;
  return n_nonnull;
}

/* Returns a table that contains a single cell, whose contents are the
   left-aligned TEXT.  */
struct table *
table_from_string (const char *text)
{
  struct tab_table *t = tab_create (1, 1);
  tab_text (t, 0, 0, TAB_LEFT, text);
  return &t->table;
}

const char *
table_halign_to_string (enum table_halign halign)
{
  switch (halign)
    {
    case TABLE_HALIGN_LEFT: return "left";
    case TABLE_HALIGN_CENTER: return "center";
    case TABLE_HALIGN_RIGHT: return "right";
    case TABLE_HALIGN_DECIMAL: return "decimal";
    case TABLE_HALIGN_MIXED: return "mixed";
    default: return "**error**";
    }
}

const char *
table_valign_to_string (enum table_valign valign)
{
  switch (valign)
    {
    case TABLE_VALIGN_TOP: return "top";
    case TABLE_VALIGN_CENTER: return "center";
    case TABLE_VALIGN_BOTTOM: return "bottom";
    default: return "**error**";
    }
}

enum table_halign
table_halign_interpret (enum table_halign halign, bool numeric)
{
  switch (halign)
    {
    case TABLE_HALIGN_LEFT:
    case TABLE_HALIGN_CENTER:
    case TABLE_HALIGN_RIGHT:
      return halign;

    case TABLE_HALIGN_MIXED:
      return numeric ? TABLE_HALIGN_RIGHT : TABLE_HALIGN_LEFT;

    case TABLE_HALIGN_DECIMAL:
      return TABLE_HALIGN_DECIMAL;

    default:
      NOT_REACHED ();
    }
}

void
font_style_copy (struct font_style *dst, const struct font_style *src)
{
  *dst = *src;
  if (dst->typeface)
    dst->typeface = xstrdup (dst->typeface);
}

void
font_style_uninit (struct font_style *font)
{
  if (font)
    free (font->typeface);
}

void
area_style_copy (struct area_style *dst, const struct area_style *src)
{
  font_style_copy (&dst->font_style, &src->font_style);
  dst->cell_style = src->cell_style;
}

void
area_style_uninit (struct area_style *area)
{
  if (area)
    font_style_uninit (&area->font_style);
}

const char *
table_stroke_to_string (enum table_stroke stroke)
{
  switch (stroke)
    {
    case TABLE_STROKE_NONE: return "none";
    case TABLE_STROKE_SOLID: return "solid";
    case TABLE_STROKE_DASHED: return "dashed";
    case TABLE_STROKE_THICK: return "thick";
    case TABLE_STROKE_THIN: return "thin";
    case TABLE_STROKE_DOUBLE: return "double";
    default:
      return "**error**";
    }
}

void
cell_color_dump (const struct cell_color *c)
{
  if (c->alpha != 255)
    printf ("rgba(%d, %d, %d, %d)", c->r, c->g, c->b, c->alpha);
  else
    printf ("#%02"PRIx8"%02"PRIx8"%02"PRIx8, c->r, c->g, c->b);
}

void
font_style_dump (const struct font_style *f)
{
  printf ("%s %dpx ", f->typeface, f->size);
  cell_color_dump (&f->fg[0]);
  putchar ('/');
  cell_color_dump (&f->bg[0]);
  if (!cell_color_equal (&f->fg[0], &f->fg[1])
      || !cell_color_equal (&f->bg[0], &f->bg[1]))
    {
      printf (" alt=");
      cell_color_dump (&f->fg[1]);
      putchar ('/');
      cell_color_dump (&f->bg[1]);
    }
  if (f->bold)
    fputs (" bold", stdout);
  if (f->italic)
    fputs (" italic", stdout);
  if (f->underline)
    fputs (" underline", stdout);
}

void
cell_style_dump (const struct cell_style *c)
{
  fputs (table_halign_to_string (c->halign), stdout);
  if (c->halign == TABLE_HALIGN_DECIMAL)
    printf ("(%.2gpx)", c->decimal_offset);
  printf (" %s", table_valign_to_string (c->valign));
  printf (" %d,%d,%d,%dpx",
          c->margin[TABLE_HORZ][0], c->margin[TABLE_HORZ][1],
          c->margin[TABLE_VERT][0], c->margin[TABLE_VERT][1]);
}
