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

#include <stdlib.h>

#include "libpspp/cast.h"
#include "libpspp/str.h"
#include "output/chart-item-provider.h"

#include "gl/xalloc.h"

/* Creates and returns a chart that will render a barchart with
   the given TITLE and the N_BARS described in BARS. */
struct chart_item *
barchart_create (const char *title, const char *ylabel, const struct slice *bars, int n_bars)
{
  struct barchart *bar;
  int i;

  bar = xmalloc (sizeof *bar);
  chart_item_init (&bar->chart_item, &barchart_class, title);
  bar->bars = xnmalloc (n_bars, sizeof *bar->bars);
  bar->largest = 0;
  bar->ylabel = strdup (ylabel);
  for (i = 0; i < n_bars; i++)
    {
      const struct slice *src = &bars[i];
      struct slice *dst = &bar->bars[i];

      ds_init_string (&dst->label, &src->label);
      dst->magnitude = src->magnitude;
      if (dst->magnitude > bar->largest)
	bar->largest = dst->magnitude;
    }
  bar->n_bars = n_bars;
  return &bar->chart_item;
}

static void
barchart_destroy (struct chart_item *chart_item)
{
  struct barchart *bar = to_barchart (chart_item);
  int i;

  for (i = 0; i < bar->n_bars; i++)
    {
      struct slice *slice = &bar->bars[i];
      ds_destroy (&slice->label);
    }
  free (bar->ylabel);
  free (bar->bars);
  free (bar);
}

const struct chart_item_class barchart_class =
  {
    barchart_destroy
  };
