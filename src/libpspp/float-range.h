/* PSPP - a program for statistical analysis.
   Copyright (C) 2023 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_FLOAT_RANGE_H
#define LIBPSPP_FLOAT_RANGE_H 1

#include <float.h>
#include <limits.h>

#ifndef LONG_WIDTH
#error                          /* Defined in C2x <limits.h> */
#endif

/* Maximum positive integer 'double' represented with no loss of precision
   (that is, with unit precision).

   The maximum negative integer with this property is -DBL_UNIT_MAX. */
#if DBL_MANT_DIG == 53          /* 64-bit double */
#define DBL_UNIT_MAX 9007199254740992.0
#elif DBL_MANT_DIG == 64        /* 80-bit double */
#define DBL_UNIT_MAX 18446744073709551616.0
#elif DBL_MANT_DIG == 113       /* 128-bit double */
#define DBL_UNIT_MAX 10384593717069655257060992658440192.0
#else
#error "Please define DBL_UNIT_MAX for your system (as 2**DBL_MANT_DIG)."
#endif

/* Intersection of ranges [LONG_MIN,LONG_MAX] and [-DBL_UNIT_MAX,DBL_UNIT_MAX],
   as a range of 'long's.  This range is the (largest contiguous) set of
   integer values that can be safely converted between 'long' and 'double'
   without loss of precision. */
#if DBL_MANT_DIG < LONG_WIDTH - 1
#define DBL_UNIT_LONG_MIN ((long) -DBL_UNIT_MAX)
#define DBL_UNIT_LONG_MAX ((long) DBL_UNIT_MAX)
#else
#define DBL_UNIT_LONG_MIN LONG_MIN
#define DBL_UNIT_LONG_MAX LONG_MAX
#endif

#endif /* float-range.h */
