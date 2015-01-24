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
#include "math/chart-geometry.h"
#include "math/decimal.h"
#include "libpspp/compiler.h"

const double in[20] =
  {
    0.00648687,
    728815,
    8.14431e-07,
    77611.4,
    3.33497,
    180.426,
    0.676168,
    2.00744e+08,
    14099.3,
    19.5186,
    1.17473e-07,
    166337,
    0.00163644,
    1.94724e-09,
    2.31564e-06,
    3.10674e+06,
    5.10314e-05,
    1.95101,
    1.40884e+09,
    78217.6
  };

int 
main (int argc UNUSED, char **argv UNUSED)
{
  int i;
  for (i = 0; i < 20; ++i)
    {
      struct decimal dout;
      chart_rounded_tick (in[i], &dout);
      
      printf ("%g %s\n", in[i], decimal_to_string (&dout));
    }

  return 0;
}

