/* PSPP - a program for statistical analysis.
   Copyright (C) 2014, 2015 Free Software Foundation, Inc.

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

#ifndef OUTPUT_CHARTS_SCATTERPLOT_H
#define OUTPUT_CHARTS_SCATTERPLOT_H 1

#include "output/chart.h"

/* Indices for the scatterplot_proto members */
enum
  {
    SP_IDX_X,    /* x value */
    SP_IDX_Y,    /* y value */
    SP_IDX_BY,   /* graph category for xy plot */
  };

/* A  scatterplot. */
struct scatterplot_chart
  {
    struct chart chart;
    struct casereader *data;
    struct variable *byvar;
    char *xlabel;
    char *ylabel;

    double y_min, y_max;
    double x_min, x_max;
    /* If the number of distinct values of byvar */
    /* exceeds a certain limit, the warning flag */
    /* is activated after the chart is drawn     */
    bool *byvar_overflow;
  };

struct scatterplot_chart *
scatterplot_create (struct casereader *,
                    const char *xlabel,
                    const char *ylabel,
                    const struct variable *,
                    bool *,
                    const char *label,
                    double xmin, double xmax, double ymin, double ymax);

/* This boilerplate for scatterplot_chart, a subclass of chart, was
   autogenerated by mk-class-boilerplate. */

#include <assert.h>
#include "libpspp/cast.h"

extern const struct chart_class scatterplot_chart_class;

/* Returns true if SUPER is a scatterplot_chart, otherwise false. */
static inline bool
is_scatterplot_chart (const struct chart *super)
{
  return super->class == &scatterplot_chart_class;
}

/* Returns SUPER converted to scatterplot_chart.  SUPER must be a scatterplot_chart, as
   reported by is_scatterplot_chart. */
static inline struct scatterplot_chart *
to_scatterplot_chart (const struct chart *super)
{
  assert (is_scatterplot_chart (super));
  return UP_CAST (super, struct scatterplot_chart, chart);
}

/* Returns INSTANCE converted to chart. */
static inline struct chart *
scatterplot_chart_super (const struct scatterplot_chart *instance)
{
  return CONST_CAST (struct chart *, &instance->chart);
}

/* Increments INSTANCE's reference count and returns INSTANCE. */
static inline struct scatterplot_chart *
scatterplot_chart_ref (const struct scatterplot_chart *instance)
{
  return to_scatterplot_chart (chart_ref (&instance->chart));
}

/* Decrements INSTANCE's reference count, then destroys INSTANCE if
   the reference count is now zero. */
static inline void
scatterplot_chart_unref (struct scatterplot_chart *instance)
{
  chart_unref (&instance->chart);
}

/* Returns true if INSTANCE's reference count is greater than 1,
   false otherwise. */
static inline bool
scatterplot_chart_is_shared (const struct scatterplot_chart *instance)
{
  return chart_is_shared (&instance->chart);
}

static inline void
scatterplot_chart_submit (struct scatterplot_chart *instance)
{
  chart_submit (&instance->chart);
}

#endif /* output/charts/scatterplot.h */
