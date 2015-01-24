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

#include <config.h>

#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>

#include "libpspp/compiler.h"
#include "math/decimal.h"
#include <limits.h>
#include <float.h>
#include <math.h>

/* Canonicalise a string  holding the decimal representation of a number.
   For example, leading zeros left of the decimal point are removed, as are
   trailing zeros to the right.

   This function is used purely for testing, and need not and is not intended
   to be efficient.
 */
static char *
canonicalise_string (const char *s)
{
  char *out;
  char *dot = NULL;
  bool negative = false;
  char *p;
  char *temp = malloc (strlen (s) + 3);
  char *last_leading_zero = NULL;

  /* Strip leading - if present */
  if (*s == '-')
    {
      negative = true;
      s++;
    }
  
  strcpy (temp, "00");
  strcat (temp, s);

  char *first_trailing_zero = NULL;
  char *significant_digit = NULL;
  for (p = temp; *p; p++)
    {
      if (*p == '0' && dot == NULL && significant_digit == NULL)
	last_leading_zero = p;

      if (*p == '0' && first_trailing_zero == NULL)
	first_trailing_zero = p;

      if (*p == '.')
	{
	  dot = p;
	  first_trailing_zero = NULL;
	}

      if (*p >= '1' && *p <= '9')
	{
	  significant_digit = p;
	  first_trailing_zero = NULL;
	}
    }

  if (first_trailing_zero && dot)
    *first_trailing_zero = '\0';

  if (last_leading_zero)
    {
      /* Strip leading zeros */
      out = last_leading_zero + 1;

      /* But if we now start with . put a zero back again */
      if (dot == last_leading_zero + 1)
	out--;
    }


  if (negative)
    {
      out--;
      out[0] = '-';
    }
  
  if (!significant_digit)
    {
      *out = '0';
      *(out+1) = '\0';
    }
    

  return out;
}


/* Tests both the decimal_to_string function, and the decimal_input_from_string 
   function */
static void
test_run (const char *input)
  {
    struct decimal test;
    struct decimal number;
    decimal_init_from_string (&number, input);

    char *s = decimal_to_string (&number);
    char *canon = canonicalise_string (input);
    if (0 != strcmp (canon, s))
      {
	fprintf (stdout, "\"%s\" does not match \n\"%s\"\n", canon, s);
	exit (1);
      }

    decimal_init_from_string (&test, s);
    assert (0 == decimal_cmp (&test, &number));

    free (s);
  }


static void
test_can (const char *in, const char *soll)
{
  char *ist = canonicalise_string (in);
  if (0 != strcmp (soll, ist))
    {
      printf ("\"%s\" canonicalises to \"%s\" (should be \"%s\")\n", in, ist, soll);
    }
}


#if 0
static void
dump_scale (const struct decimal *low, const struct decimal *interval, int n_ticks)
{
  int i;
  struct decimal tick = *interval;
  printf ("Lowest: %s\n", decimal_to_string (low));
  for (i = 0; i <= n_ticks; ++i)
    {
      printf ("Tick %d: %s (%g)\n", i, decimal_to_string (&tick), decimal_to_double (&tick));
      decimal_add (&tick, interval);
    }
}
#endif



static void
test_ceil (double x)
{
  struct decimal dx;
  decimal_from_double (&dx, x);
  int act = decimal_ceil (&dx);
  int expected = ceil (x);
  
  assert (act == expected);
}

static void
test_floor (double x)
{
  struct decimal dx;
  decimal_from_double (&dx, x);
  int act = decimal_floor (&dx);
  int expected = floor (x);
  
  assert (act == expected);
}


static void
test_addition (const struct decimal *one_, const struct decimal *two)
{
  struct decimal one = *one_;
  
  decimal_add (&one, two);
  
  double dsum = decimal_to_double (&one);

  char sdsum1[256];
  char sdsum2[256];

  snprintf (sdsum1, 256, "%s", decimal_to_string (&one));
  snprintf (sdsum2, 256, "%g", dsum);

  assert (strcmp (sdsum1, sdsum2) == 0);
}


static void
test_multiplication (const struct decimal *d, int m)
{
  char b1[256];
  char b2[256];
  struct decimal dest = *d;
  double x = decimal_to_double (&dest);

  decimal_int_multiply (&dest, m);

  double y = decimal_to_double (&dest);

  snprintf (b1, 256, "%g", m * x);
  snprintf (b2, 256, "%g", y);
  assert (0 == strcmp (b1, b2));
}



int 
main (int argc UNUSED, char **argv UNUSED)
{
  /* Test that our canonicalise function works for all corner cases we
     can think of. */

  test_can ("500", "500");
  test_can ("5", "5");
  test_can ("-3", "-3");
  test_can ("-3.001", "-3.001");
  test_can ("-03.001", "-3.001");
  test_can ("-.0301", "-0.0301");
  test_can ("0314.09", "314.09");
  test_can ("0314.090", "314.09");
  test_can ("0314.0900340", "314.090034");
  test_can ("0.0", "0");
  test_can ("0.", "0");
  test_can (".0", "0");
  test_can ("-.1", "-0.1");
  test_can (".090", "0.09");
  test_can ("03410.098700", "3410.0987");
  test_can ("-03410.098700", "-3410.0987");

  /* Test the conversion functions */
  test_run ("-90000");
  test_run ("-3");
  test_run ("50001");
  test_run ("500");
  test_run ("350");
  test_run ("050");
  test_run ("4");
  test_run ("0");
  test_run (".45");
  test_run ("-.45");
  test_run ("666666666");
  test_run ("6000000000");
  test_run ("0.000000005");
  test_run ("0.00000000000000000000000000000000000000005");
  test_run ("0.0234");
  test_run ("0.234");
  test_run ("-0123.45600");

  test_ceil (5.21);
  test_ceil (-4.32);
  test_ceil (0);
  test_ceil (0.0009);

  test_floor (4.09);
  test_floor (-4.09);
  test_floor (0);
  test_floor (0.004);


  {
    struct decimal high = {2, 0};
    struct decimal low = {2, -1};

    test_addition (&high, &low);
  }


  {
    struct decimal high = {10, 0};
    struct decimal low = {2, -1};

    test_addition (&high, &low);
  }


  {
    struct decimal high = {10, 0};
    struct decimal low = {-2, -1};

    test_addition (&high, &low);
  }

  {
    struct decimal high = {12, -5};
    struct decimal low = {-2, -1};

    test_addition (&high, &low);
  }

  {
    struct decimal high = {-112, -1};
    struct decimal low = {2, -1};

    test_addition (&high, &low);
  }


  {
    struct decimal m = {10, 0};

    test_multiplication (&m, 11);
  }


  {
    struct decimal m = {ORD_MAX - 2, 0};

    test_multiplication (&m, 11);
  }


  {
    struct decimal m = {34, 0};

    test_multiplication (&m, 0);
  }

  {
    struct decimal m = {34, -20};

    test_multiplication (&m, 33);
  }

  {
    struct decimal m = {304, 2};

    test_multiplication (&m, -33);
  }

  return 0;
}
