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

.  A table is a rectangular grid of cells.  Cells can be joined to form larger
   cells.  Rows and columns can be separated by rules of various types.  Rows
   at the top and bottom of a table and columns at the left and right edges of
   a table can be designated as headers, which means that if the table must be
   broken across more than one page, those rows or columns are repeated on each
   page.

   Every table is an instance of a particular table class that is responsible
   for keeping track of cell data.  By far the most common table class is
   struct tab_table (see output/tab.h).  This header also declares some other
   kinds of table classes, near the end of the file.

   A table is not itself an output_item, and thus a table cannot by itself be
   used for output, but they can be embedded inside struct table_item (see
   table-item.h) for that purpose. */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

struct casereader;
struct fmt_spec;
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
cell_color_equal (const struct cell_color *a, const struct cell_color *b)
{
  return a->alpha == b->alpha && a->r == b->r && a->g == b->g && a->b == b->b;
}

void cell_color_dump (const struct cell_color *);

enum table_stroke
  {
    TABLE_STROKE_NONE,
    TABLE_STROKE_SOLID,
    TABLE_STROKE_DASHED,
    TABLE_STROKE_THICK,
    TABLE_STROKE_THIN,
    TABLE_STROKE_DOUBLE,
    TABLE_N_STROKES,
  };

const char *table_stroke_to_string (enum table_stroke);

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
    int size;
  };

#define FONT_STYLE_INITIALIZER { FONT_STYLE_INITIALIZER__ }
#define FONT_STYLE_INITIALIZER__                                        \
        .fg = { [0] = CELL_COLOR_BLACK, [1] = CELL_COLOR_BLACK},        \
        .bg = { [0] = CELL_COLOR_WHITE, [1] = CELL_COLOR_WHITE},

void font_style_copy (struct font_style *, const struct font_style *);
void font_style_uninit (struct font_style *);
void font_style_dump (const struct font_style *);

struct area_style
  {
    struct cell_style cell_style;
    struct font_style font_style;
  };

#define AREA_STYLE_INITIALIZER { AREA_STYLE_INITIALIZER__ }
#define AREA_STYLE_INITIALIZER__                \
       .cell_style = CELL_STYLE_INITIALIZER,    \
       .font_style = FONT_STYLE_INITIALIZER

struct area_style *area_style_clone (struct pool *, const struct area_style *);
void area_style_copy (struct area_style *, const struct area_style *);
void area_style_uninit (struct area_style *);
void area_style_free (struct area_style *);

/* Properties of a table cell. */
enum
  {
    TAB_NONE = 0,
    TAB_FIX        = 1 << 1,    /* Use fixed font. */
    TAB_MARKUP     = 1 << 2,    /* Text contains Pango markup. */
    TAB_NUMERIC    = 1 << 3,    /* Cell contents are numeric. */
    TAB_ROTATE     = 1 << 4,    /* Rotate cell contents 90 degrees. */

    /* Bits with values (1 << TAB_FIRST_AVAILABLE) and higher are
       not used, so they are available for subclasses to use as
       they wish. */
    TAB_FIRST_AVAILABLE = 5
  };

/* Styles for the rules around table cells. */
enum
  {
    TAL_NONE = TABLE_STROKE_NONE,
#define TAL_0 TAL_NONE
    TAL_SOLID = TABLE_STROKE_SOLID,
#define TAL_1 TAL_SOLID
    TAL_DASHED = TABLE_STROKE_DASHED,
    TAL_THICK = TABLE_STROKE_THICK,
    TAL_THIN = TABLE_STROKE_THIN,
    TAL_DOUBLE = TABLE_STROKE_DOUBLE,
#define TAL_2 TAL_DOUBLE
  };

/* Given line styles A and B (each one of the TAL_* enumeration constants
   above), returns a line style that "combines" them, that is, that gives a
   reasonable line style choice for a rule for different reasons should have
   both styles A and B. */
static inline int table_rule_combine (int a, int b)
{
  return a > b ? a : b;
}

/* A table. */
struct table
  {
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
  };

/* Reference counting. */
struct table *table_ref (const struct table *);
void table_unref (struct table *);
bool table_is_shared (const struct table *);

/* Returns the number of columns or rows, respectively, in T. */
static inline int table_nc (const struct table *t)
        { return t->n[TABLE_HORZ]; }
static inline int table_nr (const struct table *t)
        { return t->n[TABLE_VERT]; }

/* Returns the number of left, right, top, or bottom headers, respectively, in
   T.  */
static inline int table_hl (const struct table *t)
        { return t->h[TABLE_HORZ][0]; }
static inline int table_hr (const struct table *t)
        { return t->h[TABLE_HORZ][1]; }
static inline int table_ht (const struct table *t)
        { return t->h[TABLE_VERT][0]; }
static inline int table_hb (const struct table *t)
        { return t->h[TABLE_VERT][1]; }

/* Table classes. */

/* Simple kinds of tables. */
struct table *table_from_string (const char *);

#endif /* output/table.h */
