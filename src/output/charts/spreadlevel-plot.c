/* PSPP - a program for statistical analysis.
   Copyright (C) 2012, 2020 Free Software Foundation, Inc.

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

#include "output/charts/spreadlevel-plot.h"

#include "libpspp/cast.h"
#include "output/chart-provider.h"

#include "gl/xalloc.h"
#include "gl/minmax.h"

#include <math.h>
#include <float.h>
#include <stdlib.h>

struct chart *
spreadlevel_plot_create (const char *label, double tx_pwr)
{
  struct spreadlevel_plot_chart *sl = xzalloc (sizeof *sl);
  chart_init (&sl->chart, &spreadlevel_plot_chart_class, label);

  sl->x_lower = DBL_MAX;
  sl->x_upper = -DBL_MAX;

  sl->y_lower = DBL_MAX;
  sl->y_upper = -DBL_MAX;

  sl->tx_pwr = tx_pwr;

  sl->n_data = 0;
  sl->data = NULL;

  return &sl->chart;
}

void
spreadlevel_plot_add (struct chart *ci, double spread, double level)
{
  struct spreadlevel_plot_chart *sl = to_spreadlevel_plot_chart (ci);

  /* Zero has a special meaning, in this implementation.  */
  if (sl->tx_pwr == 0)
    {
      spread = log (fabs (spread));
      level = log (fabs (level));
    }
  else
    {
      spread = pow (spread, sl->tx_pwr);
      level = pow (level, sl->tx_pwr);
    }

  sl->x_lower = MIN (sl->x_lower, level);
  sl->x_upper = MAX (sl->x_upper, level);

  sl->y_lower = MIN (sl->y_lower, spread);
  sl->y_upper = MAX (sl->y_upper, spread);

  sl->n_data++;
  sl->data = xrealloc (sl->data, sizeof (*sl->data) * sl->n_data);
  sl->data[sl->n_data - 1].x = level;
  sl->data[sl->n_data - 1].y = spread;
}


static void
spreadlevel_plot_chart_destroy (struct chart *chart)
{
  struct spreadlevel_plot_chart *sl = to_spreadlevel_plot_chart (chart);

  free (sl->data);
  free (sl);
}

const struct chart_class spreadlevel_plot_chart_class =
  {
    spreadlevel_plot_chart_destroy
  };
