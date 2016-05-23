/* PSPP - a program for statistical analysis.
   Copyright (C) 2014, 2015 Free Software Foundation, Inc.

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

#include "output/charts/scatterplot.h"

#include "data/case.h"
#include "data/casereader.h"
#include "data/variable.h"
#include "output/cairo-chart.h"
#include "libpspp/str.h"
#include "libpspp/message.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static const struct xrchart_colour black = {0,0,0};

void
xrchart_draw_scatterplot (const struct chart_item *chart_item, cairo_t *cr,
			  struct xrchart_geometry *geom)
{
  const struct scatterplot_chart *spc = to_scatterplot_chart (chart_item);
  struct casereader *data;
  struct ccase *c;
  /* While reading the cases, a list with categories of the byvar is build */
  /* All distinct values are stored in catvals                             */
  /* Each category will later have a different plot colour                 */
  const int MAX_PLOT_CATS = 20;
  union value catvals[MAX_PLOT_CATS];
  int n_catvals = 0;
  int byvar_width = 0;
  int i = 0;
  const struct xrchart_colour *colour;
  
  if (spc->byvar)
    byvar_width = var_get_width (spc->byvar);

  xrchart_write_xscale (cr, geom, spc->x_min, spc->x_max);
  xrchart_write_yscale (cr, geom, spc->y_min, spc->y_max);
  xrchart_write_title (cr, geom, _("Scatterplot %s"), chart_item->title);
  xrchart_write_xlabel (cr, geom, spc->xlabel);
  xrchart_write_ylabel (cr, geom, spc->ylabel);

  cairo_save (cr);
  data = casereader_clone (spc->data);
  for (; (c = casereader_read (data)) != NULL; case_unref (c))
    {
      if (spc->byvar)
	{
	  const union value *val = case_data_idx (c,SP_IDX_BY);
	  for(i=0;i<n_catvals && !value_equal (&catvals[i],val,byvar_width);i++);
	  if (i == n_catvals) /* No entry found */
	    {
	      if (n_catvals < MAX_PLOT_CATS)
		{
		  struct string label;
		  ds_init_empty (&label);
		  if (var_is_value_missing (spc->byvar,val,MV_ANY))
		    ds_put_cstr (&label,"missing");
		  else
		    var_append_value_name (spc->byvar,val,&label);
		  value_clone (&catvals[n_catvals++],val,byvar_width);
		  geom->n_datasets++;
		  geom->dataset = xrealloc (geom->dataset,
					    geom->n_datasets * sizeof (*geom->dataset));

		  geom->dataset[geom->n_datasets - 1] = strdup (ds_cstr(&label));
		  ds_destroy (&label);
		}
	      else /* Use the last plot category */
		{
		  *(spc->byvar_overflow) = true;
		  i--;
		}
	    }
          colour = &data_colour[i % XRCHART_N_COLOURS];
	}
      else
        colour = &black;

      cairo_set_source_rgb (cr,
                            colour->red / 255.0,
                            colour->green / 255.0,
                            colour->blue / 255.0);
    
      xrchart_datum (cr, geom, 0,
		     case_data_idx (c, SP_IDX_X)->f,
		     case_data_idx (c, SP_IDX_Y)->f);
    }
  casereader_destroy (data);
  cairo_restore (cr);

  for(i=0;i<n_catvals;i++)
    value_destroy (&catvals[i],byvar_width);

  if (spc->byvar)
    xrchart_write_legend (cr, geom);

}
