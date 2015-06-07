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

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include "libpspp/compiler.h"

#include "math/chart-geometry.h"
#include <limits.h>
#include <float.h>
#include <math.h>

#if 0
static void
dump_scale (const double low, const double interval, int n_ticks)
{
  int i;
  double tick = low;
  for (i = 0; i <= n_ticks; ++i)
    {
      printf ("Tick %d: %g\n", i, tick);
      tick += interval;
    }
}
#endif


static void
test_range (double low, double high)
{
  int n_ticks = 0;
  double interval;
  double lower;

  chart_get_scale (high, low,
		   &lower, &interval, &n_ticks);

  if ((high - low) < 10 * DBL_MIN){
    assert (n_ticks == 0);
    assert (lower == low);
    assert (interval <= 10 * DBL_MIN);
  }
  else
    assert (n_ticks > 4);

  assert (n_ticks <= 10);

#if 0
  printf("%s: high: %lg, low %lg, interval: %lg, nticks: %d\n", 
	 __FUNCTION__, high, low, interval, n_ticks);
  dump_scale (lower, interval, n_ticks);
#endif

  if ((high - low) > 10 * DBL_MIN) {
    assert (lower <= low);
    assert (lower + interval > low);
    assert (lower + n_ticks * interval < high);
    assert (lower + (n_ticks + 1) * interval >= high);
  }
}


int 
main (int argc UNUSED, char **argv UNUSED)
{
  test_range (0, 0);
  test_range (5, 5);
  test_range (-5, -5);
  test_range (0, 7);
  test_range (0.2, 11);
  test_range (-0.2, 11);
  test_range (-10, 0.2);
  test_range (-10, -0.2);
  test_range (-10000, 10003);
  test_range (50042,50053);
  test_range (-50010, -49999);
  test_range (0.000100002, 0.000100010);

  test_range (102, 50030); 
  test_range (0.00102, 0.0050030); 

  return 0;
}
