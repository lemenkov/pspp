/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2012 Free Software Foundation, Inc.

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

#include "data/case.h"
#include "interaction.h"

#include "data/value.h"
#include "data/variable.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include <stdio.h>

/* Creates and returns an interaction.  If V is nonnull, then the interaction
   initially contains V, otherwise it is initially empty. */
struct interaction *
interaction_create (const struct variable *v)
{
  struct interaction *iact = xmalloc (sizeof *iact);
  iact->vars = xmalloc (sizeof *iact->vars);
  iact->n_vars = 0;
  if (v)
    {
      iact->vars[0] = v;
      iact->n_vars = 1;
    }
  return iact;
}

/* Returns a (deep) copy of interaction SRC. */
struct interaction *
interaction_clone (const struct interaction *src)
{
  struct interaction *dst = xmalloc (sizeof *dst);
  dst->vars = xmemdup (src->vars, src->n_vars * sizeof *src->vars);
  dst->n_vars = src->n_vars;
  return dst;
}

/* Frees IACT. */
void
interaction_destroy (struct interaction *iact)
{
  if (iact)
    {
      free (iact->vars);
      free (iact);
    }
}

/* Appends variable V to IACT.

   V must not already be in IACT. */
void
interaction_add_variable (struct interaction *iact, const struct variable *v)
{
  iact->vars = xrealloc (iact->vars, (iact->n_vars + 1) * sizeof *iact->vars);
  iact->vars[iact->n_vars++] = v;
}

/* Returns true iff the variables in X->VARS are a proper subset of the
   variables in Y->VARS. */
bool
interaction_is_proper_subset (const struct interaction *x,
                              const struct interaction *y)
{
  return x->n_vars >= y->n_vars && interaction_is_subset (x, y);
}

static bool
interaction_contains (const struct interaction *iact, const struct variable *v)
{
  for (size_t i = 0; i < iact->n_vars; i++)
    if (iact->vars[i] == v)
      return true;
  return false;
}

/* Returns true iff the variables in X->VARS are a subset (proper or otherwise)
   of the variables in Y->VARS. */
bool
interaction_is_subset (const struct interaction *x,
                       const struct interaction *y)
{
  if (x->n_vars > y->n_vars)
    return false;

  for (size_t i = 0; i < x->n_vars; i++)
    if (!interaction_contains (y, x->vars[i]))
      return false;

  return true;
}

/* Prints the variables in IACT on stdout, for debugging purposes. */
void
interaction_dump (const struct interaction *iact)
{
  if (iact->n_vars == 0)
    printf ("(empty)\n");
  else
    {
      for (size_t v = 0; v < iact->n_vars; ++v)
        {
          printf ("%s", var_get_name (iact->vars[v]));
          if (v + 1 < iact->n_vars)
            printf (" * ");
        }
      printf ("\n");
    }
}

/* Appends STR with a representation of the interaction, suitable for user
   display.

   STR must have been initialised prior to calling this function. */
void
interaction_to_string (const struct interaction *iact, struct string *str)
{
  for (size_t v = 0; v < iact->n_vars; ++v)
    {
      ds_put_cstr (str, var_to_string (iact->vars[v]));
      if (v + 1 < iact->n_vars)
        ds_put_cstr (str, " × ");
    }
}

/* Returns a hash of the values in C given by variables in IACT, using BASE as
   a basis for the hash. */
unsigned int
interaction_case_hash (const struct interaction *iact,
                       const struct ccase *c, unsigned int base)
{
  size_t hash = base;
  for (size_t i = 0; i < iact->n_vars; ++i)
    {
      const struct variable *var = iact->vars[i];
      const union value *val = case_data (c, var);
      hash = value_hash (val, var_get_width (var), hash);
    }
  return hash;
}

/* Returns true iff all the variables in IACT have equal values in C1 and
   C2. */
bool
interaction_case_equal (const struct interaction *iact,
                        const struct ccase *c1, const struct ccase *c2)
{
  for (size_t i = 0; i < iact->n_vars; ++i)
    {
      const struct variable *var = iact->vars[i];
      if (!value_equal (case_data (c1, var), case_data (c2, var),
                        var_get_width (var)))
        return false;
    }

  return true;
}

/* Returns a strcmp()-like comparison result for the variables in IACT and
   their values in C1 and C2. */
int
interaction_case_cmp_3way (const struct interaction *iact,
                           const struct ccase *c1, const struct ccase *c2)
{
  for (size_t i = 0; i < iact->n_vars; ++i)
    {
      const struct variable *var = iact->vars[i];
      int cmp = value_compare_3way (case_data (c1, var), case_data (c2, var),
                                    var_get_width (var));
      if (cmp)
        return cmp;
    }

  return 0;
}

/* Returns true iff any of the variables in IACT have a missing value in C,
   using EXCLUDE to decide which kinds of missing values to count. */
bool
interaction_case_is_missing (const struct interaction *iact,
                             const struct ccase *c, enum mv_class exclude)
{
  for (size_t i = 0; i < iact->n_vars; ++i)
    if (var_is_value_missing (iact->vars[i], case_data (c, iact->vars[i]))
        & exclude)
      return true;

  return false;
}

