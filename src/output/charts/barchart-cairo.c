/* PSPP - a program for statistical analysis.
   Copyright (C) 2015, 2016 Free Software Foundation, Inc.

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
#include "data/variable.h"

#include "output/cairo-chart.h"

#include "gl/minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)



static void
abscissa_label (const struct barchart *bc, cairo_t *cr,
		struct xrchart_geometry *geom,
		const union value *prev,
		double x_pos,
		double width,
		int n_last_cat)
{
  struct category *foo = NULL;
  size_t hash = value_hash (prev, bc->widths[0], 0);
  HMAP_FOR_EACH_WITH_HASH (foo, struct category, node, hash, &bc->primaries)
    {
      if (value_equal (&foo->val, prev, bc->widths[0])) 
	break;
    }
  
  draw_tick (cr, geom, SCALE_ABSCISSA, false,
	     x_pos - (width * n_last_cat) / 2.0,
	     "%s",  ds_cstr (&foo->label));
}




void
xrchart_draw_barchart (const struct chart_item *chart_item, cairo_t *cr,
                       struct xrchart_geometry *geom)
{
  struct barchart *bc = to_barchart (chart_item);
  int i;

  xrchart_write_title (cr, geom, _("Bar Chart"));

  xrchart_write_ylabel (cr, geom, bc->ylabel);
  xrchart_write_xlabel (cr, geom, chart_item_get_title (chart_item));

  if (bc->percent)
    xrchart_write_yscale (cr, geom, 0, bc->largest * 100.0 / bc->total_count );
  else
    xrchart_write_yscale (cr, geom, 0, bc->largest);

  const double abscale = geom->axis[SCALE_ABSCISSA].data_max - geom->axis[SCALE_ABSCISSA].data_min;
  const double width = abscale / (double) (bc->n_nzcats + bc->n_pcats);

  double x_pos = 0.5 * width;
  union value *prev = NULL;

  if (bc->ss)
    {
      const int blob_size = 13;
      const int height = blob_size * (hmap_count (&bc->secondaries) * 2);
      
      cairo_rectangle (cr,
		       geom->axis[SCALE_ABSCISSA].data_max + 10,
		       geom->axis[SCALE_ORDINATE].data_max - height,
		       100, height);

      cairo_stroke (cr);

      int ypos = blob_size * 1.5;
      for (i = 0 ; i < hmap_count (&bc->secondaries) ; ++i)
	{
	  const struct category *foo = bc->ss[i];

	  cairo_move_to (cr, 
			 geom->axis[SCALE_ABSCISSA].data_max + (1.5 * blob_size) + 20,
			 geom->axis[SCALE_ORDINATE].data_max - ypos);
	  
	  xrchart_label (cr, 'l', 'b', geom->font_size, ds_cstr (&foo->label));

	  cairo_rectangle (cr,
			   geom->axis[SCALE_ABSCISSA].data_max + 20,
			   geom->axis[SCALE_ORDINATE].data_max - ypos,
			   blob_size, blob_size);

	  cairo_save (cr);
	  cairo_set_source_rgb (cr,
				data_colour[foo->idx].red / 255.0,
				data_colour[foo->idx].green / 255.0,
				data_colour[foo->idx].blue / 255.0);
	  cairo_fill_preserve (cr);

	  cairo_restore (cr);
	  
	  cairo_stroke (cr);

	  ypos += blob_size * 2;
	}
    }

  int n_last_cat = 0;
  for (i = 0; i < bc->n_nzcats; i++)
    {
      double height = geom->axis[SCALE_ORDINATE].scale * bc->cats[i]->count;
      if (bc->percent)
	height *= 100.0  /  bc->total_count ;

      if (prev && !value_equal (prev, &bc->cats[i]->values[0], bc->widths[0]))
	{
	  abscissa_label (bc, cr, geom, prev, x_pos, width, n_last_cat);

	  x_pos += width;
	  n_last_cat = 0;
	}

      cairo_rectangle (cr,
		       geom->axis[SCALE_ABSCISSA].data_min + x_pos,
		       geom->axis[SCALE_ORDINATE].data_min,
		       width, height);
      cairo_save (cr);


      int cidx = 0;
      if (bc->ss)
	{
	  struct category *foo;
	  size_t hash = value_hash (&bc->cats[i]->values[1], bc->widths[1], 0);
	  HMAP_FOR_EACH_WITH_HASH (foo, struct category, node, hash, &bc->secondaries)
	    {
	      if (value_equal (&foo->val, &bc->cats[i]->values[1], bc->widths[1]))
		{
		  cidx = foo->idx;
		  break;
		}
	    }
	}

      cairo_set_source_rgb (cr,
			    data_colour[cidx].red / 255.0,
			    data_colour[cidx].green / 255.0,
			    data_colour[cidx].blue / 255.0);
      cairo_fill_preserve (cr);

      cairo_restore (cr);
      cairo_stroke (cr);

      x_pos += width;

      prev = &bc->cats[i]->values[0];
      n_last_cat ++;
    }

  abscissa_label (bc, cr, geom, prev, x_pos, width, n_last_cat);
}

