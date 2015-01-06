/* PSPP - a program for statistical analysis.
   Copyright (C) 2015 Free Software Foundation, Inc.

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


#ifndef DECIMAL_H
#define DECIMAL_H

/* This module provides a rudimentary floating point implementation using a decimal
   base.   It can be used for floating point calculations where it is desirable that
   the result is representable in decimal base.

   Any of the functions may set the static variable dec_warning to non-zero if a
   loss of precision or other issue occurs.

   This does not purport to be efficient, either in time or space.
 */

#include <stdio.h>
#include <stdbool.h>

#include <limits.h>

#define DEC_PREC 1 /* operation resulted in a loss of precision */
extern int dec_warning;


#define ORDINATE_LONG
#define MANTISSA_LONG

#ifdef ORDINATE_SHORT
typedef short ord_t;
static const short ORD_MAX = SHRT_MAX;
#define PR_ORD "%d"
#endif

#ifdef ORDINATE_INT
typedef int ord_t;
static const int ORD_MAX = INT_MAX;
#define PR_ORD "%d"
#endif

#ifdef ORDINATE_LONG
typedef long ord_t;
static const long ORD_MAX = LONG_MAX;
#define PR_ORD "%ld"
#endif



#ifdef MANTISSA_SHORT
typedef short mant_t;
static const short MANT_MAX = SHRT_MAX;
#define PR_MANT "%d"
#endif

#ifdef MANTISSA_INT
typedef int mant_t;
static const int MANT_MAX = INT_MAX;
#define PR_MANT "%d"
#endif

#ifdef MANTISSA_LONG
typedef long mant_t;
static const long MANT_MAX = LONG_MAX;
#define PR_MANT "%ld"
#endif



#define MANT_MIN	(-MANT_MAX - 1)
#define ORD_MIN	        (-ORD_MAX - 1)

struct decimal 
{
  ord_t ordinate;
  mant_t mantissa;
};

void normalise (struct decimal *d1, struct decimal *d2);
void decimal_init (struct decimal *dec, ord_t ord, mant_t mant);
void decimal_init_from_string (struct decimal *dec, const char *s);
int decimal_cmp (const struct decimal *d1, const struct decimal *d2);
void decimal_int_multiply (struct decimal *dest, ord_t m);
void decimal_int_divide (struct decimal *dest, ord_t m);
void decimal_divide (struct decimal *dest, const struct decimal *src);
void decimal_show (const struct decimal *dec, FILE *f);
char *decimal_to_string (const struct decimal *dec);

void decimal_add (struct decimal *dest, const struct decimal *);
void decimal_subtract (struct decimal *dest, const struct decimal *);
ord_t decimal_ceil (const struct decimal *d);
ord_t decimal_floor (const struct decimal *d);
mant_t dec_log10 (const struct decimal *d);


void decimal_from_double (struct decimal *dec, double x);
double decimal_to_double (const struct decimal *dec);



#endif
