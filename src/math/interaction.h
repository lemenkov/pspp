/* PSPP - a program for statistical analysis.
   Copyright (C) 2011 Free Software Foundation, Inc.

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


#ifndef _INTERACTION_H__
#define _INTERACTION_H__ 1

#include <stdbool.h>
#include "libpspp/compiler.h"
#include "data/missing-values.h"

struct ccase;
struct interaction;
struct string;
struct variable;

#include <stddef.h>

/* An interaction is a structure containing a "product" of other variables.
   The variables can be either string or numeric.

   Interaction is commutative.  That means, that from a mathematical point of
   view, the order of the variables is irrelevant.  However, for display
   purposes, and for matching with an interaction's value the order is
   pertinent.  Therefore, when using these functions, make sure the orders of
   variables and values match when appropriate.

   Some functions for interactions will not work properly for interactions that
   contain a given variable more than once, so this should be regarded as an
   invariant.  The functions to modify interactions don't check for this
   invariant. */
struct interaction
  {
    const struct variable **vars;
    size_t n_vars;
  };

struct interaction *interaction_create (const struct variable *);
struct interaction *interaction_clone (const struct interaction *);
void interaction_destroy (struct interaction *);
void interaction_add_variable (struct interaction *, const struct variable *);
void interaction_dump (const struct interaction *);
void interaction_to_string (const struct interaction *, struct string *str);
bool interaction_is_proper_subset (const struct interaction *,
                                   const struct interaction *);
bool interaction_is_subset (const struct interaction *,
                            const struct interaction *);


unsigned int interaction_case_hash (const struct interaction *,
                                    const struct ccase *,
                                    unsigned int base) WARN_UNUSED_RESULT;
bool interaction_case_equal (const struct interaction *, const struct ccase *,
                             const struct ccase *);
bool interaction_case_is_missing (const struct interaction *,
                                  const struct ccase *, enum mv_class);
int interaction_case_cmp_3way (const struct interaction *,
                               const struct ccase *, const struct ccase *);

#endif
