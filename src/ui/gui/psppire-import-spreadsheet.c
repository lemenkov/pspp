/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2015, 2016, 2017, 2018, 2020  Free Software Foundation

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


#include "psppire-import-assistant.h"
#include "psppire-import-spreadsheet.h"
#include "builder-wrapper.h"

#include "libpspp/misc.h"
#include "psppire-spreadsheet-model.h"
#include "psppire-spreadsheet-data-model.h"
#include "psppire-data-store.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void
set_column_header_label (GtkWidget *button, uint i, gpointer user_data)
{
  gchar *x = int_to_ps26 (i);
  gtk_button_set_label (GTK_BUTTON (button), x);
  g_free (x);
}

static void do_selection_update (PsppireImportAssistant *ia);

static void
on_sheet_combo_changed (GtkComboBox *cb, PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->spread_builder;
  gint sheet_number = gtk_combo_box_get_active (cb);

  gint coli = spreadsheet_get_sheet_n_columns (ia->spreadsheet, sheet_number) - 1;
  gint rowi = spreadsheet_get_sheet_n_rows (ia->spreadsheet, sheet_number) - 1;

  {
    /* Now set the spin button upper limits according to the size of the selected sheet.  */

    GtkWidget *sb0 = get_widget_assert (builder, "sb0");
    GtkWidget *sb1 = get_widget_assert (builder, "sb1");
    GtkWidget *sb2 = get_widget_assert (builder, "sb2");
    GtkWidget *sb3 = get_widget_assert (builder, "sb3");

    /* The row spinbuttons contain decimal digits.  So there should be
       enough space to display them.  */
    int digits = (rowi > 0) ? intlog10 (rowi + 1): 1;
    gtk_entry_set_max_width_chars (GTK_ENTRY (sb1), digits);
    gtk_entry_set_max_width_chars (GTK_ENTRY (sb3), digits);

    /* The column spinbuttons are pseudo-base-26 digits.  The
       exact formula for the number required is complicated.  However
       3 is a reasonable amount.  It's not too large, and anyone importing
       a spreadsheet with more than 3^26 columns is likely to experience
       other problems anyway.  */
    gtk_entry_set_max_width_chars (GTK_ENTRY (sb0), 3);
    gtk_entry_set_max_width_chars (GTK_ENTRY (sb2), 3);


    GtkAdjustment *adj = gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (sb0));
    gtk_adjustment_set_upper (adj, coli);

    adj = gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (sb1));
    gtk_adjustment_set_upper (adj, rowi);

    adj = gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (sb2));
    gtk_adjustment_set_upper (adj, coli);

    adj = gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (sb3));
    gtk_adjustment_set_upper (adj, rowi);
  }

  GtkTreeModel *data_model =
    psppire_spreadsheet_data_model_new (ia->spreadsheet, sheet_number);
  g_object_set (ia->preview_sheet,
                "data-model", data_model,
                "editable", FALSE,
                NULL);
  g_object_unref (data_model);

  GObject *hmodel = NULL;
  g_object_get (ia->preview_sheet, "hmodel", &hmodel, NULL);
  
  g_object_set (hmodel,
                "post-button-create-func", set_column_header_label,
                NULL);

  ia->selection.start_x = ia->selection.start_y = 0;
  ia->selection.end_x = coli;
  ia->selection.end_y = rowi;
  do_selection_update (ia);
}

/* Ensure that PARTNER is never less than than SUBJECT.  */
static void
on_value_change_lower (GtkSpinButton *subject, GtkSpinButton *partner)
{
  gint p = gtk_spin_button_get_value_as_int (partner);
  gint s = gtk_spin_button_get_value_as_int (subject);

  if (s > p)
    gtk_spin_button_set_value (partner, s);
}

/* Ensure that PARTNER is never greater than to SUBJECT.  */
static void
on_value_change_upper (GtkSpinButton *subject, GtkSpinButton *partner)
{
  gint p = gtk_spin_button_get_value_as_int (partner);
  gint s = gtk_spin_button_get_value_as_int (subject);

  if (s < p)
    gtk_spin_button_set_value (partner, s);
}


/* Sets SB to use 1 based display instead of 0 based.  */
static gboolean
row_output (GtkSpinButton *sb, gpointer unused)
{
  gint value = gtk_spin_button_get_value_as_int (sb);
  char *text = g_strdup_printf ("%d", value + 1);
  gtk_entry_set_text (GTK_ENTRY (sb), text);
  free (text);

  return TRUE;
}

/* Sets SB to use text like A, B, C instead of 0, 1, 2 etc.  */
static gboolean
column_output (GtkSpinButton *sb, gpointer unused)
{
  gint value = gtk_spin_button_get_value_as_int (sb);
  char *text = int_to_ps26 (value);
  if (text == NULL)
    return FALSE;

  gtk_entry_set_text (GTK_ENTRY (sb), text);
  free (text);

  return TRUE;
}

/* Interprets the SBs text as 1 based instead of zero based.  */
static gint
row_input (GtkSpinButton *sb, gpointer new_value, gpointer unused)
{
  const char *text = gtk_entry_get_text (GTK_ENTRY (sb));
  gdouble value = g_strtod (text, NULL) - 1;

  if (value < 0)
    return FALSE;

  memcpy (new_value, &value, sizeof (value));

  return TRUE;
}


/* Interprets the SBs text of the form A, B, C etc and
   sets NEW_VALUE as a double.  */
static gint
column_input (GtkSpinButton *sb, gpointer new_value, gpointer unused)
{
  const char *text = gtk_entry_get_text (GTK_ENTRY (sb));
  double value = ps26_to_int (text);

  if (value < 0)
    return FALSE;

  memcpy (new_value, &value, sizeof (value));

  return TRUE;
}

static void
reset_page (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->spread_builder;
  GtkWidget *readnames_checkbox = get_widget_assert (builder, "readnames-checkbox");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (readnames_checkbox), FALSE);

  gint sheet_number = 0;
  GtkWidget *sheet_entry = get_widget_assert (builder, "sheet-entry");
  gtk_combo_box_set_active (GTK_COMBO_BOX (sheet_entry), sheet_number);

  gint coli = spreadsheet_get_sheet_n_columns (ia->spreadsheet, sheet_number) - 1;
  gint rowi = spreadsheet_get_sheet_n_rows (ia->spreadsheet, sheet_number) - 1;

  ia->selection.start_x = ia->selection.start_y = 0;
  ia->selection.end_x = coli;
  ia->selection.end_y = rowi;
  do_selection_update (ia);
}

/* Prepares IA's sheet_spec page. */
static void
prepare_sheet_spec_page (PsppireImportAssistant *ia, GtkWidget *page, enum IMPORT_ASSISTANT_DIRECTION dir)
{
  if (dir != IMPORT_ASSISTANT_FORWARDS)
    return;

  GtkBuilder *builder = ia->spread_builder;
  GtkWidget *sheet_entry = get_widget_assert (builder, "sheet-entry");
  GtkWidget *readnames_checkbox = get_widget_assert (builder, "readnames-checkbox");

  GtkTreeModel *model = psppire_spreadsheet_model_new (ia->spreadsheet);
  gtk_combo_box_set_model (GTK_COMBO_BOX (sheet_entry), model);
  g_object_unref (model);

  gint items = gtk_tree_model_iter_n_children (model, NULL);
  gtk_widget_set_sensitive (sheet_entry, items > 1);

  gtk_combo_box_set_active (GTK_COMBO_BOX (sheet_entry), 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (readnames_checkbox), FALSE);

  GtkWidget *file_name_label = get_widget_assert (builder, "file-name-label");
  gtk_label_set_text (GTK_LABEL (file_name_label), ia->file_name);

  /* Gang the increment/decrement buttons, so that the upper always exceeds the lower.  */
  GtkWidget *sb0 = get_widget_assert (builder, "sb0");
  GtkWidget *sb2 = get_widget_assert (builder, "sb2");

  g_signal_connect (sb0, "value-changed", G_CALLBACK (on_value_change_lower), sb2);
  g_signal_connect (sb2, "value-changed", G_CALLBACK (on_value_change_upper), sb0);

  GtkWidget *sb1 = get_widget_assert (builder, "sb1");
  GtkWidget *sb3 = get_widget_assert (builder, "sb3");

  g_signal_connect (sb1, "value-changed", G_CALLBACK (on_value_change_lower), sb3);
  g_signal_connect (sb3, "value-changed", G_CALLBACK (on_value_change_upper), sb1);


  /* Set the column spinbuttons to display as A, B, C notation,
     and the row spinbuttons to display as 1 based instead of zero based. */
  g_signal_connect (sb0, "output", G_CALLBACK (column_output), NULL);
  g_signal_connect (sb0, "input", G_CALLBACK (column_input), NULL);

  g_signal_connect (sb2, "output", G_CALLBACK (column_output), NULL);
  g_signal_connect (sb2, "input", G_CALLBACK (column_input), NULL);

  g_signal_connect (sb1, "output", G_CALLBACK (row_output), NULL);
  g_signal_connect (sb1, "input", G_CALLBACK (row_input), NULL);

  g_signal_connect (sb3, "output", G_CALLBACK (row_output), NULL);
  g_signal_connect (sb3, "input", G_CALLBACK (row_input), NULL);
}

static void
do_selection_update (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->spread_builder;

  /* Stop this function re-entering itself.  */
  if (ia->updating_selection)
    return;
  ia->updating_selection = TRUE;

  /* We must take a copy of the selection.  A pointer will not suffice,
     because the selection can change under us.  */
  SswRange sel = ia->selection;

  g_object_set (ia->preview_sheet, "selection", &sel, NULL);

  char *range = create_cell_range (sel.start_x, sel.start_y, sel.end_x, sel.end_y);

  GtkWidget *range_entry = get_widget_assert (builder, "cell-range-entry");
  if (range)
    gtk_entry_set_text (GTK_ENTRY (range_entry), range);
  free (range);

  GtkWidget *sb0 = get_widget_assert (builder, "sb0");
  GtkWidget *sb1 = get_widget_assert (builder, "sb1");
  GtkWidget *sb2 = get_widget_assert (builder, "sb2");
  GtkWidget *sb3 = get_widget_assert (builder, "sb3");

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (sb0), sel.start_x);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (sb1), sel.start_y);

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (sb2), sel.end_x);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (sb3), sel.end_y);

  ia->updating_selection = FALSE;
}

static void
on_preview_selection_changed (SswSheet *sheet, gpointer selection,
                              PsppireImportAssistant *ia)
{
  memcpy (&ia->selection, selection, sizeof (ia->selection));
  do_selection_update (ia);
}

static void
entry_update_selected_range (GtkEntry *entry, PsppireImportAssistant *ia)
{
  const char *text = gtk_entry_get_text (entry);

  if (convert_cell_ref (text,
                        &ia->selection.start_x, &ia->selection.start_y,
                        &ia->selection.end_x, &ia->selection.end_y))
    {
      do_selection_update (ia);
    }
}

/* On change of any spinbutton, update the selected range accordingly.   */
static void
sb_update_selected_range (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->spread_builder;
  GtkWidget *sb0 = get_widget_assert (builder, "sb0");
  GtkWidget *sb1 = get_widget_assert (builder, "sb1");
  GtkWidget *sb2 = get_widget_assert (builder, "sb2");
  GtkWidget *sb3 = get_widget_assert (builder, "sb3");

  ia->selection.start_x = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sb0));
  ia->selection.start_y = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sb1));

  ia->selection.end_x = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sb2));
  ia->selection.end_y = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (sb3));

  do_selection_update (ia);
}


/* Initializes IA's sheet_spec substructure. */
void
sheet_spec_page_create (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->spread_builder;
  GtkWidget *page = get_widget_assert (builder, "Spreadsheet-Importer");

  ia->preview_sheet = get_widget_assert (builder, "preview-sheet");

  g_signal_connect (ia->preview_sheet, "selection-changed",
		    G_CALLBACK (on_preview_selection_changed), ia);

  gtk_widget_show (ia->preview_sheet);

  {
    GtkWidget *combo_box = get_widget_assert (builder, "sheet-entry");
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
    gtk_cell_layout_clear (GTK_CELL_LAYOUT (combo_box));
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), renderer,
				    "text", 0,
				    NULL);

    g_signal_connect (combo_box, "changed", G_CALLBACK (on_sheet_combo_changed), ia);
  }

  {
    GtkWidget *range_entry = get_widget_assert (builder, "cell-range-entry");
    g_signal_connect (range_entry, "changed", G_CALLBACK (entry_update_selected_range), ia);

    GtkWidget *sb0 = get_widget_assert (builder, "sb0");
    g_signal_connect_swapped (sb0, "value-changed", G_CALLBACK (sb_update_selected_range), ia);
    GtkWidget *sb1 = get_widget_assert (builder, "sb1");
    g_signal_connect_swapped (sb1, "value-changed", G_CALLBACK (sb_update_selected_range), ia);
    GtkWidget *sb2 = get_widget_assert (builder, "sb2");
    g_signal_connect_swapped (sb2, "value-changed", G_CALLBACK (sb_update_selected_range), ia);
    GtkWidget *sb3 = get_widget_assert (builder, "sb3");
    g_signal_connect_swapped (sb3, "value-changed", G_CALLBACK (sb_update_selected_range), ia);
  }


  add_page_to_assistant (ia, page,
			 GTK_ASSISTANT_PAGE_CONTENT, _("Importing Spreadsheet Data"));

  g_object_set_data (G_OBJECT (page), "on-entering", prepare_sheet_spec_page);
  g_object_set_data (G_OBJECT (page), "on-reset",   reset_page);
}


/* Set the data model for both the data sheet and the variable sheet.  */
void
spreadsheet_set_data_models (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->spread_builder;
  GtkWidget *range_entry = get_widget_assert (builder, "cell-range-entry");
  GtkWidget *rnc = get_widget_assert (builder, "readnames-checkbox");
  GtkWidget *combo_box = get_widget_assert (builder, "sheet-entry");

  struct spreadsheet_read_options opts;
  opts.sheet_name = NULL;
  opts.sheet_index = gtk_combo_box_get_active (GTK_COMBO_BOX (combo_box)) + 1;
  opts.read_names = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rnc));
  opts.cell_range = g_strdup (gtk_entry_get_text (GTK_ENTRY (range_entry)));
  opts.asw = 8;

  struct casereader *reader = spreadsheet_make_reader (ia->spreadsheet, &opts);

  PsppireDict *dict = psppire_dict_new_from_dict (ia->spreadsheet->dict);
  PsppireDataStore *store = psppire_data_store_new (dict);
  psppire_data_store_set_reader (store, reader);
  g_object_set (ia->data_sheet, "data-model", store, NULL);
  g_object_set (ia->var_sheet, "data-model", dict, NULL);
}


