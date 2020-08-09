/*
PSPP - a program for statistical analysis.
Copyright (C) 2017 Free Software Foundation, Inc.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef BARCHART_DEF_H
#define BARCHART_DEF_H 1

#include <stdbool.h>

struct ag_func
{
  const char *name;
  const char *description;

  int arity;
  bool cumulative;
  double (*pre) (void);
  double (*calc) (double acc, double x, double w);
  double (*post) (double acc, double cc);
  double (*ppost) (double acc, double ccc);
};

extern const struct ag_func ag_func[];

extern const int N_AG_FUNCS;

#endif
