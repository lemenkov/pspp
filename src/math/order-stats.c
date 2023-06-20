/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2011 Free Software Foundation, Inc.

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

#include "math/order-stats.h"

#include <math.h>
#include <string.h>

#include "data/casereader.h"
#include "data/val-type.h"
#include "data/variable.h"
#include "libpspp/assertion.h"

#include "gl/xalloc.h"

#if 0

#include <stdio.h>

static void
order_stats_dump_k1 (const struct order_stats *os)
{
  struct k *k = &os->k[0];
  printf ("K1: tc %g; c %g cc %g ccp %g\n",
          k->tc, k->c, k->cc, k->cc_p1);

}

static void
order_stats_dump_k2 (const struct order_stats *os)
{
  struct k *k = &os->k[1];
  printf ("K2: tc %g; c %g cc %g ccp %g\n",
          k->tc, k->c, k->cc, k->cc_p1);
}


void
order_stats_dump (const struct order_stats *os)
{
  order_stats_dump_k1 (os);
  order_stats_dump_k2 (os);
}

#endif

static void
update_k_values (const struct ccase *cx, double y_i, double c_i, double cc_i,
                 struct order_stats **os, size_t n_os)
{
  for (size_t j = 0; j < n_os; ++j)
    {
      struct order_stats *tos = os[j];
      struct statistic  *stat = &tos->parent;

      for (struct k *k = tos->k; k < &tos->k[tos->n_k]; ++k)
        {
          /* Update 'k' lower values. */
          if (cc_i <= k->tc)
            {
              k->cc = cc_i;
              k->c = c_i;
              k->y = y_i;
            }

          /* Update 'k' upper values. */
          if (cc_i > k->tc && k->c_p1 == 0)
            {
              k->cc_p1 = cc_i;
              k->c_p1 = c_i;
              k->y_p1 = y_i;
            }
        }

      if (tos->accumulate)
        tos->accumulate (stat, cx, c_i, cc_i, y_i);
    }
}

/* Reads all the cases from READER and accumulates their data into the N_OS
   order statistics in OS, taking data from case index DATA_IDX and weights
   from case index WEIGHT_IDX.  WEIGHT_IDX may be -1 to assume weight 1.

   This function must be used only once per order_stats.

   Takes ownership of READER.

   Data values must be numeric and sorted in ascending order.  Use
   sort_execute_1var() or related functions to sort unsorted data before
   passing it to this function. */
void
order_stats_accumulate_idx (struct order_stats **os, size_t n_os,
                            struct casereader *reader,
                            int weight_idx, int data_idx)
{
  struct ccase *cx;
  struct ccase *prev_cx = NULL;
  double prev_value = -DBL_MAX;

  double cc_i = 0;
  double c_i = 0;

  for (; (cx = casereader_read (reader)) != NULL; case_unref (cx))
    {
      const double weight = weight_idx == -1 ? 1.0 : case_num_idx (cx, weight_idx);
      if (weight == SYSMIS || weight <= 0)
        continue;

      const double this_value = case_num_idx (cx, data_idx);
      if (!isfinite (this_value) || this_value == SYSMIS)
        continue;

      if (!prev_cx || this_value > prev_value)
        {
          if (prev_cx)
            update_k_values (prev_cx, prev_value, c_i, cc_i, os, n_os);
          prev_value = this_value;
          c_i = weight;
        }
      else
        {
          /* Data values must be sorted. */
          assert (this_value == prev_value);

          c_i += weight;
        }

      cc_i += weight;
      case_unref (prev_cx);
      prev_cx = case_ref (cx);
    }
  if (prev_cx)
    {
      update_k_values (prev_cx, prev_value, c_i, cc_i, os, n_os);
      case_unref (prev_cx);
    }

  casereader_destroy (reader);
}

/* Reads all the cases from READER and accumulates their data into the N_OS
   order statistics in OS, taking data from DATA_VAR and weights from
   WEIGHT_VAR.  Drops cases for which the value of DATA_VAR is missing
   according to EXCLUDE.  WEIGHT_VAR may be NULL to assume weight 1.

   This function must be used only once per order_stats.

   Takes ownership of READER.

   DATA_VAR must be numeric and sorted in ascending order.  Use
   sort_execute_1var() or related functions to sort unsorted data before
   passing it to this function. */
void
order_stats_accumulate (struct order_stats **os, size_t n_os,
                        struct casereader *reader,
                        const struct variable *weight_var,
                        const struct variable *data_var,
                        enum mv_class exclude)
{
  reader = casereader_create_filter_missing (reader, &data_var, 1,
                                             exclude, NULL, NULL);

  order_stats_accumulate_idx (os, n_os, reader,
                              weight_var ? var_get_dict_index (weight_var) : -1,
                              var_get_dict_index (data_var));
}
