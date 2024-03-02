/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2007, 2009, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#ifndef DATA_DICTIONARY_H
#define DATA_DICTIONARY_H 1

#include <stdbool.h>
#include <stddef.h>
#include "data/case.h"
#include "data/dict-class.h"

struct string;
struct ccase;

/* Creating dictionaries. */
struct dictionary *dict_create (const char *encoding);
struct dictionary *dict_clone (const struct dictionary *) WARN_UNUSED_RESULT;
struct dictionary *dict_ref (struct dictionary *s) WARN_UNUSED_RESULT;


/* Clearing and destroying dictionaries. */
void dict_clear (struct dictionary *);
void dict_unref (struct dictionary *);

/* Common ways to access variables. */
struct variable *dict_lookup_var (const struct dictionary *, const char *);
struct variable *dict_lookup_var_assert (const struct dictionary *,
                                         const char *);
struct variable *dict_get_var (const struct dictionary *, size_t position);
size_t dict_get_n_vars (const struct dictionary *);

/* Other access to variables. */
bool dict_contains_var (const struct dictionary *, const struct variable *);
void dict_get_vars (const struct dictionary *,
                    const struct variable ***vars, size_t *n_vars,
                    enum dict_class exclude);
void dict_get_vars_mutable (const struct dictionary *,
                            struct variable ***vars, size_t *n_vars,
                            enum dict_class exclude);

/* Creating variables. */
struct variable *dict_create_var (struct dictionary *, const char *,
                                  int width);
struct variable *dict_create_var_assert (struct dictionary *, const char *,
                                         int width);
struct variable *dict_clone_var (struct dictionary *, const struct variable *);
struct variable *dict_clone_var_assert (struct dictionary *,
                                        const struct variable *);
struct variable *dict_clone_var_as (struct dictionary *,
                                    const struct variable *, const char *);
struct variable *dict_clone_var_as_assert (struct dictionary *,
                                           const struct variable *,
                                           const char *);
struct variable *dict_create_var_with_unique_name (struct dictionary *,
                                                   const char *hint,
                                                   int width);

/* Deleting variables. */
void dict_delete_var (struct dictionary *, struct variable *);
void dict_delete_vars (struct dictionary *,
                       struct variable *const *, size_t count);
void dict_delete_consecutive_vars (struct dictionary *,
                                   size_t idx, size_t count);
void dict_delete_scratch_vars (struct dictionary *);

/* Changing the order of variables. */
void dict_reorder_var (struct dictionary *, struct variable *,
                       size_t new_index);
void dict_reorder_vars (struct dictionary *,
                        struct variable *const *, size_t count);

/* Variable names. */
bool dict_try_rename_var (struct dictionary *,
                          struct variable *, const char *);
void dict_rename_var (struct dictionary *, struct variable *, const char *);
bool dict_rename_vars (struct dictionary *,
                       struct variable **, char **new_names,
                       size_t count, char **err_name);
char *dict_make_unique_var_name (const struct dictionary *, const char *hint);

bool dict_get_names_must_be_ids (const struct dictionary *);
void dict_set_names_must_be_ids (struct dictionary *, bool);

/* Weight variable. */
double dict_get_case_weight (const struct dictionary *,
                             const struct ccase *, bool *);
double dict_get_rounded_case_weight (const struct dictionary *,
                                     const struct ccase *, bool *);
struct variable *dict_get_weight (const struct dictionary *);
void dict_set_weight (struct dictionary *, struct variable *);
struct fmt_spec dict_get_weight_format (const struct dictionary *);

/* Filter variable. */
struct variable *dict_get_filter (const struct dictionary *);
void dict_set_filter (struct dictionary *, struct variable *);

/* Case limit (N OF CASES). */
casenumber dict_get_case_limit (const struct dictionary *);
void dict_set_case_limit (struct dictionary *, casenumber);

/* Size of cases for this dictionary. */
const struct caseproto *dict_get_proto (const struct dictionary *);

/* SPLIT FILE variables.

   SPLIT_NONE is used if and only if there are no split file variables. */
enum split_type
  {
    SPLIT_NONE,                 /* No split file variables. */
    SPLIT_SEPARATE,             /* Produce separate output for each split. */
    SPLIT_LAYERED,              /* Output splits in same table.  */
  };
#define MAX_SPLITS 8
const struct variable *const *dict_get_split_vars (const struct dictionary *);
size_t dict_get_n_splits (const struct dictionary *);
enum split_type dict_get_split_type (const struct dictionary *);
void dict_set_split_vars (struct dictionary *,
                          struct variable *const *, size_t n,
                          enum split_type);
void dict_clear_split_vars (struct dictionary *);

/* File label. */
const char *dict_get_label (const struct dictionary *);
void dict_set_label (struct dictionary *, const char *);

/* Documents. */
#define DOC_LINE_LENGTH 80 /* Fixed length of document lines. */

const struct string_array *dict_get_documents (const struct dictionary *);
void dict_set_documents (struct dictionary *, const struct string_array *);
void dict_set_documents_string (struct dictionary *, const char *);
void dict_clear_documents (struct dictionary *);

bool dict_add_document_line (struct dictionary *, const char *,
                             bool issue_warning);
size_t dict_get_document_n_lines (const struct dictionary *);
const char *dict_get_document_line (const struct dictionary *, size_t);

/* Vectors. */
bool dict_create_vector (struct dictionary *, const char *name,
                         struct variable **, size_t n);
void dict_create_vector_assert (struct dictionary *, const char *name,
                                struct variable **, size_t n);
const struct vector *dict_get_vector (const struct dictionary *, size_t idx);
size_t dict_get_n_vectors (const struct dictionary *);
const struct vector *dict_lookup_vector (const struct dictionary *,
                                         const char *name);
void dict_clear_vectors (struct dictionary *);

/* Multiple response sets. */
const struct mrset *dict_get_mrset (const struct dictionary *, size_t idx);
size_t dict_get_n_mrsets (const struct dictionary *);
const struct mrset *dict_lookup_mrset (const struct dictionary *,
                                       const char *name);

bool dict_add_mrset (struct dictionary *, struct mrset *);
bool dict_delete_mrset (struct dictionary *, const char *name);
void dict_clear_mrsets (struct dictionary *);

/* Variable sets. */
const struct varset *dict_get_varset (const struct dictionary *, size_t idx);
size_t dict_get_n_varsets (const struct dictionary *);
const struct varset *dict_lookup_varset (const struct dictionary *,
                                         const char *name);

bool dict_add_varset (struct dictionary *, struct varset *);
bool dict_delete_varset (struct dictionary *, const char *name);
void dict_clear_varsets (struct dictionary *);

/* Attributes. */
struct attrset *dict_get_attributes (const struct dictionary *);
void dict_set_attributes (struct dictionary *, const struct attrset *);
bool dict_has_attributes (const struct dictionary *);

/* Data encoding. */
const char *dict_get_encoding (const struct dictionary *d);

char *dict_id_is_valid__ (const struct dictionary *, const char *id,
                          enum dict_class)
  WARN_UNUSED_RESULT;
bool dict_id_is_valid (const struct dictionary *, const char *id,
                       enum dict_class);

/* Functions to be called upon dictionary changes. */
struct dict_callbacks
 {
  void (*var_added) (struct dictionary *, int, void *);
  void (*vars_deleted) (struct dictionary *, int dict_index, unsigned int n, void *);
  void (*var_moved) (struct dictionary *, int new_dict_index, int old_dict_index, void *);
  void (*var_changed) (struct dictionary *, int, unsigned int, const struct variable *, void *);
  void (*weight_changed) (struct dictionary *, int, void *);
  void (*filter_changed) (struct dictionary *, int, void *);
  void (*split_changed) (struct dictionary *, void *);
 };

void dict_set_callbacks (struct dictionary *, const struct dict_callbacks *,
                         void *);
void dict_copy_callbacks (struct dictionary *, const struct dictionary *);

void dict_set_change_callback (struct dictionary *d,
                               void (*changed) (struct dictionary *, void*),
                               void *data);


/* Debug use only. */
void dict_dump (const struct dictionary *);

#endif /* data/dictionary.h */
