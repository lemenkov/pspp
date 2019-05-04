/* PSPP - a program for statistical analysis.
   Copyright (C) 2019 Free Software Foundation, Inc.

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

#ifndef __SHAPIRO_WILK_H__
#define __SHAPIRO_WILK_H__

#include "order-stats.h"

struct shapiro_wilk
{
  struct order_stats parent;
  int n;
  double a_n1;
  double a_n2;
  double epsilon;

  double mean;
  double numerator;
  double denominator;

  bool warned;
};


struct shapiro_wilk * shapiro_wilk_create (int n, double mean);

double shapiro_wilk_calculate (const struct shapiro_wilk *sw);

double shapiro_wilk_significance (double n, double w);



#endif
