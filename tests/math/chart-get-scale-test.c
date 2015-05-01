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

#include "math/decimal.h"
#include "math/chart-geometry.h"
#include <limits.h>
#include <float.h>
#include <math.h>

#if 0
static void
dump_scale (const struct decimal *low, const struct decimal *interval, int n_ticks)
{
  int i;
  struct decimal tick = *low;
  for (i = 0; i <= n_ticks; ++i)
    {
      printf ("Tick %d: %s (%g)\n", i, decimal_to_string (&tick), decimal_to_double (&tick));
      decimal_add (&tick, interval);
    }
}
#endif


static void
test_range (double low, double high)
{
  int n_ticks = 0;
  struct decimal interval;
  struct decimal lower;

  chart_get_scale (high, low,
		   &lower, &interval, &n_ticks);

  assert (n_ticks > 0);
  assert (n_ticks < 12);

  //  dump_scale (&lower, &interval, n_ticks);

  assert (decimal_to_double (&lower) <= low);
  
  {
    struct decimal  l = lower;
    decimal_add (&l, &interval);
    assert (decimal_to_double (&l) > low);
  }

  {
    struct decimal  i = interval;

    decimal_int_multiply (&i, n_ticks - 1);

    decimal_add (&i, &lower);

    /* i now contains the upper bound minus one tick */
    assert (decimal_to_double (&i) < high);

    decimal_add (&i, &interval);

    assert (decimal_to_double (&i) >= high);
  }

}


int 
main (int argc UNUSED, char **argv UNUSED)
{
  test_range (0.2, 11);
  test_range (-0.2, 11);
  test_range (-10, 0.2);
  test_range (-10, -0.2);

  test_range (102, 50030); 
  test_range (0.00102, 0.0050030); 

  return 0;
}
