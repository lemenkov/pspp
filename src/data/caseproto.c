/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011 Free Software Foundation, Inc.

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

#include "data/caseproto.h"

#include "data/val-type.h"
#include "data/value.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/pool.h"

#include "gl/minmax.h"

static struct caseproto *caseproto_unshare (struct caseproto *);
static bool try_init_strings (const struct caseproto *,
                                   size_t first, size_t last, union value[]);
static void init_strings (const struct caseproto *,
                               size_t first, size_t last, union value[]);
static void destroy_strings (const struct caseproto *,
                                  size_t first, size_t last, union value[]);
static size_t count_strings (const struct caseproto *,
                                  size_t idx, size_t count);

/* Creates and returns a case prototype that initially has no
   widths. */
struct caseproto *
caseproto_create (void)
{
  struct caseproto *proto = xmalloc (sizeof *proto);
  *proto = (struct caseproto) {
    .ref_cnt = 1,
  };
  return proto;
}

struct caseproto * MALLOC_LIKE
caseproto_from_widths (short int *widths, size_t n)
{
  struct caseproto *proto = xmalloc (sizeof *proto);
  *proto = (struct caseproto) {
    .ref_cnt = 1,
    .n_widths = n,
    .allocated_widths = n,
    .widths = widths,
  };
  proto->n_strings = count_strings (proto, 0, n);
  return proto;
}

static void
do_unref (void *proto_)
{
  struct caseproto *proto = proto_;
  caseproto_unref (proto);
}

/* Creates and returns a new reference to PROTO.  When POOL is
   destroyed, the new reference will be destroyed (unrefed).  */
struct caseproto *
caseproto_ref_pool (const struct caseproto *proto_, struct pool *pool)
{
  struct caseproto *proto = caseproto_ref (proto_);
  pool_register (pool, do_unref, proto);
  return proto;
}

/* Returns a replacement for PROTO with WIDTH appended.  */
struct caseproto *
caseproto_add_width (struct caseproto *proto, int width)
{
  assert (width >= 0 && width <= MAX_STRING);

  proto = caseproto_unshare (proto);
  if (proto->n_widths >= proto->allocated_widths)
    proto->widths = x2nrealloc (proto->widths, &proto->allocated_widths,
                                sizeof *proto->widths);
  proto->widths[proto->n_widths++] = width;
  if (width > 0)
    proto->n_strings++;

  return proto;
}

/* Returns a replacement for PROTO with the width at index IDX replaced by
   WIDTH.  */
struct caseproto *
caseproto_set_width (struct caseproto *proto, size_t idx, int width)
{
  assert (idx < proto->n_widths);
  assert (width >= 0 && width <= MAX_STRING);

  proto = caseproto_unshare (proto);
  proto->n_strings -= count_strings (proto, idx, 1);
  proto->widths[idx] = width;
  proto->n_strings += count_strings (proto, idx, 1);

  return proto;
}

/* Returns a replacement for PROTO with WIDTH inserted just
   before index BEFORE, or just after the last element if BEFORE
   is the number of widths in PROTO. */
struct caseproto *
caseproto_insert_width (struct caseproto *proto, size_t before, int width)
{
  assert (width >= 0 && width <= MAX_STRING);
  assert (before <= proto->n_widths);

  proto = caseproto_unshare (proto);
  if (proto->n_widths >= proto->allocated_widths)
    proto->widths = x2nrealloc (proto->widths, &proto->allocated_widths,
                                sizeof *proto->widths);
  proto->n_strings += value_needs_init (width);
  insert_element (proto->widths, proto->n_widths, sizeof *proto->widths,
                  before);
  proto->widths[before] = width;
  proto->n_widths++;

  return proto;
}

/* Returns a replacement for PROTO with N widths removed
   starting at index IDX. */
struct caseproto *
caseproto_remove_widths (struct caseproto *proto, size_t idx, size_t n)
{
  assert (caseproto_range_is_valid (proto, idx, n));

  proto = caseproto_unshare (proto);
  proto->n_strings -= count_strings (proto, idx, n);
  remove_range (proto->widths, proto->n_widths, sizeof *proto->widths,
                idx, n);
  proto->n_widths -= n;
  return proto;
}

/* Returns a replacement for PROTO in which the N widths
   starting at index OLD_WIDTH now start at index NEW_WIDTH, with
   other widths shifting out of the way to make room. */
struct caseproto *
caseproto_move_widths (struct caseproto *proto,
                       size_t old_start, size_t new_start,
                       size_t n)
{
  assert (caseproto_range_is_valid (proto, old_start, n));
  assert (caseproto_range_is_valid (proto, new_start, n));

  proto = caseproto_unshare (proto);
  move_range (proto->widths, proto->n_widths, sizeof *proto->widths,
              old_start, new_start, n);
  return proto;
}

/* Returns true if PROTO contains COUNT widths starting at index
   OFS, false if any of those widths are out of range for
   PROTO. */
bool
caseproto_range_is_valid (const struct caseproto *proto,
                          size_t ofs, size_t count)
{
  return (count <= proto->n_widths
          && ofs <= proto->n_widths
          && ofs + count <= proto->n_widths);
}

/* Returns true if A and B have the same widths along their
   common length.  (When this is so, a case with prototype A may
   be extended or truncated to have prototype B without having to
   change any existing values, and vice versa.) */
bool
caseproto_is_conformable (const struct caseproto *a, const struct caseproto *b)
{
  size_t min;
  size_t i;

  min = MIN (a->n_widths, b->n_widths);
  for (i = 0; i < min; i++)
    if (a->widths[i] != b->widths[i])
      return false;
  return true;
}

/* Returns true if the N widths starting at A_START in A are the
   same as the N widths starting at B_START in B, false if any of
   the corresponding widths differ. */
bool
caseproto_range_equal (const struct caseproto *a, size_t a_start,
                       const struct caseproto *b, size_t b_start,
                       size_t n)
{
  size_t i;

  assert (caseproto_range_is_valid (a, a_start, n));
  assert (caseproto_range_is_valid (b, b_start, n));
  for (i = 0; i < n; i++)
    if (a->widths[a_start + i] != b->widths[b_start + i])
      return false;
  return true;
}

/* Returns true if A and B have the same widths, false otherwise. */
bool
caseproto_equal (const struct caseproto *a, const struct caseproto *b)
{
  return (a == b ? true
          : a->n_widths != b->n_widths ? false
          : caseproto_range_equal (a, 0, b, 0, a->n_widths));
}

/* Returns true if an array of values that is to be used for
   data of the format specified in PROTO needs to be initialized
   by calling caseproto_init_values, false if that step may be
   skipped because such an initialization would be a no-op anyhow.

   This optimization is useful only when a large number of
   initializations of such arrays may be skipped as a group. */
bool
caseproto_needs_init_values (const struct caseproto *proto)
{
  return proto->n_strings > 0;
}

/* Initializes the values in VALUES as required by PROTO, by
   calling value_init() on each value for which this is required.
   The data in VALUES have indeterminate contents until
   explicitly written.

   VALUES must have at least caseproto_get_n_widths(PROTO)
   elements; only that many elements of VALUES are initialized.

   The caller retains ownership of PROTO. */
void
caseproto_init_values (const struct caseproto *proto, union value values[])
{
  init_strings (proto, 0, proto->n_strings, values);
}

/* Like caseproto_init_values, but returns false instead of
   terminating if memory cannot be obtained. */
bool
caseproto_try_init_values (const struct caseproto *proto, union value values[])
{
  return try_init_strings (proto, 0, proto->n_strings, values);
}

/* Initializes the data in VALUES that are in NEW but not in OLD,
   destroys the data in VALUES that are in OLD but not NEW, and
   does not modify the data in VALUES that are in both OLD and
   NEW.  VALUES must previously have been initialized as required
   by OLD using e.g. caseproto_init_values.  The data in VALUES
   that are in NEW but not in OLD will have indeterminate
   contents until explicitly written.

   OLD and NEW must be conformable for this operation, as
   reported by caseproto_is_conformable.

   The caller retains ownership of OLD and NEW. */
void
caseproto_reinit_values (const struct caseproto *old,
                         const struct caseproto *new, union value values[])
{
  size_t old_n_long = old->n_strings;
  size_t new_n_long = new->n_strings;

  expensive_assert (caseproto_is_conformable (old, new));

  if (new_n_long > old_n_long)
    init_strings (new, old_n_long, new_n_long, values);
  else if (new_n_long < old_n_long)
    destroy_strings (old, new_n_long, old_n_long, values);
}

/* Frees the values in VALUES as required by PROTO, by calling
   value_destroy() on each value for which this is required.  The
   values must previously have been initialized using
   e.g. caseproto_init_values.

   The caller retains ownership of PROTO. */
void
caseproto_destroy_values (const struct caseproto *proto, union value values[])
{
  destroy_strings (proto, 0, proto->n_strings, values);
}

/* Copies COUNT values, whose widths are given by widths in PROTO
   starting with index IDX, from SRC to DST.  The caller must
   ensure that the values in SRC and DST were appropriately
   initialized using e.g. caseproto_init_values. */
void
caseproto_copy (const struct caseproto *proto, size_t idx, size_t count,
                union value *dst, const union value *src)
{
  size_t i;

  assert (caseproto_range_is_valid (proto, idx, count));
  for (i = 0; i < count; i++)
    value_copy (&dst[idx + i], &src[idx + i], proto->widths[idx + i]);
}

void
caseproto_free__ (struct caseproto *proto)
{
  free (proto->strings);
  free (proto->widths);
  free (proto);
}

void
caseproto_refresh_string_cache__ (const struct caseproto *proto_)
{
  struct caseproto *proto = CONST_CAST (struct caseproto *, proto_);
  size_t n, i;

  assert (proto->strings == NULL);
  assert (proto->n_strings > 0);

  proto->strings = xmalloc (proto->n_strings * sizeof *proto->strings);
  n = 0;
  for (i = 0; i < proto->n_widths; i++)
    if (proto->widths[i] > 0)
      proto->strings[n++] = i;
  assert (n == proto->n_strings);
}

/* Returns a caseproto that can be modified without affecting the contents of
   any caseproto shared with OLD.

   The returned caseproto has no strings cache.  This is helpful because the
   caller might be about to invalidate it. */
static struct caseproto *
caseproto_unshare (struct caseproto *old)
{
  assert (old->ref_cnt > 0);
  if (old->ref_cnt <= 1)
    {
      free (old->strings);
      old->strings = NULL;
      return old;
    }

  struct caseproto *new = xmalloc (sizeof *new);
  *new = (struct caseproto) {
    .ref_cnt = 1,
    .n_strings = old->n_strings,
    .n_widths = old->n_widths,
    .allocated_widths = old->allocated_widths,
    .widths = xmemdup (old->widths, old->allocated_widths * sizeof *old->widths),
  };
  --old->ref_cnt;
  return new;
}

static bool
try_init_strings (const struct caseproto *proto,
                       size_t first, size_t last, union value values[])
{
  size_t i;

  if (last > 0 && proto->strings == NULL)
    caseproto_refresh_string_cache__ (proto);

  for (i = first; i < last; i++)
    {
      size_t idx = proto->strings[i];
      if (!value_try_init (&values[idx], proto->widths[idx]))
        {
          destroy_strings (proto, first, i, values);
          return false;
        }
    }
  return true;
}

static void
init_strings (const struct caseproto *proto,
                   size_t first, size_t last, union value values[])
{
  if (!try_init_strings (proto, first, last, values))
    xalloc_die ();
}

static void
destroy_strings (const struct caseproto *proto, size_t first, size_t last,
                      union value values[])
{
  size_t i;

  if (last > 0 && proto->strings == NULL)
    caseproto_refresh_string_cache__ (proto);

  for (i = first; i < last; i++)
    {
      size_t idx = proto->strings[i];
      value_destroy (&values[idx], proto->widths[idx]);
    }
}

static size_t
count_strings (const struct caseproto *proto, size_t idx, size_t count)
{
  size_t n, i;

  n = 0;
  for (i = 0; i < count; i++)
    n += proto->widths[idx + i] > 0;
  return n;
}
