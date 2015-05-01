/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2009, 2010 Free Software Foundation, Inc.

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

#include "language/stats/freq.h"

#include <stdlib.h>

#include "data/variable.h"
#include "data/value.h"
#include "libpspp/array.h"
#include "libpspp/compiler.h"

struct freq *
freq_clone (const struct freq *in, int values, int *widths)
{
  int i;
  struct freq *f = xmalloc (sizeof (struct freq) +
			    (sizeof (union value) * (values - 1)));

  f->node = in->node;
  f->count = in->count;
  for (i = 0; i < values; ++i)
    {
      value_init (&f->values[i],  widths[i]);
      value_copy (&f->values[i], &in->values[i], widths[i]);
    }

  return f;
}

void
freq_destroy (struct freq *f, int values, int *widths)
{
  int i;
  for (i = 0; i < values; ++i)
    {
      value_destroy (&f->values[i],  widths[i]);
    }

  free (f);
}



void
freq_hmap_destroy (struct hmap *hmap, int width)
{
  struct freq *f, *next;

  HMAP_FOR_EACH_SAFE (f, next, struct freq, node, hmap)
    {
      value_destroy (&f->values[0], width);
      hmap_delete (hmap, &f->node);
      free (f);
    }
  hmap_destroy (hmap);
}

struct freq *
freq_hmap_search (struct hmap *hmap,
                  const union value *value, int width, size_t hash)
{
  struct freq *f;

  HMAP_FOR_EACH_WITH_HASH (f, struct freq, node, hash, hmap)
    if (value_equal (value, &f->values[0], width))
      return f;

  return NULL;
}

struct freq *
freq_hmap_insert (struct hmap *hmap,
                  const union value *value, int width, size_t hash)
{
  struct freq *f = xmalloc (sizeof *f);
  value_clone (&f->values[0], value, width);
  f->count = 0;
  hmap_insert (hmap, &f->node, hash);
  return f;
}

int
compare_freq_ptr_3way (const void *a_, const void *b_, const void *width_)
{
  const struct freq *const *ap = a_;
  const struct freq *const *bp = b_;
  const int *widthp = width_;

  return value_compare_3way (&(*ap)->values[0], &(*bp)->values[0], *widthp);
}

struct freq **
freq_hmap_sort (struct hmap *hmap, int width)
{
  size_t n_entries = hmap_count (hmap);
  struct freq **entries;
  struct freq *f;
  size_t i;

  entries = xnmalloc (n_entries, sizeof *entries);
  i = 0;
  HMAP_FOR_EACH (f, struct freq, node, hmap)
    entries[i++] = f;
  assert (i == n_entries);

  sort (entries, n_entries, sizeof *entries, compare_freq_ptr_3way, &width);

  return entries;
}

struct freq *
freq_hmap_extract (struct hmap *hmap)
{
  struct freq *freqs, *f;
  size_t n_freqs;
  size_t i;

  n_freqs = hmap_count (hmap);
  freqs = xnmalloc (n_freqs, sizeof *freqs);
  i = 0;
  HMAP_FOR_EACH (f, struct freq, node, hmap)
    freqs[i++] = *f;
  assert (i == n_freqs);

  return freqs;
}

