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
#include "psppire-data-sheet.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define P_(X) (X)

#include "value-variant.h"

#include "ui/gui/executor.h"
#include "psppire-data-window.h"

static void
do_sort (PsppireDataSheet *sheet, GtkSortType order)
{
  SswRange *range = SSW_SHEET(sheet)->selection;

  PsppireDataStore *data_store = NULL;
  g_object_get (sheet, "data-model", &data_store, NULL);

  int n_vars = 0;
  int i;

  PsppireDataWindow *pdw =
     psppire_data_window_for_data_store (data_store);

  GString *syntax = g_string_new ("SORT CASES BY");
  for (i = range->start_x ; i <= range->end_x; ++i)
    {
      const struct variable *var = psppire_dict_get_variable (data_store->dict, i);
      if (var != NULL)
        {
          g_string_append_printf (syntax, " %s", var_get_name (var));
          n_vars++;
        }
    }
  if (n_vars > 0)
    {
      if (order == GTK_SORT_DESCENDING)
        g_string_append (syntax, " (DOWN)");
      g_string_append_c (syntax, '.');
      execute_const_syntax_string (pdw, syntax->str);
    }
  g_string_free (syntax, TRUE);
}


static void
sort_ascending (PsppireDataSheet *sheet)
{
  do_sort (sheet, GTK_SORT_ASCENDING);

  gtk_widget_queue_draw (GTK_WIDGET (sheet));
}

static void
sort_descending (PsppireDataSheet *sheet)
{
  do_sort (sheet, GTK_SORT_DESCENDING);

  gtk_widget_queue_draw (GTK_WIDGET (sheet));
}



static void
change_data_value (PsppireDataSheet *sheet, gint col, gint row, GValue *value)
{
  PsppireDataStore *store = NULL;
  g_object_get (sheet, "data-model", &store, NULL);

  const struct variable *var = psppire_dict_get_variable (store->dict, col);

  if (NULL == var)
    return;

  union value v;

  GVariant *vrnt = g_value_get_variant (value);

  value_variant_get (&v, vrnt);

  psppire_data_store_set_value (store, row, var, &v);

  value_destroy_from_variant (&v, vrnt);
}

gboolean myreversefunc (GtkTreeModel *model, gint col, gint row, const gchar *in,
		    GValue *out);



static void
show_cases_row_popup (PsppireDataSheet *sheet, int row,
		      uint button, uint state, gpointer p)
{
  GListModel *vmodel = NULL;
  g_object_get (sheet, "vmodel", &vmodel, NULL);
  if (vmodel == NULL)
    return;

  guint n_items = g_list_model_get_n_items (vmodel);

  if (row >= n_items)
    return;

  if (button != 3)
    return;

  g_object_set_data (G_OBJECT (sheet->data_sheet_cases_row_popup), "item",
		     GINT_TO_POINTER (row));

  gtk_menu_popup_at_pointer (GTK_MENU (sheet->data_sheet_cases_row_popup), NULL);
}


static void
insert_new_case (PsppireDataSheet *sheet)
{
  PsppireDataStore *data_store = NULL;
  g_object_get (sheet, "data-model", &data_store, NULL);

  gint posn = GPOINTER_TO_INT (g_object_get_data
				(G_OBJECT (sheet->data_sheet_cases_row_popup), "item"));

  psppire_data_store_insert_new_case (data_store, posn);

  gtk_widget_queue_draw (GTK_WIDGET (sheet));
}

static void
delete_cases (PsppireDataSheet *sheet)
{
  SswRange *range = SSW_SHEET(sheet)->selection;

  PsppireDataStore *data_store = NULL;
  g_object_get (sheet, "data-model", &data_store, NULL);

  psppire_data_store_delete_cases (data_store, range->start_y,
  				   range->end_y - range->start_y + 1);

  gtk_widget_queue_draw (GTK_WIDGET (sheet));
}

static GtkWidget *
create_data_row_header_popup_menu (PsppireDataSheet *sheet)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *item =
    gtk_menu_item_new_with_mnemonic  (_("_Insert Case"));

  g_signal_connect_swapped (item, "activate", G_CALLBACK (insert_new_case), sheet);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  sheet->data_clear_cases_menu_item = gtk_menu_item_new_with_mnemonic (_("Cl_ear Cases"));
  gtk_widget_set_sensitive (sheet->data_clear_cases_menu_item, FALSE);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sheet->data_clear_cases_menu_item);
  g_signal_connect_swapped (sheet->data_clear_cases_menu_item, "activate",
			    G_CALLBACK (delete_cases), sheet);

  gtk_widget_show_all (menu);
  return menu;
}


static void
show_cases_column_popup (PsppireDataSheet *sheet, int column, uint button, uint state,
			 gpointer p)
{
  GListModel *hmodel = NULL;
  g_object_get (sheet, "hmodel", &hmodel, NULL);
  if (hmodel == NULL)
    return;

  guint n_items = g_list_model_get_n_items (hmodel);

  if (column >= n_items)
    return;

  if (button != 3)
    return;

  g_object_set_data (G_OBJECT (sheet->data_sheet_cases_column_popup), "item",
		     GINT_TO_POINTER (column));

  gtk_menu_popup_at_pointer (GTK_MENU (sheet->data_sheet_cases_column_popup), NULL);
}

static void
insert_new_variable (PsppireDataSheet *sheet)
{
  PsppireDataStore *data_store = NULL;
  g_object_get (sheet, "data-model", &data_store, NULL);

  gint posn = GPOINTER_TO_INT (g_object_get_data
				(G_OBJECT (sheet->data_sheet_cases_column_popup),
				 "item"));

  const struct variable *v = psppire_dict_insert_variable (data_store->dict,
							   posn, NULL);

  psppire_data_store_insert_value (data_store, var_get_width(v),
				   var_get_case_index (v));

  gtk_widget_queue_draw (GTK_WIDGET (sheet));
}

static void
set_menu_items_sensitivity (PsppireDataSheet *sheet, gpointer selection, gpointer p)
{
  SswRange *range = selection;

  PsppireDataStore *data_store = NULL;
  g_object_get (sheet, "data-model", &data_store, NULL);


  gint width = gtk_tree_model_get_n_columns (GTK_TREE_MODEL (data_store));
  gint length = psppire_data_store_get_case_count (data_store);


  gboolean whole_row_selected = (range->start_x == 0 && range->end_x == width - 1);
  gtk_widget_set_sensitive (sheet->data_clear_cases_menu_item, whole_row_selected);

  gboolean whole_column_selected =
    (range->start_y == 0 && range->end_y == length - 1);
  gtk_widget_set_sensitive (sheet->data_clear_variables_menu_item,
			    whole_column_selected);
  gtk_widget_set_sensitive (sheet->data_sort_ascending_menu_item,
			    whole_column_selected);
  gtk_widget_set_sensitive (sheet->data_sort_descending_menu_item,
			    whole_column_selected);
}

static void
delete_variables (PsppireDataSheet *sheet)
{
  SswRange *range = SSW_SHEET(sheet)->selection;

  PsppireDataStore *data_store = NULL;
  g_object_get (sheet, "data-model", &data_store, NULL);

  psppire_dict_delete_variables (data_store->dict, range->start_x,
				 (range->end_x - range->start_x + 1));

  gtk_widget_queue_draw (GTK_WIDGET (sheet));
}



static GtkWidget *
create_data_column_header_popup_menu (PsppireDataSheet *sheet)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *item =
    gtk_menu_item_new_with_mnemonic  (_("_Insert Variable"));
  g_signal_connect_swapped (item, "activate", G_CALLBACK (insert_new_variable),
			    sheet);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  sheet->data_clear_variables_menu_item =
    gtk_menu_item_new_with_mnemonic  (_("Cl_ear Variables"));
  g_signal_connect_swapped (sheet->data_clear_variables_menu_item, "activate",
			    G_CALLBACK (delete_variables),
			    sheet);
  gtk_widget_set_sensitive (sheet->data_clear_variables_menu_item, FALSE);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sheet->data_clear_variables_menu_item);

  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);


  sheet->data_sort_ascending_menu_item =
    gtk_menu_item_new_with_mnemonic (_("Sort _Ascending"));
  g_signal_connect_swapped (sheet->data_sort_ascending_menu_item, "activate",
			    G_CALLBACK (sort_ascending), sheet);
  gtk_widget_set_sensitive (sheet->data_sort_ascending_menu_item, FALSE);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sheet->data_sort_ascending_menu_item);

  sheet->data_sort_descending_menu_item =
    gtk_menu_item_new_with_mnemonic (_("Sort _Descending"));
  g_signal_connect_swapped (sheet->data_sort_descending_menu_item, "activate",
			    G_CALLBACK (sort_descending), sheet);
  gtk_widget_set_sensitive (sheet->data_sort_descending_menu_item, FALSE);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), sheet->data_sort_descending_menu_item);

  gtk_widget_show_all (menu);
  return menu;
}




G_DEFINE_TYPE (PsppireDataSheet, psppire_data_sheet, SSW_TYPE_SHEET)

static GObjectClass * parent_class = NULL;
static gboolean dispose_has_run = FALSE;

static void
psppire_data_sheet_dispose (GObject *obj)
{
  //  PsppireDataSheet *sheet = PSPPIRE_DATA_SHEET (obj);

  if (dispose_has_run)
    return;

  dispose_has_run = TRUE;

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
psppire_data_sheet_class_init (PsppireDataSheetClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  object_class->dispose = psppire_data_sheet_dispose;

  parent_class = g_type_class_peek_parent (class);
}

GtkWidget*
psppire_data_sheet_new (void)
{
  GObject *obj =
    g_object_new (PSPPIRE_TYPE_DATA_SHEET,
		  "forward-conversion", psppire_data_store_value_to_string,
		  "reverse-conversion", myreversefunc,
		  NULL);

  return GTK_WIDGET (obj);
}

static void
set_dictionary (PsppireDataSheet *sheet)
{
  GtkTreeModel *data_model = NULL;
  g_object_get (sheet, "data-model", &data_model, NULL);

  PsppireDataStore *store = PSPPIRE_DATA_STORE (data_model);
  g_object_set (sheet, "hmodel", store->dict, NULL);
}

static void
move_variable (PsppireDataSheet *sheet, gint from, gint to, gpointer ud)
{
  PsppireDataStore *data_store = NULL;
  g_object_get (sheet, "data-model", &data_store, NULL);

  if (data_store == NULL)
    return;

  PsppireDict *dict = data_store->dict;
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
psppire_data_sheet_init (PsppireDataSheet *sheet)
{
  sheet->data_sheet_cases_column_popup =
    create_data_column_header_popup_menu (sheet);

  sheet->data_sheet_cases_row_popup =
    create_data_row_header_popup_menu (sheet);

  g_signal_connect (sheet, "selection-changed",
		    G_CALLBACK (set_menu_items_sensitivity), sheet);

  g_signal_connect (sheet, "column-header-pressed",
		    G_CALLBACK (show_cases_column_popup), sheet);

  g_signal_connect (sheet, "row-header-pressed",
		    G_CALLBACK (show_cases_row_popup), sheet);

  g_signal_connect (sheet, "value-changed",
		    G_CALLBACK (change_data_value), NULL);

  g_signal_connect (sheet, "notify::data-model",
		    G_CALLBACK (set_dictionary), NULL);

  g_signal_connect (sheet, "column-moved", G_CALLBACK (move_variable), NULL);
}
