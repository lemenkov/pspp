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
#include "output/table.h"
#include "output/text-item.h"

#include "gl/xalloc.h"

/* This file uses TABLE_HORZ and TABLE_VERT enough to warrant abbreviating. */
#define H TABLE_HORZ
#define V TABLE_VERT

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
        pool_destroy (table->container);
    }
}

/* Returns true if TABLE has more than one owner.  A table item that is shared
   among multiple owners must not be modified. */
bool
table_is_shared (const struct table *table)
{
  return table->ref_cnt > 1;
}

struct table_area_style *
table_area_style_clone (struct pool *pool, const struct table_area_style *old)
{
  struct table_area_style *new = pool_malloc (pool, sizeof *new);
  *new = *old;
  if (new->font_style.typeface)
    new->font_style.typeface = pool_strdup (pool, new->font_style.typeface);
  return new;
}

void
table_area_style_free (struct table_area_style *style)
{
  if (style)
    {
      free (style->font_style.typeface);
      free (style);
    }
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
  for (int y = 0; y < t->n[V]; y++)
    {
      struct table_cell cell;
      for (int x = 0; x < t->n[H]; x = cell.d[TABLE_HORZ][1])
        {
          table_get_cell (t, x, y, &cell);

          if (x == cell.d[TABLE_HORZ][0] && y == cell.d[TABLE_VERT][0])
            footnotes = add_footnotes (cell.footnotes, cell.n_footnotes,
                                       footnotes, &allocated, &n);
        }
    }

  const struct table_item_text *title = table_item_get_title (item);
  if (title)
    footnotes = add_footnotes (title->footnotes, title->n_footnotes,
                               footnotes, &allocated, &n);

  const struct table_item_layers *layers = table_item_get_layers (item);
  if (layers)
    {
      for (size_t i = 0; i < layers->n_layers; i++)
        footnotes = add_footnotes (layers->layers[i].footnotes,
                                   layers->layers[i].n_footnotes,
                                   footnotes, &allocated, &n);
    }

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
font_style_copy (struct pool *container,
                 struct font_style *dst, const struct font_style *src)
{
  *dst = *src;
  if (dst->typeface)
    dst->typeface = pool_strdup (container, dst->typeface);
}

void
font_style_uninit (struct font_style *font)
{
  if (font)
    free (font->typeface);
}

void
table_area_style_copy (struct pool *container, struct table_area_style *dst,
                       const struct table_area_style *src)
{
  font_style_copy (container, &dst->font_style, &src->font_style);
  dst->cell_style = src->cell_style;
}

void
table_area_style_uninit (struct table_area_style *area)
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


static const bool debugging = true;

/* Creates and returns a new table with NC columns and NR rows and initially no
   header rows or columns.

   Sets the number of header rows on each side of TABLE to HL on the
   left, HR on the right, HT on the top, HB on the bottom.  Header rows
   are repeated when a table is broken across multiple columns or
   multiple pages.

   The table's cells are initially empty. */
struct table *
table_create (int nc, int nr, int hl, int hr, int ht, int hb)
{
  struct table *t;

  t = pool_create_container (struct table, container);
  t->n[TABLE_HORZ] = nc;
  t->n[TABLE_VERT] = nr;
  t->h[TABLE_HORZ][0] = hl;
  t->h[TABLE_HORZ][1] = hr;
  t->h[TABLE_VERT][0] = ht;
  t->h[TABLE_VERT][1] = hb;
  t->ref_cnt = 1;

  t->cc = pool_calloc (t->container, nr * nc, sizeof *t->cc);
  t->ct = pool_calloc (t->container, nr * nc, sizeof *t->ct);

  t->rh = pool_nmalloc (t->container, nc, nr + 1);
  memset (t->rh, TABLE_STROKE_NONE, nc * (nr + 1));

  t->rv = pool_nmalloc (t->container, nr, nc + 1);
  memset (t->rv, TABLE_STROKE_NONE, nr * (nc + 1));

  memset (t->styles, 0, sizeof t->styles);
  memset (t->rule_colors, 0, sizeof t->rule_colors);

  return t;
}

/* Rules. */

/* Draws a vertical line to the left of cells at horizontal position X
   from Y1 to Y2 inclusive in style STYLE, if style is not -1. */
void
table_vline (struct table *t, int style, int x, int y1, int y2)
{
  if (debugging)
    {
      if (x < 0 || x > t->n[H]
          || y1 < 0 || y1 >= t->n[V]
          || y2 < 0 || y2 >= t->n[V])
        {
          printf ("bad vline: x=%d y=(%d,%d) in table size (%d,%d)\n",
                  x, y1, y2, t->n[H], t->n[V]);
          return;
        }
    }

  assert (x >= 0);
  assert (x <= t->n[H]);
  assert (y1 >= 0);
  assert (y2 >= y1);
  assert (y2 <= t->n[V]);

  if (style != -1)
    {
      int y;
      for (y = y1; y <= y2; y++)
        t->rv[x + (t->n[H] + 1) * y] = style;
    }
}

/* Draws a horizontal line above cells at vertical position Y from X1
   to X2 inclusive in style STYLE, if style is not -1. */
void
table_hline (struct table *t, int style, int x1, int x2, int y)
{
  if (debugging)
    {
      if (y < 0 || y > t->n[V]
          || x1 < 0 || x1 >= t->n[H]
          || x2 < 0 || x2 >= t->n[H])
        {
          printf ("bad hline: x=(%d,%d) y=%d in table size (%d,%d)\n",
                  x1, x2, y, t->n[H], t->n[V]);
          return;
        }
    }

  assert (y >= 0);
  assert (y <= t->n[V]);
  assert (x2 >= x1);
  assert (x1 >= 0);
  assert (x2 < t->n[H]);

  if (style != -1)
    {
      int x;
      for (x = x1; x <= x2; x++)
        t->rh[x + t->n[H] * y] = style;
    }
}

/* Draws a box around cells (X1,Y1)-(X2,Y2) inclusive with horizontal
   lines of style F_H and vertical lines of style F_V.  Fills the
   interior of the box with horizontal lines of style I_H and vertical
   lines of style I_V.  Any of the line styles may be -1 to avoid
   drawing those lines.  This is distinct from 0, which draws a null
   line. */
void
table_box (struct table *t, int f_h, int f_v, int i_h, int i_v,
           int x1, int y1, int x2, int y2)
{
  if (debugging)
    {
      if (x1 < 0 || x1 >= t->n[H]
          || x2 < 0 || x2 >= t->n[H]
          || y1 < 0 || y1 >= t->n[V]
          || y2 < 0 || y2 >= t->n[V])
        {
          printf ("bad box: (%d,%d)-(%d,%d) in table size (%d,%d)\n",
                  x1, y1, x2, y2, t->n[H], t->n[V]);
          NOT_REACHED ();
        }
    }

  assert (x2 >= x1);
  assert (y2 >= y1);
  assert (x1 >= 0);
  assert (y1 >= 0);
  assert (x2 < t->n[H]);
  assert (y2 < t->n[V]);

  if (f_h != -1)
    {
      int x;
      for (x = x1; x <= x2; x++)
        {
          t->rh[x + t->n[H] * y1] = f_h;
          t->rh[x + t->n[H] * (y2 + 1)] = f_h;
        }
    }
  if (f_v != -1)
    {
      int y;
      for (y = y1; y <= y2; y++)
        {
          t->rv[x1 + (t->n[H] + 1) * y] = f_v;
          t->rv[(x2 + 1) + (t->n[H] + 1) * y] = f_v;
        }
    }

  if (i_h != -1)
    {
      int y;

      for (y = y1 + 1; y <= y2; y++)
        {
          int x;

          for (x = x1; x <= x2; x++)
            t->rh[x + t->n[H] * y] = i_h;
        }
    }
  if (i_v != -1)
    {
      int x;

      for (x = x1 + 1; x <= x2; x++)
        {
          int y;

          for (y = y1; y <= y2; y++)
            t->rv[x + (t->n[H] + 1) * y] = i_v;
        }
    }
}

/* Cells. */

static void
do_table_text (struct table *table, int c, int r, unsigned opt, char *text)
{
  assert (c >= 0);
  assert (r >= 0);
  assert (c < table->n[H]);
  assert (r < table->n[V]);

  if (debugging)
    {
      if (c < 0 || r < 0 || c >= table->n[H] || r >= table->n[V])
        {
          printf ("table_text(): bad cell (%d,%d) in table size (%d,%d)\n",
                  c, r, table->n[H], table->n[V]);
          return;
        }
    }

  table->cc[c + r * table->n[H]] = text;
  table->ct[c + r * table->n[H]] = opt;
}

/* Sets cell (C,R) in TABLE, with options OPT, to have text value
   TEXT. */
void
table_text (struct table *table, int c, int r, unsigned opt,
          const char *text)
{
  do_table_text (table, c, r, opt, pool_strdup (table->container, text));
}

/* Sets cell (C,R) in TABLE, with options OPT, to have text value
   FORMAT, which is formatted as if passed to printf. */
void
table_text_format (struct table *table, int c, int r, unsigned opt,
                   const char *format, ...)
{
  va_list args;

  va_start (args, format);
  do_table_text (table, c, r, opt,
                 pool_vasprintf (table->container, format, args));
  va_end (args);
}

static struct table_cell *
add_joined_cell (struct table *table, int x1, int y1, int x2, int y2,
                 unsigned opt)
{
  assert (x1 >= 0);
  assert (y1 >= 0);
  assert (y2 >= y1);
  assert (x2 >= x1);
  assert (y2 < table->n[V]);
  assert (x2 < table->n[H]);

  if (debugging)
    {
      if (x1 < 0 || x1 >= table->n[H]
          || y1 < 0 || y1 >= table->n[V]
          || x2 < x1 || x2 >= table->n[H]
          || y2 < y1 || y2 >= table->n[V])
        {
          printf ("table_joint_text(): bad cell "
                  "(%d,%d)-(%d,%d) in table size (%d,%d)\n",
                  x1, y1, x2, y2, table->n[H], table->n[V]);
          return NULL;
        }
    }

  table_box (table, -1, -1, TABLE_STROKE_NONE, TABLE_STROKE_NONE,
             x1, y1, x2, y2);

  struct table_cell *cell = pool_alloc (table->container, sizeof *cell);
  *cell = (struct table_cell) {
    .d = { [TABLE_HORZ] = { x1, ++x2 },
           [TABLE_VERT] = { y1, ++y2 } },
    .options = opt,
  };

  void **cc = &table->cc[x1 + y1 * table->n[H]];
  unsigned short *ct = &table->ct[x1 + y1 * table->n[H]];
  const int ofs = table->n[H] - (x2 - x1);
  for (int y = y1; y < y2; y++)
    {
      for (int x = x1; x < x2; x++)
        {
          *cc++ = cell;
          *ct++ = opt | TAB_JOIN;
        }

      cc += ofs;
      ct += ofs;
    }

  return cell;
}

/* Joins cells (X1,X2)-(Y1,Y2) inclusive in TABLE, and sets them with
   options OPT to have text value TEXT. */
void
table_joint_text (struct table *table, int x1, int y1, int x2, int y2,
                  unsigned opt, const char *text)
{
  char *s = pool_strdup (table->container, text);
  if (x1 == x2 && y1 == y2)
    do_table_text (table, x1, y1, opt, s);
  else
    add_joined_cell (table, x1, y1, x2, y2, opt)->text = s;
}

static struct table_cell *
get_joined_cell (struct table *table, int x, int y)
{
  int index = x + y * table->n[H];
  unsigned short opt = table->ct[index];
  struct table_cell *cell;

  if (opt & TAB_JOIN)
    cell = table->cc[index];
  else
    {
      char *text = table->cc[index];

      cell = add_joined_cell (table, x, y, x, y, table->ct[index]);
      cell->text = text ? text : pool_strdup (table->container, "");
    }
  return cell;
}

/* Sets the subscripts for column X, row Y in TABLE. */
void
table_add_subscripts (struct table *table, int x, int y,
                      char **subscripts, size_t n_subscripts)
{
  struct table_cell *cell = get_joined_cell (table, x, y);

  cell->n_subscripts = n_subscripts;
  cell->subscripts = pool_nalloc (table->container, n_subscripts,
                                  sizeof *cell->subscripts);
  for (size_t i = 0; i < n_subscripts; i++)
    cell->subscripts[i] = pool_strdup (table->container, subscripts[i]);
}

/* Sets the superscript for column X, row Y in TABLE. */
void
table_add_superscript (struct table *table, int x, int y,
                       const char *superscript)
{
  get_joined_cell (table, x, y)->superscript
    = pool_strdup (table->container, superscript);
}

/* Create a footnote in TABLE with MARKER (e.g. "a") as its marker and CONTENT
   as its content.  The footnote will be styled as STYLE, which is mandatory.
   IDX must uniquely identify the footnote within TABLE.

   Returns the new footnote.  The return value is the only way to get to the
   footnote later, so it is important for the caller to remember it. */
struct footnote *
table_create_footnote (struct table *table, size_t idx, const char *content,
                       const char *marker, struct table_area_style *style)
{
  assert (style);

  struct footnote *f = pool_alloc (table->container, sizeof *f);
  f->idx = idx;
  f->content = pool_strdup (table->container, content);
  f->marker = pool_strdup (table->container, marker);
  f->style = style;
  return f;
}

/* Attaches a reference to footnote F to the cell at column X, row Y in
   TABLE. */
void
table_add_footnote (struct table *table, int x, int y,
                    const struct footnote *f)
{
  assert (f->style);

  struct table_cell *cell = get_joined_cell (table, x, y);

  cell->footnotes = pool_realloc (
    table->container, cell->footnotes,
    (cell->n_footnotes + 1) * sizeof *cell->footnotes);

  cell->footnotes[cell->n_footnotes++] = f;
}

/* Overrides the style for column X, row Y in TABLE with STYLE.
   Does not make a copy of STYLE, so it should either be allocated from
   TABLE->container or have a lifetime that will outlive TABLE. */
void
table_add_style (struct table *table, int x, int y,
                 const struct table_area_style *style)
{
  get_joined_cell (table, x, y)->style = style;
}

/* Returns true if column C, row R has no contents, otherwise false. */
bool
table_cell_is_empty (const struct table *table, int c, int r)
{
  return table->cc[c + r * table->n[H]] == NULL;
}

/* Initializes CELL with the contents of the table cell at column X and row Y
   within TABLE.  When CELL is no longer needed, the caller is responsible for
   freeing it by calling table_cell_free(CELL).

   The caller must ensure that CELL is destroyed before TABLE is unref'ed. */
void
table_get_cell (const struct table *t, int x, int y, struct table_cell *cell)
{
  assert (x >= 0 && x < t->n[TABLE_HORZ]);
  assert (y >= 0 && y < t->n[TABLE_VERT]);

  int index = x + y * t->n[H];
  unsigned short opt = t->ct[index];
  const void *cc = t->cc[index];

  const struct table_area_style *style
    = t->styles[(opt & TAB_STYLE_MASK) >> TAB_STYLE_SHIFT];
  if (opt & TAB_JOIN)
    {
      const struct table_cell *jc = cc;
      *cell = *jc;
      if (!cell->style)
        cell->style = style;
    }
  else
    *cell = (struct table_cell) {
      .d = { [TABLE_HORZ] = { x, x + 1 },
             [TABLE_VERT] = { y, y + 1 } },
      .options = opt,
      .text = CONST_CAST (char *, cc ? cc : ""),
      .style = style,
    };

  assert (cell->style);
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

  uint8_t raw = (axis == TABLE_VERT
                 ? table->rh[x + table->n[H] * y]
                 : table->rv[x + (table->n[H] + 1) * y]);
  struct cell_color *p = table->rule_colors[(raw & TAB_RULE_STYLE_MASK)
                                            >> TAB_RULE_STYLE_SHIFT];
  *color = p ? *p : (struct cell_color) CELL_COLOR_BLACK;
  return (raw & TAB_RULE_TYPE_MASK) >> TAB_RULE_TYPE_SHIFT;
}
