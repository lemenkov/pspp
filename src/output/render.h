/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2014 Free Software Foundation, Inc.

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

#ifndef OUTPUT_RENDER_H
#define OUTPUT_RENDER_H 1

#include <stdbool.h>
#include <stddef.h>
#include "output/table-provider.h"

struct table_item;

enum render_line_style
  {
    RENDER_LINE_NONE,
    RENDER_LINE_SINGLE,
    RENDER_LINE_DASHED,
    RENDER_LINE_THICK,
    RENDER_LINE_THIN,
    RENDER_LINE_DOUBLE,
    RENDER_N_LINES
  };

/* Parameters for rendering a table_item to a device.


   Coordinate system
   =================

   The rendering code assumes that larger 'x' is to the right and larger 'y'
   toward the bottom of the page.

   The rendering code assumes that the table being rendered has its upper left
   corner at (0,0) in device coordinates.  This is usually not the case from
   the driver's perspective, so the driver should expect to apply its own
   offset to coordinates passed to callback functions.
*/
struct render_params
  {
    /* Functional parameters and auxiliary data to pass to them. */
    const struct render_ops *ops;
    void *aux;

    /* Page size to try to fit the rendering into.  Some tables will, of
       course, overflow this size. */
    int size[TABLE_N_AXES];

    /* Nominal size of a character in the most common font:
       font_size[TABLE_HORZ]: Em width.
       font_size[TABLE_VERT]: Line spacing. */
    int font_size[TABLE_N_AXES];

    /* Width of different kinds of lines. */
    const int *line_widths;           /* RENDER_N_LINES members. */

    /* Minimum cell width or height before allowing the cell to be broken
       across two pages.  (Joined cells may always be broken at join
       points.) */
    int min_break[TABLE_N_AXES];

    /* True if the driver supports cell margins.  (If false, the rendering
       engine will insert a small space betweeen adjacent cells that don't have
       an intervening rule.)  */
    bool supports_margins;

    /* True if the local language has a right-to-left direction, otherwise
       false.  (Use render_direction_rtl() to find out.) */
    bool rtl;

    /* True if the table is being rendered for printing (as opposed to
       on-screen display). */
    bool printing;
  };

struct render_ops
  {
    /* Measures CELL's width.  Stores in *MIN_WIDTH the minimum width required
       to avoid splitting a single word across multiple lines (normally, this
       is the width of the longest word in the cell) and in *MAX_WIDTH the
       minimum width required to avoid line breaks other than at new-lines.
       */
    void (*measure_cell_width) (void *aux, const struct table_cell *cell,
                                int *min_width, int *max_width);

    /* Returns the height required to render CELL given a width of WIDTH. */
    int (*measure_cell_height) (void *aux, const struct table_cell *cell,
                                int width);

    /* Given that there is space measuring WIDTH by HEIGHT to render CELL,
       where HEIGHT is insufficient to render the entire height of the cell,
       returns the largest height less than HEIGHT at which it is appropriate
       to break the cell.  For example, if breaking at the specified HEIGHT
       would break in the middle of a line of text, the return value would be
       just sufficiently less that the breakpoint would be between lines of
       text.

       Optional.  If NULL, the rendering engine assumes that all breakpoints
       are acceptable. */
    int (*adjust_break) (void *aux, const struct table_cell *cell,
                         int width, int height);

    /* Draws a generalized intersection of lines in the rectangle whose
       top-left corner is (BB[TABLE_HORZ][0], BB[TABLE_VERT][0]) and whose
       bottom-right corner is (BB[TABLE_HORZ][1], BB[TABLE_VERT][1]).

       STYLES is interpreted this way:

       STYLES[TABLE_HORZ][0]: style of line from top of BB to its center.
       STYLES[TABLE_HORZ][1]: style of line from bottom of BB to its center.
       STYLES[TABLE_VERT][0]: style of line from left of BB to its center.
       STYLES[TABLE_VERT][1]: style of line from right of BB to its center. */
    void (*draw_line) (void *aux, int bb[TABLE_N_AXES][2],
                       enum render_line_style styles[TABLE_N_AXES][2],
                       struct cell_color colors[TABLE_N_AXES][2]);

    /* Draws CELL within bounding box BB.  CLIP is the same as BB (the common
       case) or a subregion enclosed by BB.  In the latter case only the part
       of the cell that lies within CLIP should actually be drawn, although BB
       should used to determine the layout of the cell.

       The text in the cell needs to be vertically offset VALIGN_OFFSET units
       from the top of the bounding box.  This handles vertical alignment with
       the cell.  (The caller doesn't just reduce the bounding box size because
       that would prevent the implementation from filling the entire cell with
       the background color.)  The implementation must handle horizontal
       alignment itself. */
    void (*draw_cell) (void *aux, const struct table_cell *cell, int color_idx,
                       int bb[TABLE_N_AXES][2], int valign_offset,
                       int spill[TABLE_N_AXES][2],
                       int clip[TABLE_N_AXES][2]);

    /* Scales all output by FACTOR, e.g. a FACTOR of 0.5 would cause everything
       subsequent to be drawn half-size.  FACTOR will be greater than 0 and
       less than or equal to 1.

       Optional.  If NULL, the rendering engine won't try to scale output. */
    void (*scale) (void *aux, double factor);
  };

/* An iterator for breaking render_pages into smaller chunks. */
struct render_pager *render_pager_create (const struct render_params *,
                                          const struct pivot_table *,
                                          const size_t *layer_indexes);
void render_pager_destroy (struct render_pager *);

bool render_pager_has_next (const struct render_pager *);
int render_pager_draw_next (struct render_pager *, int space);

void render_pager_draw (const struct render_pager *);
void render_pager_draw_region (const struct render_pager *,
                               int x, int y, int w, int h);

int render_pager_get_size (const struct render_pager *, enum table_axis);
int render_pager_get_best_breakpoint (const struct render_pager *, int height);

bool render_direction_rtl (void);


#endif /* output/render.h */
