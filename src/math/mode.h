/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2008, 2009, 2022 Free Software Foundation, Inc.

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

#ifndef MATH_MODE_H
#define MATH_MODE_H 1

#include <stddef.h>

#include "order-stats.h"

/* To calculate the mode:

   - Create a "struct mode" with mode_create().
   - Feed in the data with order_stats_accumulate() or
     order_stats_accumulate_idx().  The data must be in sorted order: if
     necessary, use one of the sorting functions from sort.h to sort them.
   - The members of "struct mode" then designate the mode.
   - Destroy the data structure with statistic_destroy().
*/

struct mode
{
  struct order_stats parent;

  /* These are initialized by order_stats_accumulate{_idx}(). */
  double mode;          /* The value of the smallest mode, if 'n_modes' > 0. */
  double mode_weight;   /* The weight of each mode, if 'n_modes' > 0. */
  size_t n_modes;       /* The number of modes. */
};

struct mode *mode_create (void);

#endif /* mode.h */
