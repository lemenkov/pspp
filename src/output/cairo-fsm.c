/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016 Free Software Foundation, Inc.

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

#include "output/cairo-fsm.h"

#include <math.h>
#include <pango/pango-layout.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include "libpspp/assertion.h"
#include "libpspp/str.h"
#include "output/cairo-chart.h"
#include "output/chart-provider.h"
#include "output/charts/barchart.h"
#include "output/charts/boxplot.h"
#include "output/charts/np-plot.h"
#include "output/charts/piechart.h"
#include "output/charts/plot-hist.h"
#include "output/charts/roc-chart.h"
#include "output/charts/scatterplot.h"
#include "output/charts/scree.h"
#include "output/charts/spreadlevel-plot.h"
#include "output/pivot-output.h"
#include "output/pivot-table.h"
#include "output/render.h"
#include "output/output-item.h"

#include "gl/c-ctype.h"
#include "gl/c-strcase.h"
#include "gl/xalloc.h"

/* This file uses TABLE_HORZ and TABLE_VERT enough to warrant abbreviating. */
#define H TABLE_HORZ
#define V TABLE_VERT

struct xr_fsm_style *
xr_fsm_style_ref (const struct xr_fsm_style *style_)
{
  assert (style_->ref_cnt > 0);

  struct xr_fsm_style *style = CONST_CAST (struct xr_fsm_style *, style_);
  style->ref_cnt++;
  return style;
}

struct xr_fsm_style *
xr_fsm_style_unshare (struct xr_fsm_style *old)
{
  assert (old->ref_cnt > 0);
  if (old->ref_cnt == 1)
    return old;

  xr_fsm_style_unref (old);

  struct xr_fsm_style *new = xmemdup (old, sizeof *old);
  new->ref_cnt = 1;
  if (old->font)
    new->font = pango_font_description_copy (old->font);

  return new;
}

void
xr_fsm_style_unref (struct xr_fsm_style *style)
{
  if (style)
    {
      assert (style->ref_cnt > 0);
      if (!--style->ref_cnt)
        {
          pango_font_description_free (style->font);
          free (style);
        }
    }
}

bool
xr_fsm_style_equals (const struct xr_fsm_style *a,
                     const struct xr_fsm_style *b)
{
  if (a->size[H] != b->size[H]
      || a->size[V] != b->size[V]
      || a->min_break[H] != b->min_break[H]
      || a->min_break[V] != b->min_break[V]
      || !pango_font_description_equal (a->font, b->font)
      || a->use_system_colors != b->use_system_colors
      || a->object_spacing != b->object_spacing
      || a->font_resolution != b->font_resolution)
    return false;

  return true;
}

/* Renders a single output_item to an output device in one of two ways:

   - 'print == true': Broken across multiple pages if necessary.

   - 'print == false': In a single region that the user may scroll around if
     needed.

   Normally 'output_item' corresponds to a single rendering.  There is a
   special case when 'print == true' and 'output_item' is a table_item with
   multiple layers and 'item->pt->table_look->print_all_layers == true'.  In
   that case, each layer is rendered separately from the FSM's internal point
   of view; from the client's point of view, it is all one operation.
*/
struct xr_fsm
  {
    struct xr_fsm_style *style;
    struct output_item *item;
    bool print;

    /* Print mode only. */
    bool done;

    /* Table items only. */
    size_t *layer_indexes;
    struct render_params rp;
    struct render_pager *p;
    cairo_t *cairo;             /* XXX should this be here?! */
  };

/* The unit used for internal measurements is inch/(72 * XR_POINT).
   (Thus, XR_POINT units represent one point.) */
#define XR_POINT PANGO_SCALE

/* Conversions to and from points. */
static double
xr_to_pt (int x)
{
  return x / (double) XR_POINT;
}

/* Conversion from 1/96" units ("pixels") to Cairo/Pango units. */
static int
px_to_xr (int x)
{
  return x * (PANGO_SCALE * 72 / 96);
}

static int
pango_to_xr (int pango)
{
  return (XR_POINT != PANGO_SCALE
          ? ceil (pango * (1. * XR_POINT / PANGO_SCALE))
          : pango);
}

static int
xr_to_pango (int xr)
{
  return (XR_POINT != PANGO_SCALE
          ? ceil (xr * (1. / XR_POINT * PANGO_SCALE))
          : xr);
}

/* Dimensions for drawing lines in tables. */
#define XR_LINE_WIDTH (XR_POINT / 2) /* Width of an ordinary line. */
#define XR_LINE_SPACE XR_POINT       /* Space between double lines. */

static void
xr_layout_cell (struct xr_fsm *, const struct table_cell *,
                int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2],
                int *width, int *height, int *brk);

static void
xr_set_source_rgba (cairo_t *cairo, const struct cell_color *color)
{
  cairo_set_source_rgba (cairo,
                         color->r / 255., color->g / 255., color->b / 255.,
                         color->alpha / 255.);
}

static void
xr_draw_line (struct xr_fsm *xr, int x0, int y0, int x1, int y1, int style,
              const struct cell_color *color)
{
  cairo_new_path (xr->cairo);
  cairo_set_line_width (
    xr->cairo,
    xr_to_pt (style == RENDER_LINE_THICK ? XR_LINE_WIDTH * 2
              : style == RENDER_LINE_THIN ? XR_LINE_WIDTH / 2
              : XR_LINE_WIDTH));
  cairo_move_to (xr->cairo, xr_to_pt (x0), xr_to_pt (y0));
  cairo_line_to (xr->cairo, xr_to_pt (x1), xr_to_pt (y1));

  if (!xr->style->use_system_colors)
    xr_set_source_rgba (xr->cairo, color);
  if (style == RENDER_LINE_DASHED)
    cairo_set_dash (xr->cairo, (double[]) { 2 }, 1, 0);
  cairo_stroke (xr->cairo);
  if (style == RENDER_LINE_DASHED)
    cairo_set_dash (xr->cairo, NULL, 0, 0);
}

static void UNUSED
xr_draw_rectangle (struct xr_fsm *xr, int x0, int y0, int x1, int y1)
{
  cairo_new_path (xr->cairo);
  cairo_set_line_width (xr->cairo, xr_to_pt (XR_LINE_WIDTH));
  cairo_close_path (xr->cairo);
  cairo_stroke (xr->cairo);
  cairo_move_to (xr->cairo, xr_to_pt (x0), xr_to_pt (y0));
  cairo_line_to (xr->cairo, xr_to_pt (x1), xr_to_pt (y0));
  cairo_line_to (xr->cairo, xr_to_pt (x1), xr_to_pt (y1));
  cairo_line_to (xr->cairo, xr_to_pt (x0), xr_to_pt (y1));
}

static void
fill_rectangle (struct xr_fsm *xr, int x0, int y0, int x1, int y1)
{
  cairo_new_path (xr->cairo);
  cairo_set_line_width (xr->cairo, xr_to_pt (XR_LINE_WIDTH));
  cairo_rectangle (xr->cairo,
                   xr_to_pt (x0), xr_to_pt (y0),
                   xr_to_pt (x1 - x0), xr_to_pt (y1 - y0));
  cairo_fill (xr->cairo);
}

/* Draws a horizontal line X0...X2 at Y if LEFT says so,
   shortening it to X0...X1 if SHORTEN is true.
   Draws a horizontal line X1...X3 at Y if RIGHT says so,
   shortening it to X2...X3 if SHORTEN is true. */
static void
xr_draw_horz_line (struct xr_fsm *xr, int x0, int x1, int x2, int x3, int y,
                   enum render_line_style left, enum render_line_style right,
                   const struct cell_color *left_color,
                   const struct cell_color *right_color,
                   bool shorten)
{
  if (left != RENDER_LINE_NONE && right != RENDER_LINE_NONE && !shorten
      && cell_color_equal (left_color, right_color))
    xr_draw_line (xr, x0, y, x3, y, left, left_color);
  else
    {
      if (left != RENDER_LINE_NONE)
        xr_draw_line (xr, x0, y, shorten ? x1 : x2, y, left, left_color);
      if (right != RENDER_LINE_NONE)
        xr_draw_line (xr, shorten ? x2 : x1, y, x3, y, right, right_color);
    }
}

/* Draws a vertical line Y0...Y2 at X if TOP says so,
   shortening it to Y0...Y1 if SHORTEN is true.
   Draws a vertical line Y1...Y3 at X if BOTTOM says so,
   shortening it to Y2...Y3 if SHORTEN is true. */
static void
xr_draw_vert_line (struct xr_fsm *xr, int y0, int y1, int y2, int y3, int x,
                   enum render_line_style top, enum render_line_style bottom,
                   const struct cell_color *top_color,
                   const struct cell_color *bottom_color,
                   bool shorten)
{
  if (top != RENDER_LINE_NONE && bottom != RENDER_LINE_NONE && !shorten
      && cell_color_equal (top_color, bottom_color))
    xr_draw_line (xr, x, y0, x, y3, top, top_color);
  else
    {
      if (top != RENDER_LINE_NONE)
        xr_draw_line (xr, x, y0, x, shorten ? y1 : y2, top, top_color);
      if (bottom != RENDER_LINE_NONE)
        xr_draw_line (xr, x, shorten ? y2 : y1, x, y3, bottom, bottom_color);
    }
}

static void
xrr_draw_line (void *xr_, int bb[TABLE_N_AXES][2],
               enum render_line_style styles[TABLE_N_AXES][2],
               struct cell_color colors[TABLE_N_AXES][2])
{
  const int x0 = bb[H][0];
  const int y0 = bb[V][0];
  const int x3 = bb[H][1];
  const int y3 = bb[V][1];
  const int top = styles[H][0];
  const int bottom = styles[H][1];

  int start_side = render_direction_rtl();
  int end_side = !start_side;
  const int start_of_line = styles[V][start_side];
  const int end_of_line   = styles[V][end_side];
  const struct cell_color *top_color = &colors[H][0];
  const struct cell_color *bottom_color = &colors[H][1];
  const struct cell_color *start_color = &colors[V][start_side];
  const struct cell_color *end_color = &colors[V][end_side];

  /* The algorithm here is somewhat subtle, to allow it to handle
     all the kinds of intersections that we need.

     Three additional ordinates are assigned along the x axis.  The
     first is xc, midway between x0 and x3.  The others are x1 and
     x2; for a single vertical line these are equal to xc, and for
     a double vertical line they are the ordinates of the left and
     right half of the double line.

     yc, y1, and y2 are assigned similarly along the y axis.

     The following diagram shows the coordinate system and output
     for double top and bottom lines, single left line, and no
     right line:

                 x0       x1 xc  x2      x3
               y0 ________________________
                  |        #     #       |
                  |        #     #       |
                  |        #     #       |
                  |        #     #       |
                  |        #     #       |
     y1 = y2 = yc |#########     #       |
                  |        #     #       |
                  |        #     #       |
                  |        #     #       |
                  |        #     #       |
               y3 |________#_____#_______|
  */
  struct xr_fsm *xr = xr_;

  /* Offset from center of each line in a pair of double lines. */
  int double_line_ofs = (XR_LINE_SPACE + XR_LINE_WIDTH) / 2;

  /* Are the lines along each axis single or double?
     (It doesn't make sense to have different kinds of line on the
     same axis, so we don't try to gracefully handle that case.) */
  bool double_vert = top == RENDER_LINE_DOUBLE || bottom == RENDER_LINE_DOUBLE;
  bool double_horz = start_of_line == RENDER_LINE_DOUBLE || end_of_line == RENDER_LINE_DOUBLE;

  /* When horizontal lines are doubled,
     the left-side line along y1 normally runs from x0 to x2,
     and the right-side line along y1 from x3 to x1.
     If the top-side line is also doubled, we shorten the y1 lines,
     so that the left-side line runs only to x1,
     and the right-side line only to x2.
     Otherwise, the horizontal line at y = y1 below would cut off
     the intersection, which looks ugly:
               x0       x1     x2      x3
             y0 ________________________
                |        #     #       |
                |        #     #       |
                |        #     #       |
                |        #     #       |
             y1 |#########     ########|
                |                      |
                |                      |
             y2 |######################|
                |                      |
                |                      |
             y3 |______________________|
     It is more of a judgment call when the horizontal line is
     single.  We actually choose to cut off the line anyhow, as
     shown in the first diagram above.
  */
  bool shorten_y1_lines = top == RENDER_LINE_DOUBLE;
  bool shorten_y2_lines = bottom == RENDER_LINE_DOUBLE;
  bool shorten_yc_line = shorten_y1_lines && shorten_y2_lines;
  int horz_line_ofs = double_vert ? double_line_ofs : 0;
  int xc = (x0 + x3) / 2;
  int x1 = xc - horz_line_ofs;
  int x2 = xc + horz_line_ofs;

  bool shorten_x1_lines = start_of_line == RENDER_LINE_DOUBLE;
  bool shorten_x2_lines = end_of_line == RENDER_LINE_DOUBLE;
  bool shorten_xc_line = shorten_x1_lines && shorten_x2_lines;
  int vert_line_ofs = double_horz ? double_line_ofs : 0;
  int yc = (y0 + y3) / 2;
  int y1 = yc - vert_line_ofs;
  int y2 = yc + vert_line_ofs;

  if (!double_horz)
    xr_draw_horz_line (xr, x0, x1, x2, x3, yc, start_of_line, end_of_line,
                       start_color, end_color, shorten_yc_line);
  else
    {
      xr_draw_horz_line (xr, x0, x1, x2, x3, y1, start_of_line, end_of_line,
                         start_color, end_color, shorten_y1_lines);
      xr_draw_horz_line (xr, x0, x1, x2, x3, y2, start_of_line, end_of_line,
                         start_color, end_color, shorten_y2_lines);
    }

  if (!double_vert)
    xr_draw_vert_line (xr, y0, y1, y2, y3, xc, top, bottom,
                       top_color, bottom_color, shorten_xc_line);
  else
    {
      xr_draw_vert_line (xr, y0, y1, y2, y3, x1, top, bottom,
                         top_color, bottom_color, shorten_x1_lines);
      xr_draw_vert_line (xr, y0, y1, y2, y3, x2, top, bottom,
                         top_color, bottom_color, shorten_x2_lines);
    }
}

static void
xrr_measure_cell_width (void *xr_, const struct table_cell *cell,
                        int *min_width, int *max_width)
{
  struct xr_fsm *xr = xr_;
  int bb[TABLE_N_AXES][2];
  int clip[TABLE_N_AXES][2];
  int h;

  bb[H][0] = 0;
  bb[H][1] = INT_MAX;
  bb[V][0] = 0;
  bb[V][1] = INT_MAX;
  clip[H][0] = clip[H][1] = clip[V][0] = clip[V][1] = 0;
  xr_layout_cell (xr, cell, bb, clip, max_width, &h, NULL);

  bb[H][1] = 1;
  xr_layout_cell (xr, cell, bb, clip, min_width, &h, NULL);

  const int (*margin)[2] = cell->cell_style->margin;
  if (*min_width > 0)
    *min_width += px_to_xr (margin[H][0] + margin[H][1]);
  if (*max_width > 0)
    *max_width += px_to_xr (margin[H][0] + margin[H][1]);
}

static int
xrr_measure_cell_height (void *xr_, const struct table_cell *cell, int width)
{
  struct xr_fsm *xr = xr_;

  const int (*margin)[2] = cell->cell_style->margin;

  int bb[TABLE_N_AXES][2] = {
    [H][0] = 0,
    [H][1] = width - px_to_xr (margin[H][0] + margin[H][1]),
    [V][0] = 0,
    [V][1] = INT_MAX,
  };

  int clip[TABLE_N_AXES][2] = { { 0, 0 }, { 0, 0 } };

  int w, h;
  xr_layout_cell (xr, cell, bb, clip, &w, &h, NULL);
  h += px_to_xr (margin[V][0] + margin[V][1]);
  return h;
}

static void xr_clip (struct xr_fsm *, int clip[TABLE_N_AXES][2]);

static void
xrr_draw_cell (void *xr_, const struct table_cell *cell, int color_idx,
               int bb[TABLE_N_AXES][2], int valign_offset,
               int spill[TABLE_N_AXES][2],
               int clip[TABLE_N_AXES][2])
{
  struct xr_fsm *xr = xr_;
  int w, h, brk;

  const struct cell_color *bg = &cell->font_style->bg[color_idx];
  if ((bg->r != 255 || bg->g != 255 || bg->b != 255) && bg->alpha)
    {
      cairo_save (xr->cairo);
      int bg_clip[TABLE_N_AXES][2];
      for (int axis = 0; axis < TABLE_N_AXES; axis++)
	{
	  bg_clip[axis][0] = clip[axis][0];
	  if (bb[axis][0] == clip[axis][0])
	    bg_clip[axis][0] -= spill[axis][0];

	  bg_clip[axis][1] = clip[axis][1];
	  if (bb[axis][1] == clip[axis][1])
	    bg_clip[axis][1] += spill[axis][1];
	}
      xr_clip (xr, bg_clip);
      xr_set_source_rgba (xr->cairo, bg);
      fill_rectangle (xr,
		      bb[H][0] - spill[H][0],
		      bb[V][0] - spill[V][0],
		      bb[H][1] + spill[H][1],
		      bb[V][1] + spill[V][1]);
      cairo_restore (xr->cairo);
    }
  cairo_save (xr->cairo);
  if (!xr->style->use_system_colors)
    xr_set_source_rgba (xr->cairo, &cell->font_style->fg[color_idx]);

  bb[V][0] += valign_offset;

  for (int axis = 0; axis < TABLE_N_AXES; axis++)
    {
      bb[axis][0] += px_to_xr (cell->cell_style->margin[axis][0]);
      bb[axis][1] -= px_to_xr (cell->cell_style->margin[axis][1]);
    }
  if (bb[H][0] < bb[H][1] && bb[V][0] < bb[V][1])
    xr_layout_cell (xr, cell, bb, clip, &w, &h, &brk);
  cairo_restore (xr->cairo);
}

static int
xrr_adjust_break (void *xr_, const struct table_cell *cell,
                  int width, int height)
{
  struct xr_fsm *xr = xr_;

  if (xrr_measure_cell_height (xr_, cell, width) < height)
    return -1;

  const int (*margin)[2] = cell->cell_style->margin;

  int bb[TABLE_N_AXES][2] = {
    [H][0] = 0,
    [V][0] = 0,
    [H][1] = width - px_to_xr (margin[H][0] + margin[H][1]),
    [V][1] = height - px_to_xr (margin[V][0] + margin[V][1]),
  };
  if (bb[H][1] <= 0)
    return 0;

  int clip[TABLE_N_AXES][2] = { { 0, 0 }, { 0, 0 } };

  int w, h, brk;
  xr_layout_cell (xr, cell, bb, clip, &w, &h, &brk);
  return brk;
}

static void
xrr_scale (void *xr_, double scale)
{
  struct xr_fsm *xr = xr_;
  cairo_scale (xr->cairo, scale, scale);
}

static void
xr_clip (struct xr_fsm *xr, int clip[TABLE_N_AXES][2])
{
  if (clip[H][1] != INT_MAX || clip[V][1] != INT_MAX)
    {
      double x0 = xr_to_pt (clip[H][0]);
      double y0 = xr_to_pt (clip[V][0]);
      double x1 = xr_to_pt (clip[H][1]);
      double y1 = xr_to_pt (clip[V][1]);

      cairo_rectangle (xr->cairo, x0, y0, x1 - x0, y1 - y0);
      cairo_clip (xr->cairo);
    }
}

static void
add_attr (PangoAttrList *list, PangoAttribute *attr,
          guint start_index, guint end_index)
{
  attr->start_index = start_index;
  attr->end_index = end_index;
  pango_attr_list_insert (list, attr);
}

static void
markup_escape (struct string *out, bool markup, const char *in, size_t len)
{
  if (!markup)
    {
      ds_put_substring (out, ss_buffer (in, len == -1 ? strlen (in) : len));
      return;
    }

  while (len-- > 0)
    {
      int c = *in++;
      switch (c)
        {
        case 0:
          return;
        case '&':
          ds_put_cstr (out, "&amp;");
          break;
        case '<':
          ds_put_cstr (out, "&lt;");
          break;
        case '>':
          ds_put_cstr (out, "&gt;");
          break;
        default:
          ds_put_byte (out, c);
          break;
        }
    }
}

static int
get_layout_dimension (PangoLayout *layout, enum table_axis axis)
{
  int size[TABLE_N_AXES];
  pango_layout_get_size (layout, &size[H], &size[V]);
  return size[axis];
}

static PangoFontDescription *
parse_font (const char *font, int default_size, bool bold, bool italic)
{
  if (!c_strcasecmp (font, "Monospaced"))
    font = "Monospace";

  PangoFontDescription *desc = pango_font_description_from_string (font);
  if (desc == NULL)
    return NULL;

  /* If the font description didn't include an explicit font size, then set it
     to DEFAULT_SIZE, which is in inch/72000 units. */
  if (!(pango_font_description_get_set_fields (desc) & PANGO_FONT_MASK_SIZE))
    pango_font_description_set_size (desc,
                                     (default_size / 1000.0) * PANGO_SCALE);

  pango_font_description_set_weight (desc, (bold
                                            ? PANGO_WEIGHT_BOLD
                                            : PANGO_WEIGHT_NORMAL));
  pango_font_description_set_style (desc, (italic
                                           ? PANGO_STYLE_ITALIC
                                           : PANGO_STYLE_NORMAL));

  return desc;
}

static int
xr_layout_cell_text (struct xr_fsm *xr, const struct table_cell *cell,
                     int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2],
                     int *widthp, int *brk)
{
  const struct pivot_table *pt = xr->item->table;
  const struct font_style *font_style = cell->font_style;
  const struct cell_style *cell_style = cell->cell_style;
  unsigned int options = cell->options;

  enum table_axis X = options & TAB_ROTATE ? V : H;
  enum table_axis Y = !X;
  int R = options & TAB_ROTATE ? 0 : 1;

  PangoFontDescription *desc = NULL;
  if (font_style->typeface)
      desc = parse_font (
        font_style->typeface,
        font_style->size ? font_style->size * 1000 : 10000,
        font_style->bold, font_style->italic);
  if (!desc)
    desc = xr->style->font;

  assert (xr->cairo);
  PangoContext *context = pango_cairo_create_context (xr->cairo);
  pango_cairo_context_set_resolution (context, xr->style->font_resolution);
  PangoLayout *layout = pango_layout_new (context);
  g_object_unref (context);

  pango_layout_set_font_description (layout, desc);

  struct string body = DS_EMPTY_INITIALIZER;
  bool numeric = pivot_value_format_body (cell->value, pt, &body);

  enum table_halign halign = table_halign_interpret (
    cell->cell_style->halign, numeric);

  if (cell_style->halign == TABLE_HALIGN_DECIMAL
      && !(cell->options & TAB_ROTATE))
    {
      int margin_adjustment = -px_to_xr (cell_style->decimal_offset);

      const char *decimal = strrchr (ds_cstr (&body),
                                     cell_style->decimal_char);
      if (decimal)
        {
          pango_layout_set_text (layout, decimal, strlen (decimal));
          pango_layout_set_width (layout, -1);
          margin_adjustment += get_layout_dimension (layout, H);
        }

      if (margin_adjustment < 0)
        bb[H][1] += margin_adjustment;
    }

  PangoAttrList *attrs = NULL;

  /* Deal with an oddity of the Unicode line-breaking algorithm (or perhaps in
     Pango's implementation of it): it will break after a period or a comma
     that precedes a digit, e.g. in ".000" it will break after the period.
     This code looks for such a situation and inserts a U+2060 WORD JOINER
     to prevent the break.

     This isn't necessary when the decimal point is between two digits
     (e.g. "0.000" won't be broken) or when the display width is not limited so
     that word wrapping won't happen.

     It isn't necessary to look for more than one period or comma, as would
     happen with grouping like 1,234,567.89 or 1.234.567,89 because if groups
     are present then there will always be a digit on both sides of every
     period and comma. */
  bool markup = cell->font_style->markup;
  if (markup)
    {
      PangoAttrList *new_attrs;
      char *new_text;
      if (pango_parse_markup (ds_cstr (&body), -1, 0,
                              &new_attrs, &new_text, NULL, NULL))
        {
          attrs = new_attrs;
          ds_destroy (&body);
          body.ss = ss_cstr (new_text);
          body.capacity = body.ss.length;
        }
      else
        {
          /* XXX should we report the error? */
        }
    }
  else if (options & TAB_ROTATE || bb[H][1] != INT_MAX)
    {
      const char *text = ds_cstr (&body);
      const char *decimal = text + strcspn (text, ".,");
      if (decimal[0]
          && c_isdigit (decimal[1])
          && (decimal == text || !c_isdigit (decimal[-1])))
        {
          struct string tmp = DS_EMPTY_INITIALIZER;
          ds_extend (&tmp, ds_length (&body) + 16);
          markup_escape (&tmp, markup, text, decimal - text + 1);
          ds_put_unichar (&tmp, 0x2060 /* U+2060 WORD JOINER */);
          markup_escape (&tmp, markup, decimal + 1, -1);
          ds_swap (&tmp, &body);
          ds_destroy (&tmp);
        }
    }

  if (font_style->underline)
    {
      if (!attrs)
        attrs = pango_attr_list_new ();
      pango_attr_list_insert (attrs, pango_attr_underline_new (
                                PANGO_UNDERLINE_SINGLE));
    }

  const struct pivot_value *value = cell->value;
  if (value->n_footnotes || value->n_subscripts)
    {
      size_t subscript_ofs = ds_length (&body);
      for (size_t i = 0; i < value->n_subscripts; i++)
        {
          if (i)
            ds_put_byte (&body, ',');
          ds_put_cstr (&body, value->subscripts[i]);
        }

      size_t footnote_ofs = ds_length (&body);
      size_t n_footnotes = 0;
      for (size_t i = 0; i < value->n_footnotes; i++)
        {
          const struct pivot_footnote *f
            = pt->footnotes[value->footnote_indexes[i]];
          if (f->show)
            {
              if (n_footnotes++)
                ds_put_byte (&body, ',');
              pivot_footnote_format_marker (f, pt, &body);
            }
        }

      /* Allow footnote markers to occupy the right margin.  That way, numbers
         in the column are still aligned. */
      if (value->n_footnotes && halign == TABLE_HALIGN_RIGHT)
        {
          /* Measure the width of the footnote marker, so we know how much we
             need to make room for. */
          pango_layout_set_text (layout, ds_cstr (&body) + footnote_ofs,
                                 ds_length (&body) - footnote_ofs);

          PangoAttrList *fn_attrs = pango_attr_list_new ();
          pango_attr_list_insert (
            fn_attrs, pango_attr_scale_new (PANGO_SCALE_SMALL));
          pango_attr_list_insert (fn_attrs, pango_attr_rise_new (3000));
          pango_layout_set_attributes (layout, fn_attrs);
          pango_attr_list_unref (fn_attrs);
          int footnote_width = get_layout_dimension (layout, X);

          /* Bound the adjustment by the width of the right margin. */
          int right_margin = px_to_xr (cell_style->margin[X][R]);
          int footnote_adjustment = MIN (footnote_width, right_margin);

          /* Adjust the bounding box. */
          if (options & TAB_ROTATE)
            footnote_adjustment = -footnote_adjustment;
          bb[X][R] += footnote_adjustment;

          /* Clean up. */
          pango_layout_set_attributes (layout, NULL);
        }

      /* Set attributes. */
      if (!attrs)
        attrs = pango_attr_list_new ();
      add_attr (attrs, pango_attr_font_desc_new (desc), subscript_ofs,
                PANGO_ATTR_INDEX_TO_TEXT_END);
      add_attr (attrs, pango_attr_scale_new (PANGO_SCALE_SMALL),
                subscript_ofs, PANGO_ATTR_INDEX_TO_TEXT_END);
      if (value->n_subscripts)
        add_attr (attrs, pango_attr_rise_new (-3000), subscript_ofs,
                  footnote_ofs - subscript_ofs);
      if (value->n_footnotes)
        {
          bool superscript = pt->look->footnote_marker_superscripts;
          add_attr (attrs, pango_attr_rise_new (superscript ? 3000 : -3000),
                    footnote_ofs, PANGO_ATTR_INDEX_TO_TEXT_END);
        }
    }

  /* Set the attributes, if any. */
  if (attrs)
    {
      pango_layout_set_attributes (layout, attrs);
      pango_attr_list_unref (attrs);
    }

  /* Set the text. */
  pango_layout_set_text (layout, ds_cstr (&body), ds_length (&body));

  pango_layout_set_alignment (layout,
                              (halign == TABLE_HALIGN_RIGHT ? PANGO_ALIGN_RIGHT
                               : halign == TABLE_HALIGN_LEFT ? PANGO_ALIGN_LEFT
                               : PANGO_ALIGN_CENTER));
  pango_layout_set_width (
    layout,
    bb[X][1] == INT_MAX ? -1 : xr_to_pango (bb[X][1] - bb[X][0]));
  pango_layout_set_wrap (layout, PANGO_WRAP_WORD);

  int size[TABLE_N_AXES];
  pango_layout_get_size (layout, &size[H], &size[V]);

  if (clip[H][0] != clip[H][1])
    {
      cairo_save (xr->cairo);
      if (!(options & TAB_ROTATE))
        xr_clip (xr, clip);
      if (options & TAB_ROTATE)
        {
          int extra = bb[H][1] - bb[H][0] - size[V];
          int halign_offset = extra > 0 ? extra / 2 : 0;
          cairo_translate (xr->cairo,
                           xr_to_pt (bb[H][0] + halign_offset),
                           xr_to_pt (bb[V][1]));
          cairo_rotate (xr->cairo, -M_PI_2);
        }
      else
        cairo_translate (xr->cairo,
                         xr_to_pt (bb[H][0]),
                         xr_to_pt (bb[V][0]));
      pango_cairo_show_layout (xr->cairo, layout);

      /* If enabled, this draws a blue rectangle around the extents of each
         line of text, which can be rather useful for debugging layout
         issues. */
      if (0)
        {
          PangoLayoutIter *iter;
          iter = pango_layout_get_iter (layout);
          do
            {
              PangoRectangle extents;

              pango_layout_iter_get_line_extents (iter, &extents, NULL);
              cairo_save (xr->cairo);
              cairo_set_source_rgb (xr->cairo, 1, 0, 0);
              xr_draw_rectangle (
                xr,
                pango_to_xr (extents.x),
                pango_to_xr (extents.y),
                pango_to_xr (extents.x + extents.width),
                pango_to_xr (extents.y + extents.height));
              cairo_restore (xr->cairo);
            }
          while (pango_layout_iter_next_line (iter));
          pango_layout_iter_free (iter);
        }

      cairo_restore (xr->cairo);
    }

  int w = pango_to_xr (size[X]);
  int h = pango_to_xr (size[Y]);
  if (w > *widthp)
    *widthp = w;
  if (bb[V][0] + h >= bb[V][1] && !(options & TAB_ROTATE))
    {
      PangoLayoutIter *iter;
      int best = 0;

      /* Choose a breakpoint between lines instead of in the middle of one. */
      iter = pango_layout_get_iter (layout);
      do
        {
          PangoRectangle extents;
          int y0, y1;
          int bottom;

          pango_layout_iter_get_line_extents (iter, NULL, &extents);
          pango_layout_iter_get_line_yrange (iter, &y0, &y1);
          extents.x = pango_to_xr (extents.x);
          extents.y = pango_to_xr (y0);
          extents.width = pango_to_xr (extents.width);
          extents.height = pango_to_xr (y1 - y0);
          bottom = bb[V][0] + extents.y + extents.height;
          if (bottom < bb[V][1])
            {
              if (brk && clip[H][0] != clip[H][1])
                best = bottom;
              if (brk)
                *brk = bottom;
            }
          else
            break;
        }
      while (pango_layout_iter_next_line (iter));
      pango_layout_iter_free (iter);

      /* If enabled, draws a green line across the chosen breakpoint, which can
         be useful for debugging issues with breaking.  */
      if (0)
        {
          if (best)
            xr_draw_line (xr, 0, best,
                          xr->style->size[H], best,
                          RENDER_LINE_SINGLE,
                          &(struct cell_color) CELL_COLOR (0, 255, 0));
        }
    }

  pango_layout_set_attributes (layout, NULL);

  if (desc != xr->style->font)
    pango_font_description_free (desc);
  g_object_unref (G_OBJECT (layout));
  ds_destroy (&body);

  return h;
}

static void
xr_layout_cell (struct xr_fsm *xr, const struct table_cell *cell,
                int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2],
                int *width, int *height, int *brk)
{
  *width = 0;
  *height = 0;

  /* If enabled, draws a blue rectangle around the cell extents, which can be
     useful for debugging layout. */
  if (0)
    {
      if (clip[H][0] != clip[H][1])
        {
          cairo_save (xr->cairo);
          cairo_set_source_rgb (xr->cairo, 0, 0, 1);
          xr_draw_rectangle (xr, bb[H][0], bb[V][0], bb[H][1], bb[V][1]);
          cairo_restore (xr->cairo);
        }
    }

  if (brk)
    *brk = bb[V][0];
  *height = xr_layout_cell_text (xr, cell, bb, clip, width, brk);
}

#define CHART_WIDTH 500
#define CHART_HEIGHT 375

static struct xr_fsm *
xr_fsm_create (const struct output_item *item_,
               const struct xr_fsm_style *style, cairo_t *cr,
               bool print)
{
  struct output_item *item;

  switch (item_->type)
    {
    case OUTPUT_ITEM_CHART:
    case OUTPUT_ITEM_IMAGE:
    case OUTPUT_ITEM_PAGE_BREAK:
    case OUTPUT_ITEM_TABLE:
      item = output_item_ref (item_);
      break;

    case OUTPUT_ITEM_GROUP:
    case OUTPUT_ITEM_PAGE_SETUP:
      return NULL;

    case OUTPUT_ITEM_MESSAGE:
      item = text_item_to_table_item (message_item_to_text_item (
                                        output_item_ref (item_)));
      break;

    case OUTPUT_ITEM_TEXT:
      if (item_->text.subtype == TEXT_ITEM_PAGE_TITLE)
        return NULL;

      item = text_item_to_table_item (output_item_ref (item_));
      break;

    default:
      NOT_REACHED ();
    }

  assert (item->type == OUTPUT_ITEM_TABLE
          || item->type == OUTPUT_ITEM_CHART
          || item->type == OUTPUT_ITEM_IMAGE
          || item->type == OUTPUT_ITEM_PAGE_BREAK);

  size_t *layer_indexes = NULL;
  if (item->type == OUTPUT_ITEM_TABLE)
    {
      layer_indexes = pivot_output_next_layer (item->table, NULL, print);
      if (!layer_indexes)
        return NULL;
    }

  static const struct render_ops xrr_render_ops = {
    .measure_cell_width = xrr_measure_cell_width,
    .measure_cell_height = xrr_measure_cell_height,
    .adjust_break = xrr_adjust_break,
    .draw_line = xrr_draw_line,
    .draw_cell = xrr_draw_cell,
    .scale = xrr_scale,
  };

  enum { LW = XR_LINE_WIDTH, LS = XR_LINE_SPACE };
  static const int xr_line_widths[RENDER_N_LINES] =
    {
      [RENDER_LINE_NONE] = 0,
      [RENDER_LINE_SINGLE] = LW,
      [RENDER_LINE_DASHED] = LW,
      [RENDER_LINE_THICK] = LW * 2,
      [RENDER_LINE_THIN] = LW / 2,
      [RENDER_LINE_DOUBLE] = 2 * LW + LS,
    };

  struct xr_fsm *fsm = xmalloc (sizeof *fsm);
  *fsm = (struct xr_fsm) {
    .style = xr_fsm_style_ref (style),
    .item = item,
    .print = print,
    .layer_indexes = layer_indexes,
    .rp = {
      .ops = &xrr_render_ops,
      .aux = fsm,
      .size = { [H] = style->size[H], [V] = style->size[V] },
      .line_widths = xr_line_widths,
      .min_break = { [H] = style->min_break[H], [V] = style->min_break[V] },
      .supports_margins = true,
      .rtl = render_direction_rtl (),
      .printing = print,
    }
  };

  /* Get font size. */
  PangoContext *context = pango_cairo_create_context (cr);
  pango_cairo_context_set_resolution (context, style->font_resolution);
  PangoLayout *layout = pango_layout_new (context);
  g_object_unref (context);

  pango_layout_set_font_description (layout, style->font);

  pango_layout_set_text (layout, "0", 1);

  int char_size[TABLE_N_AXES];
  pango_layout_get_size (layout, &char_size[H], &char_size[V]);
  for (int a = 0; a < TABLE_N_AXES; a++)
    {
      int csa = pango_to_xr (char_size[a]);
      fsm->rp.font_size[a] = MAX (fsm->rp.font_size[a], csa);
    }

  g_object_unref (G_OBJECT (layout));

  if (item->type == OUTPUT_ITEM_TABLE)
    {
      fsm->cairo = cr;
      fsm->p = render_pager_create (&fsm->rp, item->table, fsm->layer_indexes);
      fsm->cairo = NULL;
    }

  return fsm;
}

struct xr_fsm *
xr_fsm_create_for_printing (const struct output_item *item,
                            const struct xr_fsm_style *style, cairo_t *cr)
{
  return xr_fsm_create (item, style, cr, true);
}

void
xr_fsm_destroy (struct xr_fsm *fsm)
{
  if (fsm)
    {
      xr_fsm_style_unref (fsm->style);
      output_item_unref (fsm->item);
      free (fsm->layer_indexes);
      render_pager_destroy (fsm->p);
      assert (!fsm->cairo);
      free (fsm);
    }
}

/* Scrolling API. */

struct xr_fsm *
xr_fsm_create_for_scrolling (const struct output_item *item,
                             const struct xr_fsm_style *style, cairo_t *cr)
{
  return xr_fsm_create (item, style, cr, false);
}

void
xr_fsm_measure (struct xr_fsm *fsm, cairo_t *cr, int *wp, int *hp)
{
  assert (!fsm->print);

  int w, h;

  switch (fsm->item->type)
    {
    case OUTPUT_ITEM_CHART:
      w = CHART_WIDTH;
      h = CHART_HEIGHT;
      break;

    case OUTPUT_ITEM_IMAGE:
      w = cairo_image_surface_get_width (fsm->item->image);
      h = cairo_image_surface_get_height (fsm->item->image);
      break;

    case OUTPUT_ITEM_TABLE:
      fsm->cairo = cr;
      w = render_pager_get_size (fsm->p, H) / XR_POINT;
      h = render_pager_get_size (fsm->p, V) / XR_POINT;
      fsm->cairo = NULL;
      break;

    case OUTPUT_ITEM_GROUP:
    case OUTPUT_ITEM_MESSAGE:
    case OUTPUT_ITEM_PAGE_BREAK:
    case OUTPUT_ITEM_PAGE_SETUP:
    case OUTPUT_ITEM_TEXT:
    default:
      NOT_REACHED ();
    }

  if (wp)
    *wp = w;
  if (hp)
    *hp = h;
}

void
xr_fsm_draw_all (struct xr_fsm *fsm, cairo_t *cr)
{
  assert (!fsm->print);
  xr_fsm_draw_region (fsm, cr, 0, 0, INT_MAX, INT_MAX);
}

static int
mul_XR_POINT (int x)
{
  return (x >= INT_MAX / XR_POINT ? INT_MAX
          : x <= INT_MIN / XR_POINT ? INT_MIN
          : x * XR_POINT);
}

static void
draw_image (cairo_surface_t *image, cairo_t *cr)
{
  cairo_save (cr);
  cairo_set_source_surface (cr, image, 0, 0);
  cairo_rectangle (cr, 0, 0, cairo_image_surface_get_width (image),
                   cairo_image_surface_get_height (image));
  cairo_clip (cr);
  cairo_paint (cr);
  cairo_restore (cr);
}

void
xr_fsm_draw_region (struct xr_fsm *fsm, cairo_t *cr,
                    int x, int y, int w, int h)
{
  assert (!fsm->print);
  switch (fsm->item->type)
    {
    case OUTPUT_ITEM_CHART:
      xr_draw_chart (fsm->item->chart, cr, CHART_WIDTH, CHART_HEIGHT);
      break;

    case OUTPUT_ITEM_IMAGE:
      draw_image (fsm->item->image, cr);
      break;

    case OUTPUT_ITEM_TABLE:
      fsm->cairo = cr;
      render_pager_draw_region (fsm->p, mul_XR_POINT (x), mul_XR_POINT (y),
                                mul_XR_POINT (w), mul_XR_POINT (h));
      fsm->cairo = NULL;
      break;

    case OUTPUT_ITEM_GROUP:
    case OUTPUT_ITEM_MESSAGE:
    case OUTPUT_ITEM_PAGE_BREAK:
    case OUTPUT_ITEM_PAGE_SETUP:
    case OUTPUT_ITEM_TEXT:
      NOT_REACHED ();
    }
}

/* Printing API. */

static int
xr_fsm_draw_table (struct xr_fsm *fsm, int space)
{
  int used = render_pager_draw_next (fsm->p, space);
  if (!render_pager_has_next (fsm->p))
    {
      render_pager_destroy (fsm->p);

      fsm->layer_indexes = pivot_output_next_layer (fsm->item->table,
                                                    fsm->layer_indexes, true);
      if (fsm->layer_indexes)
        {
          fsm->p = render_pager_create (&fsm->rp, fsm->item->table,
                                        fsm->layer_indexes);
          if (fsm->item->table->look->paginate_layers)
            used = space;
          else
            used += fsm->style->object_spacing;
        }
      else
        {
          fsm->p = NULL;
          fsm->done = true;
        }
    }
  return MIN (used, space);
}

static int
xr_fsm_draw_chart (struct xr_fsm *fsm, int space)
{
  const int chart_height = 0.8 * MIN (fsm->rp.size[H], fsm->rp.size[V]);
  if (space < chart_height)
    return 0;

  fsm->done = true;
  xr_draw_chart (fsm->item->chart, fsm->cairo,
                 xr_to_pt (fsm->rp.size[H]), xr_to_pt (chart_height));
  return chart_height;
}

static int
xr_fsm_draw_image (struct xr_fsm *fsm, int space)
{
  cairo_surface_t *image = fsm->item->image;
  int width = cairo_image_surface_get_width (image) * XR_POINT;
  int height = cairo_image_surface_get_height (image) * XR_POINT;
  if (!width || !height)
    goto error;

  if (height > fsm->rp.size[V])
    {
      double scale = fsm->rp.size[V] / (double) height;
      width *= scale;
      height *= scale;
      if (!width || !height)
        goto error;

      cairo_scale (fsm->cairo, scale, scale);
    }

  if (width > fsm->rp.size[H])
    {
      double scale = fsm->rp.size[H] / (double) width;
      width *= scale;
      height *= scale;
      if (!width || !height)
        goto error;

      cairo_scale (fsm->cairo, scale, scale);
    }

  if (space < height)
    return 0;

  draw_image (image, fsm->cairo);
  fsm->done = true;
  return height;

error:
  fsm->done = true;
  return 0;
}

static int
xr_fsm_draw_page_break (struct xr_fsm *fsm, int space)
{
  if (space >= fsm->rp.size[V])
    fsm->done = true;
  return 0;
}

int
xr_fsm_draw_slice (struct xr_fsm *fsm, cairo_t *cr, int space)
{
  assert (fsm->print);

  if (fsm->done || space <= 0)
    return 0;

  cairo_save (cr);
  fsm->cairo = cr;
  int used;
  switch (fsm->item->type)
    {
    case OUTPUT_ITEM_CHART:
      used = xr_fsm_draw_chart (fsm, space);
      break;

    case OUTPUT_ITEM_IMAGE:
      used = xr_fsm_draw_image (fsm, space);
      break;

    case OUTPUT_ITEM_PAGE_BREAK:
      used = xr_fsm_draw_page_break (fsm, space);
      break;

    case OUTPUT_ITEM_TABLE:
      used = xr_fsm_draw_table (fsm, space);
      break;

    case OUTPUT_ITEM_GROUP:
    case OUTPUT_ITEM_MESSAGE:
    case OUTPUT_ITEM_PAGE_SETUP:
    case OUTPUT_ITEM_TEXT:
    default:
      NOT_REACHED ();
    }
  fsm->cairo = NULL;
  cairo_restore (cr);

  return used;
}

bool
xr_fsm_is_empty (const struct xr_fsm *fsm)
{
  assert (fsm->print);

  return fsm->done;
}
