/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2020  Free Software Foundation

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
#include <glib.h>

#include <stdint.h>

#include <ui/gui/psppire-marshal.h>

#include "psppire-spreadsheet-data-model.h"
#include "data/spreadsheet-reader.h"


static void psppire_spreadsheet_data_model_init (PsppireSpreadsheetDataModel *
                                            spreadsheetModel);
static void psppire_spreadsheet_data_model_class_init (PsppireSpreadsheetDataModelClass
                                                  * class);

static void psppire_spreadsheet_data_model_finalize (GObject * object);
static void psppire_spreadsheet_data_model_dispose (GObject * object);

static GObjectClass *parent_class = NULL;


static void spreadsheet_tree_model_init (GtkTreeModelIface * iface);

enum
  {
    ITEMS_CHANGED,
    n_SIGNALS
  };

static guint signals [n_SIGNALS];

G_DEFINE_TYPE_WITH_CODE (PsppireSpreadsheetDataModel,\
			 psppire_spreadsheet_data_model,\
			 G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
						spreadsheet_tree_model_init))

/* Properties */
enum
{
  PROP_0,
  PROP_SPREADSHEET,
  PROP_SHEET_NUMBER
};

static void
psppire_spreadsheet_data_model_get_property (GObject         *object,
                                             guint            prop_id,
                                             GValue          *value,
                                             GParamSpec      *pspec)
{
  PsppireSpreadsheetDataModel *sp = PSPPIRE_SPREADSHEET_DATA_MODEL (object);

  switch (prop_id)
    {
    case PROP_SPREADSHEET:
      g_value_set_pointer (value, sp->spreadsheet);
      break;
    case PROP_SHEET_NUMBER:
      g_value_set_int (value, sp->sheet_number);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_spreadsheet_data_model_set_property (GObject * object,
                                             guint prop_id,
                                             const GValue * value,
                                             GParamSpec * pspec)
{
  PsppireSpreadsheetDataModel *sp = PSPPIRE_SPREADSHEET_DATA_MODEL (object);

  switch (prop_id)
    {
    case PROP_SPREADSHEET:
      {
      struct spreadsheet *old = sp->spreadsheet;
      sp->spreadsheet = spreadsheet_ref (g_value_get_pointer (value));
      if (old)
        spreadsheet_unref (old);
      g_signal_emit (sp, signals[ITEMS_CHANGED], 0, 0, 0, 0);
      }
      break;
    case PROP_SHEET_NUMBER:
      sp->sheet_number = g_value_get_int (value);
      g_signal_emit (sp, signals[ITEMS_CHANGED], 0, 0, 0, 0);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_spreadsheet_data_model_dispose (GObject * object)
{
  PsppireSpreadsheetDataModel *spreadsheetModel = PSPPIRE_SPREADSHEET_DATA_MODEL (object);

  if (spreadsheetModel->dispose_has_run)
    return;

  spreadsheetModel->dispose_has_run = TRUE;

  spreadsheet_unref (spreadsheetModel->spreadsheet);
  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
psppire_spreadsheet_data_model_finalize (GObject * object)
{
  //  PsppireSpreadsheetDataModel *spreadsheetModel = PSPPIRE_SPREADSHEET_DATA_MODEL (object);
}

static void
psppire_spreadsheet_data_model_class_init (PsppireSpreadsheetDataModelClass * class)
{
  GObjectClass *object_class;

  GParamSpec *spreadsheet_spec = g_param_spec_pointer ("spreadsheet",
                                                       "Spreadsheet",
                                                       "The spreadsheet that this model represents",
                                                       G_PARAM_CONSTRUCT_ONLY
                                                       | G_PARAM_WRITABLE);


  GParamSpec *sheet_number_spec = g_param_spec_int ("sheet-number",
                                                    "Sheet Number",
                                                    "The number of the sheet",
                                                    0, G_MAXINT,
                                                    0,
                                                    G_PARAM_READABLE | G_PARAM_WRITABLE);

  parent_class = g_type_class_peek_parent (class);
  object_class = (GObjectClass *) class;

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



  object_class->set_property = psppire_spreadsheet_data_model_set_property;
  object_class->get_property = psppire_spreadsheet_data_model_get_property;

  g_object_class_install_property (object_class,
                                   PROP_SPREADSHEET, spreadsheet_spec);

  g_object_class_install_property (object_class,
                                   PROP_SHEET_NUMBER, sheet_number_spec);

  object_class->finalize = psppire_spreadsheet_data_model_finalize;
  object_class->dispose = psppire_spreadsheet_data_model_dispose;
}


static void
psppire_spreadsheet_data_model_init (PsppireSpreadsheetDataModel * spreadsheetModel)
{
  spreadsheetModel->dispose_has_run = FALSE;
  spreadsheetModel->stamp = g_random_int ();
}


GtkTreeModel *
psppire_spreadsheet_data_model_new (struct spreadsheet *sp, gint sheet_number)
{
  return g_object_new (psppire_spreadsheet_data_model_get_type (),
                       "spreadsheet", sp,
                       "sheet-number", sheet_number,
                       NULL);
}



static gint
tree_model_n_columns (GtkTreeModel *model)
{
  PsppireSpreadsheetDataModel *sp = PSPPIRE_SPREADSHEET_DATA_MODEL (model);

  return spreadsheet_get_sheet_n_columns (sp->spreadsheet, sp->sheet_number);
}

static GtkTreeModelFlags
tree_model_get_flags (GtkTreeModel * model)
{
  g_return_val_if_fail (PSPPIRE_IS_SPREADSHEET_DATA_MODEL (model),
                        (GtkTreeModelFlags) 0);

  return GTK_TREE_MODEL_LIST_ONLY;
}

static GType
tree_model_column_type (GtkTreeModel * model, gint index)
{
  g_print ("%s:%d %p\n", __FILE__, __LINE__, model);
  g_return_val_if_fail (PSPPIRE_IS_SPREADSHEET_DATA_MODEL (model), (GType) 0);

  return G_TYPE_STRING;
}


static gboolean
tree_model_get_iter (GtkTreeModel * model, GtkTreeIter * iter,
                     GtkTreePath * path)
{
  g_print ("%s:%d %p\n", __FILE__, __LINE__, model);
  PsppireSpreadsheetDataModel *spreadsheetModel =
    PSPPIRE_SPREADSHEET_DATA_MODEL (model);
  gint *indices, depth;
  gint n;

  g_return_val_if_fail (path, FALSE);

  depth = gtk_tree_path_get_depth (path);

  g_return_val_if_fail (depth == 1, FALSE);

  indices = gtk_tree_path_get_indices (path);

  n = indices[0];

  iter->stamp = spreadsheetModel->stamp;
  iter->user_data = (gpointer) (intptr_t) n;

  return TRUE;
}

static gboolean
tree_model_iter_next (GtkTreeModel *model, GtkTreeIter *iter)
{
  g_print ("%s:%d %p\n", __FILE__, __LINE__, model);
  PsppireSpreadsheetDataModel *spreadsheetModel = PSPPIRE_SPREADSHEET_DATA_MODEL (model);
  g_assert (iter);
  g_return_val_if_fail (iter->stamp == spreadsheetModel->stamp, FALSE);


  iter->user_data = GINT_TO_POINTER (GPOINTER_TO_INT (iter->user_data) + 1);

  return TRUE;
}

static void
tree_model_get_value (GtkTreeModel *model, GtkTreeIter *iter,
                      gint column, GValue *value)
{
  PsppireSpreadsheetDataModel *sp = PSPPIRE_SPREADSHEET_DATA_MODEL (model);
  g_return_if_fail (column >= 0);
  g_return_if_fail (iter->stamp == sp->stamp);

  gint row = GPOINTER_TO_INT (iter->user_data);

  g_value_init (value, G_TYPE_STRING);

  char *x = spreadsheet_get_cell (sp->spreadsheet, sp->sheet_number, row, column);

  g_value_take_string (value, x);
}

static gboolean
tree_model_nth_child (GtkTreeModel *model, GtkTreeIter *iter,
                      GtkTreeIter *parent, gint n)
{
  PsppireSpreadsheetDataModel *spreadsheetModel =
    PSPPIRE_SPREADSHEET_DATA_MODEL (model);

  if (parent)
    return FALSE;

  iter->stamp = spreadsheetModel->stamp;
  iter->user_data = GINT_TO_POINTER (n);

  return TRUE;
}

static gint
tree_model_n_children (GtkTreeModel *model, GtkTreeIter *iter)
{
  PsppireSpreadsheetDataModel *sp = PSPPIRE_SPREADSHEET_DATA_MODEL (model);

  if (iter == NULL)
    {
      return spreadsheet_get_sheet_n_rows (sp->spreadsheet, sp->sheet_number);
    }

  return 0;
}

static gboolean
tree_model_iter_has_child (GtkTreeModel *model, GtkTreeIter *iter)
{
  g_print ("%s:%d %p\n", __FILE__, __LINE__, model);
  return FALSE;
}

static GtkTreePath *
tree_model_get_path (GtkTreeModel * model, GtkTreeIter * iter)
{
  g_print ("%s:%d %p\n", __FILE__, __LINE__, model);
  PsppireSpreadsheetDataModel *spreadsheetModel =
    PSPPIRE_SPREADSHEET_DATA_MODEL (model);
  GtkTreePath *path;
  gint index = GPOINTER_TO_INT (iter->user_data);

  g_return_val_if_fail (iter->stamp == spreadsheetModel->stamp, NULL);

  path = gtk_tree_path_new ();

  gtk_tree_path_append_index (path, index);

  return path;
}


static gboolean
tree_model_children (GtkTreeModel *model, GtkTreeIter *iter, GtkTreeIter *parent)
{
  g_print ("%s:%d %p\n", __FILE__, __LINE__, model);
  PsppireSpreadsheetDataModel *spreadsheetModel = PSPPIRE_SPREADSHEET_DATA_MODEL (model);

  if (parent != NULL)
    return FALSE;

  iter->stamp = spreadsheetModel->stamp;
  iter->user_data = 0;

  return TRUE;
}

static void
spreadsheet_tree_model_init (GtkTreeModelIface * iface)
{
  iface->get_flags = tree_model_get_flags;
  iface->get_n_columns = tree_model_n_columns;
  iface->get_column_type = tree_model_column_type;
  iface->get_iter = tree_model_get_iter;
  iface->iter_next = tree_model_iter_next;
  iface->get_value = tree_model_get_value;

  iface->iter_children = tree_model_children;
  iface->iter_parent = NULL;

  iface->get_path = tree_model_get_path;
  iface->iter_has_child = tree_model_iter_has_child;
  iface->iter_n_children = tree_model_n_children;
  iface->iter_nth_child = tree_model_nth_child;
}
