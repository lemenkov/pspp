/*
PSPP - a program for statistical analysis.
Copyright (C) 2017 Free Software Foundation, Inc.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MATH_DISTRIBUTIONS_H
#define MATH_DISTRIBUTIONS_H 1

double ncdf_beta (double x, double a, double b, double lambda);
double npdf_beta (double x, double a, double b, double lambda);

double cdf_bvnor (double x0, double x1, double r);

double idf_fdist (double P, double a, double b);

#endif /* math/distributions.h */
