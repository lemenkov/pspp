/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2015 Free Software Foundation, Inc.

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


#ifndef CHART_GEOMETRY_H
#define CHART_GEOMETRY_H

void chart_get_scale (double high, double low,
		      double *lower, double *interval, int *n_ticks);

char *
chart_get_ticks_format (const double lower, const double interval, const unsigned int nticks,
			double *scale);

#endif
