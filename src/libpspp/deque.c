/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2011 Free Software Foundation, Inc.

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

#include "libpspp/deque.h"

#include <string.h>

#include "gl/minmax.h"
#include "gl/xalloc.h"

/* Initializes DEQUE as an empty deque of elements ELEM_SIZE
   bytes in size, with an initial capacity of at least
   CAPACITY.  Returns the initial deque data array. */
void *
deque_init (struct deque *deque, size_t capacity, size_t elem_size)
{
  void *data = NULL;
  *deque = (struct deque) DEQUE_EMPTY_INITIALIZER;
  if (capacity > 0)
    {
      deque->capacity = 1;
      while (deque->capacity < capacity)
        deque->capacity <<= 1;
      data = xnmalloc (deque->capacity, elem_size);
    }
  return data;
}

/* Increases the capacity of DEQUE and returns a new deque data
   array that replaces the old data array. */
void *
deque_expand (struct deque *deque, void *old_data_, size_t elem_size)
{
  size_t old_capacity = deque->capacity;
  size_t new_capacity = MAX (4, old_capacity * 2);
  char *old_data = old_data_;
  char *new_data = xnmalloc (new_capacity, elem_size);
  size_t idx, n_copy;
  for (idx = deque->back; idx != deque->front; idx += n_copy)
    {
      size_t can_copy = old_capacity - (idx & (old_capacity - 1));
      size_t want_copy = deque->front - idx;
      n_copy = MIN (can_copy, want_copy);
      memcpy (new_data + (idx & (new_capacity - 1)) * elem_size,
              old_data + (idx & (old_capacity - 1)) * elem_size,
              n_copy * elem_size);
    }
  deque->capacity = new_capacity;
  free (old_data);
  return new_data;
}
