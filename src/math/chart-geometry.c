/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2015 Free Software Foundation, Inc.

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
#include <math.h>
#include <float.h>
#include <assert.h>

#include "chart-geometry.h"
#include <stdlib.h>

#include "gl/xalloc.h"
#include "gl/minmax.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static const double standard_tick[] = {1, 2, 5, 10};

/* 
   Find a set {LOWER, INTERVAL, N_TICKS} such that:

   LOWER <= LOWDBL,
   LOWER + INTERVAL > LOWDBL,
   
   LOWER + N_TICKS * INTERVAL < HIGHDBL
   LOWER + (N_TICKS + 1) * INTERVAL >= HIGHDBL

   INTERVAL = X * 10^N
    where: N is integer 
    and    X is an element of {1, 2, 5}

   In other words:

         INTERVAL
         >      <
     |....+....+....+.      .+....|
   LOWER  1    2    3     N_TICKS
        ^LOWDBL                 ^HIGHDBL
*/
void
chart_get_scale (double high, double low,
		 double *lower, double *interval,
		 int *n_ticks)
{
  int i;
  double fitness = DBL_MAX;
  double logrange;
  *n_ticks = 0;

  assert (high >= low);

  if ((high - low) < 10 * DBL_MIN) {
    *n_ticks = 0;
    *lower = low;
    *interval = 0.0;
    return;
  }

  logrange = floor(log10(high-low));

  /* Find the most pleasing interval */
  for (i = 1; i < 4; ++i)
    {
      double cinterval = standard_tick[i] * pow(10.0,logrange-1);
      double clower = floor(low/cinterval) * cinterval;
      int cnticks = ceil((high - clower) / cinterval)-1;
      double cfitness = fabs(7.5 - cnticks);

      if (cfitness < fitness) {
	fitness = cfitness;
	*lower = clower;
	*interval = cinterval;
	*n_ticks = cnticks;
      }
    }
}

/* 
   Generate a format string which can be passed to printf like functions,
   which will produce a string in scientific notation representing a real 
   number.  N_DECIMALS is the number of decimal places EXPONENT is the 
   value of the exponent.
*/
static inline char *
gen_pango_markup_scientific_format_string (int n_decimals, int exponent)
{
  /* TRANSLATORS: This is a format string which, when presented to
     printf like functions, will create a pango markup string to
     display real number in scientific  notation. 
     
     In its untranslated form, it will display similar to "1.23 x 10^4". You 
     can leave it untranslated if this is how scientific notation is usually
     presented in your language.
     
     Some locales (such as German) prefer the centered dot rather than the
     multiplication sign between the mantissa an exponent. In which
     case, you can change "#215;" to "#8901;" or other unicode code
     point as appropriate. 

     The . in this string does not and should not be changed, since
     that is taken care of by the stdc library. 

     For information on Pango markup, see 
     http://developer.gnome.org/pango/stable/PangoMarkupFormat.html

     For tables of unicode code points, see http://unicode.org/charts
   */
  return xasprintf(_("%%.%dlf&#215;10<sup>%d</sup>"), n_decimals, exponent);
}

/*
 * Compute the optimum format string and the scaling
 * for the tick drawing on a chart axis
 * Input:  lower:   the lowest tick
 *         interval:the interval between the ticks
 *         nticks:  the number of tick intervals (bins) on the axis
 * Return: fs:      format string for printf to print the tick value
 *         scale:   scaling factor for the tick value
 * The format string has to be freed after usage.
 * An example format string and scalefactor:
 * Non Scientific: "%.3lf", scale=1.00
 * Scientific:     "%.2lfe3", scale = 0.001
 * Usage example:
 *   fs = chart_get_ticks_format(-0.7,0.1,8,&scale);
 *   printf(fs,value*scale);
 *   free(fs);
 */
char *
chart_get_ticks_format (const double lower, const double interval,
			const unsigned int nticks, double *scale)
{
  double logmax = log10(fmax(fabs(lower + (nticks+1)*interval),fabs(lower)));
  double logintv = log10(interval);
  int logshift = 0;
  char *format_string = NULL;
  int nrdecs = 0;

  if (logmax > 0.0 && logintv < 0.0)
    {
      nrdecs = MIN(6,(int)(ceil(fabs(logintv))));
      logshift = 0;
      if (logmax < 12.0)
	format_string = xasprintf("%%.%dlf",nrdecs);
      else
	format_string = xasprintf("%%lg");
    }
  else if (logmax > 0.0) /*logintv is > 0*/
    {
      if (logintv < 5.0 && logmax < 10.0)
	{
	  logshift = 0; /* No scientific format */
	  nrdecs = 0;
	  format_string = xstrdup("%.0lf");
	}
      else
	{
	  logshift = (int)logmax;
	  /* Possible intervals are 0.2Ex, 0.5Ex, 1.0Ex                    */
	  /* log10(0.2E9) = 8.30, log10(0.5E9) = 8.69, log10(1.0E9) = 9    */
	  /* 0.2 and 0.5 need one decimal more. For stability subtract 0.1 */
	  nrdecs = MIN(8,(int)(ceil(logshift-logintv-0.1)));
	  format_string = gen_pango_markup_scientific_format_string (nrdecs, logshift);
	}
    }
  else /* logmax and logintv are < 0 */
    {
      if (logmax > -3.0)
	{
	  logshift = 0; /* No scientific format */
	  nrdecs = MIN(8,(int)(ceil(-logintv)));
	  format_string = xasprintf("%%.%dlf",nrdecs);
	}
      else
	{
	  logshift = (int)logmax-1;
	  nrdecs = MIN(8,(int)(ceil(logshift-logintv-0.1)));
	  format_string = gen_pango_markup_scientific_format_string (nrdecs, logshift);
	}
      }
  *scale = pow(10.0,-(double)logshift);
  return format_string;
}
