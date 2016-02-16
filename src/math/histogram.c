/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008, 2009, 2011, 2012 Free Software Foundation, Inc.

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

#include "math/histogram.h"

#include <gsl/gsl_histogram.h>
#include <math.h>

#include "data/settings.h"
#include "libpspp/message.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "math/chart-geometry.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


#include "gl/xalloc.h"

void
histogram_add (struct histogram *h, double y, double c)
{
  struct statistic *stat = &h->parent;
  stat->accumulate (stat, NULL, c, 0, y);
}

static void
acc (struct statistic *s, const struct ccase *cx UNUSED, double c, double cc UNUSED, double y)
{
  struct histogram *hist = UP_CAST (s, struct histogram, parent);

  gsl_histogram_accumulate (hist->gsl_hist, y, c);
}

static void
destroy (struct statistic *s)
{
  struct histogram *h = UP_CAST (s, struct histogram, parent);
  gsl_histogram_free (h->gsl_hist);
  free (s);
}


/* Find a bin width which is adapted to the scaling of the x axis
In the example here, the binwidth is half of the tick interval.

        binwidth
         >   <
     |....+....+....+.      .+....|
   LOWER  1    2    3     N_TICKS
        ^LOWDBL                 ^HIGHDBL

This only works, when the min and max value for the histogram are adapted
such that (max-min) is a multiple of the binwidth. Then the location of the
first bin has to be aligned to the ticks.
*/
static int
hist_find_pretty_no_of_bins(double bin_width_in, double min, double max,
			    double *adjusted_min, double *adjusted_max)
{
  double lower, interval;
  int n_ticks;
  double binwidth;
  int nbins;

  chart_get_scale (max, min, &lower, &interval, &n_ticks);

  if (bin_width_in >= 2 * interval)
    {
      binwidth = floor(bin_width_in/interval) * interval;
      *adjusted_min = lower;
    }
  else if (bin_width_in >= 1.5 * interval)
    {
      binwidth = 1.5 * interval;
      if (min < (lower + 0.5 * interval))
	*adjusted_min = lower;
      else
	*adjusted_min = lower + 0.5 * interval;
    }
  else if (bin_width_in >= interval)
    {
      binwidth = interval;
      *adjusted_min = lower;
    }
  else if (bin_width_in >= (2.0/3.0 * interval))
    {
      binwidth = (2.0/3.0 * interval);
      if (min >= lower + binwidth)
	*adjusted_min = lower + binwidth;
      else
	*adjusted_min = lower;
    }
  else
    {
      int i;
      for(i = 2; bin_width_in < interval/i; i++);
      binwidth = interval/i;
      *adjusted_min = floor((min - lower)/binwidth)*binwidth + lower;
    }

  nbins = ceil((max-*adjusted_min)/binwidth);
  *adjusted_max = nbins*binwidth + *adjusted_min;

  /* adjusted_max should never be smaller than max but if it is equal
     then the gsl_histogram will not add the cases which have max value */
  if (*adjusted_max <= max)
    {
      *adjusted_max += binwidth;
      nbins++;
    }
  assert (*adjusted_min <= min);

  return nbins;
}


struct histogram *
histogram_create (double bin_width_in, double min, double max)
{
  struct histogram *h;
  struct statistic *stat;
  int bins;
  double adjusted_min, adjusted_max;

  if (max == min)
    {
      msg (MW, _("Not creating histogram because the data contains less than 2 distinct values"));
      return NULL;
    }

  assert (bin_width_in > 0);

  bins = hist_find_pretty_no_of_bins(bin_width_in, min, max, &adjusted_min, &adjusted_max);

  h = xmalloc (sizeof *h);

  h->gsl_hist = gsl_histogram_alloc (bins);

  /* The bin ranges could be computed with gsl_histogram_set_ranges_uniform,
     but the number of bins is adapted to the ticks of the axis such that for example
     data ranging from 1.0 to 7.0 with 6 bins will have bin limits at
     2.0,3.0,4.0 and 5.0. Due to numerical accuracy the computed bin limits might
     be 4.99999999 for a value which is expected to be 5.0. But the limits of
     the histogram bins should be that what is expected from the displayed ticks.
     Therefore the bin limits are computed from the rounded values which is similar
     to the procedure at the chart_get_ticks_format. Actual bin limits should be
     exactly what is displayed at the ticks.
     But... I cannot reproduce the problem that I see with gsl_histogram_set_ranges_uniform
     with the code below without rounding...
  */
  {
    double *ranges = xmalloc (sizeof (double) * (bins + 1));
    double interval = (adjusted_max - adjusted_min) / bins;
    for (int i = 0; i < bins; i++)
      ranges[i] = adjusted_min + interval * i;
    ranges[bins] = adjusted_max;
    gsl_histogram_set_ranges (h->gsl_hist, ranges, bins + 1);
    free (ranges);
  }

  stat = &h->parent;
  stat->accumulate = acc;
  stat->destroy = destroy;

  return h;
}

