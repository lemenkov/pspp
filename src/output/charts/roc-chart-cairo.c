/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011 Free Software Foundation, Inc.

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

#include "output/charts/roc-chart.h"

#include "data/case.h"
#include "data/casereader.h"
#include "language/commands/roc.h"
#include "output/cairo-chart.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

void
xrchart_draw_roc (const struct chart *chart, cairo_t *cr,
                  struct xrchart_geometry *geom)
{
  const struct roc_chart *rc = to_roc_chart (chart);
  size_t i;

  xrchart_write_title (cr, geom, _("ROC Curve"));
  xrchart_write_xlabel (cr, geom, _("1 - Specificity"));
  xrchart_write_ylabel (cr, geom, _("Sensitivity"));

  if (! xrchart_write_xscale (cr, geom, 0, 1))
    return;
  if (! xrchart_write_yscale (cr, geom, 0, 1))
    return;

  if (rc->reference)
    {
      xrchart_line (cr, geom, 1.0, 0,
                    0.0, 1.0,
                    XRCHART_DIM_X);
    }

  for (i = 0; i < rc->n_vars; ++i)
    {
      const struct roc_var *rv = &rc->vars[i];
      struct casereader *r = casereader_clone (rv->cutpoint_reader);
      struct ccase *cc;

      xrchart_vector_start (cr, geom, rv->name);
      for (; (cc = casereader_read (r)) != NULL; case_unref (cc))
        {
          double se = case_num_idx (cc, ROC_TP);
          double sp = case_num_idx (cc, ROC_TN);

          se /= case_num_idx (cc, ROC_FN) + case_num_idx (cc, ROC_TP);
          sp /= case_num_idx (cc, ROC_TN) + case_num_idx (cc, ROC_FP);

          xrchart_vector (cr, geom, 1 - sp, se);
        }
      xrchart_vector_end (cr, geom);
      casereader_destroy (r);
    }

  xrchart_write_legend (cr, geom);
}

