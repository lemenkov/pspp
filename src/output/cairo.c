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
    XR_SVG
  };

/* Cairo output driver. */
struct xr_driver
  {
    struct output_driver driver;

    struct xr_fsm_style *fsm_style;
    struct xr_page_style *page_style;
    struct xr_pager *pager;
    cairo_surface_t *surface;
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
xr_allocate (const char *name, int device_type, struct string_map *o)
{
  struct xr_driver *xr = xzalloc (sizeof *xr);
  struct output_driver *d = &xr->driver;

  output_driver_init (d, &cairo_driver_class, name, device_type);

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
  PangoFontDescription *fixed_font = parse_font_option
    (d, o, "fixed-font", "monospace", font_size, false, false);
  PangoFontDescription *proportional_font = parse_font_option (
    d, o, "prop-font", "sans serif", font_size, false, false);

  struct cell_color fg = parse_color (opt (d, o, "foreground-color", "black"));

  bool transparent = parse_boolean (opt (d, o, "transparent", "false"));
  struct cell_color bg = (transparent
                          ? (struct cell_color) { .alpha = 0 }
                          : parse_color (opt (d, o, "background-color",
                                              "white")));

  bool systemcolors = parse_boolean (opt (d, o, "systemcolors", "false"));

  int object_spacing
    = parse_dimension (opt (d, o, "object-spacing", NULL)) * scale;
  if (object_spacing <= 0)
    object_spacing = XR_POINT * 12;

  xr->page_style = xmalloc (sizeof *xr->page_style);
  *xr->page_style = (struct xr_page_style) {
    .ref_cnt = 1,

    .margins = {
      [H] = { margins[H][0], margins[H][1], },
      [V] = { margins[V][0], margins[V][1], },
    },

    .bg = bg,
    .initial_page_number = 1,
    .object_spacing = object_spacing,
  };

  xr->fsm_style = xmalloc (sizeof *xr->fsm_style);
  *xr->fsm_style = (struct xr_fsm_style) {
    .ref_cnt = 1,
    .size = { [H] = size[H], [V] = size[V] },
    .min_break = { [H] = min_break[H], [V] = min_break[V] },
    .fonts = {
      [XR_FONT_PROPORTIONAL] = proportional_font,
      [XR_FONT_FIXED] = fixed_font,
    },
    .fg = fg,
    .use_system_colors = systemcolors,
    .transparent = transparent,
    .font_resolution = 72.0,
  };

  return xr;
}

static struct output_driver *
xr_create (struct file_handle *fh, enum settings_output_devices device_type,
           struct string_map *o, enum xr_output_type file_type)
{
  const char *file_name = fh_get_file_name (fh);
  struct xr_driver *xr = xr_allocate (file_name, device_type, o);

  double paper[TABLE_N_AXES];
  for (int a = 0; a < TABLE_N_AXES; a++)
    paper[a] = xr_to_pt (xr_page_style_paper_size (xr->page_style,
                                                   xr->fsm_style, a));
  if (file_type == XR_PDF)
    xr->surface = cairo_pdf_surface_create (file_name, paper[H], paper[V]);
  else if (file_type == XR_PS)
    xr->surface = cairo_ps_surface_create (file_name, paper[H], paper[V]);
  else if (file_type == XR_SVG)
    xr->surface = cairo_svg_surface_create (file_name, paper[H], paper[V]);
  else
    NOT_REACHED ();

  cairo_status_t status = cairo_surface_status (xr->surface);
  if (status != CAIRO_STATUS_SUCCESS)
    {
      msg (ME, _("error opening output file `%s': %s"),
           file_name, cairo_status_to_string (status));
      goto error;
    }

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

  if (xr->surface)
    {
      cairo_surface_finish (xr->surface);
      cairo_status_t status = cairo_surface_status (xr->surface);
      if (status != CAIRO_STATUS_SUCCESS)
        fprintf (stderr,  _("error drawing output for %s driver: %s"),
                 output_driver_get_name (driver),
                 cairo_status_to_string (status));
      cairo_surface_destroy (xr->surface);
    }

  xr_pager_destroy (xr->pager);
  xr_page_style_unref (xr->page_style);
  xr_fsm_style_unref (xr->fsm_style);
  free (xr);
}

static void
xr_flush (struct output_driver *driver)
{
  struct xr_driver *xr = xr_driver_cast (driver);

  if (xr->surface)
    cairo_surface_flush (xr->surface);
}

static void
xr_update_page_setup (struct output_driver *driver,
                      const struct page_setup *ps)
{
  struct xr_driver *xr = xr_driver_cast (driver);

  const double scale = 72 * XR_POINT;

  int swap = ps->orientation == PAGE_LANDSCAPE;
  enum table_axis h = H ^ swap;
  enum table_axis v = V ^ swap;

  xr_page_style_unref (xr->page_style);
  xr->page_style = xmalloc (sizeof *xr->page_style);
  *xr->page_style = (struct xr_page_style) {
    .ref_cnt = 1,

    .margins = {
      [H] = { ps->margins[h][0] * scale, ps->margins[h][1] * scale },
      [V] = { ps->margins[v][0] * scale, ps->margins[v][1] * scale },
    },

    .bg = xr->page_style->bg,
    .initial_page_number = ps->initial_page_number,
    .object_spacing = ps->object_spacing * 72 * XR_POINT,
  };
  for (size_t i = 0; i < 2; i++)
    page_heading_copy (&xr->page_style->headings[i], &ps->headings[i]);

  struct xr_fsm_style *old_fs = xr->fsm_style;
  xr->fsm_style = xmalloc (sizeof *xr->fsm_style);
  *xr->fsm_style = (struct xr_fsm_style) {
    .ref_cnt = 1,
    .size = { [H] = ps->paper[H] * scale, [V] = ps->paper[V] * scale },
    .min_break = {
      [H] = ps->paper[H] * scale / 2,
      [V] = ps->paper[V] * scale / 2,
    },
    .fg = old_fs->fg,
    .use_system_colors = old_fs->use_system_colors,
    .transparent = old_fs->transparent,
    .font_resolution = 72.0,
  };
  for (size_t i = 0; i < XR_N_FONTS; i++)
    xr->fsm_style->fonts[i] = pango_font_description_copy (old_fs->fonts[i]);
  xr_fsm_style_unref (old_fs);

  cairo_pdf_surface_set_size (xr->surface, ps->paper[H] * 72.0,
                              ps->paper[V] * 72.0);
}

static void
xr_submit (struct output_driver *driver, const struct output_item *output_item)
{
  struct xr_driver *xr = xr_driver_cast (driver);

  if (is_page_setup_item (output_item))
    {
      if (!xr->pager)
        xr_update_page_setup (driver,
                              to_page_setup_item (output_item)->page_setup);
      return;
    }

  if (!xr->pager)
    {
      xr->pager = xr_pager_create (xr->page_style, xr->fsm_style);
      xr_pager_add_page (xr->pager, cairo_create (xr->surface));
    }

  xr_pager_add_item (xr->pager, output_item);
  while (xr_pager_needs_new_page (xr->pager))
    {
      cairo_surface_show_page (xr->surface);
      xr_pager_add_page (xr->pager, cairo_create (xr->surface));
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
