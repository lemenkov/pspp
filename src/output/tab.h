/* PSPP - a program for statistical analysis.
   Copyright (C) 1997, 1998, 1999, 2000, 2009, 2011, 2014 Free Software Foundation, Inc.

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

#ifndef OUTPUT_TAB_H
#define OUTPUT_TAB_H

/* Simple table class.

   This is a type of table (see output/table.h) whose content is composed
   manually by the code that generates it, by filling in cells one by one.
*/

#include "libpspp/compiler.h"
#include "output/table.h"
#include "data/format.h"

#define TAB_STYLE_MASK (7u << (TAB_FIRST_AVAILABLE + 1))
#define TAB_STYLE_SHIFT (TAB_FIRST_AVAILABLE + 1)

enum
  {
    /* Horizontal alignment of cell contents. */
    TAB_RIGHT      = 0 << (TAB_FIRST_AVAILABLE + 4),
    TAB_LEFT       = 1 << (TAB_FIRST_AVAILABLE + 4),
    TAB_CENTER     = 2 << (TAB_FIRST_AVAILABLE + 4),
    TAB_HALIGN     = 3 << (TAB_FIRST_AVAILABLE + 4), /* Alignment mask. */

    /* Vertical alignment of cell contents. */
    TAB_TOP        = 0 << (TAB_FIRST_AVAILABLE + 6),
    TAB_MIDDLE     = 1 << (TAB_FIRST_AVAILABLE + 6),
    TAB_BOTTOM     = 2 << (TAB_FIRST_AVAILABLE + 6),
    TAB_VALIGN     = 3 << (TAB_FIRST_AVAILABLE + 6), /* Alignment mask. */
  };

/* Rule masks. */
#define TAB_RULE_TYPE_MASK   7
#define TAB_RULE_TYPE_SHIFT  0
#define TAB_RULE_STYLE_MASK  (31 << TAB_RULE_STYLE_SHIFT)
#define TAB_RULE_STYLE_SHIFT 3

/* A table. */
struct tab_table
  {
    struct table table;
    struct pool *container;

    /* Table contents.

       Each array element in cc[] is ordinarily a "char *" pointer to a
       string.  If TAB_JOIN (defined in tab.c) is set in ct[] for the element,
       however, it is a joined cell and the corresponding element of cc[]
       points to a struct tab_joined_cell. */
    void **cc;                  /* Cell contents; void *[nr][nc]. */
    unsigned short *ct;		/* Cell types; unsigned short[nr][nc]. */
    struct area_style *styles[8];

    /* Rules. */
    unsigned char *rh;		/* Horiz rules; unsigned char[nr+1][nc]. */
    unsigned char *rv;		/* Vert rules; unsigned char[nr][nc+1]. */
    struct cell_color *rule_colors[32];
  };

struct tab_table *tab_cast (const struct table *);

/* Number of rows or columns in TABLE. */
static inline int tab_nr (const struct tab_table *table)
        { return table_nr (&table->table); }
static inline int tab_nc (const struct tab_table *table)
        { return table_nc (&table->table); }

/* Number of left/right/top/bottom header columns/rows in TABLE. */
static inline int tab_l (const struct tab_table *table)
        { return table_hl (&table->table); }
static inline int tab_r (const struct tab_table *table)
        { return table_hr (&table->table); }
static inline int tab_t (const struct tab_table *table)
        { return table_ht (&table->table); }
static inline int tab_b (const struct tab_table *table)
        { return table_hb (&table->table); }

/* Tables. */
struct tab_table *tab_create (int nc, int nr);
void tab_headers (struct tab_table *, int l, int r, int t, int b);

/* Rules. */
void tab_hline (struct tab_table *, int style, int x1, int x2, int y);
void tab_vline (struct tab_table *, int style, int x, int y1, int y2);
void tab_box (struct tab_table *, int f_h, int f_v, int i_h, int i_v,
	      int x1, int y1, int x2, int y2);

/* Cells. */
void tab_text (struct tab_table *, int c, int r, unsigned opt, const char *);
void tab_text_format (struct tab_table *, int c, int r, unsigned opt,
                      const char *, ...)
     PRINTF_FORMAT (5, 6);

void tab_joint_text (struct tab_table *, int x1, int y1, int x2, int y2,
		     unsigned opt, const char *);

struct footnote *tab_create_footnote (struct tab_table *, size_t idx,
                                      const char *content, const char *marker,
                                      struct area_style *);
void tab_add_footnote (struct tab_table *, int x, int y,
                       const struct footnote *);

void tab_add_style (struct tab_table *, int x, int y,
                    const struct area_style *);

bool tab_cell_is_empty (const struct tab_table *, int c, int r);

/* Simple output. */
void tab_output_text (int options, const char *string);
void tab_output_text_format (int options, const char *, ...)
     PRINTF_FORMAT (2, 3);

/* For use by table-provider only. */
struct table_cell;
void tab_destroy (struct table *);
void tab_get_cell (const struct table *, int x, int y, struct table_cell *);
int tab_get_rule (const struct table *, enum table_axis, int x, int y,
                  struct cell_color *);

#endif /* output/tab.h */

