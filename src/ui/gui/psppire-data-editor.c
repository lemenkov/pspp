/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2012, 2016,
   2017 Free Software Foundation, Inc.

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

#include "ui/gui/psppire-data-editor.h"

#include <gtk/gtk.h>
#include <gtk-contrib/gtkxpaned.h>

#include "data/datasheet.h"
#include "data/value-labels.h"
#include "libpspp/range-set.h"
#include "libpspp/str.h"
#include "ui/gui/executor.h"
#include "ui/gui/helper.h"
#include "ui/gui/psppire-data-store.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-value-entry.h"
#include "ui/gui/psppire-conf.h"
#include "ui/gui/psppire-var-sheet-header.h"

#include "value-variant.h"


#include "ui/gui/efficient-sheet/jmd-sheet.h"
#include "ui/gui/efficient-sheet/jmd-sheet-body.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)


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
select_renderer_func (gint col, gint row, GType type)
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

    case DICT_TVM_COL_ALIGNMENT:
      return alignment_renderer;

    case DICT_TVM_COL_MEASURE:
      return measure_renderer;

    case DICT_TVM_COL_ROLE:
      return column_width_renderer;
    }

  return NULL;
}


static void psppire_data_editor_class_init          (PsppireDataEditorClass *klass);
static void psppire_data_editor_init                (PsppireDataEditor      *de);

static void disconnect_data_sheets (PsppireDataEditor *);
static void refresh_entry (PsppireDataEditor *);

GType
psppire_data_editor_get_type (void)
{
  static GType de_type = 0;

  if (!de_type)
    {
      static const GTypeInfo de_info =
      {
	sizeof (PsppireDataEditorClass),
	NULL, /* base_init */
        NULL, /* base_finalize */
	(GClassInitFunc) psppire_data_editor_class_init,
        NULL, /* class_finalize */
	NULL, /* class_data */
        sizeof (PsppireDataEditor),
	0,
	(GInstanceInitFunc) psppire_data_editor_init,
      };

      de_type = g_type_register_static (GTK_TYPE_NOTEBOOK, "PsppireDataEditor",
					&de_info, 0);
    }

  return de_type;
}

static GObjectClass * parent_class = NULL;

static void
psppire_data_editor_dispose (GObject *obj)
{
  PsppireDataEditor *de = (PsppireDataEditor *) obj;

  disconnect_data_sheets (de);

  if (de->data_store)
    {
      g_object_unref (de->data_store);
      de->data_store = NULL;
    }

  if (de->dict)
    {
      g_object_unref (de->dict);
      de->dict = NULL;
    }

  if (de->font != NULL)
    {
      pango_font_description_free (de->font);
      de->font = NULL;
    }

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

enum
  {
    PROP_0,
    PROP_DATA_STORE,
    PROP_DICTIONARY,
    PROP_VALUE_LABELS,
    PROP_SPLIT_WINDOW
  };

static void
psppire_data_editor_refresh_model (PsppireDataEditor *de)
{
}

static void
change_var_property (PsppireDict *dict, gint col, gint row, GValue *value)
{
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
    case DICT_TVM_COL_LABEL:
      var_set_label (var, g_value_get_string (value));
      break;
    case DICT_TVM_COL_COLUMNS:
      var_set_display_width (var, g_value_get_int (value));
      break;
    case DICT_TVM_COL_MEASURE:
      var_set_measure (var, g_value_get_enum (value));
      break;
    case DICT_TVM_COL_ALIGNMENT:
      var_set_alignment (var, g_value_get_enum (value));
      break;
    case DICT_TVM_COL_ROLE:
      var_set_role (var, g_value_get_enum (value));
      break;
    default:
      g_message ("Changing col %d of var sheet not yet supported", col);
      break;
    }
}

static void
change_data_value (PsppireDataStore *store, gint col, gint row, GValue *value)
{
  const struct variable *var = psppire_dict_get_variable (store->dict, col);

  if (NULL == var)
    return;

  union value v;

  GVariant *vrnt = g_value_get_variant (value);

  value_variant_get (&v, vrnt);

  psppire_data_store_set_value (store, row, var, &v);

  value_destroy_from_variant (&v, vrnt);
}

static void
psppire_data_editor_set_property (GObject         *object,
				  guint            prop_id,
				  const GValue    *value,
				  GParamSpec      *pspec)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (object);

  switch (prop_id)
    {
    case PROP_SPLIT_WINDOW:
      psppire_data_editor_split_window (de, g_value_get_boolean (value));
      break;
    case PROP_DATA_STORE:
      if ( de->data_store)
        {
          g_signal_handlers_disconnect_by_func (de->data_store,
                                                G_CALLBACK (refresh_entry),
                                                de);
          g_object_unref (de->data_store);
        }

      de->data_store = g_value_get_pointer (value);
      g_object_ref (de->data_store);
      g_print ("NEW STORE\n");

      g_object_set (de->data_sheet, "data-model", de->data_store, NULL);
      psppire_data_editor_refresh_model (de);

      g_signal_connect_swapped (de->data_sheet, "value-changed",
				G_CALLBACK (change_data_value), de->data_store);

      g_signal_connect_swapped (de->data_store, "case-changed",
                                G_CALLBACK (refresh_entry), de);

      break;
    case PROP_DICTIONARY:
      if (de->dict)
        g_object_unref (de->dict);
      de->dict = g_value_get_pointer (value);
      g_object_ref (de->dict);

      g_object_set (de->data_sheet, "hmodel", de->dict, NULL);
      g_object_set (de->var_sheet, "data-model", de->dict, NULL);
      g_signal_connect_swapped (de->var_sheet, "value-changed",
				G_CALLBACK (change_var_property), de->dict);

      break;
    case PROP_VALUE_LABELS:
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
psppire_data_editor_get_property (GObject         *object,
				  guint            prop_id,
				  GValue          *value,
				  GParamSpec      *pspec)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (object);

  switch (prop_id)
    {
    case PROP_SPLIT_WINDOW:
      g_value_set_boolean (value, de->split);
      break;
    case PROP_DATA_STORE:
      g_value_set_pointer (value, de->data_store);
      break;
    case PROP_DICTIONARY:
      g_value_set_pointer (value, de->dict);
      break;
    case PROP_VALUE_LABELS:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_data_editor_switch_page (GtkNotebook     *notebook,
				 GtkWidget *w,
                                 guint            page_num)
{
  GTK_NOTEBOOK_CLASS (parent_class)->switch_page (notebook, w, page_num);

}

static void
psppire_data_editor_set_focus_child (GtkContainer *container,
                                     GtkWidget    *widget)
{
  GTK_CONTAINER_CLASS (parent_class)->set_focus_child (container, widget);

}

static void
psppire_data_editor_class_init (PsppireDataEditorClass *klass)
{
  GParamSpec *data_store_spec ;
  GParamSpec *dict_spec ;
  GParamSpec *value_labels_spec;
  GParamSpec *split_window_spec;

  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);
  GtkNotebookClass *notebook_class = GTK_NOTEBOOK_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->dispose = psppire_data_editor_dispose;
  object_class->set_property = psppire_data_editor_set_property;
  object_class->get_property = psppire_data_editor_get_property;

  container_class->set_focus_child = psppire_data_editor_set_focus_child;

  notebook_class->switch_page = psppire_data_editor_switch_page;

  data_store_spec =
    g_param_spec_pointer ("data-store",
			  "Data Store",
			  "A pointer to the data store associated with this editor",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_READABLE );

  g_object_class_install_property (object_class,
                                   PROP_DATA_STORE,
                                   data_store_spec);

  dict_spec =
    g_param_spec_pointer ("dictionary",
			  "Dictionary",
			  "A pointer to the dictionary associated with this editor",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_READABLE );

  g_object_class_install_property (object_class,
                                   PROP_DICTIONARY,
                                   dict_spec);

  value_labels_spec =
    g_param_spec_boolean ("value-labels",
			 "Value Labels",
			 "Whether or not the data sheet should display labels instead of values",
			  FALSE,
			 G_PARAM_WRITABLE | G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_VALUE_LABELS,
                                   value_labels_spec);


  split_window_spec =
    g_param_spec_boolean ("split",
			  "Split Window",
			  "True iff the data sheet is split",
			  FALSE,
			  G_PARAM_READABLE | G_PARAM_WRITABLE);

  g_object_class_install_property (object_class,
                                   PROP_SPLIT_WINDOW,
                                   split_window_spec);

}


static void
on_var_sheet_var_double_clicked (void *var_sheet, gint dict_index,
                                 PsppireDataEditor *de)
{
  gtk_notebook_set_current_page (GTK_NOTEBOOK (de),
                                 PSPPIRE_DATA_EDITOR_DATA_VIEW);

  jmd_sheet_scroll_to (JMD_SHEET (de->data_sheet), dict_index, -1);
}


static void
on_data_sheet_var_double_clicked (JmdSheet *data_sheet, gint dict_index,
                                 PsppireDataEditor *de)
{

  gtk_notebook_set_current_page (GTK_NOTEBOOK (de),
                                 PSPPIRE_DATA_EDITOR_VARIABLE_VIEW);

  jmd_sheet_scroll_to (JMD_SHEET (de->var_sheet), -1, dict_index);
}



/* Refreshes 'de->cell_ref_label' and 'de->datum_entry' from the currently
   active cell or cells. */
static void
refresh_entry (PsppireDataEditor *de)
{
  union value val;
  gint row, col;
  jmd_sheet_get_active_cell (JMD_SHEET (de->data_sheet), &col, &row);

  const struct variable *var = psppire_dict_get_variable (de->dict, col);
  psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (de->datum_entry), var);

  int width = var_get_width (var);
  if (! psppire_data_store_get_value (PSPPIRE_DATA_STORE (de->data_store),
				      row, var, &val))
    return;

  psppire_value_entry_set_value (PSPPIRE_VALUE_ENTRY (de->datum_entry),
				 &val, width);
  value_destroy (&val, width);
}

static void
on_datum_entry_activate (PsppireValueEntry *entry, PsppireDataEditor *de)
{
}


static void
disconnect_data_sheets (PsppireDataEditor *de)
{
}

/* Called when the active cell or the selection in the data sheet changes */
static void
on_data_selection_change (PsppireDataEditor *de, JmdRange *sel)
{
  gchar *ref_cell_text = NULL;

  gint n_cases = abs (sel->end_y - sel->start_y) + 1;
  gint n_vars = abs (sel->end_x - sel->start_x) + 1;

  if (n_cases == 1 && n_vars == 1)
    {
      /* A single cell is selected */
      const struct variable *var = psppire_dict_get_variable (de->dict, sel->start_x);

      if (var)
	ref_cell_text = g_strdup_printf (_("%d : %s"),
					 sel->start_y + 1, var_get_name (var));
    }
  else
    {
      struct string s;

      /* The glib string library does not understand the ' printf modifier
	 on all platforms, but the "struct string" library does (because
	 Gnulib fixes that problem), so use the latter.  */
      ds_init_empty (&s);
      ds_put_format (&s, ngettext ("%'d case", "%'d cases", n_cases),
		     n_cases);
      ds_put_byte (&s, ' ');
      ds_put_unichar (&s, 0xd7); /* U+00D7 MULTIPLICATION SIGN */
      ds_put_byte (&s, ' ');
      ds_put_format (&s, ngettext ("%'d variable", "%'d variables",
				   n_vars),
		     n_vars);
      ref_cell_text = ds_steal_cstr (&s);
    }

  gtk_label_set_label (GTK_LABEL (de->cell_ref_label),
		       ref_cell_text ? ref_cell_text : "");

  g_free (ref_cell_text);
}


static void set_font_recursively (GtkWidget *w, gpointer data);

gchar *myconvfunc (GtkTreeModel *m, gint col, gint row, const GValue *v);
void myreversefunc (GtkTreeModel *model, gint col, gint row, const gchar *in, GValue *out);


enum sort_order
  {
    SORT_ASCEND,
    SORT_DESCEND
  };

static void
do_sort (PsppireDataEditor *de, enum sort_order order)
{
  JmdRange *range = JMD_SHEET(de->data_sheet)->selection;

  int n_vars = 0;
  int i;

  PsppireDataWindow *pdw =
     psppire_data_window_for_data_store (de->data_store);

  GString *syntax = g_string_new ("SORT CASES BY");
  for (i = range->start_x ; i <= range->end_x; ++i)
    {
      const struct variable *var = psppire_dict_get_variable (de->dict, i);
      if (var != NULL)
        {
          g_string_append_printf (syntax, " %s", var_get_name (var));
          n_vars++;
        }
    }
  if (n_vars > 0)
    {
      if (order == SORT_DESCEND)
        g_string_append (syntax, " (DOWN)");
      g_string_append_c (syntax, '.');
      execute_const_syntax_string (pdw, syntax->str);
    }
  g_string_free (syntax, TRUE);
}


static void
sort_ascending (PsppireDataEditor *de)
{
  do_sort (de, SORT_ASCEND);

  gtk_widget_queue_draw (GTK_WIDGET (de));
}

static void
sort_descending (PsppireDataEditor *de)
{
  do_sort (de, SORT_DESCEND);

  gtk_widget_queue_draw (GTK_WIDGET (de));
}

static void
delete_cases (PsppireDataEditor *de)
{
  JmdRange *range = JMD_SHEET(de->data_sheet)->selection;

  psppire_data_store_delete_cases (de->data_store, range->start_y,
				   range->end_y - range->start_y + 1);

  gtk_widget_queue_draw (GTK_WIDGET (de));
}

static void
insert_new_case (PsppireDataEditor *de)
{
  gint item = GPOINTER_TO_INT (g_object_get_data
				(G_OBJECT (de->data_sheet_cases_row_popup), "item"));

  psppire_data_store_insert_new_case (de->data_store, item);

  gtk_widget_queue_draw (GTK_WIDGET (de));
}

static void
data_delete_variables (PsppireDataEditor *de)
{
  JmdRange *range = JMD_SHEET(de->data_sheet)->selection;

  psppire_dict_delete_variables (de->dict, range->start_x,
				 (range->end_x - range->start_x + 1));

  gtk_widget_queue_draw (GTK_WIDGET (de->data_sheet));
}

static void
var_delete_variables (PsppireDataEditor *de)
{
  JmdRange *range = JMD_SHEET(de->var_sheet)->selection;

  psppire_dict_delete_variables (de->dict, range->start_y,
				 (range->end_y - range->start_y + 1));

  gtk_widget_queue_draw (GTK_WIDGET (de->var_sheet));
}


static void
insert_new_variable_data (PsppireDataEditor *de)
{
  gint item = GPOINTER_TO_INT (g_object_get_data
				(G_OBJECT (de->data_sheet_cases_column_popup),
				 "item"));

  const struct variable *v = psppire_dict_insert_variable (de->dict, item, NULL);
  psppire_data_store_insert_value (de->data_store, var_get_width(v),
				   var_get_case_index (v));

  gtk_widget_queue_draw (GTK_WIDGET (de));
}

static void
insert_new_variable_var (PsppireDataEditor *de)
{
  gint item = GPOINTER_TO_INT (g_object_get_data
				(G_OBJECT (de->var_sheet_row_popup),
				 "item"));

  const struct variable *v = psppire_dict_insert_variable (de->dict, item, NULL);
  psppire_data_store_insert_value (de->data_store, var_get_width(v),
				   var_get_case_index (v));

  gtk_widget_queue_draw (GTK_WIDGET (de));
}


static GtkWidget *
create_var_row_header_popup_menu (PsppireDataEditor *de)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *item =
    gtk_menu_item_new_with_mnemonic  (_("_Insert Variable"));
  g_signal_connect_swapped (item, "activate", G_CALLBACK (insert_new_variable_var),
			    de);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  de->var_clear_variables_menu_item =
    gtk_menu_item_new_with_mnemonic (_("Cl_ear Variables"));
  g_signal_connect_swapped (de->var_clear_variables_menu_item, "activate",
			    G_CALLBACK (var_delete_variables), de);
  gtk_widget_set_sensitive (de->var_clear_variables_menu_item, FALSE);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), de->var_clear_variables_menu_item);

  gtk_widget_show_all (menu);
  return menu;
}

static GtkWidget *
create_data_row_header_popup_menu (PsppireDataEditor *de)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *item =
    gtk_menu_item_new_with_mnemonic  (_("_Insert Case"));

  g_signal_connect_swapped (item, "activate", G_CALLBACK (insert_new_case), de);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  de->data_clear_cases_menu_item = gtk_menu_item_new_with_mnemonic (_("Cl_ear Cases"));
  gtk_widget_set_sensitive (de->data_clear_cases_menu_item, FALSE);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), de->data_clear_cases_menu_item);
  g_signal_connect_swapped (de->data_clear_cases_menu_item, "activate",
			    G_CALLBACK (delete_cases), de);

  gtk_widget_show_all (menu);
  return menu;
}

static GtkWidget *
create_data_column_header_popup_menu (PsppireDataEditor *de)
{
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *item =
    gtk_menu_item_new_with_mnemonic  (_("_Insert Variable"));
  g_signal_connect_swapped (item, "activate", G_CALLBACK (insert_new_variable_data),
			    de);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  de->data_clear_variables_menu_item =
    gtk_menu_item_new_with_mnemonic  (_("Cl_ear Variables"));
  g_signal_connect_swapped (de->data_clear_variables_menu_item, "activate",
			    G_CALLBACK (data_delete_variables), de);
  gtk_widget_set_sensitive (de->data_clear_variables_menu_item, FALSE);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), de->data_clear_variables_menu_item);

  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

  de->data_sort_ascending_menu_item =
    gtk_menu_item_new_with_mnemonic (_("Sort _Ascending"));
  g_signal_connect_swapped (de->data_sort_ascending_menu_item, "activate",
			    G_CALLBACK (sort_ascending), de);
  gtk_widget_set_sensitive (de->data_sort_ascending_menu_item, FALSE);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), de->data_sort_ascending_menu_item);

  de->data_sort_descending_menu_item =
    gtk_menu_item_new_with_mnemonic (_("Sort _Descending"));
  g_signal_connect_swapped (de->data_sort_descending_menu_item, "activate",
			    G_CALLBACK (sort_descending), de);
  gtk_widget_set_sensitive (de->data_sort_descending_menu_item, FALSE);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), de->data_sort_descending_menu_item);

  gtk_widget_show_all (menu);
  return menu;
}

static void
set_var_popup_sensitivity (JmdSheet *sheet, gpointer selection, gpointer p)
{

  JmdRange *range = selection;
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (p);
  gint width = gtk_tree_model_get_n_columns (sheet->data_model);

  gboolean whole_row_selected = (range->start_x == 0 &&
				 range->end_x == width - 1 - 1);
  /*  PsppireDict has an "extra" column: TVM_COL_VAR   ^^^ */
  gtk_widget_set_sensitive (de->var_clear_variables_menu_item, whole_row_selected);
}

static void
set_menu_items_sensitivity (JmdSheet *sheet, gpointer selection, gpointer p)
{
  JmdRange *range = selection;
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (p);
  gint width = gtk_tree_model_get_n_columns (sheet->data_model);
  gint length = psppire_data_store_get_case_count (de->data_store);


  gboolean whole_row_selected = (range->start_x == 0 && range->end_x == width - 1);
  gtk_widget_set_sensitive (de->data_clear_cases_menu_item, whole_row_selected);


  gboolean whole_column_selected =
    (range->start_y == 0 && range->end_y == length - 1);
  gtk_widget_set_sensitive (de->data_clear_variables_menu_item,
			    whole_column_selected);
  gtk_widget_set_sensitive (de->data_sort_ascending_menu_item,
			    whole_column_selected);
  gtk_widget_set_sensitive (de->data_sort_descending_menu_item,
			    whole_column_selected);
}

static void
show_variables_row_popup (JmdSheet *sheet, int row, uint button,
			  uint state, gpointer p)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (p);
  GListModel *vmodel = NULL;
  g_object_get (sheet, "vmodel", &vmodel, NULL);
  if (vmodel == NULL)
    return;

  guint n_items = g_list_model_get_n_items (vmodel);

  if (row >= n_items)
    return;

  if (button != 3)
    return;

  g_object_set_data (G_OBJECT (de->var_sheet_row_popup), "item",
		     GINT_TO_POINTER (row));

  gtk_menu_popup_at_pointer (GTK_MENU (de->var_sheet_row_popup), NULL);
}

static void
show_cases_row_popup (JmdSheet *sheet, int row, uint button, uint state, gpointer p)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (p);
  GListModel *vmodel = NULL;
  g_object_get (sheet, "vmodel", &vmodel, NULL);
  if (vmodel == NULL)
    return;

  guint n_items = g_list_model_get_n_items (vmodel);

  if (row >= n_items)
    return;

  if (button != 3)
    return;

  g_object_set_data (G_OBJECT (de->data_sheet_cases_row_popup), "item",
		     GINT_TO_POINTER (row));

  gtk_menu_popup_at_pointer (GTK_MENU (de->data_sheet_cases_row_popup), NULL);
}

static void
show_cases_column_popup (JmdSheet *sheet, int column, uint button, uint state,
			 gpointer p)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (p);
  GListModel *hmodel = NULL;
  g_object_get (sheet, "hmodel", &hmodel, NULL);
  if (hmodel == NULL)
    return;

  guint n_items = g_list_model_get_n_items (hmodel);

  if (column >= n_items)
    return;

  if (button != 3)
    return;

  g_object_set_data (G_OBJECT (de->data_sheet_cases_column_popup), "item",
		     GINT_TO_POINTER (column));

  gtk_menu_popup_at_pointer (GTK_MENU (de->data_sheet_cases_column_popup), NULL);
}


static void
psppire_data_editor_init (PsppireDataEditor *de)
{
  GtkWidget *hbox;
  gchar *fontname = NULL;

  GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET (de));
  gtk_style_context_add_class (context, "psppire-data-editor");

  de->font = NULL;
  de->old_vbox_widget = NULL;

  g_object_set (de, "tab-pos", GTK_POS_BOTTOM, NULL);

  de->cell_ref_label = gtk_label_new ("");
  gtk_label_set_width_chars (GTK_LABEL (de->cell_ref_label), 25);
  gtk_widget_set_valign (de->cell_ref_label, GTK_ALIGN_CENTER);

  de->datum_entry = psppire_value_entry_new ();
  g_signal_connect (GTK_ENTRY (gtk_bin_get_child (GTK_BIN (de->datum_entry))),
                    "activate", G_CALLBACK (on_datum_entry_activate), de);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (hbox), de->cell_ref_label, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), de->datum_entry, TRUE, TRUE, 0);

  de->split = FALSE;
  de->data_sheet = jmd_sheet_new ();

  de->data_sheet_cases_column_popup = create_data_column_header_popup_menu (de);
  de->data_sheet_cases_row_popup = create_data_row_header_popup_menu (de);
  de->var_sheet_row_popup = create_var_row_header_popup_menu (de);

  g_signal_connect (de->data_sheet, "row-header-pressed",
		    G_CALLBACK (show_cases_row_popup), de);

  g_signal_connect (de->data_sheet, "column-header-pressed",
		    G_CALLBACK (show_cases_column_popup), de);

  g_signal_connect (de->data_sheet, "selection-changed",
		    G_CALLBACK (set_menu_items_sensitivity), de);

  jmd_sheet_body_set_conversion_func
    (JMD_SHEET_BODY (JMD_SHEET(de->data_sheet)->selected_body),
     myconvfunc, myreversefunc);

  GtkWidget *data_button = jmd_sheet_get_button (JMD_SHEET (de->data_sheet));
  gtk_button_set_label (GTK_BUTTON (data_button), _("Case"));
  de->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start (GTK_BOX (de->vbox), hbox, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (de->vbox), de->data_sheet, TRUE, TRUE, 0);


  g_signal_connect_swapped (de->data_sheet, "selection-changed",
		    G_CALLBACK (on_data_selection_change), de);

  gtk_notebook_append_page (GTK_NOTEBOOK (de), de->vbox,
			    gtk_label_new_with_mnemonic (_("Data View")));

  gtk_widget_show_all (de->vbox);

  de->var_sheet = g_object_new (JMD_TYPE_SHEET, NULL);

  PsppireVarSheetHeader *vsh = g_object_new (PSPPIRE_TYPE_VAR_SHEET_HEADER, NULL);

  g_object_set (de->var_sheet,
		"hmodel", vsh,
		"select-renderer-func", select_renderer_func,
		NULL);

  g_signal_connect (de->var_sheet, "row-header-pressed",
		    G_CALLBACK (show_variables_row_popup), de);

  g_signal_connect (de->var_sheet, "selection-changed",
		    G_CALLBACK (set_var_popup_sensitivity), de);


  GtkWidget *var_button = jmd_sheet_get_button (JMD_SHEET (de->var_sheet));
  gtk_button_set_label (GTK_BUTTON (var_button), _("Variable"));

  gtk_notebook_append_page (GTK_NOTEBOOK (de), de->var_sheet,
			    gtk_label_new_with_mnemonic (_("Variable View")));

  gtk_widget_show_all (de->var_sheet);

  g_signal_connect (de->var_sheet, "row-header-double-clicked",
                    G_CALLBACK (on_var_sheet_var_double_clicked), de);

  g_signal_connect (de->data_sheet, "column-header-double-clicked",
                    G_CALLBACK (on_data_sheet_var_double_clicked), de);

  g_object_set (de, "can-focus", FALSE, NULL);

  if (psppire_conf_get_string (psppire_conf_new (),
			   "Data Editor", "font",
				&fontname) )
    {
      de->font = pango_font_description_from_string (fontname);
      g_free (fontname);
      set_font_recursively (GTK_WIDGET (de), de->font);
    }

}

GtkWidget*
psppire_data_editor_new (PsppireDict *dict,
			 PsppireDataStore *data_store)
{
  return  g_object_new (PSPPIRE_DATA_EDITOR_TYPE,
                        "dictionary",  dict,
                        "data-store",  data_store,
                        NULL);
}

/* Turns the visible grid on or off, according to GRID_VISIBLE, for DE's data
   sheet(s) and variable sheet. */
void
psppire_data_editor_show_grid (PsppireDataEditor *de, gboolean grid_visible)
{
  g_object_set (JMD_SHEET (de->var_sheet), "gridlines", grid_visible, NULL);
  g_object_set (JMD_SHEET (de->data_sheet), "gridlines", grid_visible, NULL);
}


static void
set_font_recursively (GtkWidget *w, gpointer data)
{
  PangoFontDescription *font_desc = data;

  GtkStyleContext *style = gtk_widget_get_style_context (w);
  GtkCssProvider *cssp = gtk_css_provider_new ();

  gchar *str = pango_font_description_to_string (font_desc);
  gchar *css =
    g_strdup_printf ("* {font: %s}", str);
  g_free (str);

  GError *err = NULL;
  gtk_css_provider_load_from_data (cssp, css, -1, &err);
  if (err)
    {
      g_warning ("Failed to load font css \"%s\": %s", css, err->message);
      g_error_free (err);
    }
  g_free (css);

  gtk_style_context_add_provider (style,
				  GTK_STYLE_PROVIDER (cssp),
				  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (cssp);


  if ( GTK_IS_CONTAINER (w))
    gtk_container_foreach (GTK_CONTAINER (w), set_font_recursively, font_desc);
}

/* Sets FONT_DESC as the font used by the data sheet(s) and variable sheet. */
void
psppire_data_editor_set_font (PsppireDataEditor *de, PangoFontDescription *font_desc)
{
  gchar *font_name;
  set_font_recursively (GTK_WIDGET (de), font_desc);

  if (de->font)
    pango_font_description_free (de->font);
  de->font = pango_font_description_copy (font_desc);
  font_name = pango_font_description_to_string (de->font);

  psppire_conf_set_string (psppire_conf_new (),
			   "Data Editor", "font",
			   font_name);
  g_free (font_name);
}

/* If SPLIT is TRUE, splits DE's data sheet into four panes.
   If SPLIT is FALSE, un-splits it into a single pane. */
void
psppire_data_editor_split_window (PsppireDataEditor *de, gboolean split)
{
  if (split == de->split)
    return;

  disconnect_data_sheets (de);

  psppire_data_editor_refresh_model (de);

  gtk_widget_show_all (de->vbox);

  if (de->font)
    set_font_recursively (GTK_WIDGET (de), de->font);

  de->split = split;
  g_object_notify (G_OBJECT (de), "split");
}

/* Makes the variable with dictionary index DICT_INDEX in DE's dictionary
   visible and selected in the active view in DE. */
void
psppire_data_editor_goto_variable (PsppireDataEditor *de, gint dict_index)
{
}
