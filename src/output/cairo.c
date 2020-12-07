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

#include "output/cairo.h"

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/hash-functions.h"
#include "libpspp/message.h"
#include "libpspp/pool.h"
#include "libpspp/start-date.h"
#include "libpspp/str.h"
#include "libpspp/string-map.h"
#include "libpspp/version.h"
#include "data/file-handle-def.h"
#include "output/cairo-chart.h"
#include "output/cairo-fsm.h"
#include "output/chart-item-provider.h"
#include "output/driver-provider.h"
#include "output/group-item.h"
#include "output/message-item.h"
#include "output/options.h"
#include "output/page-eject-item.h"
#include "output/page-setup-item.h"
#include "output/render.h"
#include "output/table-item.h"
#include "output/table.h"
#include "output/text-item.h"

#include <cairo/cairo-pdf.h>
#include <cairo/cairo-ps.h>
#include <cairo/cairo-svg.h>

#include <cairo/cairo.h>
#include <inttypes.h>
#include <math.h>
#include <pango/pango-font.h>
#include <pango/pango-layout.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <stdlib.h>

#include "gl/c-ctype.h"
#include "gl/c-strcase.h"
#include "gl/intprops.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* This file uses TABLE_HORZ and TABLE_VERT enough to warrant abbreviating. */
#define H TABLE_HORZ
#define V TABLE_VERT

/* The unit used for internal measurements is inch/(72 * XR_POINT).
   (Thus, XR_POINT units represent one point.) */
#define XR_POINT PANGO_SCALE

/* Conversions to and from points. */
static double
xr_to_pt (int x)
{
  return x / (double) XR_POINT;
}

/* Dimensions for drawing lines in tables. */
#define XR_LINE_WIDTH (XR_POINT / 2) /* Width of an ordinary line. */
#define XR_LINE_SPACE XR_POINT       /* Space between double lines. */

/* Output types. */
enum xr_output_type
  {
    XR_PDF,
    XR_PS,
    XR_SVG
  };

/* Cairo output driver. */
struct xr_driver
  {
    struct output_driver driver;

    /* User parameters. */
    PangoFontDescription *fonts[XR_N_FONTS];

    int width;                  /* Page width minus margins. */
    int length;                 /* Page length minus margins and header. */

    int left_margin;            /* Left margin in inch/(72 * XR_POINT). */
    int right_margin;           /* Right margin in inch/(72 * XR_POINT). */
    int top_margin;             /* Top margin in inch/(72 * XR_POINT). */
    int bottom_margin;          /* Bottom margin in inch/(72 * XR_POINT). */

    int min_break[TABLE_N_AXES]; /* Min cell size to break across pages. */
    int object_spacing;         /* Space between output objects. */

    struct cell_color bg;       /* Background color */
    struct cell_color fg;       /* Foreground color */
    bool transparent;           /* true -> do not render background */
    bool systemcolors;          /* true -> do not change colors     */

    int initial_page_number;

    struct page_heading headings[2]; /* Top and bottom headings. */
    int headings_height[2];

    /* Internal state. */
    struct xr_fsm_style *style;
    double font_scale;
    int char_width, char_height;
    cairo_t *cairo;
    cairo_surface_t *surface;
    int page_number;		/* Current page number. */
    int y;
    struct xr_fsm *fsm;
  };

static const struct output_driver_class cairo_driver_class;

static void xr_driver_destroy_fsm (struct xr_driver *);
static void xr_driver_run_fsm (struct xr_driver *);

/* Output driver basics. */

static struct xr_driver *
xr_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &cairo_driver_class);
  return UP_CAST (driver, struct xr_driver, driver);
}

static struct driver_option *
opt (struct output_driver *d, struct string_map *options, const char *key,
     const char *default_value)
{
  return driver_option_get (d, options, key, default_value);
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

static PangoFontDescription *
parse_font_option (struct output_driver *d, struct string_map *options,
                   const char *key, const char *default_value,
                   int default_size, bool bold, bool italic)
{
  char *string = parse_string (opt (d, options, key, default_value));
  PangoFontDescription *desc = parse_font (string, default_size, bold, italic);
  if (!desc)
    {
      msg (MW, _("`%s': bad font specification"), string);

      /* Fall back to DEFAULT_VALUE, which had better be a valid font
         description. */
      desc = parse_font (default_value, default_size, bold, italic);
      assert (desc != NULL);
    }
  free (string);

  return desc;
}

static void
apply_options (struct xr_driver *xr, struct string_map *o)
{
  struct output_driver *d = &xr->driver;

  /* In inch/72000 units used by parse_paper_size() and parse_dimension(). */
  int left_margin, right_margin;
  int top_margin, bottom_margin;
  int paper_width, paper_length;
  int font_size;
  int min_break[TABLE_N_AXES];

  /* Scale factor from inch/72000 to inch/(72 * XR_POINT). */
  const double scale = XR_POINT / 1000.;

  int i;

  for (i = 0; i < XR_N_FONTS; i++)
    if (xr->fonts[i] != NULL)
      pango_font_description_free (xr->fonts[i]);

  font_size = parse_int (opt (d, o, "font-size", "10000"), 1000, 1000000);
  xr->fonts[XR_FONT_FIXED] = parse_font_option
    (d, o, "fixed-font", "monospace", font_size, false, false);
  xr->fonts[XR_FONT_PROPORTIONAL] = parse_font_option (
    d, o, "prop-font", "sans serif", font_size, false, false);

  xr->fg = parse_color (opt (d, o, "foreground-color", "#000000000000"));
  xr->bg = parse_color (opt (d, o, "background-color", "#FFFFFFFFFFFF"));

  xr->transparent = parse_boolean (opt (d, o, "transparent", "false"));
  xr->systemcolors = parse_boolean (opt (d, o, "systemcolors", "false"));

  /* Get dimensions.  */
  parse_paper_size (opt (d, o, "paper-size", ""), &paper_width, &paper_length);
  left_margin = parse_dimension (opt (d, o, "left-margin", ".5in"));
  right_margin = parse_dimension (opt (d, o, "right-margin", ".5in"));
  top_margin = parse_dimension (opt (d, o, "top-margin", ".5in"));
  bottom_margin = parse_dimension (opt (d, o, "bottom-margin", ".5in"));

  min_break[H] = parse_dimension (opt (d, o, "min-hbreak", NULL)) * scale;
  min_break[V] = parse_dimension (opt (d, o, "min-vbreak", NULL)) * scale;

  int object_spacing = (parse_dimension (opt (d, o, "object-spacing", NULL))
                        * scale);

  /* Convert to inch/(XR_POINT * 72). */
  xr->left_margin = left_margin * scale;
  xr->right_margin = right_margin * scale;
  xr->top_margin = top_margin * scale;
  xr->bottom_margin = bottom_margin * scale;
  xr->width = (paper_width - left_margin - right_margin) * scale;
  xr->length = (paper_length - top_margin - bottom_margin) * scale;
  xr->min_break[H] = min_break[H] >= 0 ? min_break[H] : xr->width / 2;
  xr->min_break[V] = min_break[V] >= 0 ? min_break[V] : xr->length / 2;
  xr->object_spacing = object_spacing >= 0 ? object_spacing : XR_POINT * 12;

  /* There are no headings so headings_height can stay 0. */
}

static struct xr_driver *
xr_allocate (const char *name, int device_type, struct string_map *o,
             double font_scale)
{
  struct xr_driver *xr = xzalloc (sizeof *xr);
  struct output_driver *d = &xr->driver;

  output_driver_init (d, &cairo_driver_class, name, device_type);

  /* This is a nasty kluge for an issue that does not make sense.  On any
     surface other than a screen (e.g. for output to PDF or PS or SVG), the
     fonts are way too big by default.  A "9-point" font seems to appear about
     16 points tall.  We use a scale factor for these surfaces to help, but the
     underlying issue is a mystery. */
  xr->font_scale = font_scale;

  apply_options (xr, o);

  return xr;
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

static void
xr_measure_fonts (cairo_t *cairo, PangoFontDescription *fonts[XR_N_FONTS],
                  int *char_width, int *char_height)
{
  *char_width = 0;
  *char_height = 0;
  for (int i = 0; i < XR_N_FONTS; i++)
    {
      PangoLayout *layout = pango_cairo_create_layout (cairo);
      pango_layout_set_font_description (layout, fonts[i]);

      pango_layout_set_text (layout, "0", 1);

      int cw, ch;
      pango_layout_get_size (layout, &cw, &ch);
      *char_width = MAX (*char_width, pango_to_xr (cw));
      *char_height = MAX (*char_height, pango_to_xr (ch));

      g_object_unref (G_OBJECT (layout));
    }
}

static int
get_layout_height (PangoLayout *layout)
{
  int w, h;
  pango_layout_get_size (layout, &w, &h);
  return h;
}

static int
xr_render_page_heading (cairo_t *cairo, const PangoFontDescription *font,
                        const struct page_heading *ph, int page_number,
                        int width, bool draw, int base_y)
{
  PangoLayout *layout = pango_cairo_create_layout (cairo);
  pango_layout_set_font_description (layout, font);

  int y = 0;
  for (size_t i = 0; i < ph->n; i++)
    {
      const struct page_paragraph *pp = &ph->paragraphs[i];

      char *markup = output_driver_substitute_heading_vars (pp->markup,
                                                            page_number);
      pango_layout_set_markup (layout, markup, -1);
      free (markup);

      pango_layout_set_alignment (
        layout,
        (pp->halign == TABLE_HALIGN_LEFT ? PANGO_ALIGN_LEFT
         : pp->halign == TABLE_HALIGN_CENTER ? PANGO_ALIGN_CENTER
         : pp->halign == TABLE_HALIGN_MIXED ? PANGO_ALIGN_LEFT
         : PANGO_ALIGN_RIGHT));
      pango_layout_set_width (layout, xr_to_pango (width));
      if (draw)
        {
          cairo_save (cairo);
          cairo_translate (cairo, 0, xr_to_pt (y + base_y));
          pango_cairo_show_layout (cairo, layout);
          cairo_restore (cairo);
        }

      y += pango_to_xr (get_layout_height (layout));
    }

  g_object_unref (G_OBJECT (layout));

  return y;
}

static int
xr_measure_headings (cairo_surface_t *surface,
                     const PangoFontDescription *font,
                     const struct page_heading headings[2],
                     int width, int object_spacing, int height[2])
{
  cairo_t *cairo = cairo_create (surface);
  int total = 0;
  for (int i = 0; i < 2; i++)
    {
      int h = xr_render_page_heading (cairo, font, &headings[i], -1,
                                      width, false, 0);

      /* If the top heading is nonempty, add some space below it. */
      if (h && i == 0)
        h += object_spacing;

      if (height)
        height[i] = h;
      total += h;
    }
  cairo_destroy (cairo);
  return total;
}

static bool
xr_check_fonts (cairo_surface_t *surface,
                PangoFontDescription *fonts[XR_N_FONTS],
                int usable_width, int usable_length)
{
  cairo_t *cairo = cairo_create (surface);
  int char_width, char_height;
  xr_measure_fonts (cairo, fonts, &char_width, &char_height);
  cairo_destroy (cairo);

  bool ok = true;
  enum { MIN_WIDTH = 3, MIN_LENGTH = 3 };
  if (usable_width / char_width < MIN_WIDTH)
    {
      msg (ME, _("The defined page is not wide enough to hold at least %d "
                 "characters in the default font.  In fact, there's only "
                 "room for %d characters."),
           MIN_WIDTH, usable_width / char_width);
      ok = false;
    }
  if (usable_length / char_height < MIN_LENGTH)
    {
      msg (ME, _("The defined page is not long enough to hold at least %d "
                 "lines in the default font.  In fact, there's only "
                 "room for %d lines."),
           MIN_LENGTH, usable_length / char_height);
      ok = false;
    }
  return ok;
}

static void
xr_set_cairo (struct xr_driver *xr, cairo_t *cairo)
{
  xr->cairo = cairo;

  cairo_set_line_width (xr->cairo, xr_to_pt (XR_LINE_WIDTH));

  xr_measure_fonts (xr->cairo, xr->fonts, &xr->char_width, &xr->char_height);

  if (xr->style == NULL)
    {
      xr->style = xmalloc (sizeof *xr->style);
      *xr->style = (struct xr_fsm_style) {
        .ref_cnt = 1,
        .size = { [H] = xr->width, [V] = xr->length },
        .min_break = { [H] = xr->min_break[H], [V] = xr->min_break[V] },
        .use_system_colors = xr->systemcolors,
        .transparent = xr->transparent,
        .font_scale = xr->font_scale,
      };

      for (size_t i = 0; i < XR_N_FONTS; i++)
        xr->style->fonts[i] = pango_font_description_copy (xr->fonts[i]);
    }

  if (!xr->systemcolors)
    cairo_set_source_rgb (xr->cairo,
			  xr->fg.r / 255.0, xr->fg.g / 255.0, xr->fg.b / 255.0);
}

static struct output_driver *
xr_create (struct file_handle *fh, enum settings_output_devices device_type,
           struct string_map *o, enum xr_output_type file_type)
{
  const char *file_name = fh_get_file_name (fh);
  struct xr_driver *xr = xr_allocate (file_name, device_type, o, 72.0 / 128.0);
  double width_pt = xr_to_pt (xr->width + xr->left_margin + xr->right_margin);
  double length_pt = xr_to_pt (xr->length + xr->top_margin + xr->bottom_margin);
  if (file_type == XR_PDF)
    xr->surface = cairo_pdf_surface_create (file_name, width_pt, length_pt);
  else if (file_type == XR_PS)
    xr->surface = cairo_ps_surface_create (file_name, width_pt, length_pt);
  else if (file_type == XR_SVG)
    xr->surface = cairo_svg_surface_create (file_name, width_pt, length_pt);
  else
    NOT_REACHED ();

  cairo_status_t status = cairo_surface_status (xr->surface);
  if (status != CAIRO_STATUS_SUCCESS)
    {
      msg (ME, _("error opening output file `%s': %s"),
           file_name, cairo_status_to_string (status));
      goto error;
    }

  if (!xr_check_fonts (xr->surface, xr->fonts, xr->width, xr->length))
    goto error;

  fh_unref (fh);
  return &xr->driver;

 error:
  fh_unref (fh);
  output_driver_destroy (&xr->driver);
  return NULL;
}

static struct output_driver *
xr_pdf_create (struct  file_handle *fh, enum settings_output_devices device_type,
               struct string_map *o)
{
  return xr_create (fh, device_type, o, XR_PDF);
}

static struct output_driver *
xr_ps_create (struct  file_handle *fh, enum settings_output_devices device_type,
               struct string_map *o)
{
  return xr_create (fh, device_type, o, XR_PS);
}

static struct output_driver *
xr_svg_create (struct file_handle *fh, enum settings_output_devices device_type,
               struct string_map *o)
{
  return xr_create (fh, device_type, o, XR_SVG);
}

static void
xr_destroy (struct output_driver *driver)
{
  struct xr_driver *xr = xr_driver_cast (driver);
  size_t i;

  xr_driver_destroy_fsm (xr);

  if (xr->cairo != NULL)
    {
      cairo_surface_finish (xr->surface);
      cairo_status_t status = cairo_status (xr->cairo);
      if (status != CAIRO_STATUS_SUCCESS)
        fprintf (stderr,  _("error drawing output for %s driver: %s"),
                 output_driver_get_name (driver),
                 cairo_status_to_string (status));
      cairo_surface_destroy (xr->surface);

      cairo_destroy (xr->cairo);
    }

  for (i = 0; i < XR_N_FONTS; i++)
    if (xr->fonts[i] != NULL)
      pango_font_description_free (xr->fonts[i]);

  xr_fsm_style_unref (xr->style);
  free (xr);
}

static void
xr_flush (struct output_driver *driver)
{
  struct xr_driver *xr = xr_driver_cast (driver);

  cairo_surface_flush (cairo_get_target (xr->cairo));
}

static void
xr_update_page_setup (struct output_driver *driver,
                      const struct page_setup *ps)
{
  struct xr_driver *xr = xr_driver_cast (driver);

  xr->initial_page_number = ps->initial_page_number;
  xr->object_spacing = ps->object_spacing * 72 * XR_POINT;

  if (xr->cairo)
    return;

  int usable[TABLE_N_AXES];
  for (int i = 0; i < 2; i++)
    usable[i] = (ps->paper[i]
                 - (ps->margins[i][0] + ps->margins[i][1])) * 72 * XR_POINT;

  int headings_height[2];
  usable[V] -= xr_measure_headings (
    xr->surface, xr->fonts[XR_FONT_PROPORTIONAL], ps->headings,
    usable[H], xr->object_spacing, headings_height);

  enum table_axis h = ps->orientation == PAGE_LANDSCAPE;
  enum table_axis v = !h;
  if (!xr_check_fonts (xr->surface, xr->fonts, usable[h], usable[v]))
    return;

  for (int i = 0; i < 2; i++)
    {
      page_heading_uninit (&xr->headings[i]);
      page_heading_copy (&xr->headings[i], &ps->headings[i]);
      xr->headings_height[i] = headings_height[i];
    }
  xr->width = usable[h];
  xr->length = usable[v];
  xr->left_margin = ps->margins[h][0] * 72 * XR_POINT;
  xr->right_margin = ps->margins[h][1] * 72 * XR_POINT;
  xr->top_margin = ps->margins[v][0] * 72 * XR_POINT;
  xr->bottom_margin = ps->margins[v][1] * 72 * XR_POINT;
  cairo_pdf_surface_set_size (xr->surface,
                              ps->paper[h] * 72.0, ps->paper[v] * 72.0);
}

static void
xr_submit (struct output_driver *driver, const struct output_item *output_item)
{
  struct xr_driver *xr = xr_driver_cast (driver);

  if (is_page_setup_item (output_item))
    {
      xr_update_page_setup (driver,
                            to_page_setup_item (output_item)->page_setup);
      return;
    }

  if (!xr->cairo)
    {
      xr->page_number = xr->initial_page_number - 1;
      xr_set_cairo (xr, cairo_create (xr->surface));
      cairo_save (xr->cairo);
      xr_driver_next_page (xr, xr->cairo);
    }

  xr_driver_output_item (xr, output_item);
  while (xr_driver_need_new_page (xr))
    {
      cairo_restore (xr->cairo);
      cairo_show_page (xr->cairo);
      cairo_save (xr->cairo);
      xr_driver_next_page (xr, xr->cairo);
    }
}

/* Functions for rendering a series of output items to a series of Cairo
   contexts, with pagination.

   Used by PSPPIRE for printing, and by the basic Cairo output driver above as
   its underlying implementation.

   See the big comment in cairo.h for intended usage. */

/* Gives new page CAIRO to XR for output. */
void
xr_driver_next_page (struct xr_driver *xr, cairo_t *cairo)
{
  if (!xr->transparent)
    {
      cairo_save (cairo);
      cairo_set_source_rgb (cairo,
			    xr->bg.r / 255.0, xr->bg.g / 255.0, xr->bg.b / 255.0);
      cairo_rectangle (cairo, 0, 0, xr->width, xr->length);
      cairo_fill (cairo);
      cairo_restore (cairo);
    }
  cairo_translate (cairo,
                   xr_to_pt (xr->left_margin),
                   xr_to_pt (xr->top_margin + xr->headings_height[0]));

  xr->page_number++;
  xr->cairo = cairo;
  xr->y = 0;

  xr_render_page_heading (xr->cairo, xr->fonts[XR_FONT_PROPORTIONAL],
                          &xr->headings[0], xr->page_number, xr->width, true,
                          -xr->headings_height[0]);
  xr_render_page_heading (xr->cairo, xr->fonts[XR_FONT_PROPORTIONAL],
                          &xr->headings[1], xr->page_number, xr->width, true,
                          xr->length);

  xr_driver_run_fsm (xr);
}

/* Start rendering OUTPUT_ITEM to XR.  Only valid if XR is not in the middle of
   rendering a previous output item, that is, only if xr_driver_need_new_page()
   returns false. */
void
xr_driver_output_item (struct xr_driver *xr,
                       const struct output_item *output_item)
{
  assert (xr->fsm == NULL);
  xr->fsm = xr_fsm_create (output_item, xr->style, xr->cairo);
  xr_driver_run_fsm (xr);
}

/* Returns true if XR is in the middle of rendering an output item and needs a
   new page to be appended using xr_driver_next_page() to make progress,
   otherwise false. */
bool
xr_driver_need_new_page (const struct xr_driver *xr)
{
  return xr->fsm != NULL;
}

/* Returns true if the current page doesn't have any content yet. */
bool
xr_driver_is_page_blank (const struct xr_driver *xr)
{
  return xr->y == 0;
}

static void
xr_driver_destroy_fsm (struct xr_driver *xr)
{
  xr_fsm_destroy (xr->fsm);
  xr->fsm = NULL;
}

static void
xr_driver_run_fsm (struct xr_driver *xr)
{
  if (xr->fsm != NULL)
    {
      cairo_save (xr->cairo);
      cairo_translate (xr->cairo, 0, xr_to_pt (xr->y));
      int used = xr_fsm_draw_slice (xr->fsm, xr->cairo, xr->length - xr->y);
      xr->y += used;
      cairo_restore (xr->cairo);

      if (xr_fsm_is_empty (xr->fsm))
        xr_driver_destroy_fsm (xr);
    }
}

struct output_driver_factory pdf_driver_factory =
  { "pdf", "pspp.pdf", xr_pdf_create };
struct output_driver_factory ps_driver_factory =
  { "ps", "pspp.ps", xr_ps_create };
struct output_driver_factory svg_driver_factory =
  { "svg", "pspp.svg", xr_svg_create };

static const struct output_driver_class cairo_driver_class =
{
  "cairo",
  xr_destroy,
  xr_submit,
  xr_flush,
};

struct xr_driver *
xr_driver_create (cairo_t *cairo, struct string_map *options)
{
  struct xr_driver *xr = xr_allocate ("cairo", 0, options, 1.0);
  xr_set_cairo (xr, cairo);
  return xr;
}

/* Destroy XR, which should have been created with xr_driver_create().  Any
   cairo_t added to XR is not destroyed, because it is owned by the client. */
void
xr_driver_destroy (struct xr_driver *xr)
{
  if (xr != NULL)
    {
      xr->cairo = NULL;
      output_driver_destroy (&xr->driver);
    }
}
