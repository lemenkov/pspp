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

#include "math/np.h"

#include <gsl/gsl_cdf.h>
#include <math.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/casewriter.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/misc.h"
#include "math/moments.h"

#include "gl/xalloc.h"

static void
destroy (struct statistic *stat)
{
  struct np *np = UP_CAST (stat, struct np, parent.parent);
  free (np);
}

static void
acc (struct statistic *s, const struct ccase *cx UNUSED,
     double c, double cc, double y)
{
  struct np *np = UP_CAST (s, struct np, parent.parent);
  double rank = np->prev_cc + (c + 1) / 2.0;

  double ns = gsl_cdf_ugaussian_Pinv (rank / (np->n + 1));

  double z = (y - np->mean) / np->stddev;

  double dns = z - ns;

  maximize (&np->ns_max, ns);
  minimize (&np->ns_min, ns);

  maximize (&np->dns_max, dns);
  minimize (&np->dns_min, dns);

  maximize (&np->y_max, y);
  minimize (&np->y_min, y);

  struct ccase *cp = case_create (casewriter_get_proto (np->writer));
  *case_num_rw_idx (cp, NP_IDX_Y) = y;
  *case_num_rw_idx (cp, NP_IDX_NS) = ns;
  *case_num_rw_idx (cp, NP_IDX_DNS) = dns;
  casewriter_write (np->writer, cp);

  np->prev_cc = cc;
}

/* Creates and returns a data structure whose accumulated results can be used
   to produce a normal probability plot.  The caller must supply the weighted
   sample size N and the mean MEAN and variance VAR of the distribution, then
   feed in the data with order_stats_accumulate() or
   order_stats_accumulate_idx().

   There is no function to produce the results, which appear in "struct np" for
   passing directly to np_plot_create() or dnp_plot_create().

   The caller must eventually destroy the returned structure, with
   statistic_destroy(). */
struct np *
np_create (double n, double mean, double var)
{
  struct caseproto *proto = caseproto_create ();
  for (size_t i = 0; i < n_NP_IDX; i++)
    proto = caseproto_add_width (proto, 0);
  struct casewriter *writer = autopaging_writer_create (proto);
  caseproto_unref (proto);

  struct np *np = xmalloc (sizeof *np);
  *np = (struct np) {
    .parent = {
      .parent = {
        .destroy = destroy,
      },
      .accumulate = acc,
    },
    .n = n,
    .mean = mean,
    .stddev = sqrt (var),
    .ns_min = DBL_MAX,
    .ns_max = -DBL_MAX,
    .dns_min = DBL_MAX,
    .dns_max = -DBL_MAX,
    .y_min = DBL_MAX,
    .y_max = -DBL_MAX,
    .writer = writer,
  };
  return np;
}
