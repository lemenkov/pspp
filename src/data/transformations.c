/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2011, 2013 Free Software Foundation, Inc.

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

#include "data/transformations.h"

#include <assert.h>
#include <stdlib.h>

#include "libpspp/array.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

void
trns_chain_init (struct trns_chain *chain)
{
  *chain = (struct trns_chain) TRNS_CHAIN_INIT;
}

bool
trns_chain_uninit (struct trns_chain *chain)
{
  bool ok = true;
  for (size_t i = 0; i < chain->n; i++)
    {
      struct transformation *xform = &chain->xforms[i];
      if (xform->class->destroy)
        ok = xform->class->destroy (xform->aux) && ok;
    }
  free (chain->xforms);
  return ok;
}

bool
trns_chain_clear (struct trns_chain *chain)
{
  bool ok = trns_chain_uninit (chain);
  trns_chain_init (chain);
  return ok;
}

void
trns_chain_prepend (struct trns_chain *chain, const struct transformation *t)
{
  if (chain->n >= chain->allocated)
    chain->xforms = x2nrealloc (chain->xforms, &chain->allocated,
                                sizeof *chain->xforms);

  insert_element (chain->xforms, 1, sizeof *chain->xforms, 0);
  chain->xforms[0] = *t;
  chain->n++;
}

void
trns_chain_append (struct trns_chain *chain, const struct transformation *t)
{
  if (chain->n >= chain->allocated)
    chain->xforms = x2nrealloc (chain->xforms, &chain->allocated,
                                sizeof *chain->xforms);

  chain->xforms[chain->n++] = *t;
}

void
trns_chain_splice (struct trns_chain *dst, struct trns_chain *src)
{
  if (dst->n + src->n >= dst->allocated)
    {
      dst->allocated = dst->n + src->n;
      dst->xforms = xrealloc (dst->xforms,
                              dst->allocated * sizeof *dst->xforms);
    }

  memcpy (&dst->xforms[dst->n], src->xforms, src->n * sizeof *src->xforms);
  dst->n += src->n;
  src->n = 0;
}

/* Executes the N transformations in XFORMS against case *C passing CASE_NR as
   the case number.  The transformations may replace *C by a new case.  Returns
   the result code that caused the transformations to terminate, or
   TRNS_CONTINUE if the transformations finished due to "falling off the end"
   of the set of transformations. */
enum trns_result
trns_chain_execute (const struct trns_chain *chain,
                    casenumber case_nr, struct ccase **c)
{
  for (size_t i = 0; i < chain->n; i++)
    {
      const struct transformation *trns = &chain->xforms[i];
      int retval = trns->class->execute (trns->aux, c, case_nr);
      if (retval != TRNS_CONTINUE)
        return retval;
    }

  return TRNS_CONTINUE;
}
