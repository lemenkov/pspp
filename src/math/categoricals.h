/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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


#ifndef _CATEGORICALS__
#define _CATEGORICALS__

#include <stddef.h>
#include "data/missing-values.h"

struct categoricals;
struct ccase;
struct interaction;
struct variable;
union value;

/* Categoricals.

   A categorical variable has a finite and usually small number of possible
   values.  The categoricals data structure organizes an array of interactions
   maong categorical variables, that is, a set of sets of categorical
   variables.  (Both levels of "set" are ordered.)

   The life cycle of a categoricals object looks like this:

   1. Create it with categoricals_create().  This fixes the set of interactions
      and other parameters.

   2. Pass all of the desired cases through the object with
      categoricals_update().

   3. Finalize the object with categoricals_done().  Only at this point may
      most of the categoricals query functions be called.

   4. Use the categoricals object as desired.

   5. Destroy the object with categoricals_destroy().
*/

/* Creating and destroying categoricals. */
struct categoricals *categoricals_create (struct interaction *const *,
                                          size_t n,
                                          const struct variable *wv,
                                          enum mv_class fctr_excl);
void categoricals_destroy (struct categoricals *);

/* Updating categoricals. */
void categoricals_update (struct categoricals *, const struct ccase *);
void categoricals_done (const struct categoricals *);
bool categoricals_is_complete (const struct categoricals *);

/* Categories.

   A variable's number of categories is the number of unique values observed in
   the data passed to categoricals_update().

   An interaction's number of categories is the number of observed unique
   values of its variables, which will often be less than the product of its
   variables' numbers of categories.

   A categorical object's number of categories is the sum of its interactions'
   categories. */
size_t categoricals_n_count (const struct categoricals *, size_t idx);
size_t categoricals_n_total (const struct categoricals *);

union value *categoricals_get_var_values (const struct categoricals *,
                                          const struct variable *, size_t *n);

/* Degrees of freedom.

   A categorical variable with N_CATS categories has N_CATS - 1 degrees of
   freedom.

   An interaction's degrees of freedom is the product of its variables' degrees
   of freedom.

   A categorical object's degrees of freedom is the sum of its interactions'
   degrees of freedom. */
size_t categoricals_df (const struct categoricals *, size_t idx);
size_t categoricals_df_total (const struct categoricals *);

/* Sanity. */
bool categoricals_sane (const struct categoricals *cat);

/* "Short map".

   These look up an interaction within a categoricals object on the basis of a
   "subscript".  Interaction 0 with DF_0 degrees of freedom is assigned
   subscripts [0, DF_0 - 1], interaction 1 with DF_1 degrees of freedom is
   assigned subscripts [DF_0, DF_0 + DF_1 - 1], and so on.  The subscripts
   passed in must be in the range [0, DF_SUM - 1] where DF_SUM is the total
   number of degrees of freedom for the object, as returned by
   categoricals_df_total().

   These functions are intended for covariance matrix routines, where normally
   1 less than the total number of distinct values of each categorical variable
   should be considered.

   These functions may be used on an object only after calling
   categoricals_done().
*/
double categoricals_get_weight_by_subscript (const struct categoricals *,
                                             int subscript);
const struct interaction *categoricals_get_interaction_by_subscript (
  const struct categoricals *, int subscript);
double categoricals_get_sum_by_subscript (const struct categoricals *,
                                          int subscript);
double categoricals_get_dummy_code_for_case (const struct categoricals *,
                                             int subscript,
                                             const struct ccase *);
double categoricals_get_effects_code_for_case (const struct categoricals *,
                                               int subscript,
                                               const struct ccase *);


/* "Long map".

   These look up an interaction within a categoricals object on the basis of a
   "category index".  Interaction 0 in CAT with CAT_0 categories has indexes
   [0, CAT_0 - 1], interaction 1 with CAT_1 categories has indexes [CAT_0,
   CAT_0 + CAT_1 - 1], and so on.  The indexes passed in must be in the range
   [0, CAT_TOTAL - 1] where CAT_TOTAL is the total number of categories for the
   object, as returned by categoricals_n_total().

   These functions are useful for descriptive statistics.

   These functions may be used on an object only after calling
   categoricals_done().
*/
const struct ccase *categoricals_get_case_by_category_real (
  const struct categoricals *, int iact, int n);
void *categoricals_get_user_data_by_category_real (
  const struct categoricals *, int iact, int n);

int categoricals_get_value_index_by_category_real (
  const struct categoricals *, int iact_idx, int cat_idx, int var_idx);

void *categoricals_get_user_data_by_category (const struct categoricals *,
                                              int category);
const struct ccase *categoricals_get_case_by_category (
  const struct categoricals *cat, int subscript);

struct payload
  {
    void *(*create)  (const void *aux1, void *aux2);
    void (*update)  (const void *aux1, void *aux2, void *user_data,
                     const struct ccase *, double weight);
    void (*calculate) (const void *aux1, void *aux2, void *user_data);
    void (*destroy) (const void *aux1, void *aux2, void *user_data);
  };

void categoricals_set_payload (struct categoricals *, const struct payload *,
                               const void *aux1, void *aux2);
bool categoricals_isbalanced (const struct categoricals *);

#endif
