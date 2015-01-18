/* PSPP - a program for statistical analysis.
   Copyright (C) 2015 Free Software Foundation, Inc.

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

#include "output/charts/barchart.h"
#include "output/charts/piechart.h"

#include <math.h>

#include "output/cairo-chart.h"

#include "gl/minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


static void
draw_bar (cairo_t *cr, const struct xrchart_geometry *geom,
	  const struct barchart *bc, int bar)
{
  const double width =
    (geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ABSCISSA].data_min) 
    / 
    (double) bc->n_bars ;

  const double x_pos =
    (geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ABSCISSA].data_min) *
    bar 
    / 
    (double) bc->n_bars ;


  double height = geom->axis[SCALE_ORDINATE].scale * bc->bars[bar].magnitude;

  cairo_rectangle (cr,
		   geom->axis[SCALE_ABSCISSA].data_min + x_pos + width * 0.1,
		   geom->axis[SCALE_ORDINATE].data_min,
                   width * 0.8, height);
  cairo_save (cr);
  cairo_set_source_rgb (cr,
                        geom->fill_colour.red / 255.0,
                        geom->fill_colour.green / 255.0,
                        geom->fill_colour.blue / 255.0);
  cairo_fill_preserve (cr);
  cairo_restore (cr);
  cairo_stroke (cr);

  draw_tick (cr, geom, SCALE_ABSCISSA, true,
	     x_pos + width / 2.0, "%s", ds_cstr (&bc->bars[bar].label));
}


void
xrchart_draw_barchart (const struct chart_item *chart_item, cairo_t *cr,
                       struct xrchart_geometry *geom)
{
  struct barchart *bc = to_barchart (chart_item);
  int i;

  xrchart_write_title (cr, geom, _("BARCHART"));

  xrchart_write_ylabel (cr, geom, bc->ylabel);
  xrchart_write_xlabel (cr, geom, chart_item_get_title (chart_item));

  xrchart_write_yscale (cr, geom, 0, bc->largest);

  for (i = 0; i < bc->n_bars; i++)
    {
      draw_bar (cr, geom, bc, i);
    }
}

