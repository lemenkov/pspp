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
#include <stdio.h>
#include "math/chart-geometry.h"
#include "libpspp/compiler.h"

struct range {
  double lower;
  double interval;
  int nticks;
};

struct range tv[] = {
  {       1000.0,            10.0,     10},
  {      10000.0,            10.0,     10},
  {     100000.0,            10.0,     10},
  {    1000000.0,            10.0,     10},
  {   10000000.0,            10.0,     10},
  {  100000000.0,            10.0,     10},
  {          0.1,            0.01,     10},
  {         0.01,           0.001,     10},
  {        0.001,          0.0001,     10},
  {       0.0001,         0.00001,     10},
  {      0.00001,       0.0000001,     10},
  {    0.0000001,      0.00000001,     10},
  {         -5.0,             1.0,     10},
  {         -5.0,             0.5,     10},
  {         -5.0,             0.2,      9},
  {         -5.0,             2.0,     10},
  {         -0.5,             0.1,      9},
  {      0.975E9,         0.005E9,      9},
  {      0.970E9,          0.01E9,      9},
  {         -4E7,             1E7,      9},
  {         -3E7,           0.5E7,      9},
  {    1.001E-95,      0.0002E-95,     10},
  {     1.001E98,       0.0002E98,     10},
  {         5984,         0.00001,     10},
  {         3E33,           1E-22,     10},
  {         3E33,            1000,     10},
  {          0.1,           2E-42,     10},
  {          0.0,             0.0,     -1}
};

int
main (int argc UNUSED, char **argv UNUSED)
{
  char *fs;
  double scale;
  int i = 0;
  double lower, interval;
  int nticks;

  for(i=0;tv[i].nticks > 0;i++)
    {
      lower = tv[i].lower;
      interval = tv[i].interval;
      nticks = tv[i].nticks;
      fs = chart_get_ticks_format (lower, interval, nticks, &scale);
      printf("lower: %lg, interval: %lg, nticks: %d, fs: %s, scale: %lg, ex: ",
	     lower, interval, nticks, fs, scale);
      printf(fs,(lower + 3 * interval)*scale);
      printf(", ex 2: ");
      printf(fs,(lower + 4 * interval)*scale);
      printf("\n");
      free(fs);
    }

  return 0;
}
