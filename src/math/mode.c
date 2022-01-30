/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2011, 2022 Free Software Foundation, Inc.

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

#include "math/mode.h"

#include "data/casereader.h"
#include "data/val-type.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"

#include "gl/xalloc.h"


static void
mode_destroy (struct statistic *stat)
{
  struct mode *mode = UP_CAST (stat, struct mode, parent.parent);
  free (mode);
}

static void
mode_accumulate (struct statistic *stat, const struct ccase *cx UNUSED,
                 double c, double cc UNUSED, double y)
{
  struct mode *mode = UP_CAST (stat, struct mode, parent.parent);
  if (c > mode->mode_weight)
    {
      mode->mode = y;
      mode->mode_weight = c;
      mode->n_modes = 1;
    }
  else if (c == mode->mode_weight)
    mode->n_modes++;
}

struct mode *
mode_create (void)
{
  struct mode *mode = xmalloc (sizeof *mode);
  *mode = (struct mode) {
    .parent = {
      .parent = {
        mode_destroy,
      },
      .accumulate = mode_accumulate,
    },
    .mode = SYSMIS,
  };
  return mode;
}
