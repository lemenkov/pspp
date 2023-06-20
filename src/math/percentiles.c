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
percentile_calculate (const struct percentile *ptl, enum pc_alg alg)
{
  struct percentile *mutable = CONST_CAST (struct percentile *, ptl);
  const struct order_stats *os = &ptl->parent;

  if (ptl->g1 == SYSMIS)
    mutable->g1 = (os->k[0].tc - os->k[0].cc) / os->k[0].c_p1;

  if (ptl->g1_star == SYSMIS)
    mutable->g1_star = os->k[0].tc - os->k[0].cc;

  if (ptl->g2 == SYSMIS)
    {
      if (os->k[1].c == 0)
        mutable->g2 = os->k[1].tc / os->k[1].c_p1;
      else if (os->k[1].c_p1 == 0)
        mutable->g2 = 0;
      else
        mutable->g2 = (os->k[1].tc - os->k[1].cc) / os->k[1].c_p1;
    }

  if (ptl->g2_star == SYSMIS)
    {
      if (os->k[1].c == 0)
        mutable->g2_star = os->k[1].tc;
      else if (os->k[1].c_p1 == 0)
        mutable->g2_star = 0;
      else
        mutable->g2_star = os->k[1].tc - os->k[1].cc;
    }

  switch (alg)
    {
    case PC_WAVERAGE:
      if (ptl->g1_star >= 1.0)
        return os->k[0].y_p1;
      else
        {
          double a = (os->k[0].y == SYSMIS) ? 0 : os->k[0].y;

          if (os->k[0].c_p1 >= 1.0)
            return (1 - ptl->g1_star) * a + ptl->g1_star * os->k[0].y_p1;
          else
            return (1 - ptl->g1) * a + ptl->g1 * os->k[0].y_p1;
        }
      break;

    case PC_ROUND:
      {
        double a = (os->k[0].y == SYSMIS) ? 0 : os->k[0].y;

        if (os->k[0].c_p1 >= 1.0)
          return (ptl->g1_star < 0.5) ? a : os->k[0].y_p1;
        else
          return (ptl->g1 < 0.5) ? a : os->k[0].y_p1;
      }
      break;

    case PC_EMPIRICAL:
      if (ptl->g1_star == 0)
        return os->k[0].y;
      else
        return os->k[0].y_p1;
      break;

    case PC_HAVERAGE:
      if (ptl->g2_star >= 1.0)
        {
          return os->k[1].y_p1;
        }
      else
        {
          double a = (os->k[1].y == SYSMIS) ? 0 : os->k[1].y;

          if (os->k[1].c_p1 >= 1.0)
            {
              if (ptl->g2_star == 0)
                return os->k[1].y;

              return (1 - ptl->g2_star) * a + ptl->g2_star * os->k[1].y_p1;
            }
          else
            {
              return (1 - ptl->g2) * a + ptl->g2 * os->k[1].y_p1;
            }
        }

      break;

    case PC_AEMPIRICAL:
      if (ptl->g1_star == 0)
        return (os->k[0].y + os->k[0].y_p1)/ 2.0;
      else
        return os->k[0].y_p1;
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
percentile_create (double p, double W)
{
  assert (p >= 0);
  assert (p <= 1.0);

  struct percentile *ptl = xmalloc (sizeof *ptl);
  *ptl = (struct percentile) {
    .parent = {
      .parent = {
        .destroy = destroy
      },
      .k = ptl->k,
      .n_k = 2,
    },
    .ptile = p,
    .w = W,
    .g1 = SYSMIS,
    .g1_star = SYSMIS,
    .g2 = SYSMIS,
    .g2_star = SYSMIS,
    .k[0] = { .tc = W * p, .y = SYSMIS, .y_p1 = SYSMIS },
    .k[1] = { .tc = (W + 1.0) * p, .y = SYSMIS, .y_p1 = SYSMIS },
  };
  return ptl;
}
