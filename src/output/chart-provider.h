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

#ifndef OUTPUT_CHART_PROVIDER_H
#define OUTPUT_CHART_PROVIDER_H 1

#include "output/chart.h"

struct chart_class
  {
    void (*destroy) (struct chart *);
  };

void chart_init (struct chart *, const struct chart_class *,
                 const char *title);

#endif /* output/chart-provider.h */
