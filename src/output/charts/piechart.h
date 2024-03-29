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

#ifndef PIECHART_H
#define PIECHART_H

#include "libpspp/str.h"
#include "output/chart.h"
#include "language/commands/freq.h"

struct piechart
  {
    struct chart chart;
    struct slice *slices;
    int n_slices;
  };

struct slice
  {
    struct string label;
    double magnitude;
  };

struct variable;

struct chart *piechart_create (const struct variable *var,
                                    const struct freq *, int n_slices);

/* This boilerplate for piechart, a subclass of chart, was
   autogenerated by mk-class-boilerplate. */

#include <assert.h>
#include "libpspp/cast.h"

extern const struct chart_class piechart_class;

/* Returns true if SUPER is a piechart, otherwise false. */
static inline bool
is_piechart (const struct chart *super)
{
  return super->class == &piechart_class;
}

/* Returns SUPER converted to piechart.  SUPER must be a piechart, as
   reported by is_piechart. */
static inline struct piechart *
to_piechart (const struct chart *super)
{
  assert (is_piechart (super));
  return UP_CAST (super, struct piechart, chart);
}

/* Returns INSTANCE converted to chart. */
static inline struct chart *
piechart_super (const struct piechart *instance)
{
  return CONST_CAST (struct chart *, &instance->chart);
}

/* Increments INSTANCE's reference count and returns INSTANCE. */
static inline struct piechart *
piechart_ref (const struct piechart *instance)
{
  return to_piechart (chart_ref (&instance->chart));
}

/* Decrements INSTANCE's reference count, then destroys INSTANCE if
   the reference count is now zero. */
static inline void
piechart_unref (struct piechart *instance)
{
  chart_unref (&instance->chart);
}

/* Returns true if INSTANCE's reference count is greater than 1,
   false otherwise. */
static inline bool
piechart_is_shared (const struct piechart *instance)
{
  return chart_is_shared (&instance->chart);
}

static inline void
piechart_submit (struct piechart *instance)
{
  chart_submit (&instance->chart);
}

#endif /* output/charts/piechart.h */
