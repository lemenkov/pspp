/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2009, 2011 Free Software Foundation, Inc.

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

#include <assert.h>
#include <stdlib.h>

#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/str.h"
#include "output/chart-provider.h"
#include "output/output-item.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct chart *
chart_ref (const struct chart *chart_)
{
  struct chart *chart = CONST_CAST (struct chart *, chart_);
  assert (chart->ref_cnt > 0);
  chart->ref_cnt++;
  return chart;
}

void
chart_unref (struct chart *chart)
{
  if (chart)
    {
      assert (chart->ref_cnt > 0);
      if (!--chart->ref_cnt)
        {
          char *title = chart->title;
          chart->class->destroy (chart);
          free (title);
        }
    }
}

bool
chart_is_shared (const struct chart *chart)
{
  assert (chart->ref_cnt > 0);
  return chart->ref_cnt > 1;
}

/* Initializes CHART as a chart of the specified CLASS.  The new chart
   initially has the specified TITLE, which may be NULL if no title is yet
   available.  The caller retains ownership of TITLE.

   A chart is abstract, that is, a plain chart is not useful on its own.  Thus,
   this function is normally called from the initialization function of some
   subclass of chart. */
void
chart_init (struct chart *chart, const struct chart_class *class,
            const char *title)
{
  *chart = (struct chart) {
    .ref_cnt = 1,
    .class = class,
    .title = xstrdup_if_nonnull (title),
  };
}

/* Returns CHART's title, which is a null pointer if no title has been set. */
const char *
chart_get_title (const struct chart *chart)
{
  return chart->title;
}

/* Sets CHART's title to TITLE, replacing any previous title.  Specify NULL for
   TITLE to clear any title from CHART.  The caller retains ownership of
   TITLE.

   This function may only be used on a chart that is unshared. */
void
chart_set_title (struct chart *chart, const char *title)
{
  assert (!chart_is_shared (chart));
  free (chart->title);
  chart->title = xstrdup_if_nonnull (title);
}

/* Submits CHART to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
chart_submit (struct chart *chart)
{
  if (chart)
    output_item_submit (chart_item_create (chart));
}
