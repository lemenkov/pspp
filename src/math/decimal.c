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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "libpspp/i18n.h"

#include "decimal.h"

int dec_warning;

static bool
down (struct decimal *dec)
{
  if (dec->ordinate % 10 == 0 &&  dec->mantissa < MANT_MAX - 1)
    {
      dec->ordinate /= 10;
      dec->mantissa++;
      return true;
    }
  
  return false;
}

static bool
up (struct decimal *dec)
{
  if (llabs (dec->ordinate) < ORD_MAX / 10   &&   dec->mantissa > MANT_MIN)
    {
      dec->ordinate *= 10;
      dec->mantissa--;
      return true;
    }
  return false;
}


/* Reduce the absolute value of the ordinate to the smallest possible,
   without loosing precision */
static void 
reduce (struct decimal *dec)
{
  if (dec->ordinate == 0)
    {
      dec->mantissa = 0;
      return;
    }
    
  while (dec->ordinate % 10 == 0)
    {
      if (! down (dec))
	break;
    }
}

/* Attempt to make the mantissas of BM and SM equal.
   Prerequisite: the mantissa SM must be no greater than that of BM.
 */
static void 
normalisebs (struct decimal *sm, struct decimal *bm)
{
  while (sm->mantissa < bm->mantissa)
    {
      if (down (sm))
	;
      else if (up (bm))
	;
      else
	{
	  dec_warning = DEC_PREC;
	  break;
	}
    }

  while (sm->mantissa < bm->mantissa)
    {
      sm->ordinate /= 10;
      sm->mantissa++;
    }
}


/* arrange d1 and d2 such that thier mantissas are equal */
void 
normalise (struct decimal *d1, struct decimal *d2)
{
  normalisebs (d1, d2);
  normalisebs (d2, d1);
}



/* Return log base 10 of D */
mant_t 
dec_log10 (const struct decimal *d_)
{
  struct decimal d = *d_;

  while (llabs (d.ordinate) > 0)
    {
      d.ordinate /= 10;
      d.mantissa++;
    }

  return d.mantissa;
}



/* Return the smallest integer >= d */
static ord_t
decimal_ceil_pos (const struct decimal *d)
{
  mant_t m = d->mantissa;
  ord_t o = d->ordinate;

  assert (d->ordinate >= 0);
  
  while (m > 0)
    {
      o *= 10;
      m--;
    }

  while (m < 0)
    {
      bool flag = o % 10;
      o /= 10;
      if (flag)
	o++;
      m++;
    }

  return o;
}

/* Return the largest integer <= d */
static ord_t
decimal_floor_pos (const struct decimal *d)
{
  mant_t m = d->mantissa;
  ord_t o = d->ordinate;

  assert (d->ordinate >= 0);

  while (m > 0)
    {
      m--;
      o *= 10;
    }

  while (m < 0)
    {
      m++;
      o /= 10;
    }
  

  return o;
}

/* Return the smallest integer which is no less than D.
   (round towards minus infinity) */
ord_t
decimal_floor (const struct decimal *d)
{
  if (d->ordinate >= 0)
    return decimal_floor_pos (d);
  else
    {
      struct decimal dd = *d;
      dd.ordinate = llabs (dd.ordinate);
      return -decimal_ceil_pos (&dd);
    }
}

/* Return the largest integer which is no greater than D.
   (round towards plus infinity) */
ord_t
decimal_ceil (const struct decimal *d)
{
  if (d->ordinate >= 0)
    return decimal_ceil_pos (d);
  else
    {
      struct decimal dd = *d;
      dd.ordinate = llabs (dd.ordinate);
      return -decimal_floor_pos (&dd);
    }
}

/* Add SRC onto DEST */
void
decimal_add (struct decimal *dest, const struct decimal *src_)
{
  struct decimal src = *src_;

  src.ordinate = -src.ordinate;

  decimal_subtract (dest, &src);
}

/* Subtract SRC from DEST */
void
decimal_subtract (struct decimal *dest, const struct decimal *src_)
{
  struct decimal src = *src_;

  normalise (dest, &src);

  bool dest_neg = dest->ordinate < 0;
  bool src_neg = src.ordinate < 0;

  bool expected_neg = dest_neg * src_neg;
  
  if (dest->ordinate == src.ordinate)
    {
      expected_neg = 0;
    }
  else if (llabs (src.ordinate) > llabs (dest->ordinate))
    {
      if (dest_neg == src_neg)
	expected_neg = !expected_neg;
    }

  dest->ordinate -= src.ordinate;

  bool result_neg = dest->ordinate < 0;

  if (expected_neg != result_neg)
    {
      /* The operation has resulted in an overflow.
	 To resolve this, undo the operation, 
	 reduce the precision and try again */

      dest->ordinate += src.ordinate;

      dest->ordinate /= 10;
      src.ordinate /= 10;

      dest->mantissa ++;
      src.mantissa ++;

      dest->ordinate -= src.ordinate;
    }

  reduce (dest);

}

/* Initialise DEC with ordinate ORD and mantissa MANT */
void
decimal_init (struct decimal *dec, ord_t ord, mant_t mant)
{
  dec->ordinate = ord;
  dec->mantissa = mant;
  reduce (dec);
}


/*
  Compare D1 and D2.

  Returns zero if equal, +1 if D1 > D2 and -1 if D1 < D2
*/
int
decimal_cmp (const struct decimal *d1, const struct decimal *d2)
{
  struct decimal td1 = *d1;
  struct decimal td2 = *d2;
  
  normalise (&td1, &td2);

  if (td1.ordinate < td2.ordinate)
    return -1;

  return (td1.ordinate > td2.ordinate);
}


/* Multiply DEST by M */
void
decimal_int_multiply (struct decimal *dest, ord_t m)
{
  if (m != 0)
    while (llabs (dest->ordinate) > llabs (ORD_MAX / m))
      {
        dest->ordinate /= 10;
        dest->mantissa++;
      }

  dest->ordinate *= m;

  reduce (dest);
}


/* Divide DEST by M */
void
decimal_int_divide (struct decimal *dest, ord_t m)
{
  while (dest->ordinate % m)
    {
      if (labs (dest->ordinate) > ORD_MAX / 10)
	{
	  dec_warning = DEC_PREC;
	  break;
	}
      up (dest);
    }
  dest->ordinate /= m;
}

/* Divide DEST by SRC */
void
decimal_divide (struct decimal *dest, const struct decimal *src)
{
  while (dest->ordinate % src->ordinate)
    {
      if (labs (dest->ordinate) > ORD_MAX / 10)
	{
	  dec_warning = DEC_PREC;
	  break;
	}
      up (dest);
    }

  dest->ordinate /= src->ordinate;
  dest->mantissa -= src->mantissa;
}

/* Print the value of DEC to F.  Probably useful only for debugging */
void
decimal_show (const struct decimal *dec, FILE *f)
{
  fprintf (f, PR_ORD " x 10^" PR_MANT "\n", dec->ordinate, dec->mantissa);
}


/* Reverse the characters in string S which has length LEN */
static void 
reverse (char *s, int len)
{
  int i;
  for (i = 0; i < len / 2; ++i)
    {
      char temp = s[len - i - 1];
      s[len - i - 1] = s[i];
      s[i] = temp;
    }
}

/* Return a string representation of DEC on the heap.
   The caller is responsible for freeing the string */
char *
decimal_to_string (const struct decimal *dec)
{
  int cap = 16;
  int len = 0;
  char *s = calloc (cap, 1);
  ord_t ordinate = dec->ordinate;

  while (len < dec->mantissa)
    {
      s[len++] = '0';
      if (len >= cap) s = realloc (s, cap <<= 1);
    }

  while (ordinate)
    {
      s[len++] = labs (ordinate % 10) + '0';
      if (len >= cap) s = realloc (s, cap <<= 1);
      ordinate /= 10;
    }

  if (ordinate < 0)
      ordinate = -ordinate;

  while (len < -dec->mantissa)
    {
      s[len++] = '0';
      if (len >= cap) s = realloc (s, cap <<= 1);
    }

  if (dec->mantissa < 0 )
    {
      if (len <= -dec->mantissa)
	{
	  s[len++] = get_system_decimal ();
	  if (len >= cap) s = realloc (s, cap <<= 1);
	  s[len++] = '0';
	  if (len >= cap) s = realloc (s, cap <<= 1);
	}
      else
	{
	  int i;
	  if (len >= cap) s = realloc (s, cap <<= 1);
	  for (i = len - 1 ; i >= -dec->mantissa ; --i)
	    s[i + 1] = s[i];
	  s[i + 1] = get_system_decimal ();
	  len++;
	}
    }

  if (dec->ordinate < 0)
    {
      s[len++] = '-';
      if (len >= cap) s = realloc (s, cap <<= 1);
    }


  reverse (s, len);

  {
    int abs_len = len;
    if (dec->ordinate < 0)
      abs_len--;

    while (abs_len++ <= dec->mantissa)
      {
	s[len++] = '0';
	if (len >= cap) s = realloc (s, cap <<= 1);
      }
  }
  
  return s;
}


/* Initialise DECIMAL from INPUT.
   INPUT should be a convential decimal representation.
 */
void
decimal_init_from_string (struct decimal *decimal, const char *input)
{
  ord_t ordinate = 0;

  int point = -1;
  int lsd = -1;
  int fsd = -1;
  int i = 0;
  int len = 0;
  int sign = 1;

  const char *p;

  for (p = input; *p ; p++)
    {
      if (*p == '-')
	{
	  sign = -1;
	}
      else if (*p == get_system_decimal ())
	{
	  assert (point == -1);
	  point = i;
	}
      else if (*p > '0' && *p <= '9')
	{
	  lsd = i;
	  if (fsd == -1)
	    fsd = i;
	}
      else if (*p == '0')
	/* ignore */
	;
      else 
	{
	  fprintf (stderr, "Error: invalid character %c\n", *p);
	  return;
	}

      i++;
    }
  len = i;

  if (point == -1)
    point = len;

  mant_t mantissa = 0;
  if (fsd >= 0)
    {
      mant_t m = 1;
      for (i = lsd ; i >= fsd ; --i)
	{
	  if (input[i] != get_system_decimal ())
	    {
	      if (ordinate > ORD_MAX - m * (input[i] - '0'))
		{
		  fprintf (stderr, "Overflow reading value %s\n", input);
		  break;
		}
	      ordinate += m * (input[i] - '0');
	      m *= 10;
	    }
	}

      if (lsd > point)
	mantissa = point - lsd;
      else
	mantissa = point - lsd - 1;
    }

  decimal->ordinate = ordinate * sign;
  decimal->mantissa = mantissa;
}



/* Initialise DEC from the binary fp value X */
void 
decimal_from_double (struct decimal *dec, double x)
{
  dec->mantissa = 0;

  while (trunc (x) != x)
    {
      if (fabs (x) > ORD_MAX / 10.0)
	{
	  dec_warning = DEC_PREC;
	  break;
	}
      x *= 10.0;
      dec->mantissa--;
    }

  dec->ordinate = x;
}

/* Return a binary floating point value 
   approximating DEC */
double
decimal_to_double (const struct decimal *dec)
{
  double x = dec->ordinate;
  int mult = dec->mantissa;

  while (mult < 0)
    {
      x /= 10.0;
      mult++;
    }

  while (mult > 0)
    {
      x *= 10.0;
      mult--;
    }

  return x;
}
