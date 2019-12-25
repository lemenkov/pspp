/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2013, 2014, 2016 Free Software Foundation, Inc.

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

#include "output/tab.h"

#include <ctype.h>
#include <stdarg.h>
#include <limits.h>
#include <stdlib.h>

#include "data/data-out.h"
#include "data/format.h"
#include "data/settings.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "output/driver.h"
#include "output/table-item.h"
#include "output/table-provider.h"
#include "output/text-item.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


static const bool debugging = true;


/* Cell options. */
#define TAB_JOIN     (1u << TAB_FIRST_AVAILABLE)

/* Joined cell. */
struct tab_joined_cell
{
  int d[TABLE_N_AXES][2];       /* Table region, same as struct table_cell. */
  char *text;

  size_t n_footnotes;
  const struct footnote **footnotes;

  const struct area_style *style;
};

static const struct table_class tab_table_class;

/* Creates and returns a new table with NC columns and NR rows and initially no
   header rows or columns.  The table's cells are initially empty. */
struct tab_table *
tab_create (int nc, int nr)
{
  struct tab_table *t;

  t = pool_create_container (struct tab_table, container);
  table_init (&t->table, &tab_table_class, nc, nr);

  t->cc = pool_calloc (t->container, nr * nc, sizeof *t->cc);
  t->ct = pool_calloc (t->container, nr * nc, sizeof *t->ct);

  t->rh = pool_nmalloc (t->container, nc, nr + 1);
  memset (t->rh, TAL_0, nc * (nr + 1));

  t->rv = pool_nmalloc (t->container, nr, nc + 1);
  memset (t->rv, TAL_0, nr * (nc + 1));

  memset (t->styles, 0, sizeof t->styles);
  memset (t->rule_colors, 0, sizeof t->rule_colors);

  return t;
}


/* Sets the number of header rows on each side of TABLE to L on the
   left, R on the right, T on the top, B on the bottom.  Header rows
   are repeated when a table is broken across multiple columns or
   multiple pages. */
void
tab_headers (struct tab_table *table, int l, int r, int t, int b)
{
  table_set_hl (&table->table, l);
  table_set_hr (&table->table, r);
  table_set_ht (&table->table, t);
  table_set_hb (&table->table, b);
}

/* Rules. */

/* Draws a vertical line to the left of cells at horizontal position X
   from Y1 to Y2 inclusive in style STYLE, if style is not -1. */
void
tab_vline (struct tab_table *t, int style, int x, int y1, int y2)
{
  if (debugging)
    {
      if (x < 0 || x > tab_nc (t)
          || y1 < 0 || y1 >= tab_nr (t)
          || y2 < 0 || y2 >= tab_nr (t))
        {
          printf (_("bad vline: x=%d y=(%d,%d) in table size (%d,%d)\n"),
                  x, y1, y2, tab_nc (t), tab_nr (t));
          return;
        }
    }

  assert (x >= 0);
  assert (x <= tab_nc (t));
  assert (y1 >= 0);
  assert (y2 >= y1);
  assert (y2 <= tab_nr (t));

  if (style != -1)
    {
      int y;
      for (y = y1; y <= y2; y++)
        t->rv[x + (tab_nc (t) + 1) * y] = style;
    }
}

/* Draws a horizontal line above cells at vertical position Y from X1
   to X2 inclusive in style STYLE, if style is not -1. */
void
tab_hline (struct tab_table *t, int style, int x1, int x2, int y)
{
  if (debugging)
    {
      if (y < 0 || y > tab_nr (t)
          || x1 < 0 || x1 >= tab_nc (t)
          || x2 < 0 || x2 >= tab_nc (t))
        {
          printf (_("bad hline: x=(%d,%d) y=%d in table size (%d,%d)\n"),
                  x1, x2, y, tab_nc (t), tab_nr (t));
          return;
        }
    }

  assert (y >= 0);
  assert (y <= tab_nr (t));
  assert (x2 >= x1);
  assert (x1 >= 0);
  assert (x2 < tab_nc (t));

  if (style != -1)
    {
      int x;
      for (x = x1; x <= x2; x++)
        t->rh[x + tab_nc (t) * y] = style;
    }
}

/* Draws a box around cells (X1,Y1)-(X2,Y2) inclusive with horizontal
   lines of style F_H and vertical lines of style F_V.  Fills the
   interior of the box with horizontal lines of style I_H and vertical
   lines of style I_V.  Any of the line styles may be -1 to avoid
   drawing those lines.  This is distinct from 0, which draws a null
   line. */
void
tab_box (struct tab_table *t, int f_h, int f_v, int i_h, int i_v,
         int x1, int y1, int x2, int y2)
{
  if (debugging)
    {
      if (x1 < 0 || x1 >= tab_nc (t)
          || x2 < 0 || x2 >= tab_nc (t)
          || y1 < 0 || y1 >= tab_nr (t)
          || y2 < 0 || y2 >= tab_nr (t))
        {
          printf (_("bad box: (%d,%d)-(%d,%d) in table size (%d,%d)\n"),
                  x1, y1, x2, y2, tab_nc (t), tab_nr (t));
          NOT_REACHED ();
        }
    }

  assert (x2 >= x1);
  assert (y2 >= y1);
  assert (x1 >= 0);
  assert (y1 >= 0);
  assert (x2 < tab_nc (t));
  assert (y2 < tab_nr (t));

  if (f_h != -1)
    {
      int x;
      for (x = x1; x <= x2; x++)
        {
          t->rh[x + tab_nc (t) * y1] = f_h;
          t->rh[x + tab_nc (t) * (y2 + 1)] = f_h;
        }
    }
  if (f_v != -1)
    {
      int y;
      for (y = y1; y <= y2; y++)
        {
          t->rv[x1 + (tab_nc (t) + 1) * y] = f_v;
          t->rv[(x2 + 1) + (tab_nc (t) + 1) * y] = f_v;
        }
    }

  if (i_h != -1)
    {
      int y;

      for (y = y1 + 1; y <= y2; y++)
        {
          int x;

          for (x = x1; x <= x2; x++)
            t->rh[x + tab_nc (t) * y] = i_h;
        }
    }
  if (i_v != -1)
    {
      int x;

      for (x = x1 + 1; x <= x2; x++)
        {
          int y;

          for (y = y1; y <= y2; y++)
            t->rv[x + (tab_nc (t) + 1) * y] = i_v;
        }
    }
}

/* Cells. */

static void
do_tab_text (struct tab_table *table, int c, int r, unsigned opt, char *text)
{
  assert (c >= 0);
  assert (r >= 0);
  assert (c < tab_nc (table));
  assert (r < tab_nr (table));

  if (debugging)
    {
      if (c < 0 || r < 0 || c >= tab_nc (table) || r >= tab_nr (table))
        {
          printf ("tab_text(): bad cell (%d,%d) in table size (%d,%d)\n",
                  c, r, tab_nc (table), tab_nr (table));
          return;
        }
    }

  table->cc[c + r * tab_nc (table)] = text;
  table->ct[c + r * tab_nc (table)] = opt;
}

/* Sets cell (C,R) in TABLE, with options OPT, to have text value
   TEXT. */
void
tab_text (struct tab_table *table, int c, int r, unsigned opt,
          const char *text)
{
  do_tab_text (table, c, r, opt, pool_strdup (table->container, text));
}

/* Sets cell (C,R) in TABLE, with options OPT, to have text value
   FORMAT, which is formatted as if passed to printf. */
void
tab_text_format (struct tab_table *table, int c, int r, unsigned opt,
                 const char *format, ...)
{
  va_list args;

  va_start (args, format);
  do_tab_text (table, c, r, opt,
               pool_vasprintf (table->container, format, args));
  va_end (args);
}

static struct tab_joined_cell *
add_joined_cell (struct tab_table *table, int x1, int y1, int x2, int y2,
                 unsigned opt)
{
  struct tab_joined_cell *j;

  assert (x1 >= 0);
  assert (y1 >= 0);
  assert (y2 >= y1);
  assert (x2 >= x1);
  assert (y2 < tab_nr (table));
  assert (x2 < tab_nc (table));

  if (debugging)
    {
      if (x1 < 0 || x1 >= tab_nc (table)
          || y1 < 0 || y1 >= tab_nr (table)
          || x2 < x1 || x2 >= tab_nc (table)
          || y2 < y1 || y2 >= tab_nr (table))
        {
          printf ("tab_joint_text(): bad cell "
                  "(%d,%d)-(%d,%d) in table size (%d,%d)\n",
                  x1, y1, x2, y2, tab_nc (table), tab_nr (table));
          return NULL;
        }
    }

  tab_box (table, -1, -1, TAL_0, TAL_0, x1, y1, x2, y2);

  j = pool_alloc (table->container, sizeof *j);
  j->d[TABLE_HORZ][0] = x1;
  j->d[TABLE_VERT][0] = y1;
  j->d[TABLE_HORZ][1] = ++x2;
  j->d[TABLE_VERT][1] = ++y2;
  j->n_footnotes = 0;
  j->footnotes = NULL;
  j->style = NULL;

  {
    void **cc = &table->cc[x1 + y1 * tab_nc (table)];
    unsigned short *ct = &table->ct[x1 + y1 * tab_nc (table)];
    const int ofs = tab_nc (table) - (x2 - x1);

    int y;

    for (y = y1; y < y2; y++)
      {
        int x;

        for (x = x1; x < x2; x++)
          {
            *cc++ = j;
            *ct++ = opt | TAB_JOIN;
          }

        cc += ofs;
        ct += ofs;
      }
  }

  return j;
}

/* Joins cells (X1,X2)-(Y1,Y2) inclusive in TABLE, and sets them with
   options OPT to have text value TEXT. */
void
tab_joint_text (struct tab_table *table, int x1, int y1, int x2, int y2,
                unsigned opt, const char *text)
{
  char *s = pool_strdup (table->container, text);
  if (x1 == x2 && y1 == y2)
    do_tab_text (table, x1, y1, opt, s);
  else
    add_joined_cell (table, x1, y1, x2, y2, opt)->text = s;
}

struct footnote *
tab_create_footnote (struct tab_table *table, size_t idx, const char *content,
                     const char *marker, struct area_style *style)
{
  struct footnote *f = pool_alloc (table->container, sizeof *f);
  f->idx = idx;
  f->content = pool_strdup (table->container, content);
  f->marker = pool_strdup (table->container, marker);
  f->style = style;
  return f;
}

void
tab_add_footnote (struct tab_table *table, int x, int y,
                  const struct footnote *f)
{
  int index = x + y * tab_nc (table);
  unsigned short opt = table->ct[index];
  struct tab_joined_cell *j;

  if (opt & TAB_JOIN)
    j = table->cc[index];
  else
    {
      char *text = table->cc[index];

      j = add_joined_cell (table, x, y, x, y, table->ct[index]);
      j->text = text ? text : xstrdup ("");
    }

  j->footnotes = pool_realloc (table->container, j->footnotes,
                               (j->n_footnotes + 1) * sizeof *j->footnotes);

  j->footnotes[j->n_footnotes++] = f;
}

void
tab_add_style (struct tab_table *table, int x, int y,
               const struct area_style *style)
{
  int index = x + y * tab_nc (table);
  unsigned short opt = table->ct[index];
  struct tab_joined_cell *j;

  if (opt & TAB_JOIN)
    j = table->cc[index];
  else
    {
      char *text = table->cc[index];

      j = add_joined_cell (table, x, y, x, y, table->ct[index]);
      j->text = text ? text : xstrdup ("");
    }

  j->style = style;
}

bool
tab_cell_is_empty (const struct tab_table *table, int c, int r)
{
  return table->cc[c + r * tab_nc (table)] == NULL;
}

/* Editing. */

/* Writes STRING to the output.  OPTIONS may be any valid combination of TAB_*
   bits.

   This function is obsolete.  Please do not add new uses of it.  Instead, use
   a text_item (see output/text-item.h). */
void
tab_output_text (int options UNUSED, const char *string)
{
  text_item_submit (text_item_create (TEXT_ITEM_LOG, string));
}

/* Same as tab_output_text(), but FORMAT is passed through printf-like
   formatting before output. */
void
tab_output_text_format (int options, const char *format, ...)
{
  va_list args;
  char *text;

  va_start (args, format);
  text = xvasprintf (format, args);
  va_end (args);

  tab_output_text (options, text);

  free (text);
}

/* Table class implementation. */

static void
tab_destroy (struct table *table)
{
  struct tab_table *t = tab_cast (table);
  pool_destroy (t->container);
}

static void
tab_get_cell (const struct table *table, int x, int y,
              struct table_cell *cell)
{
  const struct tab_table *t = tab_cast (table);
  int index = x + y * tab_nc (t);
  unsigned short opt = t->ct[index];
  const void *cc = t->cc[index];

  cell->options = opt;
  cell->n_footnotes = 0;

  int style_idx = (opt & TAB_STYLE_MASK) >> TAB_STYLE_SHIFT;
  const struct area_style *style = t->styles[style_idx];
  if (style)
    cell->style = style;
  else
    {
      static const struct area_style styles[3][3] = {
#define S(H,V) [H][V] = { AREA_STYLE_INITIALIZER__,     \
                          .cell_style.halign = H,       \
                          .cell_style.valign = V }
        S(TABLE_HALIGN_LEFT, TABLE_VALIGN_TOP),
        S(TABLE_HALIGN_LEFT, TABLE_VALIGN_CENTER),
        S(TABLE_HALIGN_LEFT, TABLE_VALIGN_BOTTOM),
        S(TABLE_HALIGN_CENTER, TABLE_VALIGN_TOP),
        S(TABLE_HALIGN_CENTER, TABLE_VALIGN_CENTER),
        S(TABLE_HALIGN_CENTER, TABLE_VALIGN_BOTTOM),
        S(TABLE_HALIGN_RIGHT, TABLE_VALIGN_TOP),
        S(TABLE_HALIGN_RIGHT, TABLE_VALIGN_CENTER),
        S(TABLE_HALIGN_RIGHT, TABLE_VALIGN_BOTTOM),
      };

      enum table_halign halign
        = ((opt & TAB_HALIGN) == TAB_LEFT ? TABLE_HALIGN_LEFT
           : (opt & TAB_HALIGN) == TAB_CENTER ? TABLE_HALIGN_CENTER
           : TABLE_HALIGN_RIGHT);
      enum table_valign valign
        = ((opt & TAB_VALIGN) == TAB_TOP ? TABLE_VALIGN_TOP
           : (opt & TAB_VALIGN) == TAB_MIDDLE ? TABLE_VALIGN_CENTER
           : TABLE_VALIGN_BOTTOM);

      cell->style = &styles[halign][valign];
    }

  if (opt & TAB_JOIN)
    {
      const struct tab_joined_cell *jc = cc;
      cell->text = jc->text;

      cell->footnotes = jc->footnotes;
      cell->n_footnotes = jc->n_footnotes;

      cell->d[TABLE_HORZ][0] = jc->d[TABLE_HORZ][0];
      cell->d[TABLE_HORZ][1] = jc->d[TABLE_HORZ][1];
      cell->d[TABLE_VERT][0] = jc->d[TABLE_VERT][0];
      cell->d[TABLE_VERT][1] = jc->d[TABLE_VERT][1];

      if (jc->style)
        cell->style = jc->style;
    }
  else
    {
      cell->d[TABLE_HORZ][0] = x;
      cell->d[TABLE_HORZ][1] = x + 1;
      cell->d[TABLE_VERT][0] = y;
      cell->d[TABLE_VERT][1] = y + 1;
      cell->text = CONST_CAST (char *, cc ? cc : "");
    }
}

static int
tab_get_rule (const struct table *table, enum table_axis axis, int x, int y,
              struct cell_color *color)
{
  const struct tab_table *t = tab_cast (table);
  uint8_t raw = (axis == TABLE_VERT
                 ? t->rh[x + tab_nc (t) * y]
                 : t->rv[x + (tab_nc (t) + 1) * y]);
  struct cell_color *p = t->rule_colors[(raw & TAB_RULE_STYLE_MASK)
                                        >> TAB_RULE_STYLE_SHIFT];
  if (p)
    *color = *p;
  return (raw & TAB_RULE_TYPE_MASK) >> TAB_RULE_TYPE_SHIFT;
}

static const struct table_class tab_table_class = {
  tab_destroy,
  tab_get_cell,
  tab_get_rule,
};

struct tab_table *
tab_cast (const struct table *table)
{
  assert (table->klass == &tab_table_class);
  return UP_CAST (table, struct tab_table, table);
}
