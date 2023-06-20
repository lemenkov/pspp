/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2013, 2014  Free Software Foundation

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

/* This file implements a GtkTreeModel.  It allows GtkComboBox and
   GtkTreeView to display the names and non-empty cell ranges of the
   sheets aka "Tables" of spreadsheet files.
   It doesn't take any notice of the spreadsheet data itself.
*/

#include <config.h>
#include <glib.h>

#include <stdint.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


#include "psppire-spreadsheet-model.h"
#include "data/spreadsheet-reader.h"

static void psppire_spreadsheet_model_finalize (GObject * object);
static void psppire_spreadsheet_model_dispose (GObject * object);

static GObjectClass *parent_class = NULL;

static void spreadsheet_tree_model_init (GtkTreeModelIface * iface);

G_DEFINE_TYPE_WITH_CODE (PsppireSpreadsheetModel,\
                         psppire_spreadsheet_model,\
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
                                                spreadsheet_tree_model_init))

/* Properties */
enum
{
  PROP_0,
  PROP_SPREADSHEET
};


static void
psppire_spreadsheet_model_set_property (GObject * object,
                                        guint prop_id,
                                        const GValue * value,
                                        GParamSpec * pspec)
{
  PsppireSpreadsheetModel *spreadsheetModel =
    PSPPIRE_SPREADSHEET_MODEL (object);

  switch (prop_id)
    {
    case PROP_SPREADSHEET:
      {
        struct spreadsheet *old = spreadsheetModel->spreadsheet;
        spreadsheetModel->spreadsheet = spreadsheet_ref (g_value_get_pointer (value));
        if (old)
          spreadsheet_unref (old);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_spreadsheet_model_dispose (GObject * object)
{
  PsppireSpreadsheetModel *spreadsheetModel = PSPPIRE_SPREADSHEET_MODEL (object);

  if (!spreadsheetModel->dispose_has_run)
    {
      spreadsheet_unref (spreadsheetModel->spreadsheet);

      spreadsheetModel->dispose_has_run = TRUE;
    }
}

static void
psppire_spreadsheet_model_finalize (GObject * object)
{
  //  PsppireSpreadsheetModel *spreadsheetModel = PSPPIRE_SPREADSHEET_MODEL (object);
}

static void
psppire_spreadsheet_model_class_init (PsppireSpreadsheetModelClass * class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  GParamSpec *spreadsheet_spec = g_param_spec_pointer ("spreadsheet",
                                                       "Spreadsheet",
                                                       "The spreadsheet that this model represents",
                                                       G_PARAM_CONSTRUCT_ONLY
                                                       | G_PARAM_WRITABLE);

  parent_class = g_type_class_peek_parent (class);


  object_class->set_property = psppire_spreadsheet_model_set_property;

  g_object_class_install_property (object_class,
                                   PROP_SPREADSHEET, spreadsheet_spec);

  object_class->finalize = psppire_spreadsheet_model_finalize;
  object_class->dispose = psppire_spreadsheet_model_dispose;
}


static void
psppire_spreadsheet_model_init (PsppireSpreadsheetModel * spreadsheetModel)
{
  spreadsheetModel->dispose_has_run = FALSE;
  spreadsheetModel->stamp = g_random_int ();
}


GtkTreeModel *
psppire_spreadsheet_model_new (struct spreadsheet *sp)
{
  return g_object_new (psppire_spreadsheet_model_get_type (),
                       "spreadsheet", sp, NULL);
}




static gint
tree_model_n_columns (GtkTreeModel * model)
{
  return PSPPIRE_SPREADSHEET_MODEL_N_COLS;
}

static GtkTreeModelFlags
tree_model_get_flags (GtkTreeModel * model)
{
  g_return_val_if_fail (PSPPIRE_IS_SPREADSHEET_MODEL (model),
                        (GtkTreeModelFlags) 0);

  return GTK_TREE_MODEL_LIST_ONLY;
}

static GType
tree_model_column_type (GtkTreeModel * model, gint index)
{
  g_return_val_if_fail (PSPPIRE_IS_SPREADSHEET_MODEL (model), (GType) 0);
  g_return_val_if_fail (index < PSPPIRE_SPREADSHEET_MODEL_N_COLS, (GType) 0);

  if (index ==  PSPPIRE_SPREADSHEET_MODEL_COL_SHEET_ROWS)
    return G_TYPE_UINT;

  return G_TYPE_STRING;
}


static gboolean
tree_model_get_iter (GtkTreeModel * model, GtkTreeIter * iter,
                     GtkTreePath * path)
{
  PsppireSpreadsheetModel *spreadsheetModel =
    PSPPIRE_SPREADSHEET_MODEL (model);
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
  PsppireSpreadsheetModel *spreadsheetModel = PSPPIRE_SPREADSHEET_MODEL (model);
  g_assert (iter);
  g_return_val_if_fail (iter->stamp == spreadsheetModel->stamp, FALSE);

  if ((intptr_t) iter->user_data >=
      spreadsheet_get_sheet_n_sheets (spreadsheetModel->spreadsheet) - 1)
    {
      iter->user_data = NULL;
      iter->stamp = 0;
      return FALSE;
    }

  iter->user_data = GINT_TO_POINTER (GPOINTER_TO_INT (iter->user_data) + 1);

  return TRUE;
}


static void
tree_model_get_value (GtkTreeModel * model, GtkTreeIter * iter,
                      gint column, GValue * value)
{
  PsppireSpreadsheetModel *spreadsheetModel =
    PSPPIRE_SPREADSHEET_MODEL (model);
  g_return_if_fail (column < PSPPIRE_SPREADSHEET_MODEL_N_COLS);
  g_return_if_fail (iter->stamp == spreadsheetModel->stamp);

  switch (column)
    {
    case PSPPIRE_SPREADSHEET_MODEL_COL_NAME:
      {
        g_value_init (value, G_TYPE_STRING);
        const char *x =
          spreadsheet_get_sheet_name (spreadsheetModel->spreadsheet,
                                      (intptr_t) iter->user_data);

        g_value_set_string (value, x);
      }
      break;
    case PSPPIRE_SPREADSHEET_MODEL_COL_RANGE:
      {
        g_value_init (value, G_TYPE_STRING);
        char *x =
          spreadsheet_get_sheet_range (spreadsheetModel->spreadsheet,
                                       (intptr_t) iter->user_data);

        g_value_set_string (value, x ? x : _("(empty)"));
        g_free (x);
      }
      break;
    case PSPPIRE_SPREADSHEET_MODEL_COL_SHEET_ROWS:
      {
        g_value_init (value, G_TYPE_UINT);
        unsigned int rows =
          spreadsheet_get_sheet_n_rows (spreadsheetModel->spreadsheet,
                                        (intptr_t) iter->user_data);

        g_value_set_uint (value, rows);
      }
      break;
    case PSPPIRE_SPREADSHEET_MODEL_COL_SHEET_COLUMNS:
      {
        g_value_init (value, G_TYPE_UINT);
        unsigned int columns =
          spreadsheet_get_sheet_n_columns (spreadsheetModel->spreadsheet,
                                           (intptr_t) iter->user_data);

        g_value_set_uint (value, columns);
      }
      break;
    default:
      g_error ("%s:%d Invalid column in spreadsheet model",
               __FILE__, __LINE__);
      break;
    }
}

static gboolean
tree_model_nth_child (GtkTreeModel * model, GtkTreeIter * iter,
                      GtkTreeIter * parent, gint n)
{
  PsppireSpreadsheetModel *spreadsheetModel =
    PSPPIRE_SPREADSHEET_MODEL (model);

  if (parent)
    return FALSE;

  if (n >= spreadsheet_get_sheet_n_sheets (spreadsheetModel->spreadsheet))
    return FALSE;

  iter->stamp = spreadsheetModel->stamp;
  iter->user_data = (gpointer) (intptr_t) n;

  return TRUE;
}

static gint
tree_model_n_children (GtkTreeModel * model, GtkTreeIter * iter)
{
  PsppireSpreadsheetModel *spreadsheetModel =
    PSPPIRE_SPREADSHEET_MODEL (model);

  if (iter == NULL)
    return spreadsheet_get_sheet_n_sheets (spreadsheetModel->spreadsheet);

  return 0;
}

static gboolean
tree_model_iter_has_child (GtkTreeModel *tree_model, GtkTreeIter *iter)
{
  return FALSE;
}

static GtkTreePath *
tree_model_get_path (GtkTreeModel * model, GtkTreeIter * iter)
{
  PsppireSpreadsheetModel *spreadsheetModel =
    PSPPIRE_SPREADSHEET_MODEL (model);
  GtkTreePath *path;
  gint index = (intptr_t) iter->user_data;

  g_return_val_if_fail (iter->stamp == spreadsheetModel->stamp, NULL);

  path = gtk_tree_path_new ();

  gtk_tree_path_append_index (path, index);

  return path;
}


static gboolean
tree_model_children (GtkTreeModel *model, GtkTreeIter *iter, GtkTreeIter *parent)
{
  PsppireSpreadsheetModel *spreadsheetModel = PSPPIRE_SPREADSHEET_MODEL (model);

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
