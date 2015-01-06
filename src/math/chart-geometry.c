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
#include "decimal.h"
#include <stdlib.h>

static const double standard_tick[] = {1, 2, 5, 10};

/* Adjust tick to be a sensible value
   ie:  ... 0.1,0.2,0.5,   1,2,5,  10,20,50 ... */
void
chart_rounded_tick (double tick, struct decimal *result)
{
  int i;

  struct decimal ddif = {1, 1000};

  /* Avoid arithmetic problems with very small values */
  if (fabs (tick) < DBL_EPSILON)
    {
      result->ordinate = 0;
      result->mantissa = 0;
      return;
    }

  struct decimal dt;
  decimal_from_double (&dt, tick);
  
  double expd = dec_log10 (&dt) - 1;

  for (i = 0  ; i < 4 ; ++i)
    {
      struct decimal candidate;
      struct decimal delta;

      decimal_init (&candidate, standard_tick[i], expd);
      
      delta = dt;
      decimal_subtract (&delta, &candidate);
      delta.ordinate = llabs (delta.ordinate);

      if (decimal_cmp (&delta, &ddif) < 0)
	{
	  ddif = delta;
	  *result = candidate;
	}
    }
}

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
chart_get_scale (double highdbl, double lowdbl,
		 struct decimal *lower, 
		 struct decimal *interval,
		 int *n_ticks)
{
  int i;
  double fitness = DBL_MAX;
  *n_ticks = 0;
  struct decimal high;
  struct decimal low;

  assert (highdbl >= lowdbl);

  decimal_from_double (&high, highdbl);
  decimal_from_double (&low, lowdbl);
  
  struct decimal diff = high;
  decimal_subtract (&diff, &low);

  double expd = dec_log10 (&diff) - 2;

  /* Find the most pleasing interval */
  for (i = 1; i < 4; ++i)
    {
      struct decimal clbound = low;
      struct decimal cubound = high;
      struct decimal candidate;
      decimal_init (&candidate, standard_tick[i], expd);

      decimal_divide (&clbound, &candidate);
      int fl = decimal_floor (&clbound);
      decimal_int_multiply (&candidate, fl);
      clbound = candidate;


      decimal_init (&candidate, standard_tick[i], expd);
      decimal_divide (&cubound, &candidate);
      int fu = decimal_ceil (&cubound);
      decimal_int_multiply (&candidate, fu);

      cubound = candidate;

      decimal_init (&candidate, standard_tick[i], expd);
      decimal_subtract (&cubound, &clbound);
      decimal_divide (&cubound, &candidate);


      ord_t n_int = decimal_floor (&cubound);

      /* We prefer to have between 5 and 10 tick marks on a scale */
      double f = 1 - exp (-0.2 *  fabs (n_int - 7.5) / 7.5);

      if (f < fitness)
	{
	  fitness = f;
	  *lower = clbound;
	  *interval = candidate;
	  *n_ticks = n_int;
	}
    }
}
