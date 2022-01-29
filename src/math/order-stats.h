/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008, 2011 Free Software Foundation, Inc.

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

#ifndef __ORDER_STATS_H__
#define __ORDER_STATS_H__

/* Support for order statistics.

   This data structure supplies infrastructure for higher-level statistics that
   rely on order statistics.  It is a kind of "abstract base class" that is not
   useful on its own.  The common pattern for using the statistics based on it
   is this:

   - Create the higher-level statistic with, for example, percentile_create().

   - Feed in all the data with order_stats_accumulate() or
     order_stats_accumulate_idx(). The data must be in sorted order: if
     necessary, use one of the sorting functions from sort.h to sort them.

   - Obtain the desired results by examining the higher-level data structure or
     by calling an appropriate function, e.g. percentile_calculate().

   - Destroy the data structure with statistic_destroy().
*/

#include <stddef.h>
#include "data/missing-values.h"
#include "math/statistic.h"

struct casereader;
struct variable;

/*
  cc <= tc < cc_p1
*/
struct k
{
  double tc;
  double cc;
  double cc_p1;
  double c;
  double c_p1;
  double y;
  double y_p1;
};

/* Order statistics calculation data structure.  See the comment at the top of
   this file for usage details. */
struct order_stats
{
  struct statistic parent;
  int n_k;
  struct k *k;

  double cc;
};

void order_stats_accumulate_idx (struct order_stats **os, size_t n_os,
                                 struct casereader *reader,
                                 int weight_idx,
                                 int data_idx);
void order_stats_accumulate (struct order_stats **os, size_t n_os,
			     struct casereader *,
			     const struct variable *weight_var,
			     const struct variable *data_var,
			     enum mv_class exclude);

/* Debugging support. */
void order_stats_dump (const struct order_stats *);

#endif
