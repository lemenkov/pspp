/* PSPP - a program for statistical analysis.
   Copyright (C) 2019 Free Software Foundation, Inc.

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

#include "libpspp/bit-vector.h"

#include "libpspp/misc.h"

#include "gl/xalloc.h"

unsigned long int *
bitvector_allocate(size_t n)
{
  return XCALLOC (DIV_RND_UP (n, BITS_PER_ULONG), unsigned long int);
}

size_t
bitvector_count (const unsigned long int *vec, size_t n)
{
  /* XXX This can be optimized. */
  size_t count = 0;
  for (size_t i = 0; i < n; i++)
    count += bitvector_is_set (vec, i);
  return count;
}

