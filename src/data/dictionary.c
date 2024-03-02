/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009, 2010, 2011, 2012, 2013, 2014,
   2015, 2020 Free Software Foundation, Inc.

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

#include "data/dictionary.h"

#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistr.h>

#include "data/attributes.h"
#include "data/case.h"
#include "data/identifier.h"
#include "data/mrset.h"
#include "data/settings.h"
#include "data/value-labels.h"
#include "data/vardict.h"
#include "data/variable.h"
#include "data/varset.h"
#include "data/vector.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"
#include "libpspp/ll.h"

#include "gl/intprops.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A dictionary. */
struct dictionary
  {
    int ref_cnt;
    struct vardict_info *vars;        /* Variables. */
    size_t n_vars;              /* Number of variables. */
    size_t allocated_vars;      /* Allocated space in 'vars'. */
    struct caseproto *proto;    /* Prototype for dictionary cases
                                   (updated lazily). */
    struct hmap name_map;        /* Variable index by name. */
    const struct variable **split;    /* SPLIT FILE vars. */
    size_t n_splits;            /* SPLIT FILE count. */
    enum split_type split_type;
    struct variable *weight;    /* WEIGHT variable. */
    struct variable *filter;    /* FILTER variable. */
    casenumber case_limit;      /* Current case limit (N command). */
    char *label;                /* File label. */
    struct string_array documents; /* Documents. */
    struct vector **vector;     /* Vectors of variables. */
    size_t n_vectors;           /* Number of vectors. */
    struct attrset attributes;  /* Custom attributes. */
    struct mrset **mrsets;      /* Multiple response sets. */
    size_t n_mrsets;            /* Number of multiple response sets. */
    struct varset **varsets;    /* Variable sets. */
    size_t n_varsets;           /* Number of variable sets. */

    /* Number of VAR### names created by dict_make_unique_var_name(), or
       less. */
    unsigned long int n_unique_names;

    /* Whether variable names must be valid identifiers.  Normally, this is
       true, but sometimes a dictionary is prepared for external use
       (e.g. output to a CSV file) where names don't have to be valid. */
    bool names_must_be_ids;

    char *encoding;             /* Character encoding of string data */

    const struct dict_callbacks *callbacks; /* Callbacks on dictionary
                                               modification */
    void *cb_data ;                  /* Data passed to callbacks */

    void (*changed) (struct dictionary *, void *); /* Generic change callback */
    void *changed_data;
  };

static void dict_unset_split_var (struct dictionary *, struct variable *, bool);
static void dict_unset_mrset_var (struct dictionary *, struct variable *);
static void dict_unset_varset_var (struct dictionary *, struct variable *);

/* Compares two double pointers to variables, which should point
   to elements of a struct dictionary's `var' member array. */
static int
compare_var_ptrs (const void *a_, const void *b_, const void *aux UNUSED)
{
  struct variable *const *a = a_;
  struct variable *const *b = b_;

  return *a < *b ? -1 : *a > *b;
}

static void
unindex_var (struct dictionary *d, struct vardict_info *vardict)
{
  hmap_delete (&d->name_map, &vardict->name_node);
}

/* This function assumes that vardict->name_node.hash is valid, that is, that
   its name has not changed since it was hashed (rename_var() updates this
   hash along with the name itself). */
static void
reindex_var (struct dictionary *d, struct vardict_info *vardict, bool skip_callbacks)
{
  struct variable *old = (d->callbacks && d->callbacks->var_changed
                          ? var_clone (vardict->var)
                          : NULL);

  struct variable *var = vardict->var;
  var_set_vardict (var, vardict);
  hmap_insert_fast (&d->name_map, &vardict->name_node,
                    vardict->name_node.hash);

  if (! skip_callbacks)
    {
      if (d->changed) d->changed (d, d->changed_data);
      if (old)
        {
          d->callbacks->var_changed (d, var_get_dict_index (var), VAR_TRAIT_POSITION, old, d->cb_data);
          var_unref (old);
        }
    }
}

/* Removes the dictionary variables with indexes from FROM to TO (exclusive)
   from name_map. */
static void
unindex_vars (struct dictionary *d, size_t from, size_t to)
{
  size_t i;

  for (i = from; i < to; i++)
    unindex_var (d, &d->vars[i]);
}

/* Re-sets the dict_index in the dictionary variables with
   indexes from FROM to TO (exclusive). */
static void
reindex_vars (struct dictionary *d, size_t from, size_t to, bool skip_callbacks)
{
  size_t i;

  for (i = from; i < to; i++)
    reindex_var (d, &d->vars[i], skip_callbacks);
}



/* Returns the encoding for data in dictionary D.  The return value is a
   nonnull string that contains an IANA character set name. */
const char *
dict_get_encoding (const struct dictionary *d)
{
  return d->encoding ;
}

/* Checks whether UTF-8 string ID is an acceptable identifier in DICT's
   encoding for a variable in the classes in CLASSES.  Returns true if it is,
   otherwise an error message that the caller must free(). */
char * WARN_UNUSED_RESULT
dict_id_is_valid__ (const struct dictionary *dict, const char *id,
                    enum dict_class classes)
{
  if (!dict->names_must_be_ids)
    return NULL;
  return id_is_valid__ (id, dict->encoding, classes);
}

static bool
error_to_bool (char *error)
{
  if (error)
    {
      free (error);
      return false;
    }
  else
    return true;
}

/* Returns true if UTF-8 string ID is an acceptable identifier in DICT's
   encoding for a variable in the classes in CLASSES, false otherwise. */
bool
dict_id_is_valid (const struct dictionary *dict, const char *id,
                  enum dict_class classes)
{
  return error_to_bool (dict_id_is_valid__ (dict, id, classes));
}

void
dict_set_change_callback (struct dictionary *d,
                          void (*changed) (struct dictionary *, void*),
                          void *data)
{
  d->changed = changed;
  d->changed_data = data;
}

/* Discards dictionary D's caseproto.  (It will be regenerated
   lazily, on demand.) */
static void
invalidate_proto (struct dictionary *d)
{
  caseproto_unref (d->proto);
  d->proto = NULL;
}

/* Print a representation of dictionary D to stdout, for
   debugging purposes. */
void
dict_dump (const struct dictionary *d)
{
  for (size_t i = 0; i < d->n_vars; ++i)
    printf ("%zu: %s\n", i, var_get_name (d->vars[i].var));
}

/* Associate CALLBACKS with DICT.  Callbacks will be invoked whenever
   the dictionary or any of the variables it contains are modified.
   Each callback will get passed CALLBACK_DATA.
   Any callback may be NULL, in which case it'll be ignored.
*/
void
dict_set_callbacks (struct dictionary *dict,
                    const struct dict_callbacks *callbacks,
                    void *callback_data)
{
  dict->callbacks = callbacks;
  dict->cb_data = callback_data;
}

/* Shallow copy the callbacks from SRC to DEST */
void
dict_copy_callbacks (struct dictionary *dest,
                     const struct dictionary *src)
{
  dest->callbacks = src->callbacks;
  dest->cb_data = src->cb_data;
}

/* Creates and returns a new dictionary with the specified ENCODING. */
struct dictionary *
dict_create (const char *encoding)
{
  struct dictionary *d = xmalloc (sizeof *d);

  *d = (struct dictionary) {
    .encoding = xstrdup (encoding),
    .names_must_be_ids = true,
    .name_map = HMAP_INITIALIZER (d->name_map),
    .attributes = ATTRSET_INITIALIZER (d->attributes),
    .split_type = SPLIT_NONE,
    .ref_cnt = 1,
  };

  return d;
}

/* Creates and returns a (deep) copy of an existing
   dictionary.

   Callbacks are not cloned. */
struct dictionary *
dict_clone (const struct dictionary *s)
{
  struct dictionary *d = dict_create (s->encoding);
  dict_set_names_must_be_ids (d, dict_get_names_must_be_ids (s));

  for (size_t i = 0; i < s->n_vars; i++)
    {
      struct variable *sv = s->vars[i].var;
      struct variable *dv = dict_clone_var_assert (d, sv);

      for (size_t j = 0; j < var_get_n_short_names (sv); j++)
        var_set_short_name (dv, j, var_get_short_name (sv, j));
    }

  d->n_splits = s->n_splits;
  if (d->n_splits > 0)
    {
       d->split = xnmalloc (d->n_splits, sizeof *d->split);
       for (size_t i = 0; i < d->n_splits; i++)
         d->split[i] = dict_lookup_var_assert (d, var_get_name (s->split[i]));
    }
  d->split_type = s->split_type;

  if (s->weight != NULL)
    dict_set_weight (d, dict_lookup_var_assert (d, var_get_name (s->weight)));

  if (s->filter != NULL)
    dict_set_filter (d, dict_lookup_var_assert (d, var_get_name (s->filter)));

  d->case_limit = s->case_limit;
  dict_set_label (d, dict_get_label (s));
  dict_set_documents (d, dict_get_documents (s));

  d->n_vectors = s->n_vectors;
  d->vector = xnmalloc (d->n_vectors, sizeof *d->vector);
  for (size_t i = 0; i < s->n_vectors; i++)
    d->vector[i] = vector_clone (s->vector[i], s, d);

  dict_set_attributes (d, dict_get_attributes (s));

  for (size_t i = 0; i < s->n_mrsets; i++)
    {
      const struct mrset *old = s->mrsets[i];
      struct mrset *new;
      size_t j;

      /* Clone old mrset, then replace vars from D by vars from S. */
      new = mrset_clone (old);
      for (j = 0; j < new->n_vars; j++)
        new->vars[j] = dict_lookup_var_assert (d, var_get_name (new->vars[j]));

      dict_add_mrset (d, new);
    }

  for (size_t i = 0; i < s->n_varsets; i++)
    {
      const struct varset *old = s->varsets[i];

      /* Clone old varset, then replace vars from D by vars from S. */
      struct varset *new = varset_clone (old);
      for (size_t j = 0; j < new->n_vars; j++)
        new->vars[j] = dict_lookup_var_assert (d, var_get_name (new->vars[j]));

      dict_add_varset (d, new);
    }

  return d;
}

/* Returns the SPLIT FILE vars (see cmd_split_file()).  Call
   dict_get_n_splits() to determine how many SPLIT FILE vars
   there are.  Returns a null pointer if and only if there are no
   SPLIT FILE vars. */
const struct variable *const *
dict_get_split_vars (const struct dictionary *d)
{
  return d->split;
}

/* Returns the number of SPLIT FILE vars. */
size_t
dict_get_n_splits (const struct dictionary *d)
{
  return d->n_splits;
}

/* Removes variable V, which must be in D, from D's set of split
   variables. */
static void
dict_unset_split_var (struct dictionary *d, struct variable *v, bool skip_callbacks)
{
  int orig_count;

  assert (dict_contains_var (d, v));

  orig_count = d->n_splits;
  d->n_splits = remove_equal (d->split, d->n_splits, sizeof *d->split,
                               &v, compare_var_ptrs, NULL);
  if (orig_count != d->n_splits && !skip_callbacks)
    {
      if (d->changed) d->changed (d, d->changed_data);
      /* We changed the set of split variables so invoke the
         callback. */
      if (d->callbacks &&  d->callbacks->split_changed)
        d->callbacks->split_changed (d, d->cb_data);
    }
}


/* Sets N split vars SPLIT in dictionary D.  N is silently capped to a maximum
   of MAX_SPLITS. */
static void
dict_set_split_vars__ (struct dictionary *d,
                       struct variable *const *split, size_t n,
                       enum split_type type, bool skip_callbacks)
{
  if (n > MAX_SPLITS)
    n = MAX_SPLITS;
  assert (n == 0 || split != NULL);

  d->n_splits = n;
  d->split_type = (n == 0 ? SPLIT_NONE
                   : type == SPLIT_NONE ? SPLIT_LAYERED
                   : type);
  if (n > 0)
    {
      d->split = xnrealloc (d->split, n, sizeof *d->split) ;
      memcpy (d->split, split, n * sizeof *d->split);
    }
  else
    {
      free (d->split);
      d->split = NULL;
    }

  if (!skip_callbacks)
    {
      if (d->changed) d->changed (d, d->changed_data);
      if (d->callbacks &&  d->callbacks->split_changed)
        d->callbacks->split_changed (d, d->cb_data);
    }
}

enum split_type
dict_get_split_type (const struct dictionary *d)
{
  return d->split_type;
}

/* Sets N split vars SPLIT in dictionary D. */
void
dict_set_split_vars (struct dictionary *d,
                     struct variable *const *split, size_t n,
                     enum split_type type)
{
  dict_set_split_vars__ (d, split, n, type, false);
}

void
dict_clear_split_vars (struct dictionary *d)
{
  dict_set_split_vars (d, NULL, 0, SPLIT_NONE);
}


/* Deletes variable V from dictionary D and frees V.

   This is a very bad idea if there might be any pointers to V
   from outside D.  In general, no variable in the active dataset's
   dictionary should be deleted when any transformations are
   active on the dictionary's dataset, because those
   transformations might reference the deleted variable.  The
   safest time to delete a variable is just after a procedure has
   been executed, as done by DELETE VARIABLES.

   Pointers to V within D are not a problem, because
   dict_delete_var() knows to remove V from split variables,
   weights, filters, etc. */
static void
dict_delete_var__ (struct dictionary *d, struct variable *v, bool skip_callbacks)
{
  d->n_unique_names = 0;

  int dict_index = var_get_dict_index (v);

  assert (dict_contains_var (d, v));

  dict_unset_split_var (d, v, skip_callbacks);
  dict_unset_mrset_var (d, v);
  dict_unset_varset_var (d, v);

  if (d->weight == v)
    dict_set_weight (d, NULL);

  if (d->filter == v)
    dict_set_filter (d, NULL);

  dict_clear_vectors (d);

  /* Remove V from var array. */
  unindex_vars (d, dict_index, d->n_vars);
  remove_element (d->vars, d->n_vars, sizeof *d->vars, dict_index);
  d->n_vars--;

  /* Update dict_index for each affected variable. */
  reindex_vars (d, dict_index, d->n_vars, skip_callbacks);

  /* Free memory. */
  var_clear_vardict (v);

  if (! skip_callbacks)
    {
      if (d->changed) d->changed (d, d->changed_data);
      if (d->callbacks &&  d->callbacks->vars_deleted)
        d->callbacks->vars_deleted (d, dict_index, 1, d->cb_data);
    }

  invalidate_proto (d);
  var_unref (v);
}

/* Deletes variable V from dictionary D and frees V.

   This is a very bad idea if there might be any pointers to V
   from outside D.  In general, no variable in the active dataset's
   dictionary should be deleted when any transformations are
   active on the dictionary's dataset, because those
   transformations might reference the deleted variable.  The
   safest time to delete a variable is just after a procedure has
   been executed, as done by DELETE VARIABLES.

   Pointers to V within D are not a problem, because
   dict_delete_var() knows to remove V from split variables,
   weights, filters, etc. */
void
dict_delete_var (struct dictionary *d, struct variable *v)
{
  dict_delete_var__ (d, v, false);
  invalidate_proto (d);
}


/* Deletes the COUNT variables listed in VARS from D.  This is
   unsafe; see the comment on dict_delete_var() for details. */
void
dict_delete_vars (struct dictionary *d,
                  struct variable *const *vars, size_t count)
{
  /* FIXME: this can be done in O(count) time, but this algorithm
     is O(count**2). */
  assert (count == 0 || vars != NULL);

  while (count-- > 0)
    dict_delete_var (d, *vars++);
  invalidate_proto (d);
}

/* Deletes the COUNT variables in D starting at index IDX.  This
   is unsafe; see the comment on dict_delete_var() for
   details. Deleting consecutive vars will result in less callbacks
   compared to iterating over dict_delete_var.
   A simple while loop over dict_delete_var will
   produce (d->n_vars - IDX) * COUNT variable changed callbacks
   plus COUNT variable delete callbacks.
   This here produces d->n_vars - IDX variable changed callbacks
   plus COUNT variable delete callbacks. */
void
dict_delete_consecutive_vars (struct dictionary *d, size_t idx, size_t count)
{
  assert (idx + count <= d->n_vars);

  struct variable **vars = xnmalloc (count, sizeof *vars);

  for (size_t i = 0; i < count; i++)
    {
      struct variable *v = d->vars[idx + i].var;
      vars[i] = v;

      dict_unset_split_var (d, v, false);
      dict_unset_mrset_var (d, v);
      dict_unset_varset_var (d, v);

      if (d->weight == v)
        dict_set_weight (d, NULL);

      if (d->filter == v)
        dict_set_filter (d, NULL);
    }

  dict_clear_vectors (d);

  /* Remove variables from var array. */
  unindex_vars (d, idx, d->n_vars);
  remove_range (d->vars, d->n_vars, sizeof *d->vars, idx, count);
  d->n_vars -= count;

  /* Reindexing will result variable-changed callback */
  reindex_vars (d, idx, d->n_vars, false);

  invalidate_proto (d);
  if (d->changed) d->changed (d, d->changed_data);

  /* Now issue the variable delete callbacks and delete
     the variables. The vardict is not valid at this point
     anymore. */
  if (d->callbacks &&  d->callbacks->vars_deleted)
    d->callbacks->vars_deleted (d, idx, count, d->cb_data);
  for (size_t i = 0; i < count; i++)
    {
      var_clear_vardict (vars[i]);
      var_unref (vars[i]);
    }
  free (vars);

  invalidate_proto (d);
}

/* Deletes scratch variables from dictionary D. */
void
dict_delete_scratch_vars (struct dictionary *d)
{
  int i;

  /* FIXME: this can be done in O(count) time, but this algorithm
     is O(count**2). */
  for (i = 0; i < d->n_vars;)
    if (var_get_dict_class (d->vars[i].var) == DC_SCRATCH)
      dict_delete_var (d, d->vars[i].var);
    else
      i++;

  invalidate_proto (d);
}



/* Clears the contents from a dictionary without destroying the
   dictionary itself. */
static void
dict_clear__ (struct dictionary *d, bool skip_callbacks)
{
  /* FIXME?  Should we really clear case_limit, label, documents?
     Others are necessarily cleared by deleting all the variables.*/
  while (d->n_vars > 0)
    dict_delete_var__ (d, d->vars[d->n_vars - 1].var, skip_callbacks);

  free (d->vars);
  d->vars = NULL;
  d->n_vars = d->allocated_vars = 0;
  invalidate_proto (d);
  hmap_clear (&d->name_map);
  dict_set_split_vars__ (d, NULL, 0, SPLIT_NONE, skip_callbacks);

  if (skip_callbacks)
    {
      d->weight = NULL;
      d->filter = NULL;
    }
  else
    {
      dict_set_weight (d, NULL);
      dict_set_filter (d, NULL);
    }
  d->case_limit = 0;
  free (d->label);
  d->label = NULL;
  string_array_clear (&d->documents);
  dict_clear_vectors (d);
  attrset_clear (&d->attributes);
}

/* Clears the contents from a dictionary without destroying the
   dictionary itself. */
void
dict_clear (struct dictionary *d)
{
  dict_clear__ (d, false);
}

/* Clears a dictionary and destroys it. */
static void
_dict_destroy (struct dictionary *d)
{
  /* In general, we don't want callbacks occurring, if the dictionary
     is being destroyed */
  d->callbacks  = NULL ;

  dict_clear__ (d, true);
  string_array_destroy (&d->documents);
  hmap_destroy (&d->name_map);
  attrset_destroy (&d->attributes);
  dict_clear_mrsets (d);
  dict_clear_varsets (d);
  free (d->encoding);
  free (d);
}

struct dictionary *
dict_ref (struct dictionary *d)
{
  d->ref_cnt++;
  return d;
}

void
dict_unref (struct dictionary *d)
{
  if (d == NULL)
    return;
  d->ref_cnt--;
  assert (d->ref_cnt >= 0);
  if (d->ref_cnt == 0)
    _dict_destroy (d);
}

/* Returns the number of variables in D. */
size_t
dict_get_n_vars (const struct dictionary *d)
{
  return d->n_vars;
}

/* Returns the variable in D with dictionary index IDX, which
   must be between 0 and the count returned by
   dict_get_n_vars(), exclusive. */
struct variable *
dict_get_var (const struct dictionary *d, size_t idx)
{
  assert (idx < d->n_vars);

  return d->vars[idx].var;
}

/* Sets *VARS to an array of pointers to variables in D and *N
   to the number of variables in *D.  All variables are returned
   except for those, if any, in the classes indicated by EXCLUDE.
   (There is no point in putting DC_SYSTEM in EXCLUDE as
   dictionaries never include system variables.) */
void
dict_get_vars (const struct dictionary *d, const struct variable ***vars,
               size_t *n, enum dict_class exclude)
{
  dict_get_vars_mutable (d, (struct variable ***) vars, n, exclude);
}

/* Sets *VARS to an array of pointers to variables in D and *N
   to the number of variables in *D.  All variables are returned
   except for those, if any, in the classes indicated by EXCLUDE.
   (There is no point in putting DC_SYSTEM in EXCLUDE as
   dictionaries never include system variables.) */
void
dict_get_vars_mutable (const struct dictionary *d, struct variable ***vars,
                       size_t *n, enum dict_class exclude)
{
  size_t count;
  size_t i;

  assert (exclude == (exclude & DC_ALL));

  count = 0;
  for (i = 0; i < d->n_vars; i++)
    {
      enum dict_class class = var_get_dict_class (d->vars[i].var);
      if (!(class & exclude))
        count++;
    }

  *vars = xnmalloc (count, sizeof **vars);
  *n = 0;
  for (i = 0; i < d->n_vars; i++)
    {
      enum dict_class class = var_get_dict_class (d->vars[i].var);
      if (!(class & exclude))
        (*vars)[(*n)++] = d->vars[i].var;
    }
  assert (*n == count);
}

static struct variable *
add_var (struct dictionary *d, struct variable *v)
{
  /* Update dictionary. */
  if (d->n_vars >= d->allocated_vars)
    {
      size_t i;

      d->vars = x2nrealloc (d->vars, &d->allocated_vars, sizeof *d->vars);
      hmap_clear (&d->name_map);
      for (i = 0; i < d->n_vars; i++)
        {
          var_set_vardict (d->vars[i].var, &d->vars[i]);
          hmap_insert_fast (&d->name_map, &d->vars[i].name_node,
                            d->vars[i].name_node.hash);
        }
    }

  struct vardict_info *vardict = &d->vars[d->n_vars++];
  *vardict = (struct vardict_info) {
    .dict = d,
    .var = v,
  };
  hmap_insert (&d->name_map, &vardict->name_node,
               utf8_hash_case_string (var_get_name (v), 0));
  var_set_vardict (v, vardict);

  if (d->changed) d->changed (d, d->changed_data);
  if (d->callbacks &&  d->callbacks->var_added)
    d->callbacks->var_added (d, var_get_dict_index (v), d->cb_data);

  invalidate_proto (d);

  return v;
}

/* Creates and returns a new variable in D with the given NAME
   and WIDTH.  Returns a null pointer if the given NAME would
   duplicate that of an existing variable in the dictionary. */
struct variable *
dict_create_var (struct dictionary *d, const char *name, int width)
{
  return (dict_lookup_var (d, name) == NULL
          ? dict_create_var_assert (d, name, width)
          : NULL);
}

/* Creates and returns a new variable in D with the given NAME
   and WIDTH.  Assert-fails if the given NAME would duplicate
   that of an existing variable in the dictionary. */
struct variable *
dict_create_var_assert (struct dictionary *d, const char *name, int width)
{
  assert (dict_lookup_var (d, name) == NULL);
  return add_var (d, var_create (name, width));
}

/* Creates and returns a new variable in D, as a copy of existing variable
   OLD_VAR, which need not be in D or in any dictionary.  Returns a null
   pointer if OLD_VAR's name would duplicate that of an existing variable in
   the dictionary. */
struct variable *
dict_clone_var (struct dictionary *d, const struct variable *old_var)
{
  return dict_clone_var_as (d, old_var, var_get_name (old_var));
}

/* Creates and returns a new variable in D, as a copy of existing variable
   OLD_VAR, which need not be in D or in any dictionary.  Assert-fails if
   OLD_VAR's name would duplicate that of an existing variable in the
   dictionary. */
struct variable *
dict_clone_var_assert (struct dictionary *d, const struct variable *old_var)
{
  return dict_clone_var_as_assert (d, old_var, var_get_name (old_var));
}

/* Creates and returns a new variable in D with name NAME, as a copy of
   existing variable OLD_VAR, which need not be in D or in any dictionary.
   Returns a null pointer if the given NAME would duplicate that of an existing
   variable in the dictionary. */
struct variable *
dict_clone_var_as (struct dictionary *d, const struct variable *old_var,
                   const char *name)
{
  return (dict_lookup_var (d, name) == NULL
          ? dict_clone_var_as_assert (d, old_var, name)
          : NULL);
}

/* Creates and returns a new variable in D with name NAME, as a copy of
   existing variable OLD_VAR, which need not be in D or in any dictionary.
   Assert-fails if the given NAME would duplicate that of an existing variable
   in the dictionary. */
struct variable *
dict_clone_var_as_assert (struct dictionary *d, const struct variable *old_var,
                          const char *name)
{
  struct variable *new_var = var_clone (old_var);
  assert (dict_lookup_var (d, name) == NULL);
  var_set_name (new_var, name);
  return add_var (d, new_var);
}

/* Creates and returns a new variable in DICT with the given WIDTH.  Uses HINT
   as the variable name, if it is nonnull, not already in use in DICT, and a
   valid name for a DC_ORDINARY variable; otherwise, chooses a unique name with
   HINT as a hint. */
struct variable *
dict_create_var_with_unique_name (struct dictionary *dict, const char *hint,
                                  int width)
{
  const char *name = (hint
                      && dict_id_is_valid (dict, hint, DC_ORDINARY)
                      && !dict_lookup_var (dict, hint)
                      ? hint
                      : dict_make_unique_var_name (dict, hint));
  struct variable *var = dict_create_var_assert (dict, name, width);
  if (name != hint)
    free (CONST_CAST (char *, name));
  return var;
}

/* Returns the variable named NAME in D, or a null pointer if no
   variable has that name. */
struct variable *
dict_lookup_var (const struct dictionary *d, const char *name)
{
  struct vardict_info *vardict;

  HMAP_FOR_EACH_WITH_HASH (vardict, struct vardict_info, name_node,
                           utf8_hash_case_string (name, 0), &d->name_map)
    {
      struct variable *var = vardict->var;
      if (!utf8_strcasecmp (var_get_name (var), name))
        return var;
    }

  return NULL;
}

/* Returns the variable named NAME in D.  Assert-fails if no
   variable has that name. */
struct variable *
dict_lookup_var_assert (const struct dictionary *d, const char *name)
{
  struct variable *v = dict_lookup_var (d, name);
  assert (v != NULL);
  return v;
}

/* Returns true if variable V is in dictionary D,
   false otherwise. */
bool
dict_contains_var (const struct dictionary *d, const struct variable *v)
{
  return (var_has_vardict (v)
          && vardict_get_dictionary (var_get_vardict (v)) == d);
}

/* Moves V to 0-based position IDX in D.  Other variables in D,
   if any, retain their relative positions.  Runs in time linear
   in the distance moved. */
void
dict_reorder_var (struct dictionary *d, struct variable *v, size_t new_index)
{
  assert (new_index < d->n_vars);

  size_t old_index = var_get_dict_index (v);
  if (new_index == old_index)
    return;

  unindex_vars (d, MIN (old_index, new_index), MAX (old_index, new_index) + 1);
  move_element (d->vars, d->n_vars, sizeof *d->vars, old_index, new_index);
  reindex_vars (d, MIN (old_index, new_index), MAX (old_index, new_index) + 1, false);

  if (d->callbacks && d->callbacks->var_moved)
    d->callbacks->var_moved (d, new_index, old_index, d->cb_data);
}

/* Reorders the variables in D, placing the COUNT variables
   listed in ORDER in that order at the beginning of D.  The
   other variables in D, if any, retain their relative
   positions. */
void
dict_reorder_vars (struct dictionary *d,
                   struct variable *const *order, size_t count)
{
  struct vardict_info *new_var;
  size_t i;

  assert (count == 0 || order != NULL);
  assert (count <= d->n_vars);

  new_var = xnmalloc (d->allocated_vars, sizeof *new_var);

  /* Add variables in ORDER to new_var. */
  for (i = 0; i < count; i++)
    {
      struct vardict_info *old_var;

      assert (dict_contains_var (d, order[i]));

      old_var = var_get_vardict (order[i]);
      new_var[i] = *old_var;
      old_var->dict = NULL;
    }

  /* Add remaining variables to new_var. */
  for (i = 0; i < d->n_vars; i++)
    if (d->vars[i].dict != NULL)
      new_var[count++] = d->vars[i];
  assert (count == d->n_vars);

  /* Replace old vardicts by new ones. */
  free (d->vars);
  d->vars = new_var;

  hmap_clear (&d->name_map);
  reindex_vars (d, 0, d->n_vars, false);
}

/* Changes the name of variable V that is currently in dictionary D to
   NEW_NAME. */
static void
rename_var (struct dictionary *d, struct variable *v, const char *new_name)
{
  d->n_unique_names = 0;

  struct vardict_info *vardict = var_get_vardict (v);
  var_clear_vardict (v);
  var_set_name (v, new_name);
  vardict->name_node.hash = utf8_hash_case_string (new_name, 0);
  var_set_vardict (v, vardict);
}

/* Tries to changes the name of V in D to name NEW_NAME.  Returns true if
   successful, false if a variable (other than V) with the given name already
   exists in D. */
bool
dict_try_rename_var (struct dictionary *d, struct variable *v,
                     const char *new_name)
{
  struct variable *conflict = dict_lookup_var (d, new_name);
  if (conflict && v != conflict)
    return false;

  struct variable *old = var_clone (v);
  unindex_var (d, var_get_vardict (v));
  rename_var (d, v, new_name);
  reindex_var (d, var_get_vardict (v), false);

  if (settings_get_algorithm () == ENHANCED)
    var_clear_short_names (v);

  if (d->changed) d->changed (d, d->changed_data);
  if (d->callbacks &&  d->callbacks->var_changed)
    d->callbacks->var_changed (d, var_get_dict_index (v), VAR_TRAIT_NAME, old, d->cb_data);

  var_unref (old);

  return true;
}

/* Changes the name of V in D to name NEW_NAME.  Assert-fails if
   a variable named NEW_NAME is already in D, except that
   NEW_NAME may be the same as V's existing name. */
void
dict_rename_var (struct dictionary *d, struct variable *v,
                 const char *new_name)
{
  bool ok UNUSED = dict_try_rename_var (d, v, new_name);
  assert (ok);
}

/* Renames COUNT variables specified in VARS to the names given
   in NEW_NAMES within dictionary D.  If the renaming would
   result in a duplicate variable name, returns false and stores a
   name that would be duplicated into *ERR_NAME (if ERR_NAME is
   non-null).  Otherwise, the renaming is successful, and true
   is returned. */
bool
dict_rename_vars (struct dictionary *d,
                  struct variable **vars, char **new_names, size_t count,
                  char **err_name)
{
  struct pool *pool;
  char **old_names;
  size_t i;

  assert (count == 0 || vars != NULL);
  assert (count == 0 || new_names != NULL);

  /* Save the names of the variables to be renamed. */
  pool = pool_create ();
  old_names = pool_nalloc (pool, count, sizeof *old_names);
  for (i = 0; i < count; i++)
    old_names[i] = pool_strdup (pool, var_get_name (vars[i]));

  /* Remove the variables to be renamed from the name hash,
     and rename them. */
  for (i = 0; i < count; i++)
    {
      unindex_var (d, var_get_vardict (vars[i]));
      rename_var (d, vars[i], new_names[i]);
    }

  /* Add the renamed variables back into the name hash,
     checking for conflicts. */
  for (i = 0; i < count; i++)
    {
      if (dict_lookup_var (d, var_get_name (vars[i])) != NULL)
        {
          /* There is a name conflict.
             Back out all the name changes that have already
             taken place, and indicate failure. */
          size_t fail_idx = i;
          if (err_name != NULL)
            *err_name = new_names[i];

          for (i = 0; i < fail_idx; i++)
            unindex_var (d, var_get_vardict (vars[i]));

          for (i = 0; i < count; i++)
            {
              rename_var (d, vars[i], old_names[i]);
              reindex_var (d, var_get_vardict (vars[i]), false);
            }

          pool_destroy (pool);
          return false;
        }
      reindex_var (d, var_get_vardict (vars[i]), false);
    }

  /* Clear short names. */
  if (settings_get_algorithm () == ENHANCED)
    for (i = 0; i < count; i++)
      var_clear_short_names (vars[i]);

  pool_destroy (pool);
  return true;
}

/* Returns true if a variable named NAME may be inserted in DICT;
   that is, if there is not already a variable with that name in
   DICT and if NAME is not a reserved word.  (The caller's checks
   have already verified that NAME is otherwise acceptable as a
   variable name.) */
static bool
var_name_is_insertable (const struct dictionary *dict, const char *name)
{
  return (dict_lookup_var (dict, name) == NULL
          && lex_id_to_token (ss_cstr (name)) == T_ID);
}

static char *
make_hinted_name (const struct dictionary *dict, const char *hint)
{
  size_t hint_len = strlen (hint);
  bool dropped = false;
  char *root, *rp;
  size_t ofs;
  int mblen;

  if (hint_len > ID_MAX_LEN)
    hint_len = ID_MAX_LEN;

  /* The allocation size here is OK: characters that are copied directly fit
     OK, and characters that are not copied directly are replaced by a single
     '_' byte.  If u8_mbtouc() replaces bad input by 0xfffd, then that will get
     replaced by '_' too.  */
  root = rp = xmalloc (hint_len + 1);
  for (ofs = 0; ofs < hint_len; ofs += mblen)
    {
      ucs4_t uc;

      mblen = u8_mbtouc (&uc, CHAR_CAST (const uint8_t *, hint + ofs),
                         hint_len - ofs);
      if (rp == root
          ? lex_uc_is_id1 (uc) && uc != '$' && uc != '#' && uc != '@'
          : lex_uc_is_idn (uc))
        {
          if (dropped)
            {
              *rp++ = '_';
              dropped = false;
            }
          rp += u8_uctomb (CHAR_CAST (uint8_t *, rp), uc, 6);
        }
      else if (rp != root)
        dropped = true;
    }
  *rp = '\0';

  if (root[0] != '\0')
    {
      unsigned long int i;

      if (var_name_is_insertable (dict, root))
        return root;

      for (i = 0; i < ULONG_MAX; i++)
        {
          char suffix[1 + F26ADIC_STRLEN_MAX + 1];

          suffix[0] = '_';
          str_format_26adic (i + 1, true, &suffix[1], sizeof suffix - 1);

          char *name = utf8_encoding_concat (root, suffix, dict->encoding, 64);
          if (var_name_is_insertable (dict, name))
            {
              free (root);
              return name;
            }
          free (name);
        }
    }

  free (root);

  return NULL;
}

static char *
make_numeric_name (struct dictionary *dict)
{
  while (dict->n_unique_names++ < ULONG_MAX)
    {
      char *name = xasprintf ("VAR%03lu", dict->n_unique_names);
      if (dict_lookup_var (dict, name) == NULL)
        return name;
      free (name);
    }

  NOT_REACHED ();
}

/* Devises and returns a variable name unique within DICT.  The variable name
   is owned by the caller, which must free it with free() when it is no longer
   needed.  The variable name will not begin with '$' or '#' or '@'.

   HINT, if it is non-null, is used as a suggestion that will be
   modified for suitability as a variable name and for
   uniqueness.

   If HINT is null or entirely unsuitable, uses a name in the form "VAR%03d",
   using the smallest available integer. */
char *
dict_make_unique_var_name (const struct dictionary *dict_, const char *hint)
{
  struct dictionary *dict = CONST_CAST (struct dictionary *, dict_);
  if (hint != NULL)
    {
      char *hinted_name = make_hinted_name (dict, hint);
      if (hinted_name != NULL)
        return hinted_name;
    }

  return make_numeric_name (dict);
}

/* Returns whether variable names must be valid identifiers.  Normally, this is
   true, but sometimes a dictionary is prepared for external use (e.g. output
   to a CSV file) where names don't have to be valid. */
bool
dict_get_names_must_be_ids (const struct dictionary *d)
{
  return d->names_must_be_ids;
}

/* Sets whether variable names must be valid identifiers.  Normally, this is
   true, but sometimes a dictionary is prepared for external use (e.g. output
   to a CSV file) where names don't have to be valid.

   Changing this setting from false to true doesn't make the dictionary check
   all the existing variable names, so it can cause an invariant violation. */
void
dict_set_names_must_be_ids (struct dictionary *d, bool names_must_be_ids)
{
  d->names_must_be_ids = names_must_be_ids;
}

/* Returns the weighting variable in dictionary D, or a null
   pointer if the dictionary is unweighted. */
struct variable *
dict_get_weight (const struct dictionary *d)
{
  assert (d->weight == NULL || dict_contains_var (d, d->weight));

  return d->weight;
}

/* Returns the value of D's weighting variable in case C, except
   that a negative or missing weight is returned as 0.  Returns 1 if the
   dictionary is unweighted.  Will warn about missing, negative,
   or zero values if *WARN_ON_INVALID is true.  The function will
   set *WARN_ON_INVALID to false if an invalid weight is
   found. */
double
dict_get_case_weight (const struct dictionary *d, const struct ccase *c,
                      bool *warn_on_invalid)
{
  assert (c != NULL);

  if (d->weight == NULL)
    return 1.0;
  else
    {
      double w = case_num (c, d->weight);

      return var_force_valid_weight (d->weight, w, warn_on_invalid);
    }
}

/* Like dict_get_case_weight(), but additionally rounds each weight to the
   nearest integer.  */
double
dict_get_rounded_case_weight (const struct dictionary *d,
                              const struct ccase *c, bool *warn_on_invalid)
{
  return floor (dict_get_case_weight (d, c, warn_on_invalid) + 0.5);
}

/* Returns the format to use for weights. */
struct fmt_spec
dict_get_weight_format (const struct dictionary *d)
{
  return d->weight ? var_get_print_format (d->weight) : F_8_0;
}

/* Sets the weighting variable of D to V, or turning off
   weighting if V is a null pointer. */
void
dict_set_weight (struct dictionary *d, struct variable *v)
{
  assert (v == NULL || dict_contains_var (d, v));
  assert (v == NULL || var_is_numeric (v));

  d->weight = v;

  if (d->changed) d->changed (d, d->changed_data);
  if (d->callbacks &&  d->callbacks->weight_changed)
    d->callbacks->weight_changed (d,
                                  v ? var_get_dict_index (v) : -1,
                                  d->cb_data);
}

/* Returns the filter variable in dictionary D (see cmd_filter())
   or a null pointer if the dictionary is unfiltered. */
struct variable *
dict_get_filter (const struct dictionary *d)
{
  assert (d->filter == NULL || dict_contains_var (d, d->filter));

  return d->filter;
}

/* Sets V as the filter variable for dictionary D.  Passing a
   null pointer for V turn off filtering. */
void
dict_set_filter (struct dictionary *d, struct variable *v)
{
  assert (v == NULL || dict_contains_var (d, v));
  assert (v == NULL || var_is_numeric (v));

  d->filter = v;

  if (d->changed) d->changed (d, d->changed_data);
  if (d->callbacks && d->callbacks->filter_changed)
    d->callbacks->filter_changed (d,
                                  v ? var_get_dict_index (v) : -1,
                                      d->cb_data);
}

/* Returns the case limit for dictionary D, or zero if the number
   of cases is unlimited. */
casenumber
dict_get_case_limit (const struct dictionary *d)
{
  return d->case_limit;
}

/* Sets CASE_LIMIT as the case limit for dictionary D.  Use
   0 for CASE_LIMIT to indicate no limit. */
void
dict_set_case_limit (struct dictionary *d, casenumber case_limit)
{
  d->case_limit = case_limit;
}

/* Returns the prototype used for cases created by dictionary D. */
const struct caseproto *
dict_get_proto (const struct dictionary *d_)
{
  struct dictionary *d = CONST_CAST (struct dictionary *, d_);
  if (d->proto == NULL)
    {
      short int *widths = xnmalloc (d->n_vars, sizeof *widths);
      for (size_t i = 0; i < d->n_vars; i++)
        widths[i] = var_get_width (d->vars[i].var);
      d->proto = caseproto_from_widths (widths, d->n_vars);
    }
  return d->proto;
}

/* Returns the file label for D, or a null pointer if D is
   unlabeled (see cmd_file_label()). */
const char *
dict_get_label (const struct dictionary *d)
{
  return d->label;
}

/* Sets D's file label to LABEL, truncating it to at most 60 bytes in D's
   encoding.

   Removes D's label if LABEL is null or the empty string. */
void
dict_set_label (struct dictionary *d, const char *label)
{
  free (d->label);
  if (label == NULL || label[0] == '\0')
    d->label = NULL;
  else
    d->label = utf8_encoding_trunc (label, d->encoding, 60);
}

/* Returns the documents for D, as an UTF-8 encoded string_array.  The
   return value is always nonnull; if there are no documents then the
   string_arary is empty.*/
const struct string_array *
dict_get_documents (const struct dictionary *d)
{
  return &d->documents;
}

/* Replaces the documents for D by NEW_DOCS, a UTF-8 encoded string_array. */
void
dict_set_documents (struct dictionary *d, const struct string_array *new_docs)
{
  /* Swap out the old documents, instead of destroying them immediately, to
     allow the new documents to include pointers into the old ones. */
  struct string_array old_docs = STRING_ARRAY_INITIALIZER;
  string_array_swap (&d->documents, &old_docs);

  for (size_t i = 0; i < new_docs->n; i++)
    dict_add_document_line (d, new_docs->strings[i], false);

  string_array_destroy (&old_docs);
}

/* Replaces the documents for D by UTF-8 encoded string NEW_DOCS, dividing it
   into individual lines at new-line characters.  Each line is truncated to at
   most DOC_LINE_LENGTH bytes in D's encoding. */
void
dict_set_documents_string (struct dictionary *d, const char *new_docs)
{
  const char *s;

  dict_clear_documents (d);
  for (s = new_docs; *s != '\0';)
    {
      size_t len = strcspn (s, "\n");
      char *line = xmemdup0 (s, len);
      dict_add_document_line (d, line, false);
      free (line);

      s += len;
      if (*s == '\n')
        s++;
    }
}

/* Drops the documents from dictionary D. */
void
dict_clear_documents (struct dictionary *d)
{
  string_array_clear (&d->documents);
}

/* Appends the UTF-8 encoded LINE to the documents in D.  LINE will be
   truncated so that it is no more than 80 bytes in the dictionary's
   encoding.  If this causes some text to be lost, and ISSUE_WARNING is true,
   then a warning will be issued. */
bool
dict_add_document_line (struct dictionary *d, const char *line,
                        bool issue_warning)
{
  size_t trunc_len;
  bool truncated;

  trunc_len = utf8_encoding_trunc_len (line, d->encoding, DOC_LINE_LENGTH);
  truncated = line[trunc_len] != '\0';
  if (truncated && issue_warning)
    {
      /* TRANSLATORS: "bytes" is correct, not characters due to UTF encoding */
      msg (SW, _("Truncating document line to %d bytes."), DOC_LINE_LENGTH);
    }

  string_array_append_nocopy (&d->documents, xmemdup0 (line, trunc_len));

  return !truncated;
}

/* Returns the number of document lines in dictionary D. */
size_t
dict_get_document_n_lines (const struct dictionary *d)
{
  return d->documents.n;
}

/* Returns document line number IDX in dictionary D.  The caller must not
   modify or free the returned string. */
const char *
dict_get_document_line (const struct dictionary *d, size_t idx)
{
  assert (idx < d->documents.n);
  return d->documents.strings[idx];
}

/* Creates in D a vector named NAME that contains the N
   variables in VAR.  Returns true if successful, or false if a
   vector named NAME already exists in D. */
bool
dict_create_vector (struct dictionary *d,
                    const char *name,
                    struct variable **var, size_t n)
{
  assert (n > 0);
  for (size_t i = 0; i < n; i++)
    assert (dict_contains_var (d, var[i]));

  if (dict_lookup_vector (d, name) == NULL)
    {
      d->vector = xnrealloc (d->vector, d->n_vectors + 1, sizeof *d->vector);
      d->vector[d->n_vectors++] = vector_create (name, var, n);
      return true;
    }
  else
    return false;
}

/* Creates in D a vector named NAME that contains the N
   variables in VAR.  A vector named NAME must not already exist
   in D. */
void
dict_create_vector_assert (struct dictionary *d,
                           const char *name,
                           struct variable **var, size_t n)
{
  assert (dict_lookup_vector (d, name) == NULL);
  dict_create_vector (d, name, var, n);
}

/* Returns the vector in D with index IDX, which must be less
   than dict_get_n_vectors (D). */
const struct vector *
dict_get_vector (const struct dictionary *d, size_t idx)
{
  assert (idx < d->n_vectors);

  return d->vector[idx];
}

/* Returns the number of vectors in D. */
size_t
dict_get_n_vectors (const struct dictionary *d)
{
  return d->n_vectors;
}

/* Looks up and returns the vector within D with the given
   NAME. */
const struct vector *
dict_lookup_vector (const struct dictionary *d, const char *name)
{
  size_t i;
  for (i = 0; i < d->n_vectors; i++)
    if (!utf8_strcasecmp (vector_get_name (d->vector[i]), name))
      return d->vector[i];
  return NULL;
}

/* Deletes all vectors from D. */
void
dict_clear_vectors (struct dictionary *d)
{
  size_t i;

  for (i = 0; i < d->n_vectors; i++)
    vector_destroy (d->vector[i]);
  free (d->vector);

  d->vector = NULL;
  d->n_vectors = 0;
}

/* Multiple response sets. */

/* Returns the multiple response set in DICT with index IDX, which must be
   between 0 and the count returned by dict_get_n_mrsets(), exclusive. */
const struct mrset *
dict_get_mrset (const struct dictionary *dict, size_t idx)
{
  assert (idx < dict->n_mrsets);
  return dict->mrsets[idx];
}

/* Returns the number of multiple response sets in DICT. */
size_t
dict_get_n_mrsets (const struct dictionary *dict)
{
  return dict->n_mrsets;
}

/* Looks for a multiple response set named NAME in DICT.  If it finds one,
   returns its index; otherwise, returns SIZE_MAX. */
static size_t
dict_lookup_mrset_idx (const struct dictionary *dict, const char *name)
{
  size_t i;

  for (i = 0; i < dict->n_mrsets; i++)
    if (!utf8_strcasecmp (name, dict->mrsets[i]->name))
      return i;

  return SIZE_MAX;
}

/* Looks for a multiple response set named NAME in DICT.  If it finds one,
   returns it; otherwise, returns NULL. */
const struct mrset *
dict_lookup_mrset (const struct dictionary *dict, const char *name)
{
  size_t idx = dict_lookup_mrset_idx (dict, name);
  return idx != SIZE_MAX ? dict->mrsets[idx] : NULL;
}

/* Adds MRSET to DICT, replacing any existing set with the same name.  Returns
   true if a set was replaced, false if none existed with the specified name.

   Ownership of MRSET is transferred to DICT. */
bool
dict_add_mrset (struct dictionary *dict, struct mrset *mrset)
{
  size_t idx;

  assert (mrset_ok (mrset, dict));

  idx = dict_lookup_mrset_idx (dict, mrset->name);
  if (idx == SIZE_MAX)
    {
      dict->mrsets = xrealloc (dict->mrsets,
                               (dict->n_mrsets + 1) * sizeof *dict->mrsets);
      dict->mrsets[dict->n_mrsets++] = mrset;
      return true;
    }
  else
    {
      mrset_destroy (dict->mrsets[idx]);
      dict->mrsets[idx] = mrset;
      return false;
    }
}

/* Looks for a multiple response set in DICT named NAME.  If found, removes it
   from DICT and returns true.  If none is found, returns false without
   modifying DICT.

   Deleting one multiple response set causes the indexes of other sets within
   DICT to change. */
bool
dict_delete_mrset (struct dictionary *dict, const char *name)
{
  size_t idx = dict_lookup_mrset_idx (dict, name);
  if (idx != SIZE_MAX)
    {
      mrset_destroy (dict->mrsets[idx]);
      dict->mrsets[idx] = dict->mrsets[--dict->n_mrsets];
      return true;
    }
  else
    return false;
}

/* Deletes all multiple response sets from DICT. */
void
dict_clear_mrsets (struct dictionary *dict)
{
  size_t i;

  for (i = 0; i < dict->n_mrsets; i++)
    mrset_destroy (dict->mrsets[i]);
  free (dict->mrsets);
  dict->mrsets = NULL;
  dict->n_mrsets = 0;
}

/* Removes VAR, which must be in DICT, from DICT's multiple response sets. */
static void
dict_unset_mrset_var (struct dictionary *dict, struct variable *var)
{
  size_t i;

  assert (dict_contains_var (dict, var));

  for (i = 0; i < dict->n_mrsets;)
    {
      struct mrset *mrset = dict->mrsets[i];
      size_t j;

      for (j = 0; j < mrset->n_vars;)
        if (mrset->vars[j] == var)
          remove_element (mrset->vars, mrset->n_vars--,
                          sizeof *mrset->vars, j);
        else
          j++;

      if (mrset->n_vars < 2)
        {
          mrset_destroy (mrset);
          dict->mrsets[i] = dict->mrsets[--dict->n_mrsets];
        }
      else
        i++;
    }
}


/* Returns the variable set in DICT with index IDX, which must be between 0 and
   the count returned by dict_get_n_varsets(), exclusive. */
const struct varset *
dict_get_varset (const struct dictionary *dict, size_t idx)
{
  assert (idx < dict->n_varsets);
  return dict->varsets[idx];
}

/* Returns the number of variable sets in DICT. */
size_t
dict_get_n_varsets (const struct dictionary *dict)
{
  return dict->n_varsets;
}

/* Looks for a variable set named NAME in DICT.  If it finds one, returns its
   index; otherwise, returns SIZE_MAX. */
static size_t
dict_lookup_varset_idx (const struct dictionary *dict, const char *name)
{
  for (size_t i = 0; i < dict->n_varsets; i++)
    if (!utf8_strcasecmp (name, dict->varsets[i]->name))
      return i;

  return SIZE_MAX;
}

/* Looks for a multiple response set named NAME in DICT.  If it finds one,
   returns it; otherwise, returns NULL. */
const struct varset *
dict_lookup_varset (const struct dictionary *dict, const char *name)
{
  size_t idx = dict_lookup_varset_idx (dict, name);
  return idx != SIZE_MAX ? dict->varsets[idx] : NULL;
}

/* Adds VARSET to DICT, replacing any existing set with the same name.  Returns
   true if a set was replaced, false if none existed with the specified name.

   Ownership of VARSET is transferred to DICT. */
bool
dict_add_varset (struct dictionary *dict, struct varset *varset)
{
  size_t idx = dict_lookup_varset_idx (dict, varset->name);
  if (idx == SIZE_MAX)
    {
      dict->varsets = xrealloc (dict->varsets,
                               (dict->n_varsets + 1) * sizeof *dict->varsets);
      dict->varsets[dict->n_varsets++] = varset;
      return true;
    }
  else
    {
      varset_destroy (dict->varsets[idx]);
      dict->varsets[idx] = varset;
      return false;
    }
}

/* Deletes all variable sets from DICT. */
void
dict_clear_varsets (struct dictionary *dict)
{
  for (size_t i = 0; i < dict->n_varsets; i++)
    varset_destroy (dict->varsets[i]);
  free (dict->varsets);
  dict->varsets = NULL;
  dict->n_varsets = 0;
}

/* Removes VAR, which must be in DICT, from DICT's multiple response sets. */
static void
dict_unset_varset_var (struct dictionary *dict, struct variable *var)
{
  assert (dict_contains_var (dict, var));

  for (size_t i = 0; i < dict->n_varsets; i++)
    {
      struct varset *varset = dict->varsets[i];

      for (size_t j = 0; j < varset->n_vars;)
        if (varset->vars[j] == var)
          remove_element (varset->vars, varset->n_vars--,
                          sizeof *varset->vars, j);
        else
          j++;
    }
}

/* Returns D's attribute set.  The caller may examine or modify
   the attribute set, but must not destroy it.  Destroying D or
   calling dict_set_attributes for D will also destroy D's
   attribute set. */
struct attrset *
dict_get_attributes (const struct dictionary *d)
{
  return CONST_CAST (struct attrset *, &d->attributes);
}

/* Replaces D's attributes set by a copy of ATTRS. */
void
dict_set_attributes (struct dictionary *d, const struct attrset *attrs)
{
  attrset_destroy (&d->attributes);
  attrset_clone (&d->attributes, attrs);
}

/* Returns true if D has at least one attribute in its attribute
   set, false if D's attribute set is empty. */
bool
dict_has_attributes (const struct dictionary *d)
{
  return attrset_count (&d->attributes) > 0;
}

/* Called from variable.c to notify the dictionary that some property (indicated
   by WHAT) of the variable has changed.  OLDVAR is a copy of V as it existed
   prior to the change.  OLDVAR is destroyed by this function.
*/
void
dict_var_changed (const struct variable *v, unsigned int what, struct variable *oldvar)
{
  if (var_has_vardict (v))
    {
      const struct vardict_info *vardict = var_get_vardict (v);
      struct dictionary *d = vardict->dict;

      if (NULL == d)
        return;

      if (what & (VAR_TRAIT_WIDTH | VAR_TRAIT_POSITION))
        invalidate_proto (d);

      if (d->changed) d->changed (d, d->changed_data);
      if (d->callbacks && d->callbacks->var_changed)
        d->callbacks->var_changed (d, var_get_dict_index (v), what, oldvar, d->cb_data);
    }
  var_unref (oldvar);
}



int
vardict_get_dict_index (const struct vardict_info *vardict)
{
  return vardict - vardict->dict->vars;
}
