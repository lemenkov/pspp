/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009, 2010, 2011, 2013 Free Software Foundation, Inc.

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

#include "data/dataset.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "data/case.h"
#include "data/case-map.h"
#include "data/caseinit.h"
#include "data/casereader.h"
#include "data/casereader-provider.h"
#include "data/casereader-shim.h"
#include "data/casewriter.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/session.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "libpspp/deque.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"
#include "libpspp/taint.h"
#include "libpspp/i18n.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

struct dataset {
  /* A dataset is usually part of a session.  Within a session its name must
     unique.  The name must either be a valid PSPP identifier or the empty
     string.  (It must be unique within the session even if it is the empty
     string; that is, there may only be a single dataset within a session with
     the empty string as its name.) */
  struct session *session;
  char *name;
  enum dataset_display display;

  /* Cases are read from source,
     their transformation variables are initialized,
     pass through permanent_trns_chain (which transforms them into
     the format described by permanent_dict),
     are written to sink,
     pass through temporary_trns_chain (which transforms them into
     the format described by dict),
     and are finally passed to the procedure. */
  struct casereader *source;
  struct caseinit *caseinit;
  struct trns_chain permanent_trns_chain;
  struct dictionary *permanent_dict;
  struct variable *order_var;
  struct casewriter *sink;
  struct trns_chain temporary_trns_chain;
  bool temporary;
  struct dictionary *dict;

  /* Stack of transformation chains for DO IF and LOOP and INPUT PROGRAM. */
  struct trns_chain *stack;
  size_t n_stack;
  size_t allocated_stack;

  /* If true, cases are discarded instead of being written to
     sink. */
  bool discard_output;

  /* Time at which proc was last invoked. */
  time_t last_proc_invocation;

  /* Cases just before ("lagging") the current one. */
  int n_lag;                        /* Number of cases to lag. */
  struct deque lag;             /* Deque of lagged cases. */
  struct ccase **lag_cases;     /* Lagged cases managed by deque. */

  /* Procedure data. */
  enum
    {
      PROC_COMMITTED,           /* No procedure in progress. */
      PROC_OPEN,                /* proc_open called, casereader still open. */
      PROC_CLOSED               /* casereader from proc_open destroyed,
                                   but proc_commit not yet called. */
    }
  proc_state;
  casenumber cases_written;     /* Cases output so far. */
  bool ok;                      /* Error status. */
  struct casereader_shim *shim; /* Shim on proc_open() casereader. */

  const struct dataset_callbacks *callbacks;
  void *cb_data;

  /* Uniquely distinguishes datasets. */
  unsigned int seqno;
};

static void dataset_changed__ (struct dataset *);
static void dataset_transformations_changed__ (struct dataset *,
                                               bool non_empty);

static void add_measurement_level_trns (struct dataset *, struct dictionary *);
static void cancel_measurement_level_trns (struct trns_chain *);
static void add_case_limit_trns (struct dataset *ds);
static void add_filter_trns (struct dataset *ds);

static void update_last_proc_invocation (struct dataset *ds);

static void
dict_callback (struct dictionary *d UNUSED, void *ds_)
{
  struct dataset *ds = ds_;
  dataset_changed__ (ds);
}

static void
dataset_create_finish__ (struct dataset *ds, struct session *session)
{
  static unsigned int seqno;

  dict_set_change_callback (ds->dict, dict_callback, ds);
  proc_cancel_all_transformations (ds);
  dataset_set_session (ds, session);
  ds->seqno = ++seqno;
}

/* Creates a new dataset named NAME, adds it to SESSION, and returns it.  If
   SESSION already contains a dataset named NAME, it is deleted and replaced.
   The dataset initially has an empty dictionary and no data source. */
struct dataset *
dataset_create (struct session *session, const char *name)
{
  struct dataset *ds = XMALLOC (struct dataset);
  *ds = (struct dataset) {
    .name = xstrdup (name),
    .display = DATASET_FRONT,
    .dict = dict_create (get_default_encoding ()),
    .caseinit = caseinit_create (),
  };
  dataset_create_finish__ (ds, session);

  return ds;
}

/* Creates and returns a new dataset that has the same data and dictionary as
   OLD named NAME, adds it to the same session as OLD, and returns the new
   dataset.  If SESSION already contains a dataset named NAME, it is deleted
   and replaced.

   OLD must not have any active transformations or temporary state and must
   not be in the middle of a procedure.

   Callbacks are not cloned. */
struct dataset *
dataset_clone (struct dataset *old, const char *name)
{
  struct dataset *new;

  assert (old->proc_state == PROC_COMMITTED);
  assert (!old->permanent_trns_chain.n);
  assert (old->permanent_dict == NULL);
  assert (old->sink == NULL);
  assert (!old->temporary);
  assert (!old->temporary_trns_chain.n);
  assert (!old->n_stack);

  new = xzalloc (sizeof *new);
  new->name = xstrdup (name);
  new->display = DATASET_FRONT;
  new->source = casereader_clone (old->source);
  new->dict = dict_clone (old->dict);
  new->caseinit = caseinit_clone (old->caseinit);
  new->last_proc_invocation = old->last_proc_invocation;
  new->ok = old->ok;

  dataset_create_finish__ (new, old->session);

  return new;
}

/* Destroys DS. */
void
dataset_destroy (struct dataset *ds)
{
  if (ds != NULL)
    {
      dataset_set_session (ds, NULL);
      dataset_clear (ds);
      dict_unref (ds->dict);
      dict_unref (ds->permanent_dict);
      caseinit_destroy (ds->caseinit);
      trns_chain_uninit (&ds->permanent_trns_chain);
      for (size_t i = 0; i < ds->n_stack; i++)
        trns_chain_uninit (&ds->stack[i]);
      free (ds->stack);
      dataset_transformations_changed__ (ds, false);
      free (ds->name);
      free (ds);
    }
}

/* Discards the active dataset's dictionary, data, and transformations. */
void
dataset_clear (struct dataset *ds)
{
  assert (ds->proc_state == PROC_COMMITTED);

  dict_clear (ds->dict);
  fh_set_default_handle (NULL);

  ds->n_lag = 0;

  casereader_destroy (ds->source);
  ds->source = NULL;

  proc_cancel_all_transformations (ds);
}

const char *
dataset_name (const struct dataset *ds)
{
  return ds->name;
}

void
dataset_set_name (struct dataset *ds, const char *name)
{
  struct session *session = ds->session;
  bool active = false;

  if (session != NULL)
    {
      active = session_active_dataset (session) == ds;
      if (active)
        session_set_active_dataset (session, NULL);
      dataset_set_session (ds, NULL);
    }

  free (ds->name);
  ds->name = xstrdup (name);

  if (session != NULL)
    {
      dataset_set_session (ds, session);
      if (active)
        session_set_active_dataset (session, ds);
    }
}

struct session *
dataset_session (const struct dataset *ds)
{
  return ds->session;
}

void
dataset_set_session (struct dataset *ds, struct session *session)
{
  if (session != ds->session)
    {
      if (ds->session != NULL)
        session_remove_dataset (ds->session, ds);
      if (session != NULL)
        session_add_dataset (session, ds);
    }
}

/* Returns the dictionary within DS.  This is always nonnull, although it
   might not contain any variables. */
struct dictionary *
dataset_dict (const struct dataset *ds)
{
  return ds->dict;
}

/* Replaces DS's dictionary by DICT, discarding any source and
   transformations. */
void
dataset_set_dict (struct dataset *ds, struct dictionary *dict)
{
  assert (ds->proc_state == PROC_COMMITTED);
  assert (ds->dict != dict);

  dataset_clear (ds);

  dict_unref (ds->dict);
  ds->dict = dict;
  dict_set_change_callback (ds->dict, dict_callback, ds);
}

/* Returns the casereader that will be read when a procedure is executed on
   DS.  This can be NULL if none has been set up yet. */
const struct casereader *
dataset_source (const struct dataset *ds)
{
  return ds->source;
}

/* Returns true if DS has a data source, false otherwise. */
bool
dataset_has_source (const struct dataset *ds)
{
  return dataset_source (ds) != NULL;
}

/* Replaces the active dataset's data by READER.  READER's cases must have an
   appropriate format for DS's dictionary. */
bool
dataset_set_source (struct dataset *ds, struct casereader *reader)
{
  casereader_destroy (ds->source);
  ds->source = reader;

  caseinit_clear (ds->caseinit);
  caseinit_mark_as_preinited (ds->caseinit, ds->dict);

  return reader == NULL || !casereader_error (reader);
}

/* Returns the data source from DS and removes it from DS.  Returns a null
   pointer if DS has no data source. */
struct casereader *
dataset_steal_source (struct dataset *ds)
{
  struct casereader *reader = ds->source;
  ds->source = NULL;

  return reader;
}

void
dataset_delete_vars (struct dataset *ds, struct variable **vars, size_t n)
{
  assert (!proc_in_temporary_transformations (ds));
  assert (!proc_has_transformations (ds));
  assert (n < dict_get_n_vars (ds->dict));

  caseinit_mark_for_init (ds->caseinit, ds->dict);
  ds->source = caseinit_translate_casereader_to_init_vars (
    ds->caseinit, dict_get_proto (ds->dict), ds->source);
  caseinit_clear (ds->caseinit);
  caseinit_mark_as_preinited (ds->caseinit, ds->dict);

  struct case_map_stage *stage = case_map_stage_create (ds->dict);
  dict_delete_vars (ds->dict, vars, n);
  ds->source = case_map_create_input_translator (
    case_map_stage_to_case_map (stage), ds->source);
  caseinit_clear (ds->caseinit);
  caseinit_mark_as_preinited (ds->caseinit, ds->dict);
}

void
dataset_reorder_vars (struct dataset *ds, struct variable **vars, size_t n)
{
  assert (!proc_in_temporary_transformations (ds));
  assert (!proc_has_transformations (ds));
  assert (n <= dict_get_n_vars (ds->dict));

  caseinit_mark_for_init (ds->caseinit, ds->dict);
  ds->source = caseinit_translate_casereader_to_init_vars (
    ds->caseinit, dict_get_proto (ds->dict), ds->source);
  caseinit_clear (ds->caseinit);
  caseinit_mark_as_preinited (ds->caseinit, ds->dict);

  struct case_map_stage *stage = case_map_stage_create (ds->dict);
  dict_reorder_vars (ds->dict, vars, n);
  ds->source = case_map_create_input_translator (
    case_map_stage_to_case_map (stage), ds->source);
  caseinit_clear (ds->caseinit);
  caseinit_mark_as_preinited (ds->caseinit, ds->dict);
}

/* Returns a number unique to DS.  It can be used to distinguish one dataset
   from any other within a given program run, even datasets that do not exist
   at the same time. */
unsigned int
dataset_seqno (const struct dataset *ds)
{
  return ds->seqno;
}

void
dataset_set_callbacks (struct dataset *ds,
                       const struct dataset_callbacks *callbacks,
                       void *cb_data)
{
  ds->callbacks = callbacks;
  ds->cb_data = cb_data;
}

enum dataset_display
dataset_get_display (const struct dataset *ds)
{
  return ds->display;
}

void
dataset_set_display (struct dataset *ds, enum dataset_display display)
{
  ds->display = display;
}

/* Returns the last time the data was read. */
time_t
time_of_last_procedure (struct dataset *ds)
{
  if (!ds)
    return time (NULL);
  if (ds->last_proc_invocation == 0)
    update_last_proc_invocation (ds);
  return ds->last_proc_invocation;
}

/* Regular procedure. */

/* Executes any pending transformations, if necessary.
   This is not identical to the EXECUTE command in that it won't
   always read the source data.  This can be important when the
   source data is given inline within BEGIN DATA...END FILE. */
bool
proc_execute (struct dataset *ds)
{
  bool ok;

  if ((!ds->temporary || !ds->temporary_trns_chain.n)
      && !ds->permanent_trns_chain.n)
    {
      ds->n_lag = 0;
      ds->discard_output = false;
      dict_set_case_limit (ds->dict, 0);
      dict_clear_vectors (ds->dict);
      return true;
    }

  ok = casereader_destroy (proc_open (ds));
  return proc_commit (ds) && ok;
}

static const struct casereader_class proc_casereader_class;

/* Opens dataset DS for reading cases with proc_read.  If FILTER is true, then
   cases filtered out with FILTER BY will not be included in the casereader
   (which is usually desirable).  If FILTER is false, all cases will be
   included regardless of FILTER BY settings.

   proc_commit must be called when done. */
struct casereader *
proc_open_filtering (struct dataset *ds, bool filter)
{
  struct casereader *reader;

  assert (ds->n_stack == 0);
  assert (ds->source != NULL);
  assert (ds->proc_state == PROC_COMMITTED);

  update_last_proc_invocation (ds);

  caseinit_mark_for_init (ds->caseinit, ds->dict);
  ds->source = caseinit_translate_casereader_to_init_vars (
    ds->caseinit, dict_get_proto (ds->dict), ds->source);

  /* Finish up the collection of transformations. */
  add_case_limit_trns (ds);
  if (filter)
    add_filter_trns (ds);
  if (!proc_in_temporary_transformations (ds))
    add_measurement_level_trns (ds, ds->dict);

  /* Make permanent_dict refer to the dictionary right before
     data reaches the sink. */
  if (ds->permanent_dict == NULL)
    ds->permanent_dict = ds->dict;

  /* Prepare sink. */
  if (!ds->discard_output)
    {
      struct dictionary *pd = dict_clone (ds->permanent_dict);
      struct case_map_stage *stage = case_map_stage_create (pd);
      dict_delete_scratch_vars (pd);
      ds->sink = case_map_create_output_translator (
        case_map_stage_to_case_map (stage),
        autopaging_writer_create (dict_get_proto (pd)));
      dict_unref (pd);
    }
  else
    ds->sink = NULL;

  /* Allocate memory for lagged cases. */
  ds->lag_cases = deque_init (&ds->lag, ds->n_lag, sizeof *ds->lag_cases);

  ds->proc_state = PROC_OPEN;
  ds->cases_written = 0;
  ds->ok = true;

  /* FIXME: use taint in dataset in place of `ok'? */
  /* FIXME: for trivial cases we can just return a clone of
     ds->source? */

  /* Create casereader and insert a shim on top.  The shim allows us to
     arbitrarily extend the casereader's lifetime, by slurping the cases into
     the shim's buffer in proc_commit().  That is especially useful when output
     table_items are generated directly from the procedure casereader (e.g. by
     the LIST procedure) when we are using an output driver that keeps a
     reference to the output items passed to it (e.g. the GUI output driver in
     PSPPIRE). */
  reader = casereader_create_sequential (NULL, dict_get_proto (ds->dict),
                                         CASENUMBER_MAX,
                                         &proc_casereader_class, ds);
  ds->shim = casereader_shim_insert (reader);
  return reader;
}

/* Opens dataset DS for reading cases with proc_read.
   proc_commit must be called when done. */
struct casereader *
proc_open (struct dataset *ds)
{
  return proc_open_filtering (ds, true);
}

/* Returns true if a procedure is in progress, that is, if
   proc_open has been called but proc_commit has not. */
bool
proc_is_open (const struct dataset *ds)
{
  return ds->proc_state != PROC_COMMITTED;
}

/* "read" function for procedure casereader. */
static struct ccase *
proc_casereader_read (struct casereader *reader UNUSED, void *ds_)
{
  struct dataset *ds = ds_;
  enum trns_result retval = TRNS_DROP_CASE;
  struct ccase *c;

  assert (ds->proc_state == PROC_OPEN);
  for (; ; case_unref (c))
    {
      assert (retval == TRNS_DROP_CASE || retval == TRNS_ERROR);
      if (retval == TRNS_ERROR)
        ds->ok = false;
      if (!ds->ok)
        return NULL;

      /* Read a case from source. */
      c = casereader_read (ds->source);
      if (c == NULL)
        return NULL;
      c = case_unshare_and_resize (c, dict_get_proto (ds->dict));
      caseinit_restore_left_vars (ds->caseinit, c);

      /* Execute permanent transformations.  */
      casenumber case_nr = ds->cases_written + 1;
      retval = trns_chain_execute (&ds->permanent_trns_chain, case_nr, &c);
      caseinit_save_left_vars (ds->caseinit, c);
      if (retval != TRNS_CONTINUE)
        continue;

      /* Write case to collection of lagged cases. */
      if (ds->n_lag > 0)
        {
          while (deque_count (&ds->lag) >= ds->n_lag)
            case_unref (ds->lag_cases[deque_pop_back (&ds->lag)]);
          ds->lag_cases[deque_push_front (&ds->lag)] = case_ref (c);
        }

      /* Write case to replacement dataset. */
      ds->cases_written++;
      if (ds->sink != NULL)
        {
          if (ds->order_var)
            *case_num_rw (c, ds->order_var) = case_nr;
          casewriter_write (ds->sink, case_ref (c));
        }

      /* Execute temporary transformations. */
      if (ds->temporary_trns_chain.n)
        {
          retval = trns_chain_execute (&ds->temporary_trns_chain,
                                       ds->cases_written, &c);
          if (retval != TRNS_CONTINUE)
            continue;
        }

      return c;
    }
}

/* "destroy" function for procedure casereader. */
static void
proc_casereader_destroy (struct casereader *reader, void *ds_)
{
  struct dataset *ds = ds_;
  struct ccase *c;

  /* We are always the subreader for a casereader_buffer, so if we're being
     destroyed then it's because the casereader_buffer has read all the cases
     that it ever will. */
  ds->shim = NULL;

  /* Make sure transformations happen for every input case, in
     case they have side effects, and ensure that the replacement
     active dataset gets all the cases it should. */
  while ((c = casereader_read (reader)) != NULL)
    case_unref (c);

  ds->proc_state = PROC_CLOSED;
  ds->ok = casereader_destroy (ds->source) && ds->ok;
  ds->source = NULL;
  dataset_set_source (ds, NULL);
}

/* Must return false if the source casereader, a transformation,
   or the sink casewriter signaled an error.  (If a temporary
   transformation signals an error, then the return value is
   false, but the replacement active dataset may still be
   untainted.) */
bool
proc_commit (struct dataset *ds)
{
  if (ds->shim != NULL)
    casereader_shim_slurp (ds->shim);

  assert (ds->proc_state == PROC_CLOSED);
  ds->proc_state = PROC_COMMITTED;

  dataset_changed__ (ds);

  /* Free memory for lagged cases. */
  while (!deque_is_empty (&ds->lag))
    case_unref (ds->lag_cases[deque_pop_back (&ds->lag)]);
  free (ds->lag_cases);

  /* Dictionary from before TEMPORARY becomes permanent. */
  proc_cancel_temporary_transformations (ds);
  bool ok = proc_cancel_all_transformations (ds) && ds->ok;

  if (!ds->discard_output)
    {
      dict_delete_scratch_vars (ds->dict);

      /* Old data sink becomes new data source. */
      if (ds->sink != NULL)
        ds->source = casewriter_make_reader (ds->sink);
    }
  else
    {
      ds->source = NULL;
      ds->discard_output = false;
    }
  ds->sink = NULL;

  caseinit_clear (ds->caseinit);
  caseinit_mark_as_preinited (ds->caseinit, ds->dict);

  dict_clear_vectors (ds->dict);
  ds->permanent_dict = NULL;
  ds->order_var = NULL;
  return ok;
}

/* Casereader class for procedure execution. */
static const struct casereader_class proc_casereader_class =
  {
    proc_casereader_read,
    proc_casereader_destroy,
    NULL,
    NULL,
  };

/* Updates last_proc_invocation. */
static void
update_last_proc_invocation (struct dataset *ds)
{
  ds->last_proc_invocation = time (NULL);
}

/* Returns a pointer to the lagged case from N_BEFORE cases before the
   current one, or NULL if there haven't been that many cases yet. */
const struct ccase *
lagged_case (const struct dataset *ds, int n_before)
{
  assert (n_before >= 1);
  assert (n_before <= ds->n_lag);

  if (n_before <= deque_count (&ds->lag))
    return ds->lag_cases[deque_front (&ds->lag, n_before - 1)];
  else
    return NULL;
}

/* Adds TRNS to the current set of transformations. */
void
add_transformation (struct dataset *ds,
                    const struct trns_class *class, void *aux)
{
  struct trns_chain *chain = (ds->n_stack > 0 ? &ds->stack[ds->n_stack - 1]
                              : ds->temporary ? &ds->temporary_trns_chain
                              : &ds->permanent_trns_chain);
  struct transformation t = { .class = class, .aux = aux };
  trns_chain_append (chain, &t);
  dataset_transformations_changed__ (ds, true);
}

/* Returns true if the next call to add_transformation() will add
   a temporary transformation, false if it will add a permanent
   transformation. */
bool
proc_in_temporary_transformations (const struct dataset *ds)
{
  return ds->temporary;
}

/* Marks the start of temporary transformations.
   Further calls to add_transformation() will add temporary
   transformations. */
void
proc_start_temporary_transformations (struct dataset *ds)
{
  assert (!ds->n_stack);
  if (!proc_in_temporary_transformations (ds))
    {
      add_case_limit_trns (ds);

      ds->permanent_dict = dict_clone (ds->dict);
      add_measurement_level_trns (ds, ds->permanent_dict);

      ds->temporary = true;
      dataset_transformations_changed__ (ds, true);
    }
}

/* Converts all the temporary transformations, if any, to permanent
   transformations.  Further transformations will be permanent.

   The FILTER command is implemented as a temporary transformation, so a
   procedure that uses this function should usually use proc_open_filtering()
   with FILTER false, instead of plain proc_open().

   Returns true if anything changed, false otherwise. */
bool
proc_make_temporary_transformations_permanent (struct dataset *ds)
{
  if (proc_in_temporary_transformations (ds))
    {
      cancel_measurement_level_trns (&ds->permanent_trns_chain);
      trns_chain_splice (&ds->permanent_trns_chain, &ds->temporary_trns_chain);

      ds->temporary = false;

      dict_unref (ds->permanent_dict);
      ds->permanent_dict = NULL;

      return true;
    }
  else
    return false;
}

/* Cancels all temporary transformations, if any.  Further
   transformations will be permanent.
   Returns true if anything changed, false otherwise. */
bool
proc_cancel_temporary_transformations (struct dataset *ds)
{
  if (proc_in_temporary_transformations (ds))
    {
      trns_chain_clear (&ds->temporary_trns_chain);

      dict_unref (ds->dict);
      ds->dict = ds->permanent_dict;
      ds->permanent_dict = NULL;

      dataset_transformations_changed__ (ds, ds->permanent_trns_chain.n != 0);
      return true;
    }
  else
    return false;
}

/* Cancels all transformations, if any.
   Returns true if successful, false on I/O error. */
bool
proc_cancel_all_transformations (struct dataset *ds)
{
  bool ok;
  assert (ds->proc_state == PROC_COMMITTED);
  ok = trns_chain_clear (&ds->permanent_trns_chain);
  ok = trns_chain_clear (&ds->temporary_trns_chain) && ok;
  ds->temporary = false;
  for (size_t i = 0; i < ds->n_stack; i++)
    ok = trns_chain_uninit (&ds->stack[i]) && ok;
  ds->n_stack = 0;
  dataset_transformations_changed__ (ds, false);

  return ok;
}

void
proc_push_transformations (struct dataset *ds)
{
  if (ds->n_stack >= ds->allocated_stack)
    ds->stack = x2nrealloc (ds->stack, &ds->allocated_stack,
                            sizeof *ds->stack);
  trns_chain_init (&ds->stack[ds->n_stack++]);
}

void
proc_pop_transformations (struct dataset *ds, struct trns_chain *chain)
{
  assert (ds->n_stack > 0);
  *chain = ds->stack[--ds->n_stack];
}

bool
proc_has_transformations (const struct dataset *ds)
{
  return ds->permanent_trns_chain.n || ds->temporary_trns_chain.n;
}

static enum trns_result
store_case_num (void *var_, struct ccase **cc, casenumber case_num)
{
  struct variable *var = var_;

  *cc = case_unshare (*cc);
  *case_num_rw (*cc, var) = case_num;

  return TRNS_CONTINUE;
}

/* Add a variable $ORDERING which we can sort by to get back the original order. */
struct variable *
add_permanent_ordering_transformation (struct dataset *ds)
{
  struct dictionary *d = ds->permanent_dict ? ds->permanent_dict : ds->dict;
  struct variable *order_var = dict_create_var_assert (d, "$ORDER", 0);
  ds->order_var = order_var;

  if (ds->permanent_dict)
    {
      order_var = dict_create_var_assert (ds->dict, "$ORDER", 0);
      static const struct trns_class trns_class = {
        .name = "ordering",
        .execute = store_case_num
      };
      const struct transformation t = { .class = &trns_class, .aux = order_var };
      trns_chain_prepend (&ds->temporary_trns_chain, &t);
    }

  return order_var;
}

/* Causes output from the next procedure to be discarded, instead
   of being preserved for use as input for the next procedure. */
void
proc_discard_output (struct dataset *ds)
{
  ds->discard_output = true;
}


/* Checks whether DS has a corrupted active dataset.  If so,
   discards it and returns false.  If not, returns true without
   doing anything. */
bool
dataset_end_of_command (struct dataset *ds)
{
  if (ds->source != NULL)
    {
      if (casereader_error (ds->source))
        {
          dataset_clear (ds);
          return false;
        }
      else
        {
          const struct taint *taint = casereader_get_taint (ds->source);
          taint_reset_successor_taint (CONST_CAST (struct taint *, taint));
          assert (!taint_has_tainted_successor (taint));
        }
    }
  return true;
}

/* Limits the maximum number of cases processed to
   *CASES_REMAINING. */
static enum trns_result
case_limit_trns_proc (void *cases_remaining_,
                      struct ccase **c UNUSED, casenumber case_nr UNUSED)
{
  size_t *cases_remaining = cases_remaining_;
  if (*cases_remaining > 0)
    {
      (*cases_remaining)--;
      return TRNS_CONTINUE;
    }
  else
    return TRNS_DROP_CASE;
}

/* Frees the data associated with a case limit transformation. */
static bool
case_limit_trns_free (void *cases_remaining_)
{
  size_t *cases_remaining = cases_remaining_;
  free (cases_remaining);
  return true;
}

/* Adds a transformation that limits the number of cases that may
   pass through, if DS->DICT has a case limit. */
static void
add_case_limit_trns (struct dataset *ds)
{
  casenumber case_limit = dict_get_case_limit (ds->dict);
  if (case_limit != 0)
    {
      casenumber *cases_remaining = xmalloc (sizeof *cases_remaining);
      *cases_remaining = case_limit;

      static const struct trns_class trns_class = {
        .name = "case limit",
        .execute = case_limit_trns_proc,
        .destroy = case_limit_trns_free,
      };
      add_transformation (ds, &trns_class, cases_remaining);

      dict_set_case_limit (ds->dict, 0);
    }
}


/* FILTER transformation. */
static enum trns_result
filter_trns_proc (void *filter_var_,
                  struct ccase **c, casenumber case_nr UNUSED)

{
  struct variable *filter_var = filter_var_;
  double f = case_num (*c, filter_var);
  return (f != 0.0 && !var_is_num_missing (filter_var, f)
          ? TRNS_CONTINUE : TRNS_DROP_CASE);
}

/* Adds a temporary transformation to filter data according to
   the variable specified on FILTER, if any. */
static void
add_filter_trns (struct dataset *ds)
{
  struct variable *filter_var = dict_get_filter (ds->dict);
  if (filter_var != NULL)
    {
      proc_start_temporary_transformations (ds);

      static const struct trns_class trns_class = {
        .name = "FILTER",
        .execute = filter_trns_proc,
      };
      add_transformation (ds, &trns_class, filter_var);
    }
}

void
dataset_need_lag (struct dataset *ds, int n_before)
{
  ds->n_lag = MAX (ds->n_lag, n_before);
}

/* Measurement guesser, for guessing a measurement level from formats and
   data. */

struct mg_value
  {
    struct hmap_node hmap_node;
    double value;
  };

struct mg_var
  {
    struct variable *var;
    struct hmap *values;
  };

static void
mg_var_uninit (struct mg_var *mgv)
{
  struct mg_value *mgvalue, *next;
  HMAP_FOR_EACH_SAFE (mgvalue, next, struct mg_value, hmap_node,
                      mgv->values)
    {
      hmap_delete (mgv->values, &mgvalue->hmap_node);
      free (mgvalue);
    }
  hmap_destroy (mgv->values);
  free (mgv->values);
}

static enum measure
mg_var_interpret (const struct mg_var *mgv)
{
  size_t n = hmap_count (mgv->values);
  if (!n)
    {
      /* All missing (or no data). */
      return MEASURE_NOMINAL;
    }

  const struct mg_value *mgvalue;
  HMAP_FOR_EACH (mgvalue, struct mg_value, hmap_node,
                 mgv->values)
    if (mgvalue->value < 10)
      return MEASURE_NOMINAL;
  return MEASURE_SCALE;
}

static enum measure
mg_var_add_value (struct mg_var *mgv, double value)
{
  if (var_is_num_missing (mgv->var, value))
    return MEASURE_UNKNOWN;
  else if (value < 0 || value != floor (value))
    return MEASURE_SCALE;

  size_t hash = hash_double (value, 0);
  struct mg_value *mgvalue;
  HMAP_FOR_EACH_WITH_HASH (mgvalue, struct mg_value, hmap_node,
                           hash, mgv->values)
    if (mgvalue->value == value)
      return MEASURE_UNKNOWN;

  mgvalue = xmalloc (sizeof *mgvalue);
  mgvalue->value = value;
  hmap_insert (mgv->values, &mgvalue->hmap_node, hash);
  if (hmap_count (mgv->values) >= settings_get_scalemin ())
    return MEASURE_SCALE;

  return MEASURE_UNKNOWN;
}

struct measure_guesser
  {
    struct mg_var *vars;
    size_t n_vars;
  };

static struct measure_guesser *
measure_guesser_create__ (struct dictionary *dict)
{
  struct mg_var *mgvs = NULL;
  size_t n_mgvs = 0;
  size_t allocated_mgvs = 0;

  for (size_t i = 0; i < dict_get_n_vars (dict); i++)
    {
      struct variable *var = dict_get_var (dict, i);
      if (var_get_measure (var) != MEASURE_UNKNOWN)
        continue;

      struct fmt_spec f = var_get_print_format (var);
      enum measure m = var_default_measure_for_format (f.type);
      if (m != MEASURE_UNKNOWN)
        {
          var_set_measure (var, m);
          continue;
        }

      if (n_mgvs >= allocated_mgvs)
        mgvs = x2nrealloc (mgvs, &allocated_mgvs, sizeof *mgvs);

      struct mg_var *mgv = &mgvs[n_mgvs++];
      *mgv = (struct mg_var) {
        .var = var,
        .values = xmalloc (sizeof *mgv->values),
      };
      hmap_init (mgv->values);
    }
  if (!n_mgvs)
    return NULL;

  struct measure_guesser *mg = xmalloc (sizeof *mg);
  *mg = (struct measure_guesser) {
    .vars = mgvs,
    .n_vars = n_mgvs,
  };
  return mg;
}

/* Scans through DS's dictionary for variables that have an unknown measurement
   level.  For those, if the measurement level can be guessed based on the
   variable's type and format, sets a default.  If that's enough, returns NULL.
   If any remain whose levels are unknown and can't be guessed that way,
   creates and returns a structure that the caller should pass to
   measure_guesser_add_case() or measure_guesser_run() for guessing a
   measurement level based on the data.  */
struct measure_guesser *
measure_guesser_create (struct dataset *ds)
{
  return measure_guesser_create__ (dataset_dict (ds));
}

/* Adds data from case C to MG. */
static void
measure_guesser_add_case (struct measure_guesser *mg, const struct ccase *c)
{
  for (size_t i = 0; i < mg->n_vars; )
    {
      struct mg_var *mgv = &mg->vars[i];
      double value = case_num (c, mgv->var);
      enum measure m = mg_var_add_value (mgv, value);
      if (m != MEASURE_UNKNOWN)
        {
          var_set_measure (mgv->var, m);

          mg_var_uninit (mgv);
          *mgv = mg->vars[--mg->n_vars];
        }
      else
        i++;
    }
}

/* Destroys MG. */
void
measure_guesser_destroy (struct measure_guesser *mg)
{
  if (!mg)
    return;

  for (size_t i = 0; i < mg->n_vars; i++)
    {
      struct mg_var *mgv = &mg->vars[i];
      var_set_measure (mgv->var, mg_var_interpret (mgv));
      mg_var_uninit (mgv);
    }
  free (mg->vars);
  free (mg);
}

/* Adds final measurement levels based on MG, after all the cases have been
   added. */
static void
measure_guesser_commit (struct measure_guesser *mg)
{
  for (size_t i = 0; i < mg->n_vars; i++)
    {
      struct mg_var *mgv = &mg->vars[i];
      var_set_measure (mgv->var, mg_var_interpret (mgv));
    }
}

/* Passes the cases in READER through MG and uses the data in the cases to set
   measurement levels for the variables where they were still unknown. */
void
measure_guesser_run (struct measure_guesser *mg,
                     const struct casereader *reader)
{
  struct casereader *r = casereader_clone (reader);
  while (mg->n_vars > 0)
    {
      struct ccase *c = casereader_read (r);
      if (!c)
        break;
      measure_guesser_add_case (mg, c);
      case_unref (c);
    }
  casereader_destroy (r);

  measure_guesser_commit (mg);
}

/* A transformation for guessing measurement levels. */

static enum trns_result
mg_trns_proc (void *mg_, struct ccase **c, casenumber case_nr UNUSED)
{
  struct measure_guesser *mg = mg_;
  measure_guesser_add_case (mg, *c);
  return TRNS_CONTINUE;
}

static bool
mg_trns_free (void *mg_)
{
  struct measure_guesser *mg = mg_;
  measure_guesser_commit (mg);
  measure_guesser_destroy (mg);
  return true;
}

static const struct trns_class mg_trns_class = {
  .name = "add measurement level",
  .execute = mg_trns_proc,
  .destroy = mg_trns_free,
};

static void
add_measurement_level_trns (struct dataset *ds, struct dictionary *dict)
{
  struct measure_guesser *mg = measure_guesser_create__ (dict);
  if (mg)
    add_transformation (ds, &mg_trns_class, mg);
}

static void
cancel_measurement_level_trns (struct trns_chain *chain)
{
  if (!chain->n)
    return;

  struct transformation *trns = &chain->xforms[chain->n - 1];
  if (trns->class != &mg_trns_class)
    return;

  struct measure_guesser *mg = trns->aux;
  measure_guesser_destroy (mg);
  chain->n--;
}

static void
dataset_changed__ (struct dataset *ds)
{
  if (ds->callbacks != NULL && ds->callbacks->changed != NULL)
    ds->callbacks->changed (ds->cb_data);
}

static void
dataset_transformations_changed__ (struct dataset *ds, bool non_empty)
{
  if (ds->callbacks != NULL && ds->callbacks->transformations_changed != NULL)
    ds->callbacks->transformations_changed (non_empty, ds->cb_data);
}

/* Private interface for use by session code. */

void
dataset_set_session__ (struct dataset *ds, struct session *session)
{
  ds->session = session;
}
