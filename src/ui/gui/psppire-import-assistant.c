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

#include <gtk/gtk.h>

#include "data/casereader.h"
#include "data/data-in.h"
#include "data/format-guesser.h"
#include "data/gnumeric-reader.h"
#include "data/ods-reader.h"
#include "data/value-labels.h"
#include "data/casereader-provider.h"

#include "libpspp/i18n.h"

#include "builder-wrapper.h"

#include "psppire-data-sheet.h"
#include "psppire-data-store.h"
#include "psppire-dialog.h"
#include "psppire-encoding-selector.h"
#include "psppire-variable-sheet.h"

#include "psppire-import-spreadsheet.h"
#include "psppire-import-textfile.h"

#include "ui/syntax-gen.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

typedef void page_func (PsppireImportAssistant *, GtkWidget *page, enum IMPORT_ASSISTANT_DIRECTION dir);


static void formats_page_create (PsppireImportAssistant *ia);

static void psppire_import_assistant_init            (PsppireImportAssistant      *act);
static void psppire_import_assistant_class_init      (PsppireImportAssistantClass *class);

G_DEFINE_TYPE (PsppireImportAssistant, psppire_import_assistant, GTK_TYPE_ASSISTANT);


/* Properties */
enum
  {
    PROP_0,
  };

static void
psppire_import_assistant_set_property (GObject         *object,
				       guint            prop_id,
				       const GValue    *value,
				       GParamSpec      *pspec)
{
  //   PsppireImportAssistant *act = PSPPIRE_IMPORT_ASSISTANT (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_import_assistant_get_property (GObject    *object,
				       guint            prop_id,
				       GValue          *value,
				       GParamSpec      *pspec)
{
  //  PsppireImportAssistant *assistant = PSPPIRE_IMPORT_ASSISTANT (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static GObjectClass * parent_class = NULL;


static void
psppire_import_assistant_finalize (GObject *object)
{
  PsppireImportAssistant *ia = PSPPIRE_IMPORT_ASSISTANT (object);

  if (ia->spreadsheet)
    spreadsheet_unref (ia->spreadsheet);

  ds_destroy (&ia->quotes);

  dict_unref (ia->dict);
  dict_unref (ia->casereader_dict);

  g_object_unref (ia->text_builder);
  g_object_unref (ia->spread_builder);

  ia->response = -1;
  g_main_loop_unref (ia->main_loop);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}


static void
psppire_import_assistant_class_init (PsppireImportAssistantClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  object_class->set_property = psppire_import_assistant_set_property;
  object_class->get_property = psppire_import_assistant_get_property;

  object_class->finalize = psppire_import_assistant_finalize;
}


/* Causes the assistant to close, returning RESPONSE for
   interpretation by text_data_import_assistant. */
static void
close_assistant (PsppireImportAssistant *ia, int response)
{
  ia->response = response;
  g_main_loop_quit (ia->main_loop);
  gtk_widget_hide (GTK_WIDGET (ia));
}


/* Called when the Paste button on the last page of the assistant
   is clicked. */
static void
on_paste (GtkButton *button, PsppireImportAssistant *ia)
{
  close_assistant (ia, PSPPIRE_RESPONSE_PASTE);
}


/* /\* Clears the set of user-modified variables from IA's formats */
/*    substructure.  This discards user modifications to variable */
/*    formats, thereby causing formats to revert to their */
/*    defaults.  *\/ */
/* static void */
/* reset_formats_page (PsppireImportAssistant *ia, GtkWidget *page) */
/* { */
/* } */

static void prepare_formats_page (PsppireImportAssistant *ia);

/* Called when the Reset button is clicked.
   This function marshalls the callback to the relevant page.  */
static void
on_reset (GtkButton *button, PsppireImportAssistant *ia)
{
  gint pn = gtk_assistant_get_current_page (GTK_ASSISTANT (ia));
  {
    GtkWidget *page =  gtk_assistant_get_nth_page (GTK_ASSISTANT (ia), pn);

    page_func *xon_reset = g_object_get_data (G_OBJECT (page), "on-reset");

    if (xon_reset)
      xon_reset (ia, page, 0);
  }
}


static gint
next_page_func (gint old_page, gpointer data)
{
  return old_page + 1;
}


/* Called just before PAGE is displayed as the current page of
   IMPORT_ASSISTANT, this updates IA content according to the new
   page. */
static void
on_prepare (GtkAssistant *assistant, GtkWidget *page, PsppireImportAssistant *ia)
{
  gtk_widget_show (ia->reset_button);
  gtk_widget_hide (ia->paste_button);

  gint pn = gtk_assistant_get_current_page (assistant);
  gint previous_page_index = ia->previous_page;
  g_assert (pn != previous_page_index);

  if (previous_page_index >= 0)
    {
      GtkWidget *closing_page = gtk_assistant_get_nth_page (GTK_ASSISTANT (ia), previous_page_index);

        page_func *on_leaving = g_object_get_data (G_OBJECT (closing_page), "on-leaving");
        if (on_leaving)
          on_leaving (ia, closing_page, (pn > previous_page_index) ? IMPORT_ASSISTANT_FORWARDS : IMPORT_ASSISTANT_BACKWARDS);
    }

    GtkWidget *new_page = gtk_assistant_get_nth_page (GTK_ASSISTANT (ia), pn);

    page_func *on_entering = g_object_get_data (G_OBJECT (new_page), "on-entering");
    if (on_entering)
      on_entering (ia, new_page, (pn > previous_page_index) ? IMPORT_ASSISTANT_FORWARDS : IMPORT_ASSISTANT_BACKWARDS);

  ia->previous_page = pn;
}

/* Called when the Cancel button in the assistant is clicked. */
static void
on_cancel (GtkAssistant *assistant, PsppireImportAssistant *ia)
{
  close_assistant (ia, GTK_RESPONSE_CANCEL);
}

/* Called when the Apply button on the last page of the assistant
   is clicked. */
static void
on_close (GtkAssistant *assistant, PsppireImportAssistant *ia)
{
  close_assistant (ia, GTK_RESPONSE_APPLY);
}


static void
on_chosen (PsppireImportAssistant *ia, GtkWidget *page)
{
  GtkFileChooser *fc = GTK_FILE_CHOOSER (page);
  gchar *f = gtk_file_chooser_get_filename (fc);
  int i;

  for(i = gtk_assistant_get_n_pages (GTK_ASSISTANT (ia)); i > 0; --i)
    gtk_assistant_remove_page (GTK_ASSISTANT (ia), i);

  gtk_assistant_set_page_complete (GTK_ASSISTANT(ia), GTK_WIDGET (fc), FALSE);

  if (f && g_file_test (f, G_FILE_TEST_IS_REGULAR))
    {
      gtk_assistant_set_page_complete (GTK_ASSISTANT(ia), GTK_WIDGET (fc), TRUE);

      if (ia->spreadsheet)
	spreadsheet_unref (ia->spreadsheet);

      ia->spreadsheet = gnumeric_probe (f, FALSE);

      if (!ia->spreadsheet)
	ia->spreadsheet = ods_probe (f, FALSE);

      if (ia->spreadsheet)
	{
	  sheet_spec_page_create (ia);
	}
      else
	{
	  intro_page_create (ia);
	  first_line_page_create (ia);
	  separators_page_create (ia);
	}

      formats_page_create (ia);
    }

  g_free (f);
}

/* This has to be done on a map signal callback,
   because GtkFileChooserWidget resets everything when it is mapped. */
static void
on_map (PsppireImportAssistant *ia, GtkWidget *page)
{
#if TEXT_FILE
  GtkFileChooser *fc = GTK_FILE_CHOOSER (page);

  if (ia->file_name)
    gtk_file_chooser_set_filename (fc, ia->file_name);
#endif

  on_chosen (ia, page);
}



static void
chooser_page_enter (PsppireImportAssistant *ia, GtkWidget *page, enum IMPORT_ASSISTANT_DIRECTION dir)
{
}

static void
chooser_page_leave (PsppireImportAssistant *ia, GtkWidget *page, enum IMPORT_ASSISTANT_DIRECTION dir)
{
  if (dir != IMPORT_ASSISTANT_FORWARDS)
    return;

  GtkFileChooser *fc = GTK_FILE_CHOOSER (page);

  g_free (ia->file_name);
  ia->file_name = gtk_file_chooser_get_filename (fc);

  /* Add the chosen file to the recent manager.  */
  {
    gchar *uri = gtk_file_chooser_get_uri (fc);
    GtkRecentManager * manager = gtk_recent_manager_get_default ();
    gtk_recent_manager_add_item (manager, uri);
    g_free (uri);
  }

  if (!ia->spreadsheet)
    {
      gchar *encoding = psppire_encoding_selector_get_encoding (ia->encoding_selector);
      ia->text_file = psppire_text_file_new (ia->file_name, encoding);
      gtk_tree_view_set_model (GTK_TREE_VIEW (ia->first_line_tree_view),
			       GTK_TREE_MODEL (ia->text_file));

      g_free (encoding);
    }
}

static void
chooser_page_reset (PsppireImportAssistant *ia, GtkWidget *page)
{
  GtkFileChooser *fc = GTK_FILE_CHOOSER (page);

  gtk_file_chooser_set_filter (fc, ia->default_filter);
  gtk_file_chooser_unselect_all (fc);

  on_chosen (ia, page);
}


static void
on_file_activated (GtkFileChooser *chooser, PsppireImportAssistant *ia)
{
  gtk_assistant_next_page (GTK_ASSISTANT (ia));
}

static void
chooser_page_create (PsppireImportAssistant *ia)
{
  GtkFileFilter *filter = NULL;

  GtkWidget *chooser = gtk_file_chooser_widget_new (GTK_FILE_CHOOSER_ACTION_OPEN);

  g_signal_connect (chooser, "file-activated", G_CALLBACK (on_file_activated), ia);

  g_object_set_data (G_OBJECT (chooser), "on-leaving", chooser_page_leave);
  g_object_set_data (G_OBJECT (chooser), "on-reset",   chooser_page_reset);
  g_object_set_data (G_OBJECT (chooser), "on-entering",chooser_page_enter);

  g_object_set (chooser, "local-only", FALSE, NULL);


  ia->default_filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (ia->default_filter, _("All Files"));
  gtk_file_filter_add_pattern (ia->default_filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), ia->default_filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Text Files"));
  gtk_file_filter_add_mime_type (filter, "text/*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Text (*.txt) Files"));
  gtk_file_filter_add_pattern (filter, "*.txt");
  gtk_file_filter_add_pattern (filter, "*.TXT");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Plain Text (ASCII) Files"));
  gtk_file_filter_add_mime_type (filter, "text/plain");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Comma Separated Value Files"));
  gtk_file_filter_add_mime_type (filter, "text/csv");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);

  /* I've never encountered one of these, but it's listed here:
     http://www.iana.org/assignments/media-types/text/tab-separated-values  */
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Tab Separated Value Files"));
  gtk_file_filter_add_mime_type (filter, "text/tab-separated-values");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Gnumeric Spreadsheet Files"));
  gtk_file_filter_add_mime_type (filter, "application/x-gnumeric");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("OpenDocument Spreadsheet Files"));
  gtk_file_filter_add_mime_type (filter, "application/vnd.oasis.opendocument.spreadsheet");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Spreadsheet Files"));
  gtk_file_filter_add_mime_type (filter, "application/x-gnumeric");
  gtk_file_filter_add_mime_type (filter, "application/vnd.oasis.opendocument.spreadsheet");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (chooser), filter);

  ia->encoding_selector = psppire_encoding_selector_new ("Auto", TRUE);
  gtk_file_chooser_set_extra_widget (GTK_FILE_CHOOSER (chooser), ia->encoding_selector);

  add_page_to_assistant (ia, chooser,
			 GTK_ASSISTANT_PAGE_INTRO, _("Select File to Import"));

  g_signal_connect_swapped (chooser, "selection-changed", G_CALLBACK (on_chosen), ia);
  g_signal_connect_swapped (chooser, "map", G_CALLBACK (on_map), ia);
}



static void
psppire_import_assistant_init (PsppireImportAssistant *ia)
{
  ia->text_builder = builder_new ("text-data-import.ui");
  ia->spread_builder = builder_new ("spreadsheet-import.ui");

  ia->previous_page = -1 ;
  ia->file_name = NULL;

  ia->spreadsheet = NULL;
  ia->updating_selection = FALSE;
  ia->dict = NULL;
  ia->casereader_dict = NULL;

  ia->main_loop = g_main_loop_new (NULL, TRUE);

  g_signal_connect (ia, "prepare", G_CALLBACK (on_prepare), ia);
  g_signal_connect (ia, "cancel", G_CALLBACK (on_cancel), ia);
  g_signal_connect (ia, "close", G_CALLBACK (on_close), ia);

  ia->paste_button = gtk_button_new_with_label (_("Paste"));
  ia->reset_button = gtk_button_new_with_label (_("Reset"));

  gtk_assistant_add_action_widget (GTK_ASSISTANT(ia), ia->paste_button);

  g_signal_connect (ia->paste_button, "clicked", G_CALLBACK (on_paste), ia);
  g_signal_connect (ia->reset_button, "clicked", G_CALLBACK (on_reset), ia);

  gtk_assistant_add_action_widget (GTK_ASSISTANT(ia), ia->reset_button);

  gtk_window_set_title (GTK_WINDOW (ia),
                        _("Importing Delimited Text Data"));

  gtk_window_set_icon_name (GTK_WINDOW (ia), "pspp");

  chooser_page_create (ia);

  gtk_assistant_set_forward_page_func (GTK_ASSISTANT (ia), next_page_func, NULL, NULL);

  gtk_window_maximize (GTK_WINDOW (ia));
}


/* Appends a page of the given TYPE, with PAGE as its content, to
   the GtkAssistant encapsulated by IA.  Returns the GtkWidget
   that represents the page. */
GtkWidget *
add_page_to_assistant (PsppireImportAssistant *ia,
		       GtkWidget *page, GtkAssistantPageType type, const gchar *title)
{
  GtkWidget *content = page;

  gtk_assistant_append_page (GTK_ASSISTANT (ia), content);
  gtk_assistant_set_page_type (GTK_ASSISTANT(ia), content, type);
  gtk_assistant_set_page_title (GTK_ASSISTANT(ia), content, title);
  gtk_assistant_set_page_complete (GTK_ASSISTANT(ia), content, TRUE);

  return content;
}


GtkWidget *
psppire_import_assistant_new (GtkWindow *toplevel)
{
  return GTK_WIDGET (g_object_new (PSPPIRE_TYPE_IMPORT_ASSISTANT,
				   /* Some window managers (notably ratpoison)
				      ignore the maximise command when a window is
				      transient.  This causes problems for this
				      window. */
				   /* "transient-for", toplevel, */
				   NULL));
}






/* Called just before the formats page of the assistant is
   displayed. */
static void
prepare_formats_page (PsppireImportAssistant *ia)
{
/* Set the data model for both the data sheet and the variable sheet.  */
  if (ia->spreadsheet)
    spreadsheet_set_data_models (ia);
  else
    textfile_set_data_models (ia);

  /* Show half-half the data sheet and the variable sheet.  */
  gint pmax;
  g_object_get (get_widget_assert (ia->text_builder, "vpaned1"),
		"max-position", &pmax, NULL);

  g_object_set (get_widget_assert (ia->text_builder, "vpaned1"),
		"position", pmax / 2, NULL);

  gtk_widget_show (ia->paste_button);
}

static void
formats_page_create (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->text_builder;

  GtkWidget *w = get_widget_assert (builder, "Formats");
  g_object_set_data (G_OBJECT (w), "on-entering", prepare_formats_page);
  //  g_object_set_data (G_OBJECT (w), "on-reset", reset_formats_page);

  ia->data_sheet = get_widget_assert (builder, "data-sheet");
  ia->var_sheet = get_widget_assert (builder, "variable-sheet");

  add_page_to_assistant (ia, w,
			 GTK_ASSISTANT_PAGE_CONFIRM, _("Adjust Variable Formats"));
}




static void
sheet_spec_gen_syntax (PsppireImportAssistant *ia, struct string *s)
{
  GtkBuilder *builder = ia->spread_builder;
  GtkWidget *range_entry = get_widget_assert (builder, "cell-range-entry");
  GtkWidget *sheet_entry = get_widget_assert (builder, "sheet-entry");
  GtkWidget *rnc = get_widget_assert (builder, "readnames-checkbox");
  const gchar *range = gtk_entry_get_text (GTK_ENTRY (range_entry));
  int sheet_index = 1 + gtk_combo_box_get_active (GTK_COMBO_BOX (sheet_entry));
  gboolean read_names = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rnc));


  char *filename;
  if (ia->spreadsheet)
    filename = ia->spreadsheet->file_name;
  else
    g_object_get (ia->text_file, "file-name", &filename, NULL);
  syntax_gen_pspp (s,
		   "GET DATA"
		   "\n  /TYPE=%ss"
		   "\n  /FILE=%sq"
		   "\n  /SHEET=index %d"
		   "\n  /READNAMES=%ss",
		   ia->spreadsheet->type,
		   filename,
		   sheet_index,
		   read_names ? "ON" : "OFF");

  if (range && 0 != strcmp ("", range))
    {
      syntax_gen_pspp (s,
		       "\n  /CELLRANGE=RANGE %sq", range);
    }
  else
    {
      syntax_gen_pspp (s,
		       "\n  /CELLRANGE=FULL");
    }


  syntax_gen_pspp (s, ".\n");
}


gchar *
psppire_import_assistant_generate_syntax (PsppireImportAssistant *ia)
{
  struct string s = DS_EMPTY_INITIALIZER;

  if (!ia->spreadsheet)
    {
      text_spec_gen_syntax (ia, &s);
    }
  else
    {
      sheet_spec_gen_syntax (ia, &s);
    }

  return ds_cstr (&s);
}


int
psppire_import_assistant_run (PsppireImportAssistant *asst)
{
  g_main_loop_run (asst->main_loop);
  return asst->response;
}
