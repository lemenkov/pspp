/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011, 2014, 2015 Free Software Foundation, Inc.

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
#include "math/chart-geometry.h"
#include "output/charts/plot-hist.h"

#include <float.h>
#include <gsl/gsl_randist.h>

#include "data/val-type.h"
#include "output/cairo-chart.h"

#include "gl/xvasprintf.h"
#include "gl/minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Write the legend of the chart */
static void
histogram_write_legend (cairo_t *cr, const struct xrchart_geometry *geom,
                        double n, double mean, double stddev)
{
  double y = geom->axis[SCALE_ORDINATE].data_min;
  cairo_save (cr);

  if (n != SYSMIS)
    {
      char *buf = xasprintf (_("N = %.2f"), n);
      cairo_move_to (cr, geom->legend_left, y);
      xrchart_label (cr, 'l', 'b', geom->font_size, buf);
      y += geom->font_size * 1.5;
      free (buf);
    }

  if (mean != SYSMIS)
    {
      char *buf = xasprintf (_("Mean = %.1f"), mean);
      cairo_move_to (cr,geom->legend_left, y);
      xrchart_label (cr, 'l', 'b', geom->font_size, buf);
      y += geom->font_size * 1.5;
      free (buf);
    }

  if (stddev != SYSMIS)
    {
      char *buf = xasprintf (_("Std. Dev = %.2f"), stddev);
      cairo_move_to (cr, geom->legend_left, y);
      xrchart_label (cr, 'l', 'b', geom->font_size, buf);
      free (buf);
    }

  cairo_restore (cr);
}

static void
hist_draw_bar (cairo_t *cr, const struct xrchart_geometry *geom,
               const gsl_histogram *h, int bar)
{
  double upper;
  double lower;
  double height;

  assert ( 0 == gsl_histogram_get_range (h, bar, &lower, &upper));
  assert ( upper >= lower);

  const double x_pos =
    (lower - geom->axis[SCALE_ABSCISSA].min) * geom->axis[SCALE_ABSCISSA].scale
    +  geom->axis[SCALE_ABSCISSA].data_min;
  const double width = (upper - lower) * geom->axis[SCALE_ABSCISSA].scale;

  height = geom->axis[SCALE_ORDINATE].scale * gsl_histogram_get (h, bar);

  cairo_rectangle (cr,
		   x_pos,
		   geom->axis[SCALE_ORDINATE].data_min,
                   width, height);
  cairo_save (cr);
  cairo_set_source_rgb (cr,
                        geom->fill_colour.red / 255.0,
                        geom->fill_colour.green / 255.0,
                        geom->fill_colour.blue / 255.0);
  cairo_fill_preserve (cr);
  cairo_restore (cr);
  cairo_stroke (cr);
}

void
xrchart_draw_histogram (const struct chart_item *chart_item, cairo_t *cr,
                        struct xrchart_geometry *geom)
{
  struct histogram_chart *h = to_histogram_chart (chart_item);
  int i;
  int bins;

  xrchart_write_title (cr, geom, _("HISTOGRAM"));

  xrchart_write_ylabel (cr, geom, _("Frequency"));
  xrchart_write_xlabel (cr, geom, chart_item_get_title (chart_item));

  if (h->gsl_hist == NULL)
    {
      /* Probably all values are SYSMIS. */
      return;
    }

  xrchart_write_yscale (cr, geom, 0, gsl_histogram_max_val (h->gsl_hist));
  xrchart_write_xscale (cr, geom, gsl_histogram_min (h->gsl_hist),
			gsl_histogram_max (h->gsl_hist));


  /* Draw the ticks and compute if the rendered tick text is wider than the bin */
  bins = gsl_histogram_bins (h->gsl_hist);

  for (i = 0; i < bins; i++)
    {
      hist_draw_bar (cr, geom, h->gsl_hist, i);
    }

  histogram_write_legend (cr, geom, h->n, h->mean, h->stddev);

  if (h->show_normal
      && h->n != SYSMIS && h->mean != SYSMIS && h->stddev != SYSMIS)
    {
      /* Draw the normal curve */
      double x_min, x_max;
      double ordinate_scale;
      double binwidth;
      double x;

      gsl_histogram_get_range (h->gsl_hist, 0, &x_min, &x_max);
      binwidth = x_max - x_min;

      /* The integral over the histogram is binwidth * sum(bin_i), while the integral over the pdf is 1 */
      /* Therefore the pdf has to be scaled accordingly such that the integrals are equal               */
      ordinate_scale = binwidth * gsl_histogram_sum(h->gsl_hist);

      /* Clip normal curve to the rectangle formed by the axes. */
      cairo_save (cr);
      cairo_rectangle (cr, geom->axis[SCALE_ABSCISSA].data_min, geom->axis[SCALE_ORDINATE].data_min,
                       geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ABSCISSA].data_min,
                       geom->axis[SCALE_ORDINATE].data_max - geom->axis[SCALE_ORDINATE].data_min);
      cairo_clip (cr);

      cairo_move_to (cr, geom->axis[SCALE_ABSCISSA].data_min, geom->axis[SCALE_ORDINATE].data_min);
      for (x = geom->axis[SCALE_ABSCISSA].min;
	   x <= geom->axis[SCALE_ABSCISSA].max;
	   x += (geom->axis[SCALE_ABSCISSA].max - geom->axis[SCALE_ABSCISSA].min) / 100.0)
	{
	  const double y = gsl_ran_gaussian_pdf (x - h->mean, h->stddev) * ordinate_scale;
	  /* Transform to drawing coordinates */
	  const double x_pos = (x - geom->axis[SCALE_ABSCISSA].min) * geom->axis[SCALE_ABSCISSA].scale + geom->axis[SCALE_ABSCISSA].data_min;
	  const double y_pos = (y - geom->axis[SCALE_ORDINATE].min) * geom->axis[SCALE_ORDINATE].scale + geom->axis[SCALE_ORDINATE].data_min;
          cairo_line_to (cr, x_pos, y_pos);
	}
      cairo_stroke (cr);

      cairo_restore (cr);
    }
}
