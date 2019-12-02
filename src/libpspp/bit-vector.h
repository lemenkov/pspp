/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2019 Free Software Foundation, Inc.

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

#ifndef BITVECTOR_H
#define BITVECTOR_H 1

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

enum { BITS_PER_ULONG = CHAR_BIT * sizeof (unsigned long int) };

unsigned long int *bitvector_allocate(size_t n);
size_t bitvector_count (const unsigned long int *, size_t);

static unsigned long int
bitvector_mask (size_t idx)
{
  return 1UL << (idx % BITS_PER_ULONG);
}

static const unsigned long int *
bitvector_unit (const unsigned long int *vec, size_t idx)
{
  return &vec[idx / BITS_PER_ULONG];
}

static unsigned long int *
bitvector_unit_rw (unsigned long int *vec, size_t idx)
{
  return &vec[idx / BITS_PER_ULONG];
}

static inline void
bitvector_set1 (unsigned long int *vec, size_t idx)
{
  *bitvector_unit_rw (vec, idx) |= bitvector_mask (idx);
}

static inline void
bitvector_set0 (unsigned long int *vec, size_t idx)
{
  *bitvector_unit_rw (vec, idx) &= ~bitvector_mask (idx);
}

static inline bool
bitvector_is_set (const unsigned long int *vec, size_t idx)
{
  return (*bitvector_unit (vec, idx) & bitvector_mask (idx)) != 0;
}

/* Returns 2**X, 0 <= X < 32. */
#define BIT_INDEX(X) (1ul << (X))

#endif /* bitvector.h */
