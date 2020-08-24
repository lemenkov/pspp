/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2008, 2009, 2010, 2011, 2012,
   2013, 2016, 2017  Free Software Foundation

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
#include <string.h>
#include <stdlib.h>
#include <gettext.h>
#define _(msgid) gettext (msgid)
#define P_(msgid) msgid

#include <data/datasheet.h>
#include <data/data-out.h>
#include <data/variable.h>

#include <ui/gui/psppire-marshal.h>

#include <pango/pango-context.h>

#include "psppire-data-store.h"
#include <libpspp/i18n.h>
#include "helper.h"

#include <data/dictionary.h>
#include <data/missing-values.h>
#include <data/value-labels.h>
#include <data/data-in.h>
#include <data/format.h>

#include <math/sort.h>

#include "xmalloca.h"

#include "value-variant.h"

static void psppire_data_store_finalize        (GObject           *object);
static void psppire_data_store_dispose        (GObject           *object);

static gboolean psppire_data_store_insert_case (PsppireDataStore *ds,
						struct ccase *cc,
						casenumber posn);


static gboolean psppire_data_store_data_in (PsppireDataStore *ds,
					    casenumber casenum, gint idx,
					    struct substring input,
					    const struct fmt_spec *fmt);

static GObjectClass *parent_class = NULL;


enum
  {
    ITEMS_CHANGED,
    CASE_CHANGED,
    n_SIGNALS
  };

static guint signals [n_SIGNALS];

static gint
__tree_model_iter_n_children (GtkTreeModel *tree_model,
			     GtkTreeIter *iter)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (tree_model);

  if (store->datasheet == NULL)
    return 0;

  gint n =  datasheet_get_n_rows (store->datasheet);

  return n;
}

static GtkTreeModelFlags
__tree_model_get_flags (GtkTreeModel *model)
{
  g_return_val_if_fail (PSPPIRE_IS_DATA_STORE (model), (GtkTreeModelFlags) 0);

  return GTK_TREE_MODEL_LIST_ONLY;
}

static gint
__tree_model_get_n_columns (GtkTreeModel *tree_model)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (tree_model);

  return psppire_dict_get_var_cnt (store->dict);
}


static gboolean
__iter_nth_child (GtkTreeModel *tree_model,
		  GtkTreeIter *iter,
		  GtkTreeIter *parent,
		  gint n)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (tree_model);

  g_assert (parent == NULL);
  g_return_val_if_fail (store, FALSE);

  if (!store->datasheet || n >= datasheet_get_n_rows (store->datasheet))
    {
      iter->stamp = -1;
      iter->user_data = NULL;
      return FALSE;
    }

  iter->user_data = GINT_TO_POINTER (n);
  iter->stamp = store->stamp;

  return TRUE;
}

/* Set the contents of OUT to reflect the information provided by IN, COL, and
   ROW, for MODEL.  Returns TRUE if successful. */
gboolean
psppire_data_store_string_to_value (GtkTreeModel *model, gint col, gint row,
				    const gchar *in, GValue *out)
{
  PsppireDataStore *store = PSPPIRE_DATA_STORE (model);

  while (col >= psppire_dict_get_var_cnt (store->dict))
    {
      const struct variable *var =
	psppire_dict_insert_variable (store->dict,
				      psppire_dict_get_var_cnt (store->dict),
				      NULL);
      g_return_val_if_fail (var, FALSE);
    }

  const struct variable *variable = psppire_dict_get_variable (store->dict, col);
  g_return_val_if_fail (variable, FALSE);

  const struct fmt_spec *fmt = var_get_print_format (variable);

  int width = var_get_width (variable);

  union value val;
  value_init (&val, width);
  const struct val_labs *value_labels = var_get_value_labels (variable);
  const union value *vp = NULL;
  if (value_labels)
    {
      vp = val_labs_find_value (value_labels, in);
      if (vp)
	value_copy (&val, vp, width);
    }
  char *xx = NULL;
  if (vp == NULL)
    {
      xx = data_in (ss_cstr (in), psppire_dict_encoding (store->dict),
		    fmt->type, &val, width, "UTF-8");
    }

  GVariant *vrnt = value_variant_new (&val, width);
  value_destroy (&val, width);

  g_value_init (out, G_TYPE_VARIANT);
  g_value_set_variant (out, vrnt);
  free (xx);
  return TRUE;
}

static char *
unlabeled_value (PsppireDataStore *store, const struct variable *variable, const union value *val)
{
  if (var_is_numeric (variable) &&
      var_is_value_missing (variable, val, MV_SYSTEM))
    return g_strdup ("");

  const struct fmt_spec *fmt = var_get_print_format (variable);
  return value_to_text__ (*val, fmt, psppire_dict_encoding (store->dict));
}

gchar *
psppire_data_store_value_to_string (gpointer unused, PsppireDataStore *store, gint col, gint row, const GValue *v)
{
  const struct variable *variable = psppire_dict_get_variable (store->dict, col);
  g_return_val_if_fail (variable, g_strdup ("???"));

  GVariant *vrnt = g_value_get_variant (v);
  g_return_val_if_fail (vrnt, g_strdup ("???"));

  union value val;
  value_variant_get (&val, vrnt);

  char *out = unlabeled_value (store, variable, &val);

  value_destroy_from_variant (&val, vrnt);

  return out;
}

gchar *
psppire_data_store_value_to_string_with_labels (gpointer unused, PsppireDataStore *store, gint col, gint row, const GValue *v)
{
  const struct variable *variable = psppire_dict_get_variable (store->dict, col);
  g_return_val_if_fail (variable, g_strdup ("???"));

  GVariant *vrnt = g_value_get_variant (v);
  union value val;
  value_variant_get (&val, vrnt);

  char *out = NULL;

  const struct val_labs *vls = var_get_value_labels (variable);
  struct val_lab *vl = val_labs_lookup (vls, &val);
  if (vl != NULL)
    out = strdup (val_lab_get_label (vl));
  else
    out = unlabeled_value (store, variable, &val);

  value_destroy_from_variant (&val, vrnt);

  return out;
}

static void
__get_value (GtkTreeModel *tree_model,
	     GtkTreeIter *iter,
	     gint column,
	     GValue *value)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (tree_model);

  g_return_if_fail (iter->stamp == store->stamp);

  const struct variable *variable = psppire_dict_get_variable (store->dict, column);
  if (NULL == variable)
    return;

  gint row = GPOINTER_TO_INT (iter->user_data);

  struct ccase *cc = datasheet_get_row (store->datasheet, row);

  g_return_if_fail (cc);

  g_value_init (value, G_TYPE_VARIANT);

  const union value *val = case_data_idx (cc, var_get_case_index (variable));

  GVariant *vv = value_variant_new (val, var_get_width (variable));

  g_value_set_variant (value, vv);

  case_unref (cc);
}


static void
__tree_model_init (GtkTreeModelIface *iface)
{
  iface->get_flags       = __tree_model_get_flags;
  iface->get_n_columns   = __tree_model_get_n_columns ;
  iface->get_column_type = NULL;
  iface->get_iter        = NULL;
  iface->iter_next       = NULL;
  iface->get_path        = NULL;
  iface->get_value       = __get_value;

  iface->iter_children   = NULL;
  iface->iter_has_child  = NULL;
  iface->iter_n_children = __tree_model_iter_n_children;
  iface->iter_nth_child  = __iter_nth_child;
  iface->iter_parent     = NULL;
}

G_DEFINE_TYPE_WITH_CODE (PsppireDataStore, psppire_data_store, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
						__tree_model_init))

static void
psppire_data_store_class_init (PsppireDataStoreClass *class)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass*) class;

  object_class->finalize = psppire_data_store_finalize;
  object_class->dispose = psppire_data_store_dispose;

  signals [ITEMS_CHANGED] =
    g_signal_new ("items-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  psppire_marshal_VOID__UINT_UINT_UINT,
		  G_TYPE_NONE,
		  3,
		  G_TYPE_UINT,  /* Index of the start of the change */
		  G_TYPE_UINT,  /* The number of items deleted */
		  G_TYPE_UINT); /* The number of items inserted */

  signals [CASE_CHANGED] =
    g_signal_new ("case-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);
}



casenumber
psppire_data_store_get_case_count (const PsppireDataStore *store)
{
  return datasheet_get_n_rows (store->datasheet);
}

size_t
psppire_data_store_get_value_count (const PsppireDataStore *store)
{
  return psppire_dict_get_value_cnt (store->dict);
}

const struct caseproto *
psppire_data_store_get_proto (const PsppireDataStore *store)
{
  return psppire_dict_get_proto (store->dict);
}

static void
psppire_data_store_init (PsppireDataStore *data_store)
{
  data_store->dict = NULL;
  data_store->datasheet = NULL;
  data_store->dispose_has_run = FALSE;
  data_store->stamp = g_random_int ();
}


static void
psppire_data_store_delete_value (PsppireDataStore *store, gint case_index)
{
  g_return_if_fail (store->datasheet);
  g_return_if_fail (case_index < datasheet_get_n_columns (store->datasheet));

  datasheet_delete_columns (store->datasheet, case_index, 1);
  datasheet_insert_column (store->datasheet, NULL, -1, case_index);
}


/*
   A callback which occurs after a variable has been deleted.
 */
static void
delete_variable_callback (GObject *obj, const struct variable *var UNUSED,
                          gint dict_index, gint case_index,
                          gpointer data)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (data);

  psppire_data_store_delete_value (store, case_index);
}

struct resize_datum_aux
  {
    const struct dictionary *dict;
    const struct variable *new_variable;
    const struct variable *old_variable;
  };

static void
resize_datum (const union value *old, union value *new, const void *aux_)
{
  const struct resize_datum_aux *aux = aux_;
  int new_width = var_get_width (aux->new_variable);
  const char *enc = dict_get_encoding (aux->dict);
  const struct fmt_spec *newfmt = var_get_print_format (aux->new_variable);
  char *s = data_out (old, enc, var_get_print_format (aux->old_variable));
  enum fmt_type type = (fmt_usable_for_input (newfmt->type)
                        ? newfmt->type
                        : FMT_DOLLAR);
  free (data_in (ss_cstr (s), enc, type, new, new_width, enc));
  free (s);
}

static void
variable_changed_callback (GObject *obj, gint var_num, guint what, const struct variable *oldvar,
			   gpointer data)
{
  PsppireDataStore *store  = PSPPIRE_DATA_STORE (data);
  struct variable *variable = psppire_dict_get_variable (store->dict, var_num);

  if (what & VAR_TRAIT_WIDTH)
    {
      int posn = var_get_case_index (variable);
      struct resize_datum_aux aux;
      aux.old_variable = oldvar;
      aux.new_variable = variable;
      aux.dict = store->dict->dict;
      datasheet_resize_column (store->datasheet, posn, var_get_width (variable),
                               resize_datum, &aux);
    }
}

static void
insert_variable_callback (GObject *obj, gint var_num, gpointer data)
{
  struct variable *variable;
  PsppireDataStore *store;
  gint posn;

  g_return_if_fail (data);

  store  = PSPPIRE_DATA_STORE (data);

  variable = psppire_dict_get_variable (store->dict, var_num);
  posn = var_get_case_index (variable);
  psppire_data_store_insert_value (store, var_get_width (variable), posn);
}

/**
 * psppire_data_store_new:
 * @dict: The dictionary for this data_store.
 *
 *
 * Return value: a new #PsppireDataStore
 **/
PsppireDataStore *
psppire_data_store_new (PsppireDict *dict)
{
  PsppireDataStore *retval;

  retval = g_object_new (PSPPIRE_TYPE_DATA_STORE, NULL);

  psppire_data_store_set_dictionary (retval, dict);

  return retval;
}

void
psppire_data_store_set_reader (PsppireDataStore *ds,
			       struct casereader *reader)
{
  gint i;
  gint old_n = 0;
  if (ds->datasheet)
    {
      old_n = datasheet_get_n_rows (ds->datasheet);
      datasheet_destroy (ds->datasheet);
    }

  ds->datasheet = datasheet_create (reader);

  gint new_n = datasheet_get_n_rows (ds->datasheet);

  if (ds->dict)
    for (i = 0 ; i < n_dict_signals; ++i)
      {
	if (ds->dict_handler_id [i] > 0)
	  {
	    g_signal_handler_unblock (ds->dict,
				      ds->dict_handler_id[i]);
	  }
      }

  g_signal_emit (ds, signals[ITEMS_CHANGED], 0, 0, old_n, new_n);
}


/**
 * psppire_data_store_replace_set_dictionary:
 * @data_store: The variable store
 * @dict: The dictionary to set
 *
 * If a dictionary is already associated with the data-store, then it will be
 * destroyed.
 **/
void
psppire_data_store_set_dictionary (PsppireDataStore *data_store, PsppireDict *dict)
{
  int i;

  /* Disconnect any existing handlers */
  if (data_store->dict)
    for (i = 0 ; i < n_dict_signals; ++i)
      {
	g_signal_handler_disconnect (data_store->dict,
				     data_store->dict_handler_id[i]);
      }

  data_store->dict = dict;

  if (dict != NULL)
    {

      data_store->dict_handler_id [VARIABLE_INSERTED] =
	g_signal_connect (dict, "variable-inserted",
			  G_CALLBACK (insert_variable_callback),
			  data_store);

      data_store->dict_handler_id [VARIABLE_DELETED] =
	g_signal_connect (dict, "variable-deleted",
			  G_CALLBACK (delete_variable_callback),
			  data_store);

      data_store->dict_handler_id [VARIABLE_CHANGED] =
	g_signal_connect (dict, "variable-changed",
			  G_CALLBACK (variable_changed_callback),
			  data_store);
    }



  /* The entire model has changed */

  if (data_store->dict)
    for (i = 0 ; i < n_dict_signals; ++i)
      {
	if (data_store->dict_handler_id [i] > 0)
	  {
	    g_signal_handler_block (data_store->dict,
				    data_store->dict_handler_id[i]);
	  }
      }
}

static void
psppire_data_store_finalize (GObject *object)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (object);

  if (ds->datasheet)
    {
      datasheet_destroy (ds->datasheet);
      ds->datasheet = NULL;
    }

  /* must chain up */
  (* parent_class->finalize) (object);
}


static void
psppire_data_store_dispose (GObject *object)
{
  PsppireDataStore *ds = PSPPIRE_DATA_STORE (object);

  if (ds->dispose_has_run)
    return;

  psppire_data_store_set_dictionary (ds, NULL);

  /* must chain up */
  (* parent_class->dispose) (object);

  ds->dispose_has_run = TRUE;
}



/* Insert a blank case before POSN */
gboolean
psppire_data_store_insert_new_case (PsppireDataStore *ds, casenumber posn)
{
  gboolean result;
  const struct caseproto *proto;
  struct ccase *cc;
  g_return_val_if_fail (ds, FALSE);

  proto = datasheet_get_proto (ds->datasheet);
  g_return_val_if_fail (caseproto_get_n_widths (proto) > 0, FALSE);
  g_return_val_if_fail (posn <= psppire_data_store_get_case_count (ds), FALSE);

  cc = case_create (proto);
  case_set_missing (cc);

  result = psppire_data_store_insert_case (ds, cc, posn);

  case_unref (cc);

  return result;
}

gboolean
psppire_data_store_get_value (PsppireDataStore *store,
			      glong row, const struct variable *var,
			      union value *val)
{
  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (store->datasheet != NULL, FALSE);
  g_return_val_if_fail (var != NULL, FALSE);

  if (row < 0 || row >= datasheet_get_n_rows (store->datasheet))
    return FALSE;

  int width = var_get_width (var);
  value_init (val, width);
  datasheet_get_value (store->datasheet, row, var_get_case_index (var), val);

  return TRUE;
}



gchar *
psppire_data_store_get_string (PsppireDataStore *store,
                               glong row, const struct variable *var,
                               bool use_value_label)
{
  gchar *string;
  union value v;
  int width = var_get_width (var);
  if (! psppire_data_store_get_value (store, row, var, &v))
    return NULL;

  string = NULL;
  if (use_value_label)
    {
      const char *label = var_lookup_value_label (var, &v);
      if (label != NULL)
        string = g_strdup (label);
    }
  if (string == NULL)
    string = value_to_text (v, var);

  value_destroy (&v, width);

  return string;
}


/* Attempts to update that part of the variable store which corresponds to VAR
   within ROW with the value TEXT.

   If USE_VALUE_LABEL is true, and TEXT is a value label for the column's
   variable, then stores the value from that value label instead of the literal
   TEXT.

   Returns true if anything was updated, false otherwise.  */
gboolean
psppire_data_store_set_string (PsppireDataStore *store,
			       const gchar *text,
                               glong row, const struct variable *var,
                               gboolean use_value_label)
{
  gint case_index;
  glong n_cases;
  gboolean ok;

  n_cases = psppire_data_store_get_case_count (store);
  if (row > n_cases)
    return FALSE;
  if (row == n_cases)
    psppire_data_store_insert_new_case (store, row);

  case_index = var_get_case_index (var);
  if (use_value_label)
    {
      const struct val_labs *vls = var_get_value_labels (var);
      const union value *value = vls ? val_labs_find_value (vls, text) : NULL;
      if (value)
        ok = datasheet_put_value (store->datasheet, row, case_index, value);
      else
        ok = FALSE;
    }
  else
    ok = psppire_data_store_data_in (store, row, case_index, ss_cstr (text),
                                     var_get_print_format (var));

  if (ok)
    g_signal_emit (store, signals [CASE_CHANGED], 0, row);
  return ok;
}



void
psppire_data_store_clear (PsppireDataStore *ds)
{
  datasheet_destroy (ds->datasheet);
  ds->datasheet = NULL;

  psppire_dict_clear (ds->dict);

  g_signal_emit (ds, signals [ITEMS_CHANGED], 0, 0, -1, 0);
}



/* Return a casereader made from this datastore */
struct casereader *
psppire_data_store_get_reader (PsppireDataStore *ds)
{
  int i;
  struct casereader *reader ;

  if (ds->dict)
    for (i = 0 ; i < n_dict_signals; ++i)
      {
	g_signal_handler_block (ds->dict,
				ds->dict_handler_id[i]);
      }

  reader = datasheet_make_reader (ds->datasheet);

  /* We must not reference this again */
  ds->datasheet = NULL;

  return reader;
}

/* Returns the CASENUMth case, or a null pointer on failure.
 */
struct ccase *
psppire_data_store_get_case (const PsppireDataStore *ds,
			     casenumber casenum)
{
  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  return datasheet_get_row (ds->datasheet, casenum);
}


gboolean
psppire_data_store_delete_cases (PsppireDataStore *ds, casenumber first,
				 casenumber n_cases)
{
  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  g_return_val_if_fail (first + n_cases <=
			psppire_data_store_get_case_count (ds), FALSE);


  datasheet_delete_rows (ds->datasheet, first, n_cases);

  g_signal_emit (ds, signals[ITEMS_CHANGED], 0, first, n_cases, 0);

  return TRUE;
}



/* Insert case CC into the case file before POSN */
static gboolean
psppire_data_store_insert_case (PsppireDataStore *ds,
				struct ccase *cc,
				casenumber posn)
{
  bool result ;

  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  cc = case_ref (cc);
  result = datasheet_insert_rows (ds->datasheet, posn, &cc, 1);

  if (result)
    {
      g_signal_emit (ds, signals[ITEMS_CHANGED], 0, posn, 0, 1);
    }
  else
    g_warning ("Cannot insert case at position %ld\n", posn);

  return result;
}


/* Set the value of VAR in case CASENUM to V.
   V must be the correct width for IDX.
   Returns true if successful, false on I/O error. */
gboolean
psppire_data_store_set_value (PsppireDataStore *ds, casenumber casenum,
			      const struct variable *var, const union value *v)
{
  glong n_cases;
  bool ok;

  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  n_cases = psppire_data_store_get_case_count (ds);
  if (casenum > n_cases)
    return FALSE;

  if (casenum == n_cases)
    psppire_data_store_insert_new_case (ds, casenum);

  ok = datasheet_put_value (ds->datasheet, casenum, var_get_case_index (var),
                            v);
  if (ok)
    {
      g_signal_emit (ds, signals [CASE_CHANGED], 0, casenum);
      g_signal_emit (ds, signals [ITEMS_CHANGED], 0, casenum, 1, 1);
    }

  return ok;
}




/* Set the IDXth value of case C using D_IN */
static gboolean
psppire_data_store_data_in (PsppireDataStore *ds, casenumber casenum, gint idx,
			    struct substring input, const struct fmt_spec *fmt)
{
  union value value;
  int width;
  bool ok;

  PsppireDict *dict;

  g_return_val_if_fail (ds, FALSE);
  g_return_val_if_fail (ds->datasheet, FALSE);

  g_return_val_if_fail (idx < datasheet_get_n_columns (ds->datasheet), FALSE);

  dict = ds->dict;

  width = fmt_var_width (fmt);
  g_return_val_if_fail (caseproto_get_width (
                          datasheet_get_proto (ds->datasheet), idx) == width,
                        FALSE);
  value_init (&value, width);
  ok = (datasheet_get_value (ds->datasheet, casenum, idx, &value)
        && data_in_msg (input, UTF8, fmt->type, &value, width,
                        dict_get_encoding (dict->dict))
        && datasheet_put_value (ds->datasheet, casenum, idx, &value));
  value_destroy (&value, width);

  return ok;
}

/* Resize the cases in the casefile, by inserting a value of the
   given WIDTH into every one of them at the position immediately
   preceding WHERE.
*/
gboolean
psppire_data_store_insert_value (PsppireDataStore *ds,
                                 gint width, gint where)
{
  union value value;

  g_return_val_if_fail (ds, FALSE);

  g_assert (width >= 0);

  if (! ds->datasheet)
    ds->datasheet = datasheet_create (NULL);

  value_init (&value, width);
  value_set_missing (&value, width);

  datasheet_insert_column (ds->datasheet, &value, width, where);
  value_destroy (&value, width);

  return TRUE;
}

gboolean
psppire_data_store_filtered (PsppireDataStore *ds,
                             glong row)
{
  union value val;

  const struct dictionary *dict;
  const struct variable *filter;

  if (row < 0 || row >= datasheet_get_n_rows (ds->datasheet))
    return FALSE;

  dict = ds->dict->dict;
  g_return_val_if_fail (dict, FALSE);
  filter = dict_get_filter (dict);
  if (! filter)
    return FALSE;

  g_return_val_if_fail (var_is_numeric (filter), FALSE);
  value_init (&val, 0);
  if (! datasheet_get_value (ds->datasheet, row,
                              var_get_case_index (filter),
                              &val))
    return FALSE;

  return (val.f == 0.0);
}
