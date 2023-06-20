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

   The kth order statistic of a statistical sample is equal to its kth-smallest
   value.  The minimum is the first order statistic and the maximum is the
   largest.  This code and data structure supplies infrastructure for
   higher-level statistics that rely on order statistics.  It is a kind of
   "abstract base class" that is not useful on its own.

   This is implemented here as a kind of "partial frequency table".  The
   order_stats_accumulate() and order_stats_accumulate_idx() functions
   effectively generate all of the frequency table entries for the variable,
   one by one, and pass them to the "accumulate" function, if any.  They can
   also record pairs of frequency tables entries surrounding desired target
   cumulative weights in 'k' data structures.

   Client use
   ==========

   The common pattern for clients to use statistics based on order statistics
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

/* A pair of adjacent frequency table entries.

   cc <= tc < cc_p1
*/
struct k
{
  /* Target cumulative weight.
     Set by the client before invoking order_stats_accumulate{,_idx}. */
  double tc;

  /* Lower order statistics. */
  double cc;                    /* Largest cumulative weight <= tc. */
  double c;                     /* Weight for data values equal to 'y'. */
  double y;                     /* Data value. */

  /* Upper order statistics. */
  double cc_p1;                 /* Smallest cumulative weight > tc. */
  double c_p1;                  /* Weight for data values equal to 'y_p1'. */
  double y_p1;                  /* Data value. */
};

/* Order statistics calculation data structure.  See the comment at the top of
   this file for usage details. */
struct order_stats
{
  struct statistic parent;

  void (*accumulate) (struct statistic *, const struct ccase *, double c, double cc, double y);

  struct k *k;
  size_t n_k;
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
