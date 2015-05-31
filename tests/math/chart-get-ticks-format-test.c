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
  double max;
  double min;
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
  {      0.00001,        0.000001,     10},
  { 0.0000100001,         0.00001,     10},
  {  100000010.0,     100000000.0,     10},
  {     100000.0,       -500000.0,     10},
  {          5.0,            -5.0,     10},
  {          5.0,          -4.999,     10},
  {          5.0,          -4.999,      9},
  {          5.0,             0.0,     10},
  {          0.0,            -5.0,      9},
  {    1.001E-95,         1.0E-95,     10},
  {     1.001E98,          1.0E98,     10},
  {     1.001E33,         1.0E-22,     10},
  {          0.0,             0.0,     -1}
};

int
main (int argc UNUSED, char **argv UNUSED)
{
  char *fs;
  double scale;
  int i = 0;
  double max, min;
  int nticks;

  for(i=0;tv[i].nticks > 0;i++)
    {
      max = tv[i].max;
      min = tv[i].min;
      nticks = tv[i].nticks;
      fs = chart_get_ticks_format (max, min, nticks, &scale);
      printf("max: %lg, min: %lg, nticks: %d, fs: %s, scale: %lg, example: ",
	     max, min, nticks, fs, scale);
      printf(fs,((max-min)/2.0+min)*scale);
      printf("\n");
      free(fs);
    }

  return 0;
}
