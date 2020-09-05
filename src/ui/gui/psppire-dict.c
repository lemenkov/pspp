/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2006, 2007, 2009, 2010, 2011, 2012,
   2016, 2017  Free Software Foundation

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

#include "ui/gui/psppire-dict.h"

#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>

#include "data/dictionary.h"
#include "data/identifier.h"
#include "data/missing-values.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "ui/gui/helper.h"
#include "ui/gui/psppire-marshal.h"
#include "ui/gui/psppire-var-ptr.h"

#include <gobject/genums.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid



GType align_enum_type;
GType measure_enum_type;
GType role_enum_type;


enum  {
  VARIABLE_CHANGED,
  VARIABLE_INSERTED,
  VARIABLE_DELETED,

  WEIGHT_CHANGED,
  FILTER_CHANGED,
  SPLIT_CHANGED,

  RESIZE_ITEM,

  n_SIGNALS
};


/* --- prototypes --- */
static void psppire_dict_dispose	(GObject		*object);

static void dictionary_tree_model_init (GtkTreeModelIface *iface);



static guint
gni (GListModel *list)
{
  PsppireDict *dict = PSPPIRE_DICT (list);

  return psppire_dict_get_var_cnt (dict);
}

static GType
git (GListModel *list)
{
  return GTK_TYPE_BUTTON;
}

static gpointer
gi (GListModel *list, guint id)
{
  GtkWidget *button = gtk_button_new ();

  PsppireDict *dict = PSPPIRE_DICT (list);

  if (id >= psppire_dict_get_var_cnt (dict))
    {
      gtk_button_set_label (GTK_BUTTON (button),  _("Var"));
    }
  else
    {
      const struct variable *v =  psppire_dict_get_variable (dict, id);

      gtk_button_set_label (GTK_BUTTON (button),  var_get_name (v));
      gtk_widget_set_tooltip_text (button, var_get_label (v));

      {
	PangoContext *context = gtk_widget_create_pango_context (button);
	PangoLayout *layout = pango_layout_new (context);
	PangoRectangle rect;

	pango_layout_set_text (layout, "M", 1);

	pango_layout_get_extents (layout, NULL, &rect);

	g_object_unref (G_OBJECT (layout));
	g_object_unref (G_OBJECT (context));

	gtk_widget_set_size_request (button,
				     (0.25 + var_get_display_width (v))
				     * rect.width / PANGO_SCALE,
				     -1);
      }
    }

  return button;
}


static void
ssw_init_iface (GListModelInterface *iface)
{
  iface->get_n_items = gni;
  iface->get_item = gi;
  iface->get_item_type = git;
}


/* --- variables --- */
static GObjectClass     *parent_class = NULL;

static guint signals [n_SIGNALS];

/* --- functions --- */

G_DEFINE_TYPE_WITH_CODE (PsppireDict, psppire_dict, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
						dictionary_tree_model_init)
			 G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL,
						ssw_init_iface))

static void
psppire_dict_class_init (PsppireDictClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  object_class->dispose = psppire_dict_dispose;

  signals [RESIZE_ITEM] =
    g_signal_new ("resize-item",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_LAST,
		  0,
		  NULL, NULL,
		  psppire_marshal_BOOLEAN__INT_INT,
		  G_TYPE_BOOLEAN,
		  2,
		  G_TYPE_INT,
		  G_TYPE_INT);

  signals [VARIABLE_CHANGED] =
    g_signal_new ("variable-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  psppire_marshal_VOID__INT_UINT_POINTER,
		  G_TYPE_NONE,
		  3,
		  G_TYPE_INT,
		  G_TYPE_UINT,
		  G_TYPE_POINTER);

  signals [VARIABLE_INSERTED] =
    g_signal_new ("variable-inserted",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);

  signals [VARIABLE_DELETED] =
    g_signal_new ("variable-deleted",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  psppire_marshal_VOID__POINTER_INT_INT,
		  G_TYPE_NONE,
		  3,
		  G_TYPE_POINTER,
		  G_TYPE_INT,
		  G_TYPE_INT);

  signals [WEIGHT_CHANGED] =
    g_signal_new ("weight-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);

  signals [FILTER_CHANGED] =
    g_signal_new ("filter-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__INT,
		  G_TYPE_NONE,
		  1,
		  G_TYPE_INT);

  signals [SPLIT_CHANGED] =
    g_signal_new ("split-changed",
		  G_TYPE_FROM_CLASS (class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);
}

static void
psppire_dict_dispose (GObject *object)
{
  PsppireDict *d = PSPPIRE_DICT (object);

  if (!d->dispose_has_run)
    return;

  d->dispose_has_run = TRUE;

  dict_set_callbacks (d->dict, NULL, NULL);
  dict_unref (d->dict);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

/* Pass on callbacks from src/data/dictionary, as
   signals in the Gtk library */
static void
addcb (struct dictionary *d, int idx, void *pd)
{
  PsppireDict *dict = PSPPIRE_DICT (pd);

  if (! dict->disable_insert_signal)
    {
      g_signal_emit (dict, signals [VARIABLE_INSERTED], 0, idx);
      g_signal_emit_by_name (dict, "items-changed", idx, 1, 1);
    }
}

static void
delcb (struct dictionary *d, const struct variable *var,
       int dict_idx, int case_idx, void *pd)
{
  g_signal_emit (pd, signals [VARIABLE_DELETED], 0,
                 var, dict_idx, case_idx);
  g_signal_emit_by_name (pd, "items-changed",  dict_idx, 1, 0);
}

static void
mutcb (struct dictionary *d, int idx, unsigned int what, const struct variable *oldvar, void *pd)
{
  g_signal_emit (pd, signals [VARIABLE_CHANGED], 0, idx, what, oldvar);
  g_signal_emit_by_name (pd, "items-changed", idx, 1, 1);
}

static void
weight_changed_callback (struct dictionary *d, int idx, void *pd)
{
  g_signal_emit (pd, signals [WEIGHT_CHANGED], 0, idx);
}

static void
filter_changed_callback (struct dictionary *d, int idx, void *pd)
{
  g_signal_emit (pd, signals [FILTER_CHANGED], 0, idx);
}

static void
split_changed_callback (struct dictionary *d, void *pd)
{
  g_signal_emit (pd, signals [SPLIT_CHANGED], 0);
}

static const struct dict_callbacks gui_callbacks =
  {
    addcb,
    delcb,
    mutcb,
    weight_changed_callback,
    filter_changed_callback,
    split_changed_callback
  };

static void
psppire_dict_init (PsppireDict *d)
{
  d->dispose_has_run = FALSE;

  d->stamp = g_random_int ();
  d->disable_insert_signal = FALSE;
}

/**
 * psppire_dict_new_from_dict:
 * @returns: a new #PsppireDict object
 *
 * Creates a new #PsppireDict.
 */
PsppireDict*
psppire_dict_new_from_dict (struct dictionary *d)
{
  PsppireDict *new_dict = g_object_new (PSPPIRE_TYPE_DICT, NULL);
  new_dict->dict = dict_ref (d);

  dict_set_callbacks (new_dict->dict, &gui_callbacks, new_dict);

  return new_dict;
}


void
psppire_dict_replace_dictionary (PsppireDict *dict, struct dictionary *d)
{
  const struct variable *var =  dict_get_weight (d);

  struct dictionary *old_dict = dict->dict;

  guint old_n = dict_get_var_cnt (dict->dict);
  guint new_n = dict_get_var_cnt (d);

  dict->dict = dict_ref (d);
  dict_unref (old_dict);

  weight_changed_callback (d, var ? var_get_dict_index (var) : -1, dict);

  var = dict_get_filter (d);
  filter_changed_callback (d, var ? var_get_dict_index (var) : -1, dict);

  split_changed_callback (d, dict);

  dict_set_callbacks (dict->dict, &gui_callbacks, dict);

  g_signal_emit_by_name (dict, "items-changed", 0, old_n, new_n);
}


/* Stores a valid name for a new variable in DICT into the SIZE bytes in NAME.
   Returns true if successful, false if SIZE is insufficient. */
bool
psppire_dict_generate_name (const PsppireDict *dict, char *name, size_t size)
{
  gint d;

  for (d = 1; ; d++)
    {
      int len;

      /* TRANSLATORS: This string must be a valid variable name.  That means:
         - The string must be at most 64 bytes (not characters) long.
         - The string may not contain whitespace.
         - The first character may not be '$'
         - The first character may not be a digit
         - The final character may not be '.' or '_'
      */
      len = snprintf (name, size, _("Var%04d"), d);
      if (len + 1 >= size)
        return false;

      if (psppire_dict_lookup_var (dict, name) == NULL)
        return true;
    }

  return name;
}

/* Insert a new variable at posn IDX, with the name NAME, and return the
   new variable.
   IDX may take the special value -1, which will be treated the same as
   zero.   If NAME is null, then a name will be automatically assigned.
*/
struct variable *
psppire_dict_insert_variable (PsppireDict *d, gint idx, const gchar *name)
{
  struct variable *var;
  char tmpname[64];

  if (idx == -1)    /* Note bug #56392. */
    idx = 0;
  g_return_val_if_fail (d, NULL);
  g_return_val_if_fail (PSPPIRE_IS_DICT (d), NULL);

  if (name == NULL)
    {
      if (!psppire_dict_generate_name (d, tmpname, sizeof tmpname))
        g_return_val_if_reached (NULL);

      name = tmpname;
    }

  d->disable_insert_signal = TRUE;

  var = dict_create_var (d->dict, name, 0);

  dict_reorder_var (d->dict, var, idx);

  d->disable_insert_signal = FALSE;

  g_signal_emit (d, signals[VARIABLE_INSERTED], 0, idx);
  g_signal_emit_by_name (d, "items-changed", idx, 0, 1);

  return var;
}

/* Delete N variables beginning at FIRST */
void
psppire_dict_delete_variables (PsppireDict *d, gint first, gint n)
{
  g_return_if_fail (d);
  g_return_if_fail (d->dict);
  g_return_if_fail (PSPPIRE_IS_DICT (d));
  size_t varcnt = dict_get_var_cnt (d->dict);
  g_return_if_fail (first < varcnt);
  g_return_if_fail (first >= 0);
  g_return_if_fail (n > 0);
  g_return_if_fail (first + n <= varcnt);

  dict_delete_consecutive_vars (d->dict, first, n);
}


gboolean
psppire_dict_set_name (PsppireDict* d, gint idx, const gchar *name)
{
  struct variable *var;
  g_assert (d);
  g_assert (PSPPIRE_IS_DICT (d));

  if (! dict_id_is_valid (d->dict, name, false))
    return FALSE;

  if (idx < dict_get_var_cnt (d->dict))
    {
      /* This is an existing variable? */
      var = dict_get_var (d->dict, idx);
      dict_rename_var (d->dict, var, name);
    }
  else
    {
      /* new variable */
      dict_create_var (d->dict, name, 0);
    }

  return TRUE;
}



/* Return the IDXth variable.
   Will return NULL if IDX  exceeds the number of variables in the dictionary.
 */
struct variable *
psppire_dict_get_variable (const PsppireDict *d, gint idx)
{
  g_return_val_if_fail (d, NULL);
  g_return_val_if_fail (d->dict, NULL);

  if (dict_get_var_cnt (d->dict) <= idx)
    return NULL;

  return dict_get_var (d->dict, idx);
}


/* Return the number of variables in the dictionary */
gint
psppire_dict_get_var_cnt (const PsppireDict *d)
{
  g_return_val_if_fail (d, -1);
  g_return_val_if_fail (d->dict, -1);

  return dict_get_var_cnt (d->dict);
}


/* Return the number of `union value's in the dictionary */
size_t
psppire_dict_get_value_cnt (const PsppireDict *d)
{
  g_return_val_if_fail (d, -1);
  g_return_val_if_fail (d->dict, -1);

  return dict_get_next_value_idx (d->dict);
}


/* Returns the prototype for the cases that match the dictionary */
const struct caseproto *
psppire_dict_get_proto (const PsppireDict *d)
{
  g_return_val_if_fail (d, NULL);
  g_return_val_if_fail (d->dict, NULL);

  return dict_get_proto (d->dict);
}


/* Return a variable by name.
   Return NULL if it doesn't exist
*/
struct variable *
psppire_dict_lookup_var (const PsppireDict *d, const gchar *name)
{
  g_return_val_if_fail (d, NULL);
  g_return_val_if_fail (d->dict, NULL);

  return dict_lookup_var (d->dict, name);
}

/* Clears the contents of D */
void
psppire_dict_clear (PsppireDict *d)
{
  g_return_if_fail (d);
  g_return_if_fail (d->dict);

  {
    dict_clear (d->dict);
  }
}


/* Return true if NAME would be a valid name of a variable to add to the
   dictionary.  False otherwise.
   If REPORT is true, then invalid names will be reported as such as errors
*/
gboolean
psppire_dict_check_name (const PsppireDict *dict,
			 const gchar *name, gboolean report)
{
  if (! dict_id_is_valid (dict->dict, name, report))
    return FALSE;

  if (psppire_dict_lookup_var (dict, name))
    {
      if (report)
	msg (ME, _("Duplicate variable name."));
      return FALSE;
    }

  return TRUE;
}


gint
psppire_dict_get_next_value_idx (const PsppireDict *dict)
{
  return dict_get_next_value_idx (dict->dict);
}


/* Tree Model Stuff */

static GtkTreeModelFlags tree_model_get_flags (GtkTreeModel *model);

static gint tree_model_n_columns (GtkTreeModel *model);

static GType tree_model_column_type (GtkTreeModel *model, gint index);

static gboolean tree_model_get_iter (GtkTreeModel *model, GtkTreeIter *iter,
				     GtkTreePath *path);

static gboolean tree_model_iter_next (GtkTreeModel *model, GtkTreeIter *iter);

static GtkTreePath * tree_model_get_path (GtkTreeModel *model,
					  GtkTreeIter *iter);

static void tree_model_get_value (GtkTreeModel *model, GtkTreeIter *iter,
				  gint column, GValue *value);

static gboolean tree_model_nth_child (GtkTreeModel *model, GtkTreeIter *iter,
				      GtkTreeIter *parent, gint n);

static gint tree_model_n_children (GtkTreeModel *tree_model,
				   GtkTreeIter  *iter);

static gboolean tree_model_iter_children (GtkTreeModel *,
					  GtkTreeIter *,
					  GtkTreeIter *);

static gboolean tree_model_iter_parent (GtkTreeModel *tree_model,
					GtkTreeIter *iter,
					GtkTreeIter *child);

static gboolean tree_model_iter_has_child  (GtkTreeModel *tree_model,
					    GtkTreeIter  *iter);

static void
dictionary_tree_model_init (GtkTreeModelIface *iface)
{
  iface->get_flags = tree_model_get_flags;
  iface->get_n_columns = tree_model_n_columns;
  iface->get_column_type = tree_model_column_type;
  iface->get_iter = tree_model_get_iter;
  iface->iter_next = tree_model_iter_next;
  iface->get_path = tree_model_get_path;
  iface->get_value = tree_model_get_value;

  iface->iter_children = tree_model_iter_children ;
  iface->iter_has_child = tree_model_iter_has_child ;
  iface->iter_n_children = tree_model_n_children ;
  iface->iter_nth_child = tree_model_nth_child ;
  iface->iter_parent = tree_model_iter_parent ;
}

static gboolean
tree_model_iter_has_child  (GtkTreeModel *tree_model,
			    GtkTreeIter  *iter)
{
  return FALSE;
}

static gboolean
tree_model_iter_parent (GtkTreeModel *tree_model,
		        GtkTreeIter *iter,
		        GtkTreeIter *child)
{
  return TRUE;
}

static GtkTreeModelFlags
tree_model_get_flags (GtkTreeModel *model)
{
  g_return_val_if_fail (PSPPIRE_IS_DICT (model), (GtkTreeModelFlags) 0);

  return GTK_TREE_MODEL_LIST_ONLY;
}


static gint
tree_model_n_columns (GtkTreeModel *model)
{
  return n_DICT_COLS;
}

static GType
tree_model_column_type (GtkTreeModel *model, gint index)
{
  g_return_val_if_fail (PSPPIRE_IS_DICT (model), (GType) 0);

  GType t = 0;

  switch (index)
    {
    case DICT_TVM_COL_NAME:
    case DICT_TVM_COL_LABEL:
      t = G_TYPE_STRING;
      break;
    case DICT_TVM_COL_DECIMAL:
    case DICT_TVM_COL_WIDTH:
    case DICT_TVM_COL_COLUMNS:
      t = G_TYPE_INT;
      break;
    case DICT_TVM_COL_VAR:
      t = PSPPIRE_VAR_PTR_TYPE;
      break;
    case DICT_TVM_COL_ALIGNMENT:
      t = align_enum_type;
      break;
    case DICT_TVM_COL_MEASURE:
      t = measure_enum_type;
      break;
    case DICT_TVM_COL_ROLE:
      t = role_enum_type;
      break;
    }

  return t;
}

static gboolean
tree_model_get_iter (GtkTreeModel *model, GtkTreeIter *iter, GtkTreePath *path)
{
  gint *indices, depth;
  gint n;
  struct variable *var;

  PsppireDict *dict = PSPPIRE_DICT (model);

  g_return_val_if_fail (path, FALSE);

  indices = gtk_tree_path_get_indices (path);
  depth = gtk_tree_path_get_depth (path);

  g_return_val_if_fail (depth == 1, FALSE);

  n = indices [0];

  if (n < 0 || n >= psppire_dict_get_var_cnt (dict))
    {
      iter->stamp = 0;
      iter->user_data = NULL;
      return FALSE;
    }

  var = psppire_dict_get_variable (dict, n);

  g_assert (var_get_dict_index (var) == n);

  iter->stamp = dict->stamp;
  iter->user_data = var;

  return TRUE;
}


static gboolean
tree_model_iter_next (GtkTreeModel *model, GtkTreeIter *iter)
{
  PsppireDict *dict = PSPPIRE_DICT (model);
  struct variable *var;
  gint idx;

  if (iter == NULL || iter->user_data == NULL)
    return FALSE;

  g_return_val_if_fail (iter->stamp == dict->stamp, FALSE);

  var = iter->user_data;

  idx = var_get_dict_index (var);

  if (idx + 1 >= psppire_dict_get_var_cnt (dict))
    {
      iter->user_data = NULL;
      iter->stamp = 0;
      return FALSE;
    }

  var = psppire_dict_get_variable (dict, idx + 1);

  g_assert (var_get_dict_index (var) == idx + 1);

  iter->user_data = var;

  return TRUE;
}

static GtkTreePath *
tree_model_get_path (GtkTreeModel *model, GtkTreeIter *iter)
{
  GtkTreePath *path;
  struct variable *var;
  PsppireDict *dict = PSPPIRE_DICT (model);

  g_return_val_if_fail (iter->stamp == dict->stamp, FALSE);

  var = iter->user_data;

  path = gtk_tree_path_new ();
  gtk_tree_path_append_index (path, var_get_dict_index (var));

  return path;
}

const struct fmt_spec *var_get_write_format (const struct variable *);

static void
tree_model_get_value (GtkTreeModel *model, GtkTreeIter *iter,
		      gint column, GValue *value)
{
  struct variable *var;
  PsppireDict *dict = PSPPIRE_DICT (model);

  g_return_if_fail (iter->stamp == dict->stamp);

  var = iter->user_data;

  const struct fmt_spec *fs = var_get_write_format (var);

  switch (column)
    {
    case DICT_TVM_COL_NAME:
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, var_get_name (var));
      break;
    case DICT_TVM_COL_WIDTH:
      g_value_init (value, G_TYPE_INT);
      g_value_set_int (value, fs->w);
      break;
    case DICT_TVM_COL_DECIMAL:
      g_value_init (value, G_TYPE_INT);
      g_value_set_int (value, fs->d);
      break;
    case DICT_TVM_COL_LABEL:
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, var_get_label (var));
      break;
    case DICT_TVM_COL_COLUMNS:
      g_value_init (value, G_TYPE_INT);
      g_value_set_int (value, var_get_display_width (var));
      break;
    case DICT_TVM_COL_ALIGNMENT:
      g_value_init (value, align_enum_type);
      g_value_set_enum (value, var_get_alignment (var));
      break;
    case DICT_TVM_COL_MEASURE:
      g_value_init (value, measure_enum_type);
      g_value_set_enum (value, var_get_measure (var));
      break;
    case DICT_TVM_COL_ROLE:
      g_value_init (value, role_enum_type);
      g_value_set_enum (value, var_get_role (var));
      break;
    case DICT_TVM_COL_VAR:
      g_value_init (value, PSPPIRE_VAR_PTR_TYPE);
      g_value_set_boxed (value, var);
      break;
    default:
      g_value_init (value, G_TYPE_STRING);
      g_value_set_string (value, "????");
      break;
    }
}

static gboolean
tree_model_iter_children (GtkTreeModel *tree_model,
			  GtkTreeIter *iter,
			  GtkTreeIter *parent)
{
  return FALSE;
}

static gint
tree_model_n_children (GtkTreeModel *model,
		       GtkTreeIter  *iter)
{
  PsppireDict *dict = PSPPIRE_DICT (model);

  if (iter == NULL)
    return psppire_dict_get_var_cnt (dict);

  return 0;
}

static gboolean
tree_model_nth_child (GtkTreeModel *model, GtkTreeIter *iter,
		      GtkTreeIter *parent, gint n)
{
  PsppireDict *dict;

  g_return_val_if_fail (PSPPIRE_IS_DICT (model), FALSE);

  dict = PSPPIRE_DICT (model);

  if (parent)
    return FALSE;

  if (n >= psppire_dict_get_var_cnt (dict))
    return FALSE;

  iter->stamp = dict->stamp;
  iter->user_data = psppire_dict_get_variable (dict, n);

  if (!iter->user_data)
    return FALSE;

  return TRUE;
}


gboolean
psppire_dict_rename_var (PsppireDict *dict, struct variable *v,
			 const gchar *name)
{
  if (! dict_id_is_valid (dict->dict, name, false))
    return FALSE;

  /* Make sure no other variable has this name */
  if (NULL != psppire_dict_lookup_var (dict, name))
    return FALSE;

  dict_rename_var (dict->dict, v, name);

  return TRUE;
}


struct variable *
psppire_dict_get_weight_variable (const PsppireDict *dict)
{
  return dict_get_weight (dict->dict);
}

const gchar *
psppire_dict_encoding (const PsppireDict *dict)
{
  return dict_get_encoding (dict->dict);
}
