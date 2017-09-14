/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2017  John Darrington

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include "psppire-variable-sheet.h"

#include "ui/gui/psppire-var-sheet-header.h"

#include "psppire-dict.h"
#include "var-type-dialog.h"
#include "missing-val-dialog.h"
#include "val-labs-dialog.h"
#include "var-display.h"
#include "data/format.h"
#include "data/value-labels.h"
#include "helper.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define P_(X) (X)


G_DEFINE_TYPE (PsppireVariableSheet, psppire_variable_sheet, SSW_TYPE_SHEET)

static void
set_var_type (GtkCellRenderer *renderer,
     GtkCellEditable *editable,
     gchar           *path,
     gpointer         user_data)
{
  PsppireVariableSheet *sheet = PSPPIRE_VARIABLE_SHEET (user_data);
  gint row = -1, col = -1;
  ssw_sheet_get_active_cell (SSW_SHEET (sheet), &col, &row);

  PsppireDict *dict = NULL;
  g_object_get (sheet, "data-model", &dict, NULL);

  struct variable *var =
    psppire_dict_get_variable (PSPPIRE_DICT (dict), row);

  const struct fmt_spec *format = var_get_write_format (var);
  struct fmt_spec fmt = *format;
  GtkWindow *win = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sheet)));
  if (GTK_RESPONSE_OK == psppire_var_type_dialog_run (win, &fmt))
    {
      var_set_width_and_formats (var, fmt_var_width (&fmt), &fmt, &fmt);
    }
}

static void
set_missing_values (GtkCellRenderer *renderer,
     GtkCellEditable *editable,
     gchar           *path,
     gpointer         user_data)
{
  PsppireVariableSheet *sheet = PSPPIRE_VARIABLE_SHEET (user_data);
  gint row = -1, col = -1;
  ssw_sheet_get_active_cell (SSW_SHEET (sheet), &col, &row);

  PsppireDict *dict = NULL;
  g_object_get (sheet, "data-model", &dict, NULL);

  struct variable *var =
    psppire_dict_get_variable (PSPPIRE_DICT (dict), row);

  struct missing_values mv;
  if (GTK_RESPONSE_OK ==
      psppire_missing_val_dialog_run (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sheet))),
				      var, &mv))
    {
      var_set_missing_values (var, &mv);
    }

  mv_destroy (&mv);
}

static void
set_value_labels (GtkCellRenderer *renderer,
     GtkCellEditable *editable,
     gchar           *path,
     gpointer         user_data)
{
  PsppireVariableSheet *sheet = PSPPIRE_VARIABLE_SHEET (user_data);
  gint row = -1, col = -1;
  ssw_sheet_get_active_cell (SSW_SHEET (sheet), &col, &row);

  PsppireDict *dict = NULL;
  g_object_get (sheet, "data-model", &dict, NULL);

  struct variable *var =
    psppire_dict_get_variable (PSPPIRE_DICT (dict), row);

  struct val_labs *vls =
    psppire_val_labs_dialog_run (GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (sheet))), var);

  if (vls)
    {
      var_set_value_labels (var, vls);
      val_labs_destroy (vls);
    }
}

static GtkCellRenderer *
create_spin_renderer (GType type)
{
  GtkCellRenderer *r = gtk_cell_renderer_spin_new ();

  GtkAdjustment *adj = gtk_adjustment_new (0,
					   0, G_MAXDOUBLE,
					   1, 1,
					   0);
  g_object_set (r,
		"adjustment", adj,
		NULL);

  return r;
}

static GtkCellRenderer *
create_combo_renderer (GType type)
{
  GtkListStore *list_store = gtk_list_store_new (2, G_TYPE_INT, G_TYPE_STRING);

  GEnumClass *ec = g_type_class_ref (type);

  const GEnumValue *ev ;
  for (ev = ec->values; ev->value_name; ++ev)
    {
      GtkTreeIter iter;

      gtk_list_store_append (list_store, &iter);

      gtk_list_store_set (list_store, &iter,
			  0, ev->value,
			  1, gettext (ev->value_nick),
			  -1);
    }

  GtkCellRenderer *r = gtk_cell_renderer_combo_new ();

  g_object_set (r,
		"model", list_store,
		"text-column", 1,
		"has-entry", TRUE,
		NULL);

  return r;
}

static GtkCellRenderer *spin_renderer;
static GtkCellRenderer *column_width_renderer;
static GtkCellRenderer *measure_renderer;
static GtkCellRenderer *alignment_renderer;

static GtkCellRenderer *
select_renderer_func (PsppireVariableSheet *sheet, gint col, gint row, GType type, gpointer ud)
{
  if (!spin_renderer)
    spin_renderer = create_spin_renderer (type);

  if (col == DICT_TVM_COL_ROLE && !column_width_renderer)
    column_width_renderer = create_combo_renderer (type);

  if (col == DICT_TVM_COL_MEASURE && !measure_renderer)
    measure_renderer = create_combo_renderer (type);

  if (col == DICT_TVM_COL_ALIGNMENT && !alignment_renderer)
    alignment_renderer = create_combo_renderer (type);

  switch  (col)
    {
    case DICT_TVM_COL_WIDTH:
    case DICT_TVM_COL_DECIMAL:
    case DICT_TVM_COL_COLUMNS:
      return spin_renderer;

    case DICT_TVM_COL_TYPE:
      return sheet->var_type_renderer;

    case DICT_TVM_COL_VALUE_LABELS:
      return sheet->value_label_renderer;

    case DICT_TVM_COL_MISSING_VALUES:
      return sheet->missing_values_renderer;

    case DICT_TVM_COL_ALIGNMENT:
      return alignment_renderer;

    case DICT_TVM_COL_MEASURE:
      return measure_renderer;

    case DICT_TVM_COL_ROLE:
      return column_width_renderer;
    }

  return NULL;
}



static void
show_variables_row_popup (SswSheet *sheet, int row, uint button,
			  uint state, gpointer p)
{
  PsppireVariableSheet *var_sheet = PSPPIRE_VARIABLE_SHEET (sheet);
  GListModel *vmodel = NULL;
  g_object_get (sheet, "vmodel", &vmodel, NULL);
  if (vmodel == NULL)
    return;

  guint n_items = g_list_model_get_n_items (vmodel);

  if (row >= n_items)
    return;

  if (button != 3)
    return;

  g_object_set_data (G_OBJECT (var_sheet->row_popup), "item",
		     GINT_TO_POINTER (row));

  gtk_menu_popup_at_pointer (GTK_MENU (var_sheet->row_popup), NULL);
}

static void
insert_new_variable_var (PsppireVariableSheet *var_sheet)
{
  gint item = GPOINTER_TO_INT (g_object_get_data
				(G_OBJECT (var_sheet->row_popup),
				 "item"));

  PsppireDict *dict = NULL;
  g_object_get (var_sheet, "data-model", &dict, NULL);

  psppire_dict_insert_variable (dict, item, NULL);

  gtk_widget_queue_draw (GTK_WIDGET (var_sheet));
}


static void
delete_variables (SswSheet *sheet)
{
  SswRange *range = sheet->selection;

  PsppireDict *dict = NULL;
  g_object_get (sheet, "data-model", &dict, NULL);

  psppire_dict_delete_variables (dict, range->start_y,
				 (range->end_y - range->start_y + 1));

  gtk_widget_queue_draw (GTK_WIDGET (sheet));
}

static GtkWidget *
create_var_row_header_popup_menu (PsppireVariableSheet *var_sheet)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *item =
    gtk_menu_item_new_with_mnemonic  (_("_Insert Variable"));
  g_signal_connect_swapped (item, "activate", G_CALLBACK (insert_new_variable_var),
			    var_sheet);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  var_sheet->clear_variables_menu_item =
    gtk_menu_item_new_with_mnemonic (_("Cl_ear Variables"));

  g_signal_connect_swapped (var_sheet->clear_variables_menu_item, "activate",
			    G_CALLBACK (delete_variables), var_sheet);

  gtk_widget_set_sensitive (var_sheet->clear_variables_menu_item, FALSE);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu),
			 var_sheet->clear_variables_menu_item);

  gtk_widget_show_all (menu);
  return menu;
}


static void
set_var_popup_sensitivity (SswSheet *sheet, gpointer selection, gpointer p)
{
  PsppireVariableSheet *var_sheet = PSPPIRE_VARIABLE_SHEET (sheet);
  SswRange *range = selection;
  gint width = gtk_tree_model_get_n_columns (sheet->data_model);

  gboolean whole_row_selected = (range->start_x == 0 &&
				 range->end_x == width - 1 - 1);
  /*  PsppireDict has an "extra" column: TVM_COL_VAR   ^^^ */
  gtk_widget_set_sensitive (var_sheet->clear_variables_menu_item,
			    whole_row_selected);
}



static void
change_var_property (PsppireVariableSheet *var_sheet, gint col, gint row, const GValue *value)
{
  PsppireDict *dict = NULL;
  g_object_get (var_sheet, "data-model", &dict, NULL);

  /* Return the IDXth variable */
  struct variable *var =  psppire_dict_get_variable (dict, row);

  if (NULL == var)
    var = psppire_dict_insert_variable (dict, row, NULL);

  switch (col)
    {
    case DICT_TVM_COL_NAME:
      {
	const char *name = g_value_get_string (value);
	if (psppire_dict_check_name (dict, name, FALSE))
	  dict_rename_var (dict->dict, var, g_value_get_string (value));
      }
      break;
    case DICT_TVM_COL_WIDTH:
      {
      gint width = g_value_get_int (value);
      if (var_is_numeric (var))
        {
          struct fmt_spec format = *var_get_print_format (var);
	  fmt_change_width (&format, width, FMT_FOR_OUTPUT);
          var_set_both_formats (var, &format);
        }
      else
	{
	  var_set_width (var, width);
	}
      }
      break;
    case DICT_TVM_COL_DECIMAL:
      {
      gint decimals = g_value_get_int (value);
      if (decimals >= 0)
        {
          struct fmt_spec format = *var_get_print_format (var);
	  fmt_change_decimals (&format, decimals, FMT_FOR_OUTPUT);
          var_set_both_formats (var, &format);
        }
      }
      break;
    case DICT_TVM_COL_LABEL:
      var_set_label (var, g_value_get_string (value));
      break;
    case DICT_TVM_COL_COLUMNS:
      var_set_display_width (var, g_value_get_int (value));
      break;
    case DICT_TVM_COL_MEASURE:
      var_set_measure (var, g_value_get_int (value));
      break;
    case DICT_TVM_COL_ALIGNMENT:
      var_set_alignment (var, g_value_get_int (value));
      break;
    case DICT_TVM_COL_ROLE:
      var_set_role (var, g_value_get_int (value));
      break;
    default:
      g_warning ("Changing unknown column %d of variable sheet column not supported",
		 col);
      break;
    }
}

static gchar *
var_sheet_data_to_string (SswSheet *sheet, GtkTreeModel *m,
			  gint col, gint row, const GValue *in)
{
  if (col >= n_DICT_COLS - 1) /* -1 because psppire-dict has an extra column */
    return NULL;

  const struct variable *var = psppire_dict_get_variable (PSPPIRE_DICT (m), row);
  if (var == NULL)
    return NULL;

  if (col == DICT_TVM_COL_TYPE)
    {
      const struct fmt_spec *print = var_get_print_format (var);
      return strdup (fmt_gui_name (print->type));
    }
  else if (col == DICT_TVM_COL_MISSING_VALUES)
    return missing_values_to_string (var, NULL);
  else if (col == DICT_TVM_COL_VALUE_LABELS)
    {
      const struct val_labs *vls = var_get_value_labels (var);
      if (vls == NULL || val_labs_count (vls) == 0)
	return strdup (_("None"));
      const struct val_lab **labels = val_labs_sorted (vls);
      const struct val_lab *vl = labels[0];
      gchar *vstr = value_to_text (vl->value, var);
      char *text = xasprintf (_("{%s, %s}..."), vstr,
			      val_lab_get_escaped_label (vl));
      free (vstr);
      free (labels);
      return text;
    }

  return ssw_sheet_default_forward_conversion (sheet, m, col, row, in);
}



static GObjectClass * parent_class = NULL;

static void
psppire_variable_sheet_dispose (GObject *obj)
{
  PsppireVariableSheet *sheet = PSPPIRE_VARIABLE_SHEET (obj);

  if (sheet->dispose_has_run)
    return;

  sheet->dispose_has_run = TRUE;

  g_object_unref (sheet->value_label_renderer);
  g_object_unref (sheet->missing_values_renderer);
  g_object_unref (sheet->var_type_renderer);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
psppire_variable_sheet_class_init (PsppireVariableSheetClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  object_class->dispose = psppire_variable_sheet_dispose;

  parent_class = g_type_class_peek_parent (class);
}

GtkWidget*
psppire_variable_sheet_new (void)
{
  PsppireVarSheetHeader *vsh =
    g_object_new (PSPPIRE_TYPE_VAR_SHEET_HEADER, NULL);

  GObject *obj =
    g_object_new (PSPPIRE_TYPE_VARIABLE_SHEET,
		  "select-renderer-func", select_renderer_func,
		  "hmodel", vsh,
		  "forward-conversion", var_sheet_data_to_string,
		  "editable", TRUE,
		  NULL);

  return GTK_WIDGET (obj);
}

static void
move_variable (PsppireVariableSheet *sheet, gint from, gint to, gpointer ud)
{
  PsppireDict *dict = NULL;
  g_object_get (sheet, "data-model", &dict, NULL);

  if (dict == NULL)
    return;

  struct variable *var = psppire_dict_get_variable (dict, from);

  if (var == NULL)
    return;
  gint new_pos = to;
  /* The index refers to the final position, so if the source
     is less than the destination, then we must subtract 1, to
     account for the position vacated by the source */
  if (from < to)
    new_pos--;
  dict_reorder_var (dict->dict, var, new_pos);
}

static void
psppire_variable_sheet_init (PsppireVariableSheet *sheet)
{
  sheet->dispose_has_run = FALSE;

  sheet->value_label_renderer = gtk_cell_renderer_text_new ();
  g_signal_connect (sheet->value_label_renderer,
		    "editing-started", G_CALLBACK (set_value_labels),
		    sheet);

  sheet->missing_values_renderer = gtk_cell_renderer_text_new ();
  g_signal_connect (sheet->missing_values_renderer,
		    "editing-started", G_CALLBACK (set_missing_values),
		    sheet);

  sheet->var_type_renderer = gtk_cell_renderer_text_new ();
  g_signal_connect (sheet->var_type_renderer,
		    "editing-started", G_CALLBACK (set_var_type),
		    sheet);

  sheet->row_popup = create_var_row_header_popup_menu (sheet);


  g_signal_connect (sheet, "selection-changed",
		    G_CALLBACK (set_var_popup_sensitivity), sheet);

  g_signal_connect (sheet, "row-header-pressed",
                    G_CALLBACK (show_variables_row_popup), sheet);

  g_signal_connect_swapped (sheet, "value-changed",
			    G_CALLBACK (change_var_property), sheet);

  g_signal_connect (sheet, "row-moved",
		    G_CALLBACK (move_variable), NULL);
}
