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

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/string-map.h"
#include "data/file-handle-def.h"
#include "output/cairo-fsm.h"
#include "output/cairo-pager.h"
#include "output/driver-provider.h"
#include "output/options.h"
#include "output/output-item.h"
#include "output/table.h"

#include <cairo/cairo-pdf.h>
#include <cairo/cairo-ps.h>
#include <cairo/cairo-svg.h>

#include <cairo/cairo.h>
#include <inttypes.h>
#include <math.h>
#include <pango/pango-font.h>
#include <stdlib.h>

#include "gl/c-strcase.h"
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

/* Output types. */
enum xr_output_type
  {
    XR_PDF,
    XR_PS,
    XR_SVG,
    XR_PNG
  };

/* Cairo output driver. */
struct xr_driver
  {
    struct output_driver driver;

    enum xr_output_type output_type;
    struct xr_fsm_style *fsm_style;
    struct xr_page_style *page_style;
    struct xr_pager *pager;
    bool trim;

    /* This is the surface where we're currently drawing.  It is always
       nonnull.

       If 'trim' is true, this is a special Cairo "recording surface" that we
       are using to save output temporarily just to find out the bounding box,
       then later replay it into the destination surface.

       If 'trim' is false:

         - For output to a PDF or PostScript file, it is the same pointer as
           'dest_surface'.

         - For output to a PNG file, it is an image surface.

         - For output to an SVG file, it is a recording surface.
    */
    cairo_surface_t *drawing_surface;

    /* - For output to a PDF or PostScript file, this is the surface for the
         PDF or PostScript file where the output is ultimately going.

       - For output to a PNG file, this is NULL, because Cairo has very
         limited support for PNG.  Cairo can't open a PNG file for writing as
         a surface, it can only save an existing surface to a PNG file.

       - For output to a SVG file, this is NULL, because Cairo does not
         permit resizing the SVG page size after creating the file, whereas
         this driver needs to do that sometimes.  Also, SVG is not multi-page
         (according to https://wiki.inkscape.org/wiki/index.php/Multipage).
    */
    cairo_surface_t *dest_surface;

    /* Used only in file names, for PNG and SVG output where we can only write
       one page per file. */
    int page_number;
  };

static const struct output_driver_class cairo_driver_class;


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

static struct xr_driver *
xr_allocate (const char *name, int device_type,
             enum xr_output_type output_type, struct string_map *o)
{
  struct xr_driver *xr = xzalloc (sizeof *xr);
  struct output_driver *d = &xr->driver;

  output_driver_init (d, &cairo_driver_class, name, device_type);
  xr->output_type = output_type;

  /* Scale factor from inch/72000 to inch/(72 * XR_POINT). */
  const double scale = XR_POINT / 1000.;

  int paper[TABLE_N_AXES];
  parse_paper_size (opt (d, o, "paper-size", ""), &paper[H], &paper[V]);
  for (int a = 0; a < TABLE_N_AXES; a++)
    paper[a] *= scale;

  int margins[TABLE_N_AXES][2];
  margins[H][0] = parse_dimension (opt (d, o, "left-margin", ".5in")) * scale;
  margins[H][1] = parse_dimension (opt (d, o, "right-margin", ".5in")) * scale;
  margins[V][0] = parse_dimension (opt (d, o, "top-margin", ".5in")) * scale;
  margins[V][1] = parse_dimension (opt (d, o, "bottom-margin", ".5in")) * scale;

  int size[TABLE_N_AXES];
  for (int a = 0; a < TABLE_N_AXES; a++)
    size[a] = paper[a] - margins[a][0] - margins[a][1];

  int min_break[TABLE_N_AXES];
  min_break[H] = parse_dimension (opt (d, o, "min-hbreak", NULL)) * scale;
  min_break[V] = parse_dimension (opt (d, o, "min-vbreak", NULL)) * scale;
  for (int a = 0; a < TABLE_N_AXES; a++)
    if (min_break[a] <= 0)
      min_break[a] = size[a] / 2;

  int font_size = parse_int (opt (d, o, "font-size", "10000"), 1000, 1000000);
  PangoFontDescription *font = parse_font_option (
    d, o, "prop-font", "Sans Serif", font_size, false, false);

  struct cell_color fg = parse_color (opt (d, o, "foreground-color", "black"));

  bool systemcolors = parse_boolean (opt (d, o, "systemcolors", "false"));

  int object_spacing
    = parse_dimension (opt (d, o, "object-spacing", NULL)) * scale;
  if (object_spacing <= 0)
    object_spacing = XR_POINT * 12;

  const char *default_resolution = (output_type == XR_PNG ? "96" : "72");
  int font_resolution = parse_int (opt (d, o, "font-resolution",
                                        default_resolution), 10, 1000);

  xr->trim = parse_boolean (opt (d, o, "trim", "false"));

  /* Cairo 1.16.0 has a bug that causes crashes if outlines are enabled at the
     same time as trimming:
     https://lists.cairographics.org/archives/cairo/2020-December/029151.html
     For now, just disable the outline if trimming is enabled. */
  bool include_outline
    = (output_type == XR_PDF
       && parse_boolean (opt (d, o, "outline", xr->trim ? "false" : "true")));

  xr->page_style = xmalloc (sizeof *xr->page_style);
  *xr->page_style = (struct xr_page_style) {
    .ref_cnt = 1,

    .margins = {
      [H] = { margins[H][0], margins[H][1], },
      [V] = { margins[V][0], margins[V][1], },
    },

    .initial_page_number = 1,
    .include_outline = include_outline,
  };

  xr->fsm_style = xmalloc (sizeof *xr->fsm_style);
  *xr->fsm_style = (struct xr_fsm_style) {
    .ref_cnt = 1,
    .size = { [H] = size[H], [V] = size[V] },
    .min_break = { [H] = min_break[H], [V] = min_break[V] },
    .font = font,
    .fg = fg,
    .use_system_colors = systemcolors,
    .object_spacing = object_spacing,
    .font_resolution = font_resolution,
  };

  return xr;
}

static struct output_driver *
xr_create (struct file_handle *fh, enum settings_output_devices device_type,
           struct string_map *o, enum xr_output_type output_type)
{
  const char *file_name = fh_get_file_name (fh);
  struct xr_driver *xr = xr_allocate (file_name, device_type, output_type, o);

  double paper[TABLE_N_AXES];
  for (int a = 0; a < TABLE_N_AXES; a++)
    paper[a] = xr_to_pt (xr_page_style_paper_size (xr->page_style,
                                                   xr->fsm_style, a));

  xr->dest_surface
    = (output_type == XR_PDF
       ? cairo_pdf_surface_create (file_name, paper[H], paper[V])
       : output_type == XR_PS
       ? cairo_ps_surface_create (file_name, paper[H], paper[V])
       : NULL);
  if (xr->dest_surface)
    {
      cairo_status_t status = cairo_surface_status (xr->dest_surface);
      if (status != CAIRO_STATUS_SUCCESS)
        {
          msg (ME, _("error opening output file `%s': %s"),
               file_name, cairo_status_to_string (status));
          goto error;
        }
    }

  xr->drawing_surface
    = (xr->trim || output_type == XR_SVG
       ? cairo_recording_surface_create (CAIRO_CONTENT_COLOR_ALPHA,
                                         &(cairo_rectangle_t) {
                                           .width = paper[H],
                                           .height = paper[V] })
       : output_type == XR_PNG
       ? cairo_image_surface_create (CAIRO_FORMAT_ARGB32, paper[H], paper[V])
       : xr->dest_surface);

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

static struct output_driver *
xr_png_create (struct file_handle *fh, enum settings_output_devices device_type,
               struct string_map *o)
{
  return xr_create (fh, device_type, o, XR_PNG);
}

static void
xr_set_surface_size (cairo_surface_t *surface, enum xr_output_type output_type,
                     double width, double height)
{
  switch (output_type)
    {
    case XR_PDF:
      cairo_pdf_surface_set_size (surface, width, height);
      break;

    case XR_PS:
      cairo_ps_surface_set_size (surface, width, height);
      break;

    case XR_SVG:
    case XR_PNG:
      NOT_REACHED ();
    }
}

static void
xr_copy_surface (cairo_surface_t *dst, cairo_surface_t *src,
                 double x, double y)
{
  cairo_t *cr = cairo_create (dst);
  cairo_set_source_surface (cr, src, x, y);
  cairo_paint (cr);
  cairo_destroy (cr);
}

static void
clear_rectangle (cairo_surface_t *surface,
                 double x0, double y0, double x1, double y1)
{
  cairo_t *cr = cairo_create (surface);
  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_new_path (cr);
  cairo_rectangle (cr, x0, y0, x1 - x0, y1 - y0);
  cairo_fill (cr);
  cairo_destroy (cr);
}

static void
xr_report_error (cairo_status_t status, const char *file_name)
{
  if (status != CAIRO_STATUS_SUCCESS)
    fprintf (stderr,  "%s: %s\n", file_name, cairo_status_to_string (status));
}

static void
xr_finish_page (struct xr_driver *xr)
{
  xr_pager_finish_page (xr->pager);

  /* For 'trim' true:

    - If the destination is PDF or PostScript, set the dest surface size, copy
      ink extent, show_page.

    - If the destination is PNG, create image surface, copy ink extent,
      cairo_surface_write_to_png(), destroy image surface.

    - If the destination is SVG, create svg surface, copy ink extent, close.

    then destroy drawing_surface and make a new one.

    For 'trim' false:

    - If the destination is PDF or PostScript, show_page.

    - If the destination is PNG, cairo_surface_write_to_png(), destroy image
      surface, create new image surface.

    - If the destination is SVG, create svg surface, copy whole thing, close.

    */
  double paper[TABLE_N_AXES];
  for (int a = 0; a < TABLE_N_AXES; a++)
    paper[a] = xr_to_pt (xr_page_style_paper_size (
                           xr->page_style, xr->fsm_style, a));

  xr->page_number++;
  char *file_name = (xr->page_number > 1
                     ? xasprintf ("%s-%d", xr->driver.name, xr->page_number)
                     : xr->driver.name);

  if (xr->trim)
    {
      /* Get the bounding box for the drawing surface. */
      double ofs[TABLE_N_AXES], size[TABLE_N_AXES];
      cairo_recording_surface_ink_extents (xr->drawing_surface,
                                           &ofs[H], &ofs[V],
                                           &size[H], &size[V]);
      const int (*margins)[2] = xr->page_style->margins;
      for (int a = 0; a < TABLE_N_AXES; a++)
        {
          double scale = XR_POINT;
          size[a] += (margins[a][0] + margins[a][1]) / scale;
          ofs[a] = -ofs[a] + margins[a][0] / scale;
        }

      switch (xr->output_type)
        {
        case XR_PDF:
        case XR_PS:
          xr_set_surface_size (xr->dest_surface, xr->output_type,
                               size[H], size[V]);
          xr_copy_surface (xr->dest_surface, xr->drawing_surface,
                           ofs[H], ofs[V]);
          cairo_surface_show_page (xr->dest_surface);
          break;

        case XR_SVG:
          {
            cairo_surface_t *svg = cairo_svg_surface_create (
              file_name, size[H], size[V]);
            xr_copy_surface (svg, xr->drawing_surface, ofs[H], ofs[V]);
            xr_report_error (cairo_surface_status (svg), file_name);
            cairo_surface_destroy (svg);
          }
          break;

        case XR_PNG:
          {
            cairo_surface_t *png = cairo_image_surface_create (
              CAIRO_FORMAT_ARGB32, size[H], size[V]);
            clear_rectangle (png, 0, 0, size[H], size[V]);
            xr_copy_surface (png, xr->drawing_surface, ofs[H], ofs[V]);
            xr_report_error (cairo_surface_write_to_png (png, file_name),
                             file_name);
            cairo_surface_destroy (png);
          }
          break;
        }

      /* Destroy the recording surface and create a fresh one of the same
         size. */
      cairo_surface_destroy (xr->drawing_surface);
      xr->drawing_surface = cairo_recording_surface_create (
        CAIRO_CONTENT_COLOR_ALPHA,
        &(cairo_rectangle_t) { .width = paper[H], .height = paper[V] });
    }
  else
    {
      switch (xr->output_type)
        {
        case XR_PDF:
        case XR_PS:
          cairo_surface_show_page (xr->dest_surface);
          break;

        case XR_SVG:
          {
            cairo_surface_t *svg = cairo_svg_surface_create (
              file_name, paper[H], paper[V]);
            xr_copy_surface (svg, xr->drawing_surface, 0.0, 0.0);
            xr_report_error (cairo_surface_status (svg), file_name);
            cairo_surface_destroy (svg);
          }
          break;

        case XR_PNG:
          xr_report_error (cairo_surface_write_to_png (xr->drawing_surface,
                                                       file_name), file_name);
          cairo_surface_destroy (xr->drawing_surface);
          xr->drawing_surface = cairo_image_surface_create (
            CAIRO_FORMAT_ARGB32, paper[H], paper[V]);
          break;
        }
    }

  if (file_name != xr->driver.name)
    free (file_name);
}

static void
xr_destroy (struct output_driver *driver)
{
  struct xr_driver *xr = xr_driver_cast (driver);

  if (xr->pager)
    xr_finish_page (xr);

  xr_pager_destroy (xr->pager);

  if (xr->drawing_surface && xr->drawing_surface != xr->dest_surface)
    cairo_surface_destroy (xr->drawing_surface);
  if (xr->dest_surface)
    {
      cairo_surface_finish (xr->dest_surface);
      cairo_status_t status = cairo_surface_status (xr->dest_surface);
      if (status != CAIRO_STATUS_SUCCESS)
        fprintf (stderr,  _("error drawing output for %s driver: %s\n"),
                 output_driver_get_name (driver),
                 cairo_status_to_string (status));
      cairo_surface_destroy (xr->dest_surface);
    }

  xr_page_style_unref (xr->page_style);
  xr_fsm_style_unref (xr->fsm_style);
  free (xr);
}

static void
xr_update_page_setup (struct output_driver *driver,
                      const struct page_setup *setup)
{
  struct xr_driver *xr = xr_driver_cast (driver);

  const double scale = 72 * XR_POINT;

  int swap = setup->orientation == PAGE_LANDSCAPE;
  enum table_axis h = H ^ swap;
  enum table_axis v = V ^ swap;

  struct xr_page_style *old_ps = xr->page_style;
  xr->page_style = xmalloc (sizeof *xr->page_style);
  *xr->page_style = (struct xr_page_style) {
    .ref_cnt = 1,

    .margins = {
      [H] = { setup->margins[h][0] * scale, setup->margins[h][1] * scale },
      [V] = { setup->margins[v][0] * scale, setup->margins[v][1] * scale },
    },

    .initial_page_number = setup->initial_page_number,
    .include_outline = old_ps->include_outline,
  };
  for (size_t i = 0; i < 2; i++)
    page_heading_copy (&xr->page_style->headings[i], &setup->headings[i]);
  xr_page_style_unref (old_ps);

  struct xr_fsm_style *old_fs = xr->fsm_style;
  xr->fsm_style = xmalloc (sizeof *xr->fsm_style);
  *xr->fsm_style = (struct xr_fsm_style) {
    .ref_cnt = 1,
    .size = { [H] = setup->paper[H] * scale, [V] = setup->paper[V] * scale },
    .min_break = {
      [H] = setup->paper[H] * scale / 2,
      [V] = setup->paper[V] * scale / 2,
    },
    .font = pango_font_description_copy (old_fs->font),
    .fg = old_fs->fg,
    .use_system_colors = old_fs->use_system_colors,
    .object_spacing = setup->object_spacing * 72 * XR_POINT,
    .font_resolution = old_fs->font_resolution,
  };
  xr_fsm_style_unref (old_fs);

  xr_set_surface_size (xr->dest_surface, xr->output_type,
                       setup->paper[H] * 72.0, setup->paper[V] * 72.0);
}

static void
xr_submit (struct output_driver *driver, const struct output_item *item)
{
  struct xr_driver *xr = xr_driver_cast (driver);

  if (item->type == OUTPUT_ITEM_PAGE_SETUP)
    {
      if (!xr->pager)
        xr_update_page_setup (driver, item->page_setup);
      return;
    }

  if (!xr->pager)
    {
      xr->pager = xr_pager_create (xr->page_style, xr->fsm_style);
      xr_pager_add_page (xr->pager, cairo_create (xr->drawing_surface));
    }

  xr_pager_add_item (xr->pager, item);
  while (xr_pager_needs_new_page (xr->pager))
    {
      xr_finish_page (xr);
      xr_pager_add_page (xr->pager, cairo_create (xr->drawing_surface));
    }
}

struct output_driver_factory pdf_driver_factory =
  { "pdf", "pspp.pdf", xr_pdf_create };
struct output_driver_factory ps_driver_factory =
  { "ps", "pspp.ps", xr_ps_create };
struct output_driver_factory svg_driver_factory =
  { "svg", "pspp.svg", xr_svg_create };
struct output_driver_factory png_driver_factory =
  { "png", "pspp.png", xr_png_create };

static const struct output_driver_class cairo_driver_class =
{
  .name = "cairo",
  .destroy = xr_destroy,
  .submit = xr_submit,
  .handles_groups = true,
};
