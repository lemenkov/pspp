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

#include "math/percentiles.h"

#include "data/casereader.h"
#include "data/val-type.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "math/order-stats.h"

#include "gl/xalloc.h"

/* Return the value of the percentile. */
double
percentile_calculate (const struct percentile *ptl)
{
  const struct k *k = &ptl->k;

  if (k->c == 0.0)
    return k->c_p1 == 0.0 ? SYSMIS : k->y_p1;
  else if (k->c_p1 == 0.0)
    return k->y;

  double tc = k->tc;   /* Wp or (W + 1)p */

  double g = (tc - k->cc) / k->c_p1;
  double g_star = tc - k->cc;

  switch (ptl->algorithm)
    {
    case PC_WAVERAGE:
    case PC_HAVERAGE:
      if (g_star >= 1.0)
        return k->y_p1;
      else if (k->c_p1 >= 1.0)
        return (1 - g_star) * k->y + g_star * k->y_p1;
      else
        return (1 - g) * k->y + g * k->y_p1;
      break;

    case PC_ROUND:
      if (k->c_p1 >= 1.0)
        return (g_star < 0.5) ? k->y : k->y_p1;
      else
        return (g < 0.5) ? k->y : k->y_p1;
      break;

    case PC_EMPIRICAL:
      if (g_star == 0)
        return k->y;
      else
        return k->y_p1;
      break;

    case PC_AEMPIRICAL:
      if (g_star == 0)
        return (k->y + k->y_p1)/ 2.0;
      else
        return k->y_p1;
      break;

    default:
      NOT_REACHED ();
      break;
    }

  NOT_REACHED ();

  return SYSMIS;
}


static void
destroy (struct statistic *stat)
{
  struct percentile *ptl = UP_CAST (stat, struct percentile, parent.parent);
  free (ptl);
}

/* Create the Pth percentile.
   W is the total sum of weights in the data set.
*/
struct percentile *
percentile_create (double p, double W, enum pc_alg algorithm)
{
  assert (p >= 0);
  assert (p <= 1.0);

  struct percentile *ptl = xmalloc (sizeof *ptl);
  *ptl = (struct percentile) {
    .parent = {
      .parent = {
        .destroy = destroy
      },
      .k = &ptl->k,
      .n_k = 1,
    },
    .ptile = p,
    .w = W,
    .algorithm = algorithm,
    .k = {
      .tc = (algorithm == PC_HAVERAGE ? W + 1.0 : W) * p,
      .y = SYSMIS,
      .y_p1 = SYSMIS
    },
  };
  return ptl;
}
