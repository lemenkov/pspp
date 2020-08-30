/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2012, 2016,
   2017, 2019 Free Software Foundation, Inc.

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

#include "data/datasheet.h"
#include "data/value-labels.h"
#include "libpspp/range-set.h"
#include "libpspp/str.h"

#include "ui/gui/helper.h"
#include "ui/gui/var-display.h"
#include "ui/gui/val-labs-dialog.h"
#include "ui/gui/missing-val-dialog.h"
#include "ui/gui/var-type-dialog.h"
#include "ui/gui/value-variant.h"
#include "ui/gui/psppire-dict.h"
#include "ui/gui/psppire-data-store.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-value-entry.h"
#include "ui/gui/psppire-conf.h"
#include "ui/gui/psppire-variable-sheet.h"
#include "ui/gui/psppire-data-sheet.h"


#include <ssw-sheet.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)

static void refresh_entry (PsppireDataEditor *);

G_DEFINE_TYPE (PsppireDataEditor, psppire_data_editor, GTK_TYPE_NOTEBOOK)

static GObjectClass * parent_class = NULL;

static void
psppire_data_editor_finalize (GObject *obj)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (obj);
  if (de->font)
    pango_font_description_free (de->font);

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
psppire_data_editor_dispose (GObject *obj)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (obj);

  if (de->dispose_has_run)
    return;

  de->dispose_has_run = TRUE;

  g_object_unref (de->data_store);
  g_object_unref (de->dict);

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
psppire_data_editor_set_property (GObject         *object,
				  guint            prop_id,
				  const GValue    *value,
				  GParamSpec      *pspec)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (object);

  switch (prop_id)
    {
    case PROP_SPLIT_WINDOW:
      de->split = g_value_get_boolean (value);
      g_object_set (de->data_sheet, "split", de->split, NULL);
      g_object_set (de->var_sheet, "split", de->split, NULL);
      break;
    case PROP_DATA_STORE:
      if (de->data_store)
        {
          g_signal_handlers_disconnect_by_func (de->data_store,
                                                G_CALLBACK (refresh_entry),
                                                de);
          g_object_unref (de->data_store);
        }

      de->data_store = g_value_get_pointer (value);
      g_object_ref (de->data_store);

      g_object_set (de->data_sheet, "data-model", de->data_store, NULL);
      psppire_data_editor_refresh_model (de);

      g_signal_connect_swapped (de->data_sheet, "selection-changed",
				G_CALLBACK (refresh_entry),
				de);

      g_signal_connect_swapped (de->data_store, "case-changed",
                                G_CALLBACK (refresh_entry), de);

      break;
    case PROP_DICTIONARY:
      if (de->dict)
        g_object_unref (de->dict);
      de->dict = g_value_get_pointer (value);
      g_object_ref (de->dict);

      g_object_set (de->var_sheet, "data-model", de->dict, NULL);
      break;

    case PROP_VALUE_LABELS:
      {
	gboolean l = g_value_get_boolean (value);
	de->use_value_labels = l;
	g_object_set (de->data_sheet, "forward-conversion",
		      l ?
		      psppire_data_store_value_to_string_with_labels :
		      psppire_data_store_value_to_string,
		      NULL);
      }
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
      g_value_set_boolean (value, de->use_value_labels);
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


static gboolean
on_key_press (GtkWidget *w, GdkEventKey *e)
{
  PsppireDataEditor *de = PSPPIRE_DATA_EDITOR (w);
  if (e->keyval == GDK_KEY_F2 &&
      PSPPIRE_DATA_EDITOR_DATA_VIEW == gtk_notebook_get_current_page (GTK_NOTEBOOK (de)))
    {
      gtk_widget_grab_focus (de->datum_entry);
    }

  return FALSE;
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
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->dispose = psppire_data_editor_dispose;
  object_class->finalize = psppire_data_editor_finalize;
  object_class->set_property = psppire_data_editor_set_property;
  object_class->get_property = psppire_data_editor_get_property;

  container_class->set_focus_child = psppire_data_editor_set_focus_child;
  notebook_class->switch_page = psppire_data_editor_switch_page;
  widget_class->key_press_event = on_key_press;

  data_store_spec =
    g_param_spec_pointer ("data-store",
			  "Data Store",
			  "A pointer to the data store associated with this editor",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_READABLE);

  g_object_class_install_property (object_class,
                                   PROP_DATA_STORE,
                                   data_store_spec);

  dict_spec =
    g_param_spec_pointer ("dictionary",
			  "Dictionary",
			  "A pointer to the dictionary associated with this editor",
			  G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE | G_PARAM_READABLE);

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

  ssw_sheet_scroll_to (SSW_SHEET (de->data_sheet), dict_index, -1);
}


static void
on_data_sheet_var_double_clicked (SswSheet *data_sheet, gint dict_index,
                                 PsppireDataEditor *de)
{

  gtk_notebook_set_current_page (GTK_NOTEBOOK (de),
                                 PSPPIRE_DATA_EDITOR_VARIABLE_VIEW);

  ssw_sheet_scroll_to (SSW_SHEET (de->var_sheet), -1, dict_index);
}



/* Refreshes 'de->cell_ref_label' and 'de->datum_entry' from the currently
   active cell or cells. */
static void
refresh_entry (PsppireDataEditor *de)
{
  gint row, col;
  if (ssw_sheet_get_active_cell (SSW_SHEET (de->data_sheet), &col, &row))
    {
      union value val;
      const struct variable *var = psppire_dict_get_variable (de->dict, col);
      if (var == NULL)
	return;

      psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (de->datum_entry), var);

      int width = var_get_width (var);
      if (! psppire_data_store_get_value (PSPPIRE_DATA_STORE (de->data_store),
					  row, var, &val))
	return;

      psppire_value_entry_set_value (PSPPIRE_VALUE_ENTRY (de->datum_entry),
				     &val, width);
      value_destroy (&val, width);
    }
}

static void
on_datum_entry_activate (GtkEntry *entry, PsppireDataEditor *de)
{
  gint row, col;
  if (ssw_sheet_get_active_cell (SSW_SHEET (de->data_sheet), &col, &row))
    {
      union value val;
      const struct variable *var = psppire_dict_get_variable (de->dict, col);
      if (var == NULL)
	return;

      int width = var_get_width (var);
      value_init (&val, width);
      if (psppire_value_entry_get_value (PSPPIRE_VALUE_ENTRY (de->datum_entry),
					 &val, width))
	{
	  psppire_data_store_set_value (de->data_store, row, var, &val);
	}
      value_destroy (&val, width);

      gtk_widget_grab_focus (de->data_sheet);
      ssw_sheet_set_active_cell (SSW_SHEET (de->data_sheet), col, row, NULL);
    }
}


/* Called when the active cell or the selection in the data sheet changes */
static void
on_data_selection_change (PsppireDataEditor *de, SswRange *sel)
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



void
psppire_data_editor_data_delete_variables (PsppireDataEditor *de)
{
  psppire_data_sheet_delete_variables (PSPPIRE_DATA_SHEET (de->data_sheet));
}

void
psppire_data_editor_var_delete_variables (PsppireDataEditor *de)
{
  SswRange *range = SSW_SHEET(de->var_sheet)->selection;

  if (range->start_x > range->end_x)
    {
      gint temp = range->start_x;
      range->start_x = range->end_x;
      range->end_x = temp;
    }

  psppire_dict_delete_variables (de->dict, range->start_y,
				 (range->end_y - range->start_y + 1));

  gtk_widget_queue_draw (GTK_WIDGET (de->var_sheet));
}

void
psppire_data_editor_insert_new_case_at_posn  (PsppireDataEditor *de, gint posn)
{
  g_return_if_fail (posn >= 0);

  psppire_data_store_insert_new_case (de->data_store, posn);

  gtk_widget_queue_draw (GTK_WIDGET (de->data_sheet));
}

void
psppire_data_editor_insert_new_variable_at_posn (PsppireDataEditor *de, gint posn)
{
  psppire_data_sheet_insert_new_variable_at_posn (PSPPIRE_DATA_SHEET (de->data_sheet), posn);
}

static void
psppire_data_editor_init (PsppireDataEditor *de)
{
  GtkWidget *hbox;
  gchar *fontname = NULL;

  GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET (de));
  gtk_style_context_add_class (context, "psppire-data-editor");

  de->dispose_has_run = FALSE;

  de->font = NULL;

  g_object_set (de, "tab-pos", GTK_POS_BOTTOM, NULL);

  de->cell_ref_label = gtk_label_new ("");
  gtk_label_set_width_chars (GTK_LABEL (de->cell_ref_label), 25);
  gtk_widget_set_valign (de->cell_ref_label, GTK_ALIGN_CENTER);

  de->datum_entry = psppire_value_entry_new ();
  g_signal_connect (de->datum_entry, "edit-done",
		    G_CALLBACK (on_datum_entry_activate), de);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start (GTK_BOX (hbox), de->cell_ref_label, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), de->datum_entry, TRUE, TRUE, 0);

  de->split = FALSE;
  de->use_value_labels = FALSE;
  de->data_sheet = psppire_data_sheet_new ();

  GtkWidget *data_button = ssw_sheet_get_button (SSW_SHEET (de->data_sheet));
  gtk_button_set_label (GTK_BUTTON (data_button), _("Case"));
  de->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start (GTK_BOX (de->vbox), hbox, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (de->vbox), de->data_sheet, TRUE, TRUE, 0);


  g_signal_connect_swapped (de->data_sheet, "selection-changed",
		    G_CALLBACK (on_data_selection_change), de);

  gtk_notebook_append_page (GTK_NOTEBOOK (de), de->vbox,
			    gtk_label_new_with_mnemonic (_("Data View")));

  gtk_widget_show_all (de->vbox);

  de->var_sheet = psppire_variable_sheet_new ();

  GtkWidget *var_button = ssw_sheet_get_button (SSW_SHEET (de->var_sheet));
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
				&fontname))
    {
      de->font = pango_font_description_from_string (fontname);
      g_free (fontname);
      set_font_recursively (GTK_WIDGET (de), de->font);
    }

  gtk_widget_add_events (GTK_WIDGET (de), GDK_KEY_PRESS_MASK);
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
  g_object_set (SSW_SHEET (de->var_sheet), "gridlines", grid_visible, NULL);
  g_object_set (SSW_SHEET (de->data_sheet), "gridlines", grid_visible, NULL);
}


static void
set_font_recursively (GtkWidget *w, gpointer data)
{
  PangoFontDescription *font_desc = data;

  GtkStyleContext *style = gtk_widget_get_style_context (w);
  GtkCssProvider *cssp = gtk_css_provider_new ();

  /* The Pango font description as string has a different syntax than the
     css style description:
     Pango: Courier Italic 12
     CSS: italic 12pt Courier
     I ignore Weight, Style and Variant and just take family and size */
  const gchar *str = pango_font_description_get_family (font_desc);
  gint size = pango_font_description_get_size (font_desc);
  gchar *css =
    g_strdup_printf ("* {font: %dpt %s}", size/PANGO_SCALE, str);

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


  if (GTK_IS_CONTAINER (w))
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
  g_object_set (de, "split", split, NULL);
}

/* Makes the variable with dictionary index DICT_INDEX in DE's dictionary
   visible and selected in the active view in DE. */
void
psppire_data_editor_goto_variable (PsppireDataEditor *de, gint dict_index)
{
  gint page = gtk_notebook_get_current_page (GTK_NOTEBOOK (de));

  switch (page)
    {
      case PSPPIRE_DATA_EDITOR_DATA_VIEW:
	ssw_sheet_scroll_to (SSW_SHEET (de->data_sheet), dict_index, -1);
	ssw_sheet_set_active_cell (SSW_SHEET (de->data_sheet), dict_index, -1, NULL);
	break;
      case PSPPIRE_DATA_EDITOR_VARIABLE_VIEW:
	ssw_sheet_scroll_to (SSW_SHEET (de->var_sheet), -1, dict_index);
	ssw_sheet_set_active_cell (SSW_SHEET (de->var_sheet), -1, dict_index, NULL);
	break;
    }
}

/* Set the datum at COL, ROW, to that contained in VALUE.
 */
static void
store_set_datum (GtkTreeModel *model, gint col, gint row,
			 const GValue *value)
{
  PsppireDataStore *store = PSPPIRE_DATA_STORE (model);
  GVariant *v = g_value_get_variant (value);
  union value uv;
  value_variant_get (&uv, v);
  const struct variable *var = psppire_dict_get_variable (store->dict, col);
  psppire_data_store_set_value (store, row, var, &uv);
  value_destroy_from_variant (&uv, v);
}

void
psppire_data_editor_paste (PsppireDataEditor *de)
{
  SswSheet *sheet = SSW_SHEET (de->data_sheet);
  GtkClipboard *clip =
    gtk_clipboard_get_for_display (gtk_widget_get_display (GTK_WIDGET (sheet)),
				   GDK_SELECTION_CLIPBOARD);

  ssw_sheet_paste (sheet, clip, store_set_datum);
}
