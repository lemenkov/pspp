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

#ifndef DATA_CASEPROTO_H
#define DATA_CASEPROTO_H 1

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "data/value.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"

/* Case prototype.

   A case prototype specifies the number and type of the values
   in a case.  It is essentially an array of integers, where the
   array index is an index into a case and each element
   represents the width of a value in a case.  A width of 0
   indicates a numeric value, and any positive integer up to
   MAX_STRING indicate the size in bytes of a string value.

   Case prototypes are reference counted.  A newly created case
   prototype has a single owner (the code that created it),
   represented by an initial reference count of 1.  Other code
   that receives the case prototype may keep a virtual copy of it
   by calling caseproto_ref, which increments the case
   prototype's reference count.  When this is done, the case
   prototype becomes shared between its original owner and each
   piece of code that incremented the reference count.

   Functions that modifying case prototypes automatically unshare
   them as necessary.  Thus it is very important that every caller of a
   function that modifies a case prototype thereafter uses the returned
   caseproto instead of the one passed in as an argument.

   Only the case prototype code should refer to caseproto members
   directly.  Other code should use the provided helper
   functions. */
struct caseproto
  {
    size_t ref_cnt;             /* Reference count. */

    /* Tracking of string widths.  Lazily maintained: when 'strings' is null
       and 'n_strings' is nonzero, the former must be regenerated. */
    size_t *strings;            /* Array of indexes of string widths. */
    size_t n_strings;           /* Number of string widths. */

    /* Widths. */
    size_t n_widths;            /* Number of widths. */
    size_t allocated_widths;    /* Space allocated for 'widths' array. */
    short int *widths;          /* Width of each case value. */
  };

struct pool;

/* Creation and destruction. */
struct caseproto *caseproto_create (void) MALLOC_LIKE;
struct caseproto *caseproto_from_widths (short int *, size_t n) MALLOC_LIKE;
static inline struct caseproto *caseproto_ref (const struct caseproto *) WARN_UNUSED_RESULT;
struct caseproto *caseproto_ref_pool (const struct caseproto *, struct pool *) WARN_UNUSED_RESULT;
static inline void caseproto_unref (struct caseproto *);

/* Inspecting stored widths.  */
static inline int caseproto_get_width (const struct caseproto *, size_t idx);
static inline size_t caseproto_get_n_widths (const struct caseproto *);

/* Adding and removing widths. */
struct caseproto *caseproto_reserve (struct caseproto *, size_t n_widths)
  WARN_UNUSED_RESULT;
struct caseproto *caseproto_add_width (struct caseproto *, int width)
  WARN_UNUSED_RESULT;
struct caseproto *caseproto_set_width (struct caseproto *,
                                       size_t idx, int width)
  WARN_UNUSED_RESULT;
struct caseproto *caseproto_insert_width (struct caseproto *,
                                          size_t before, int width)
  WARN_UNUSED_RESULT;
struct caseproto *caseproto_remove_widths (struct caseproto *,
                                           size_t idx, size_t n)
  WARN_UNUSED_RESULT;
struct caseproto *caseproto_move_widths (struct caseproto *,
                                         size_t old_start, size_t new_start,
                                         size_t n)
  WARN_UNUSED_RESULT;

/* Working with "union value" arrays. */
bool caseproto_needs_init_values (const struct caseproto *);
void caseproto_init_values (const struct caseproto *, union value[]);
bool caseproto_try_init_values (const struct caseproto *, union value[]);
void caseproto_reinit_values (const struct caseproto *old,
                              const struct caseproto *new, union value[]);
void caseproto_destroy_values (const struct caseproto *, union value[]);

void caseproto_copy (const struct caseproto *, size_t idx, size_t count,
                     union value *dst, const union value *src);

/* Inspecting the cache of string widths.

   (These functions are useful for allocating cases, which requires allocating
   a block of memory for each string value in the case.) */
static inline size_t caseproto_get_n_strings (const struct caseproto *);
static inline size_t caseproto_get_string_idx (const struct caseproto *,
                                               size_t idx1);

/* For use in assertions. */
bool caseproto_range_is_valid (const struct caseproto *,
                               size_t ofs, size_t count);
bool caseproto_is_conformable (const struct caseproto *a,
                               const struct caseproto *b);
bool caseproto_range_equal (const struct caseproto *a, size_t a_start,
                            const struct caseproto *b, size_t b_start,
                            size_t n);
bool caseproto_equal (const struct caseproto *, const struct caseproto *);

/* Creation and destruction. */

void caseproto_free__ (struct caseproto *);

/* Increments case prototype PROTO's reference count and returns
   PROTO.  Afterward, PROTO is shared among its reference count
   holders. */
static inline struct caseproto *
caseproto_ref (const struct caseproto *proto_)
{
  struct caseproto *proto = CONST_CAST (struct caseproto *, proto_);
  proto->ref_cnt++;
  return proto;
}

/* Decrements case prototype PROTO's reference count.  Frees
   PROTO if its reference count drops to 0.

   If PROTO is a null pointer, this function has no effect. */
static inline void
caseproto_unref (struct caseproto *proto)
{
  if (proto != NULL && --proto->ref_cnt == 0)
    caseproto_free__ (proto);
}

/* Inspecting stored widths.  */

/* Returns case prototype PROTO's width with the given IDX.  IDX
   must be less than caseproto_get_n_widths(PROTO). */
static inline int
caseproto_get_width (const struct caseproto *proto, size_t idx)
{
  assert (idx < proto->n_widths);
  return proto->widths[idx];
}

/* Returns the number of widths in case prototype PROTO. */
static inline size_t
caseproto_get_n_widths (const struct caseproto *proto)
{
  return proto->n_widths;
}

/* Inspecting the cache of long string widths. */

void caseproto_refresh_string_cache__ (const struct caseproto *);

/* Returns the number of strings in PROTO. */
static inline size_t
caseproto_get_n_strings (const struct caseproto *proto)
{
  return proto->n_strings;
}

/* Given string width IDX1, returns a value IDX2 for which
   caseproto_get_width(PROTO, IDX2) will return a value greater than 0.  IDX1
   must be less than caseproto_get_n_strings(PROTO), and IDX2 will be less than
   caseproto_get_n_widths(PROTO). */
static inline size_t
caseproto_get_string_idx (const struct caseproto *proto, size_t idx1)
{
  if (proto->strings == NULL)
    caseproto_refresh_string_cache__ (proto);

  assert (idx1 < proto->n_strings);
  return proto->strings[idx1];
}

#endif /* data/caseproto.h */
