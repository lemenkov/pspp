/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008, 2009 Free Software Foundation, Inc.

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

#ifndef __PERCENTILES_H__
#define __PERCENTILES_H__

#include <stddef.h>

#include "order-stats.h"

/* To calculate a percentile:

   - Create a "struct percentile" with percentile_create().
   - Feed in the data with order_stats_accumulate() or
     order_stats_accumulate_idx().  The data must be in sorted order: if
     necessary, use one of the sorting functions from sort.h to sort them.
   - Obtain the percentile with percentile_calculate().
   - Destroy the data structure with statistic_destroy().
*/

/* The algorithm used to calculate percentiles */
enum pc_alg {
  PC_NONE=0,
  PC_HAVERAGE,
  PC_WAVERAGE,
  PC_ROUND,
  PC_EMPIRICAL,
  PC_AEMPIRICAL
} ;

struct percentile
{
  struct order_stats parent;

  double ptile;
  double w;

  /* Mutable */
  double g1;
  double g1_star;

  double g2;
  double g2_star;

  struct k k[2];
};

struct percentile *percentile_create (double p, double W);
double percentile_calculate (const struct percentile *, enum pc_alg);

#endif  /* percentiles.h */
