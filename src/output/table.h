/* PSPP - a program for statistical analysis.
   Copyright (C) 1997, 1998, 1999, 2000, 2009, 2013, 2014 Free Software Foundation, Inc.

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

#ifndef OUTPUT_TABLE_H
#define OUTPUT_TABLE_H 1

/* Tables.

   A table is a rectangular grid of cells.  Cells can be joined to form larger
   cells.  Rows and columns can be separated by rules of various types.  Rows
   at the top and bottom of a table and columns at the left and right edges of
   a table can be designated as headers, which means that if the table must be
   broken across more than one page, those rows or columns are repeated on each
   page.

   Some drivers use tables as an implementation detail of rendering pivot
   tables.
*/

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "libpspp/compiler.h"

struct casereader;
struct fmt_spec;
struct pivot_footnote;
struct pivot_value;
struct pool;
struct table_item;
struct variable;

/* A table axis.

   Many table-related declarations use 2-element arrays in place of "x" and "y"
   variables.  This reduces code duplication significantly, because much table
   code treats rows and columns the same way.

   A lot of code that uses these enumerations assumes that the two values are 0
   and 1, so don't change them to other values. */
enum table_axis
  {
    TABLE_HORZ,
    TABLE_VERT
#define TABLE_N_AXES 2
  };

struct cell_color
  {
    uint8_t alpha, r, g, b;
  };

#define CELL_COLOR(r, g, b) { 255, r, g, b }
#define CELL_COLOR_BLACK CELL_COLOR (0, 0, 0)
#define CELL_COLOR_WHITE CELL_COLOR (255, 255, 255)

static inline bool
cell_color_equal (const struct cell_color a, const struct cell_color b)
{
  return a.alpha == b.alpha && a.r == b.r && a.g == b.g && a.b == b.b;
}

void cell_color_dump (const struct cell_color *);

enum table_stroke
  {
    TABLE_STROKE_NONE = 0,      /* Must be zero. */
    TABLE_STROKE_SOLID,
    TABLE_STROKE_DASHED,
    TABLE_STROKE_THICK,
    TABLE_STROKE_THIN,
    TABLE_STROKE_DOUBLE,
    TABLE_N_STROKES,
  };

const char *table_stroke_to_string (enum table_stroke);

/* Given strokes A and B, returns a stroke that "combines" them, that is, that
   gives a reasonable stroke choice for a rule for different reasons should
   have both styles A and B. */
static inline int
table_stroke_combine (enum table_stroke a, enum table_stroke b)
{
  return a > b ? a : b;
}

struct table_border_style
  {
    enum table_stroke stroke;
    struct cell_color color;
  };

#define TABLE_BORDER_STYLE_INITIALIZER { TABLE_STROKE_SOLID, CELL_COLOR_BLACK }

enum table_halign
  {
    TABLE_HALIGN_RIGHT,
    TABLE_HALIGN_LEFT,
    TABLE_HALIGN_CENTER,
    TABLE_HALIGN_MIXED,
    TABLE_HALIGN_DECIMAL
  };

const char *table_halign_to_string (enum table_halign);

enum table_valign
  {
    TABLE_VALIGN_TOP,
    TABLE_VALIGN_CENTER,
    TABLE_VALIGN_BOTTOM,
  };

const char *table_valign_to_string (enum table_valign);

struct cell_style
  {
    enum table_halign halign;
    enum table_valign valign;
    double decimal_offset;       /* In 1/96" units. */
    char decimal_char;           /* Either '.' or ','. */
    int margin[TABLE_N_AXES][2]; /* In 1/96" units. */
  };

#define CELL_STYLE_INITIALIZER { CELL_STYLE_INITIALIZER__ }
#define CELL_STYLE_INITIALIZER__                                \
        .margin = { [TABLE_HORZ][0] = 8, [TABLE_HORZ][1] = 11,  \
                    [TABLE_VERT][0] = 1, [TABLE_VERT][1] = 1 }

void cell_style_dump (const struct cell_style *);

struct font_style
  {
    bool bold, italic, underline, markup;
    struct cell_color fg[2], bg[2];
    char *typeface;
    int size;                   /* In 1/72" units. */
  };

#define FONT_STYLE_INITIALIZER { FONT_STYLE_INITIALIZER__ }
#define FONT_STYLE_INITIALIZER__                                        \
        .fg = { [0] = CELL_COLOR_BLACK, [1] = CELL_COLOR_BLACK},        \
        .bg = { [0] = CELL_COLOR_WHITE, [1] = CELL_COLOR_WHITE},

void font_style_copy (struct pool *,
                      struct font_style *, const struct font_style *);
void font_style_uninit (struct font_style *);
void font_style_dump (const struct font_style *);
bool font_style_equal (const struct font_style *, const struct font_style *);

struct table_area_style
  {
    struct cell_style cell_style;
    struct font_style font_style;
  };

#define TABLE_AREA_STYLE_INITIALIZER { TABLE_AREA_STYLE_INITIALIZER__ }
#define TABLE_AREA_STYLE_INITIALIZER__          \
       .cell_style = CELL_STYLE_INITIALIZER,    \
       .font_style = FONT_STYLE_INITIALIZER

struct table_area_style *table_area_style_clone (
  struct pool *, const struct table_area_style *);
void table_area_style_copy (struct pool *, struct table_area_style *,
                            const struct table_area_style *);
void table_area_style_uninit (struct table_area_style *);
void table_area_style_free (struct table_area_style *);

/* Cell properties. */
enum
  {
    TABLE_CELL_ROTATE     = 1 << 0,    /* Rotate cell contents 90 degrees. */
    TABLE_CELL_JOIN       = 1 << 1,    /* Joined cell (internal use only). */
    TABLE_CELL_STYLE_SHIFT = 2,
    TABLE_CELL_STYLE_MASK = 7 << TABLE_CELL_STYLE_SHIFT,
  };

/* A table. */
struct table
  {
    struct pool *container;

    /* Table size.

       n[TABLE_HORZ]: Number of columns.
       n[TABLE_VERT]: Number of rows. */
    int n[TABLE_N_AXES];

    /* Table headers.

       Rows at the top and bottom of a table and columns at the left and right
       edges of a table can be designated as headers.  If the table must be
       broken across more than one page for output, headers rows and columns
       are repeated on each page.

       h[TABLE_HORZ][0]: Left header columns.
       h[TABLE_HORZ][1]: Right header columns.
       h[TABLE_VERT][0]: Top header rows.
       h[TABLE_VERT][1]: Bottom header rows. */
    int h[TABLE_N_AXES][2];

    /* Reference count.  A table may be shared between multiple owners,
       indicated by a reference count greater than 1.  When this is the case,
       the table must not be modified. */
    int ref_cnt;

    /* Table contents.

       Each array element in cc[] is ordinarily a "struct pivot_value *".  If
       TABLE_CELL_JOIN is set in cp[] for the element, however, it is a joined
       cell and the corresponding element of cc[] points to a struct
       table_cell. */
    void **cc;                  /* Cell contents; void *[nr][nc]. */
    unsigned char *cp;                /* Cell properties; unsigned char[nr][nc]. */
    struct table_area_style *styles[8];

    /* Rules. */
    unsigned char *rh;                /* Horiz rules; unsigned char[nr+1][nc]. */
    unsigned char *rv;                /* Vert rules; unsigned char[nr][nc+1]. */
    struct table_border_style *borders;
    size_t n_borders;
  };

/* Reference counting. */
struct table *table_ref (const struct table *);
void table_unref (struct table *);
bool table_is_shared (const struct table *);

/* Tables. */
struct table *table_create (int nc, int nr, int hl, int hr, int ht, int hb);

/* Rules. */
void table_hline (struct table *, int style, int x1, int x2, int y);
void table_vline (struct table *, int style, int x, int y1, int y2);

/* Cells. */
void table_put (struct table *, int x1, int y1, int x2, int y2,
                unsigned opt, const struct pivot_value *);
void table_put_owned (struct table *, int x1, int y1, int x2, int y2,
                      unsigned opt, struct pivot_value *);

bool table_cell_is_empty (const struct table *, int c, int r);

#endif /* output/table.h */
