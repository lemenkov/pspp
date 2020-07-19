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

#include <gtk/gtk.h>

#include "data/casereader.h"
#include "data/data-in.h"
#include "data/data-out.h"
#include "data/dictionary.h"
#include "data/format-guesser.h"
#include "data/format.h"
#include "data/gnumeric-reader.h"
#include "data/ods-reader.h"
#include "data/spreadsheet-reader.h"
#include "data/value-labels.h"
#include "data/casereader-provider.h"

#include "libpspp/i18n.h"
#include "libpspp/line-reader.h"
#include "libpspp/message.h"
#include "libpspp/hmap.h"
#include "libpspp/hash-functions.h"
#include "libpspp/str.h"

#include "builder-wrapper.h"

#include "psppire-data-sheet.h"
#include "psppire-data-store.h"
#include "psppire-dialog.h"
#include "psppire-delimited-text.h"
#include "psppire-dict.h"
#include "psppire-encoding-selector.h"
#include "psppire-import-assistant.h"
#include "psppire-scanf.h"
#include "psppire-spreadsheet-model.h"
#include "psppire-text-file.h"
#include "psppire-variable-sheet.h"

#include "ui/syntax-gen.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum { MAX_LINE_LEN = 16384 }; /* Max length of an acceptable line. */


/* Chooses a name for each column on the separators page */
static void choose_column_names (PsppireImportAssistant *ia);

static void intro_page_create (PsppireImportAssistant *ia);
static void first_line_page_create (PsppireImportAssistant *ia);

static void separators_page_create (PsppireImportAssistant *ia);
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

  g_object_unref (ia->builder);

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


/* Revises the contents of the fields tree view based on the
   currently chosen set of separators. */
static void
revise_fields_preview (PsppireImportAssistant *ia)
{
  choose_column_names (ia);
}


struct separator
{
  const char *name;           /* Name (for use with get_widget_assert). */
  gunichar c;                 /* Separator character. */
};

/* All the separators in the dialog box. */
static const struct separator separators[] =
  {
    {"space",     ' '},
    {"tab",       '\t'},
    {"bang",      '!'},
    {"colon",     ':'},
    {"comma",     ','},
    {"hyphen",    '-'},
    {"pipe",      '|'},
    {"semicolon", ';'},
    {"slash",     '/'},
  };

#define SEPARATOR_CNT (sizeof separators / sizeof *separators)

struct separator_count_node
{
  struct hmap_node node;
  int occurance; /* The number of times the separator occurs in a line */
  int quantity;  /* The number of lines with this occurance */
};


/* Picks the most likely separator and quote characters based on
   IA's file data. */
static void
choose_likely_separators (PsppireImportAssistant *ia)
{
  gint first_line = 0;
  g_object_get (ia->delimiters_model, "first-line", &first_line, NULL);

  gboolean valid;
  GtkTreeIter iter;
  int j;

  struct hmap count_map[SEPARATOR_CNT];
  for (j = 0; j < SEPARATOR_CNT; ++j)
    hmap_init (count_map + j);

  GtkTreePath *p = gtk_tree_path_new_from_indices (first_line, -1);

  for (valid = gtk_tree_model_get_iter (GTK_TREE_MODEL (ia->text_file), &iter, p);
       valid;
       valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (ia->text_file), &iter))
    {
      gchar *line_text = NULL;
      gtk_tree_model_get (GTK_TREE_MODEL (ia->text_file), &iter, 1, &line_text, -1);

      gint *counts = xzalloc (sizeof *counts * SEPARATOR_CNT);

      struct substring cs = ss_cstr (line_text);
      for (;
	   UINT32_MAX != ss_first_mb (cs);
	   ss_get_mb (&cs))
	{
	  ucs4_t character = ss_first_mb (cs);

	  int s;
	  for (s = 0; s < SEPARATOR_CNT; ++s)
	    {
	      if (character == separators[s].c)
		counts[s]++;
	    }
	}

      int j;
      for (j = 0; j < SEPARATOR_CNT; ++j)
	{
	  if (counts[j] > 0)
	    {
	      struct separator_count_node *cn = NULL;
	      unsigned int hash = hash_int (counts[j], 0);
	      HMAP_FOR_EACH_WITH_HASH (cn, struct separator_count_node, node, hash, &count_map[j])
		{
		  if (cn->occurance == counts[j])
		    break;
		}

	      if (cn == NULL)
		{
		  struct separator_count_node *new_cn = xzalloc (sizeof *new_cn);
		  new_cn->occurance = counts[j];
		  new_cn->quantity = 1;
		  hmap_insert (&count_map[j], &new_cn->node, hash);
		}
	      else
		cn->quantity++;
	    }
	}

      free (line_text);
      free (counts);
    }
  gtk_tree_path_free (p);

  if (hmap_count (count_map) > 0)
    {
      int most_frequent = -1;
      int largest = 0;
      for (j = 0; j < SEPARATOR_CNT; ++j)
        {
          struct separator_count_node *cn;
          HMAP_FOR_EACH (cn, struct separator_count_node, node, &count_map[j])
            {
              if (largest < cn->quantity)
                {
                  largest = cn->quantity;
                  most_frequent = j;
                }
            }
          hmap_destroy (&count_map[j]);
        }

      g_return_if_fail (most_frequent >= 0);

      GtkWidget *toggle =
        get_widget_assert (ia->builder, separators[most_frequent].name);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), TRUE);
    }
}

static void
repopulate_delimiter_columns (PsppireImportAssistant *ia)
{
  /* Remove all the columns */
  while (gtk_tree_view_get_n_columns (GTK_TREE_VIEW (ia->fields_tree_view)) > 0)
    {
      GtkTreeViewColumn *tvc = gtk_tree_view_get_column (GTK_TREE_VIEW (ia->fields_tree_view), 0);
      gtk_tree_view_remove_column (GTK_TREE_VIEW (ia->fields_tree_view), tvc);
    }

  gint n_fields =
    gtk_tree_model_get_n_columns (GTK_TREE_MODEL (ia->delimiters_model));

  /* ... and put them back again. */
  gint f;
  for (f = gtk_tree_view_get_n_columns (GTK_TREE_VIEW (ia->fields_tree_view));
       f < n_fields; f++)
    {
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();

      const gchar *title = NULL;

      if (f == 0)
	title = _("line");
      else
	{
	  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->variable_names_cb)))
	    {
	      title =
		psppire_delimited_text_get_header_title
		(PSPPIRE_DELIMITED_TEXT (ia->delimiters_model), f - 1);
	    }
	  if (title == NULL)
	    title = _("var");
	}

      GtkTreeViewColumn *column =
	gtk_tree_view_column_new_with_attributes (title,
						  renderer,
						  "text", f,
						  NULL);
      g_object_set (column,
		    "resizable", TRUE,
		    "sizing", GTK_TREE_VIEW_COLUMN_AUTOSIZE,
		    NULL);

      gtk_tree_view_append_column (GTK_TREE_VIEW (ia->fields_tree_view), column);
    }
}

static void
reset_tree_view_model (PsppireImportAssistant *ia)
{
  GtkTreeModel *tm = gtk_tree_view_get_model (GTK_TREE_VIEW (ia->fields_tree_view));
  g_object_ref (tm);
  gtk_tree_view_set_model (GTK_TREE_VIEW (ia->fields_tree_view), NULL);


  repopulate_delimiter_columns (ia);

  gtk_tree_view_set_model (GTK_TREE_VIEW (ia->fields_tree_view), tm);
  //  gtk_tree_view_columns_autosize (GTK_TREE_VIEW (ia->fields_tree_view));

  g_object_unref (tm);
}

/* Called just before the separators page becomes visible in the
   assistant, and when the Reset button is clicked. */
static void
prepare_separators_page (PsppireImportAssistant *ia, GtkWidget *page)
{
  gtk_tree_view_set_model (GTK_TREE_VIEW (ia->fields_tree_view),
			   GTK_TREE_MODEL (ia->delimiters_model));

  g_signal_connect_swapped (GTK_TREE_MODEL (ia->delimiters_model), "notify::delimiters",
  			G_CALLBACK (reset_tree_view_model), ia);


  repopulate_delimiter_columns (ia);

  revise_fields_preview (ia);
  choose_likely_separators (ia);
}

/* Resets IA's intro page to its initial state. */
static void
reset_intro_page (PsppireImportAssistant *ia)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->all_cases_button),
                                TRUE);
}



/* Clears the set of user-modified variables from IA's formats
   substructure.  This discards user modifications to variable
   formats, thereby causing formats to revert to their
   defaults.  */
static void
reset_formats_page (PsppireImportAssistant *ia, GtkWidget *page)
{
}

static void prepare_formats_page (PsppireImportAssistant *ia);

/* Called when the Reset button is clicked. */
static void
on_reset (GtkButton *button, PsppireImportAssistant *ia)
{
  gint pn = gtk_assistant_get_current_page (GTK_ASSISTANT (ia));
  {
    GtkWidget *page =  gtk_assistant_get_nth_page (GTK_ASSISTANT (ia), pn);

    page_func *on_reset = g_object_get_data (G_OBJECT (page), "on-reset");

    if (on_reset)
      on_reset (ia, page);
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
  gint previous_page_index = ia->current_page;

  if (previous_page_index >= 0)
    {
      GtkWidget *closing_page = gtk_assistant_get_nth_page (GTK_ASSISTANT (ia), previous_page_index);

      if (pn > previous_page_index)
	{
	  page_func *on_forward = g_object_get_data (G_OBJECT (closing_page), "on-forward");

	  if (on_forward)
	    on_forward (ia, closing_page);
	}
      else
	{
	  page_func *on_back = g_object_get_data (G_OBJECT (closing_page), "on-back");

	  if (on_back)
	    on_back (ia, closing_page);
	}
    }

  {
    GtkWidget *new_page = gtk_assistant_get_nth_page (GTK_ASSISTANT (ia), pn);

    page_func *on_entering = g_object_get_data (G_OBJECT (new_page), "on-entering");

    if (on_entering)
      on_entering (ia, new_page);
  }

  ia->current_page = pn;
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


static GtkWidget *
add_page_to_assistant (PsppireImportAssistant *ia,
		       GtkWidget *page, GtkAssistantPageType type, const gchar *);


static void
on_sheet_combo_changed (GtkComboBox *cb, PsppireImportAssistant *ia)
{
  GtkTreeIter iter;
  gchar *range = NULL;
  GtkTreeModel *model = gtk_combo_box_get_model (cb);
  GtkBuilder *builder = ia->builder;
  GtkWidget *range_entry = get_widget_assert (builder, "cell-range-entry");

  gtk_combo_box_get_active_iter (cb, &iter);
  gtk_tree_model_get (model, &iter, PSPPIRE_SPREADSHEET_MODEL_COL_RANGE, &range, -1);
  gtk_entry_set_text (GTK_ENTRY (range_entry), range ?  range : "");
  g_free (range);
}

/* Prepares IA's sheet_spec page. */
static void
prepare_sheet_spec_page (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->builder;
  GtkWidget *sheet_entry = get_widget_assert (builder, "sheet-entry");
  GtkWidget *readnames_checkbox = get_widget_assert (builder, "readnames-checkbox");

  GtkTreeModel *model = psppire_spreadsheet_model_new (ia->spreadsheet);
  gtk_combo_box_set_model (GTK_COMBO_BOX (sheet_entry), model);

  gint items = gtk_tree_model_iter_n_children (model, NULL);
  gtk_widget_set_sensitive (sheet_entry, items > 1);

  gtk_combo_box_set_active (GTK_COMBO_BOX (sheet_entry), 0);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (readnames_checkbox), FALSE);
}


/* Initializes IA's sheet_spec substructure. */
static void
sheet_spec_page_create (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->builder;
  GtkWidget *page = get_widget_assert (builder, "Spreadsheet-Importer");

  GtkWidget *combo_box = get_widget_assert (builder, "sheet-entry");
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_clear (GTK_CELL_LAYOUT (combo_box));
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo_box), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combo_box), renderer,
				  "text", 0,
				  NULL);

  g_signal_connect (combo_box, "changed", G_CALLBACK (on_sheet_combo_changed), ia);

  add_page_to_assistant (ia, page,
			 GTK_ASSISTANT_PAGE_CONTENT, _("Importing Spreadsheet Data"));

  g_object_set_data (G_OBJECT (page), "on-entering", prepare_sheet_spec_page);
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
chooser_page_enter (PsppireImportAssistant *ia, GtkWidget *page)
{
}

static void
chooser_page_leave (PsppireImportAssistant *ia, GtkWidget *page)
{
  g_free (ia->file_name);
  ia->file_name = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (page));
  gchar *encoding = psppire_encoding_selector_get_encoding (ia->encoding_selector);

  if (!ia->spreadsheet)
    {
      ia->text_file = psppire_text_file_new (ia->file_name, encoding);
      gtk_tree_view_set_model (GTK_TREE_VIEW (ia->first_line_tree_view),
			       GTK_TREE_MODEL (ia->text_file));
    }


  g_free (encoding);
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

  g_object_set_data (G_OBJECT (chooser), "on-forward", chooser_page_leave);
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
  ia->builder = builder_new ("text-data-import.ui");

  ia->current_page = -1 ;
  ia->file_name = NULL;

  ia->spreadsheet = NULL;
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
static GtkWidget *
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


/* Called when one of the radio buttons is clicked. */
static void
on_intro_amount_changed (PsppireImportAssistant *p)
{
  gtk_widget_set_sensitive (p->n_cases_spin,
			    gtk_toggle_button_get_active
			    (GTK_TOGGLE_BUTTON (p->n_cases_button)));

  gtk_widget_set_sensitive (p->percent_spin,
			    gtk_toggle_button_get_active
			    (GTK_TOGGLE_BUTTON (p->percent_button)));
}

static void
on_treeview_selection_change (PsppireImportAssistant *ia)
{
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (ia->first_line_tree_view));
  GtkTreeModel *model = NULL;
  GtkTreeIter iter;
  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      gint max_lines;
      int n;
      GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
      gint *index = gtk_tree_path_get_indices (path);
      n = *index;
      gtk_tree_path_free (path);
      g_object_get (model, "maximum-lines", &max_lines, NULL);
      gtk_widget_set_sensitive (ia->variable_names_cb,
				(n > 0 && n < max_lines));
      ia->delimiters_model =
	psppire_delimited_text_new (GTK_TREE_MODEL (ia->text_file));
      g_object_set (ia->delimiters_model, "first-line", n, NULL);
    }
}

static void
render_text_preview_line (GtkTreeViewColumn *tree_column,
		GtkCellRenderer *cell,
		GtkTreeModel *tree_model,
		GtkTreeIter *iter,
		gpointer data)
{
  /*
     Set the text  to a "insensitive" state if the row
     is greater than what the user declared to be the maximum.
  */
  GtkTreePath *path = gtk_tree_model_get_path (tree_model, iter);
  gint *ii = gtk_tree_path_get_indices (path);
  gint max_lines;
  g_object_get (tree_model, "maximum-lines", &max_lines, NULL);
  g_object_set (cell, "sensitive", (*ii < max_lines), NULL);
  gtk_tree_path_free (path);
}

/* Initializes IA's first_line substructure. */
static void
first_line_page_create (PsppireImportAssistant *ia)
{
  GtkWidget *w =  get_widget_assert (ia->builder, "FirstLine");

  g_object_set_data (G_OBJECT (w), "on-entering", on_treeview_selection_change);

  add_page_to_assistant (ia, w,
			 GTK_ASSISTANT_PAGE_CONTENT, _("Select the First Line"));

  GtkWidget *scrolled_window = get_widget_assert (ia->builder, "first-line-scroller");

  if (ia->first_line_tree_view == NULL)
    {
      ia->first_line_tree_view = gtk_tree_view_new ();
      g_object_set (ia->first_line_tree_view, "enable-search", FALSE, NULL);

      gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (ia->first_line_tree_view), TRUE);

      GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes (_("Line"), renderer,
									    "text", 0,
									    NULL);

      gtk_tree_view_column_set_cell_data_func (column, renderer, render_text_preview_line, ia, 0);
      gtk_tree_view_append_column (GTK_TREE_VIEW (ia->first_line_tree_view), column);

      renderer = gtk_cell_renderer_text_new ();
      column = gtk_tree_view_column_new_with_attributes (_("Text"), renderer, "text", 1, NULL);
      gtk_tree_view_column_set_cell_data_func (column, renderer, render_text_preview_line, ia, 0);

      gtk_tree_view_append_column (GTK_TREE_VIEW (ia->first_line_tree_view), column);

      g_signal_connect_swapped (ia->first_line_tree_view, "cursor-changed",
				G_CALLBACK (on_treeview_selection_change), ia);
      gtk_container_add (GTK_CONTAINER (scrolled_window), ia->first_line_tree_view);
    }

  gtk_widget_show_all (scrolled_window);

  ia->variable_names_cb = get_widget_assert (ia->builder, "variable-names");
}

static void
intro_on_leave (PsppireImportAssistant *ia)
{
  gint lc = 0;
  g_object_get (ia->text_file, "line-count", &lc, NULL);
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->n_cases_button)))
    {
      gint max_lines = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (ia->n_cases_spin));
      g_object_set (ia->text_file, "maximum-lines", max_lines, NULL);
    }
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->percent_button)))
    {
      gdouble percent = gtk_spin_button_get_value (GTK_SPIN_BUTTON (ia->percent_spin));
      g_object_set (ia->text_file, "maximum-lines", (gint) (lc * percent / 100.0), NULL);
    }
  else
    {
      g_object_set (ia->text_file, "maximum-lines", lc, NULL);
    }
}


static void
intro_on_enter (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->builder;
  GtkWidget *table  = get_widget_assert (builder, "button-table");

  struct string s;

  ds_init_empty (&s);
  ds_put_cstr (&s, _("This assistant will guide you through the process of "
                     "importing data into PSPP from a text file with one line "
                     "per case,  in which fields are separated by tabs, "
                     "commas, or other delimiters.\n\n"));

  if (ia->text_file)
    {
      if (ia->text_file->total_is_exact)
	{
	  ds_put_format (
			 &s, ngettext ("The selected file contains %'lu line of text.  ",
				       "The selected file contains %'lu lines of text.  ",
				       ia->text_file->total_lines),
			 ia->text_file->total_lines);
	}
      else if (ia->text_file->total_lines > 0)
	{
	  ds_put_format (
			 &s, ngettext (
				       "The selected file contains approximately %'lu line of text.  ",
				       "The selected file contains approximately %'lu lines of text.  ",
				       ia->text_file->total_lines),
			 ia->text_file->total_lines);
	  ds_put_format (
			 &s, ngettext (
				       "Only the first %zu line of the file will be shown for "
				       "preview purposes in the following screens.  ",
				       "Only the first %zu lines of the file will be shown for "
				       "preview purposes in the following screens.  ",
				       ia->text_file->line_cnt),
			 ia->text_file->line_cnt);
	}
    }

  ds_put_cstr (&s, _("You may choose below how much of the file should "
                     "actually be imported."));

  gtk_label_set_text (GTK_LABEL (get_widget_assert (builder, "intro-label")),
                      ds_cstr (&s));
  ds_destroy (&s);

  if (gtk_grid_get_child_at (GTK_GRID (table), 1, 1) == NULL)
    {
      GtkWidget *hbox_n_cases = psppire_scanf_new (_("Only the first %4d cases"), &ia->n_cases_spin);
      gtk_grid_attach (GTK_GRID (table), hbox_n_cases,
		       1, 1,
		       1, 1);
    }

  GtkAdjustment *adj = gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (ia->n_cases_spin));
  gtk_adjustment_set_lower (adj, 1.0);

  if (gtk_grid_get_child_at (GTK_GRID (table), 1, 2) == NULL)
    {
      GtkWidget *hbox_percent = psppire_scanf_new (_("Only the first %3d %% of file (approximately)"),
						   &ia->percent_spin);

      gtk_grid_attach (GTK_GRID (table), hbox_percent,
		       1, 2,
		       1, 1);
    }

  gtk_widget_show_all (table);

  on_intro_amount_changed (ia);
}

/* Initializes IA's intro substructure. */
static void
intro_page_create (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->builder;

  GtkWidget *w =  get_widget_assert (builder, "Intro");

  ia->percent_spin = gtk_spin_button_new_with_range (0, 100, 10);


  add_page_to_assistant (ia, w,  GTK_ASSISTANT_PAGE_CONTENT, _("Select the Lines to Import"));

  ia->all_cases_button = get_widget_assert (builder, "import-all-cases");

  ia->n_cases_button = get_widget_assert (builder, "import-n-cases");

  ia->percent_button = get_widget_assert (builder, "import-percent");

  g_signal_connect_swapped (ia->all_cases_button, "toggled",
			    G_CALLBACK (on_intro_amount_changed), ia);
  g_signal_connect_swapped (ia->n_cases_button, "toggled",
			    G_CALLBACK (on_intro_amount_changed), ia);
  g_signal_connect_swapped (ia->percent_button, "toggled",
			    G_CALLBACK (on_intro_amount_changed), ia);


  g_object_set_data (G_OBJECT (w), "on-forward", intro_on_leave);
  g_object_set_data (G_OBJECT (w), "on-entering", intro_on_enter);
  g_object_set_data (G_OBJECT (w), "on-reset", reset_intro_page);
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





/* Chooses a name for each column on the separators page */
static void
choose_column_names (PsppireImportAssistant *ia)
{
  int i;
  unsigned long int generated_name_count = 0;
  dict_clear (ia->dict);

  for (i = 0;
       i < gtk_tree_model_get_n_columns (GTK_TREE_MODEL (ia->delimiters_model)) - 1;
       ++i)
    {
      const gchar *candidate_name = NULL;

      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->variable_names_cb)))
	{
	  candidate_name = psppire_delimited_text_get_header_title (PSPPIRE_DELIMITED_TEXT (ia->delimiters_model), i);
	}

      char *name = dict_make_unique_var_name (ia->dict,
					      candidate_name,
					      &generated_name_count);

      dict_create_var_assert (ia->dict, name, 0);
      free (name);
    }
}

/* Called when the user toggles one of the separators
   checkboxes. */
static void
on_separator_toggle (GtkToggleButton *toggle UNUSED,
                     PsppireImportAssistant *ia)
{
  int i;
  GSList *delimiters = NULL;
  for (i = 0; i < SEPARATOR_CNT; i++)
    {
      const struct separator *s = &separators[i];
      GtkWidget *button = get_widget_assert (ia->builder, s->name);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
	{
	  delimiters = g_slist_prepend (delimiters,  GINT_TO_POINTER (s->c));
	}
    }

  g_object_set (ia->delimiters_model, "delimiters", delimiters, NULL);

  revise_fields_preview (ia);
}


/* Called when the user changes the entry field for custom
   separators. */
static void
on_separators_custom_entry_notify (GObject *gobject UNUSED,
                                   GParamSpec *arg1 UNUSED,
                                   PsppireImportAssistant *ia)
{
  revise_fields_preview (ia);
}

/* Called when the user toggles the checkbox that enables custom
   separators. */
static void
on_separators_custom_cb_toggle (GtkToggleButton *custom_cb,
                                PsppireImportAssistant *ia)
{
  bool is_active = gtk_toggle_button_get_active (custom_cb);
  gtk_widget_set_sensitive (ia->custom_entry, is_active);
  revise_fields_preview (ia);
}

/* Called when the user changes the selection in the combo box
   that selects a quote character. */
static void
on_quote_combo_change (GtkComboBox *combo, PsppireImportAssistant *ia)
{
  //  revise_fields_preview (ia);
}

/* Called when the user toggles the checkbox that enables
   quoting. */
static void
on_quote_cb_toggle (GtkToggleButton *quote_cb, PsppireImportAssistant *ia)
{
  bool is_active = gtk_toggle_button_get_active (quote_cb);
  gtk_widget_set_sensitive (ia->quote_combo, is_active);
  revise_fields_preview (ia);
}

/* Initializes IA's separators substructure. */
static void
separators_page_create (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->builder;

  size_t i;

  GtkWidget *w = get_widget_assert (builder, "Separators");

  g_object_set_data (G_OBJECT (w), "on-entering", prepare_separators_page);
  g_object_set_data (G_OBJECT (w), "on-reset", prepare_separators_page);

  add_page_to_assistant (ia, w,   GTK_ASSISTANT_PAGE_CONTENT, _("Choose Separators"));

  ia->custom_cb = get_widget_assert (builder, "custom-cb");
  ia->custom_entry = get_widget_assert (builder, "custom-entry");
  ia->quote_combo = get_widget_assert (builder, "quote-combo");
  ia->quote_cb = get_widget_assert (builder, "quote-cb");

  gtk_combo_box_set_active (GTK_COMBO_BOX (ia->quote_combo), 0);

  if (ia->fields_tree_view == NULL)
    {
      GtkWidget *scroller = get_widget_assert (ia->builder, "fields-scroller");
      ia->fields_tree_view = gtk_tree_view_new ();
      g_object_set (ia->fields_tree_view, "enable-search", FALSE, NULL);
      gtk_container_add (GTK_CONTAINER (scroller), GTK_WIDGET (ia->fields_tree_view));
      gtk_widget_show_all (scroller);
    }

  g_signal_connect (ia->quote_combo, "changed",
                    G_CALLBACK (on_quote_combo_change), ia);
  g_signal_connect (ia->quote_cb, "toggled",
                    G_CALLBACK (on_quote_cb_toggle), ia);
  g_signal_connect (ia->custom_entry, "notify::text",
                    G_CALLBACK (on_separators_custom_entry_notify), ia);
  g_signal_connect (ia->custom_cb, "toggled",
                    G_CALLBACK (on_separators_custom_cb_toggle), ia);
  for (i = 0; i < SEPARATOR_CNT; i++)
    g_signal_connect (get_widget_assert (builder, separators[i].name),
                      "toggled", G_CALLBACK (on_separator_toggle), ia);

}






static struct casereader_random_class my_casereader_class;

static struct ccase *
my_read (struct casereader *reader, void *aux, casenumber idx)
{
  PsppireImportAssistant *ia = PSPPIRE_IMPORT_ASSISTANT (aux);
  GtkTreeModel *tm = GTK_TREE_MODEL (ia->delimiters_model);

  GtkTreePath *tp = gtk_tree_path_new_from_indices (idx, -1);

  const struct caseproto *proto = casereader_get_proto (reader);

  GtkTreeIter iter;
  struct ccase *c = NULL;
  if (gtk_tree_model_get_iter (tm, &iter, tp))
    {
      c = case_create (proto);
      int i;
      for (i = 0 ; i < caseproto_get_n_widths (proto); ++i)
	{
	  GValue value = {0};
	  gtk_tree_model_get_value (tm, &iter, i + 1, &value);

	  const struct variable *var = dict_get_var (ia->casereader_dict, i);

	  const gchar *ss = g_value_get_string (&value);
	  if (ss)
	    {
	      union value *v = case_data_rw (c, var);
	      /* In this reader we derive the union value from the
		 string in the tree_model. We retrieve the width and format
		 from a dictionary which is stored directly after
		 the reader creation. Changes in ia->dict in the
		 variable window are not reflected here and therefore
		 this is always compatible with the width in the
		 caseproto. See bug #58298 */
	      char *xx = data_in (ss_cstr (ss),
				  "UTF-8",
				  var_get_write_format (var)->type,
				  v, var_get_width (var), "UTF-8");

	      /* if (xx) */
	      /*   g_print ("%s:%d Err %s\n", __FILE__, __LINE__, xx); */
	      free (xx);
	    }
	  g_value_unset (&value);
	}
    }

  gtk_tree_path_free (tp);

  return c;
}

static void
my_destroy (struct casereader *reader, void *aux)
{
  g_print ("%s:%d %p\n", __FILE__, __LINE__, reader);
}

static void
my_advance (struct casereader *reader, void *aux, casenumber cnt)
{
  g_print ("%s:%d\n", __FILE__, __LINE__);
}

static struct casereader *
textfile_create_reader (PsppireImportAssistant *ia)
{
  int n_vars = dict_get_var_cnt (ia->dict);

  int i;

  struct fmt_guesser **fg = XCALLOC (n_vars,  struct fmt_guesser *);
  for (i = 0 ; i < n_vars; ++i)
    {
      fg[i] = fmt_guesser_create ();
    }

  gint n_rows = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (ia->delimiters_model), NULL);

  GtkTreeIter iter;
  gboolean ok;
  for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (ia->delimiters_model), &iter);
       ok;
       ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (ia->delimiters_model), &iter))
    {
      for (i = 0 ; i < n_vars; ++i)
	{
	  gchar *s = NULL;
	  gtk_tree_model_get (GTK_TREE_MODEL (ia->delimiters_model), &iter, i+1, &s, -1);
	  if (s)
	    fmt_guesser_add (fg[i], ss_cstr (s));
	  free (s);
	}
    }

  struct caseproto *proto = caseproto_create ();
  for (i = 0 ; i < n_vars; ++i)
    {
      struct fmt_spec fs;
      fmt_guesser_guess (fg[i], &fs);

      fmt_fix (&fs, FMT_FOR_INPUT);

      struct variable *var = dict_get_var (ia->dict, i);

      int width = fmt_var_width (&fs);

      var_set_width_and_formats (var, width,
				 &fs, &fs);

      proto = caseproto_add_width (proto, width);
      fmt_guesser_destroy (fg[i]);
    }

  free (fg);

  struct casereader *cr = casereader_create_random (proto, n_rows, &my_casereader_class,  ia);
  /* Store the dictionary at this point when the casereader is created.
     my_read depends on the dictionary to interpret the strings in the treeview.
     This guarantees that the union value is produced according to the
     caseproto in the reader. */
  ia->casereader_dict = dict_clone (ia->dict);
  caseproto_unref (proto);
  return cr;
}

/* When during import the variable type is changed, the reader is reinitialized
   based on the new dictionary with a fresh caseprototype. The default behaviour
   when a variable type is changed and the column is resized is that the union
   value is interpreted with new variable type and an overlay for that column
   is generated. Here we reinit to the original reader based on strings.
   As a result you can switch from string to numeric to string without loosing
   the string information. */
static void
ia_variable_changed_cb (GObject *obj, gint var_num, guint what,
			const struct variable *oldvar, gpointer data)
{
  PsppireImportAssistant *ia  = PSPPIRE_IMPORT_ASSISTANT (data);

  struct caseproto *proto = caseproto_create();
  for (int i = 0; i < dict_get_var_cnt (ia->dict); i++)
    {
      const struct variable *var = dict_get_var (ia->dict, i);
      int width = var_get_width (var);
      proto = caseproto_add_width (proto, width);
    }

  gint n_rows = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (ia->delimiters_model), NULL);

  PsppireDataStore *store = NULL;
  g_object_get (ia->data_sheet, "data-model", &store, NULL);

  struct casereader *cr = casereader_create_random (proto, n_rows,
						    &my_casereader_class, ia);
  psppire_data_store_set_reader (store, cr);
  dict_unref (ia->casereader_dict);
  ia->casereader_dict = dict_clone (ia->dict);
}

/* Called just before the formats page of the assistant is
   displayed. */
static void
prepare_formats_page (PsppireImportAssistant *ia)
{
  my_casereader_class.read = my_read;
  my_casereader_class.destroy = my_destroy;
  my_casereader_class.advance = my_advance;

  if (ia->spreadsheet)
    {
      GtkBuilder *builder = ia->builder;
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
  else
    {
      struct casereader *reader = textfile_create_reader (ia);

      PsppireDict *dict = psppire_dict_new_from_dict (ia->dict);
      PsppireDataStore *store = psppire_data_store_new (dict);
      psppire_data_store_set_reader (store, reader);
      g_signal_connect (dict, "variable-changed",
                        G_CALLBACK (ia_variable_changed_cb),
                        ia);

      g_object_set (ia->data_sheet, "data-model", store, NULL);
      g_object_set (ia->var_sheet, "data-model", dict, NULL);
    }

  gint pmax;
  g_object_get (get_widget_assert (ia->builder, "vpaned1"),
		"max-position", &pmax, NULL);


  g_object_set (get_widget_assert (ia->builder, "vpaned1"),
		"position", pmax / 2, NULL);

  gtk_widget_show (ia->paste_button);
}

static void
formats_page_create (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->builder;

  GtkWidget *w = get_widget_assert (builder, "Formats");
  g_object_set_data (G_OBJECT (w), "on-entering", prepare_formats_page);
  g_object_set_data (G_OBJECT (w), "on-reset", reset_formats_page);

  GtkWidget *vars_scroller = get_widget_assert (builder, "vars-scroller");
  if (ia->var_sheet == NULL)
    {
      ia->var_sheet = psppire_variable_sheet_new ();

      gtk_container_add (GTK_CONTAINER (vars_scroller), ia->var_sheet);

      ia->dict = dict_create (get_default_encoding ());

      gtk_widget_show_all (vars_scroller);
    }
  GtkWidget *data_scroller = get_widget_assert (builder, "data-scroller");
  if (ia->data_sheet == NULL)
    {
      ia->data_sheet = psppire_data_sheet_new ();
      g_object_set (ia->data_sheet, "editable", FALSE, NULL);

      gtk_container_add (GTK_CONTAINER (data_scroller), ia->data_sheet);

      gtk_widget_show_all (data_scroller);
    }

  add_page_to_assistant (ia, w,
			 GTK_ASSISTANT_PAGE_CONFIRM, _("Adjust Variable Formats"));
}




static void
separators_append_syntax (const PsppireImportAssistant *ia, struct string *s)
{
  int i;

  ds_put_cstr (s, "  /DELIMITERS=\"");

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (get_widget_assert (ia->builder, "tab"))))
    ds_put_cstr (s, "\\t");
  for (i = 0; i < SEPARATOR_CNT; i++)
    {
      const struct separator *seps = &separators[i];
      GtkWidget *button = get_widget_assert (ia->builder, seps->name);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
	{
	  if (seps->c == '\t')
	    continue;

	  ds_put_byte (s, seps->c);
	}
    }
  ds_put_cstr (s, "\"\n");

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->quote_cb)))
    {
      GtkComboBoxText *cbt = GTK_COMBO_BOX_TEXT (ia->quote_combo);
      gchar *quotes = gtk_combo_box_text_get_active_text (cbt);
      if (quotes && *quotes)
        syntax_gen_pspp (s, "  /QUALIFIER=%sq\n", quotes);
      free (quotes);
    }
}

static void
formats_append_syntax (const PsppireImportAssistant *ia, struct string *s)
{
  int i;
  int var_cnt;

  g_return_if_fail (ia->dict);

  ds_put_cstr (s, "  /VARIABLES=\n");

  var_cnt = dict_get_var_cnt (ia->dict);
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *var = dict_get_var (ia->dict, i);
      char format_string[FMT_STRING_LEN_MAX + 1];
      fmt_to_string (var_get_print_format (var), format_string);
      ds_put_format (s, "    %s %s%s\n",
		     var_get_name (var), format_string,
		     i == var_cnt - 1 ? "." : "");
    }
}

static void
first_line_append_syntax (const PsppireImportAssistant *ia, struct string *s)
{
  gint first_case = 0;
  g_object_get (ia->delimiters_model, "first-line", &first_case, NULL);

  if (first_case > 0)
    ds_put_format (s, "  /FIRSTCASE=%d\n", first_case + 1);
}

static void
intro_append_syntax (const PsppireImportAssistant *ia, struct string *s)
{
  gint first_line = 0;
  g_object_get (ia->delimiters_model, "first-line", &first_line, NULL);

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->n_cases_button)))
    ds_put_format (s, "SELECT IF ($CASENUM <= %d).\n",
		   gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (ia->n_cases_spin)) - first_line);
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->percent_button)))
    ds_put_format (s, "SAMPLE %.4g.\n",
		   gtk_spin_button_get_value (GTK_SPIN_BUTTON (ia->percent_spin)) / 100.0);
}


/* Emits PSPP syntax to S that applies the dictionary attributes
   (such as missing values and value labels) of the variables in
   DICT.  */
static void
apply_dict (const struct dictionary *dict, struct string *s)
{
  size_t var_cnt = dict_get_var_cnt (dict);
  size_t i;

  for (i = 0; i < var_cnt; i++)
    {
      struct variable *var = dict_get_var (dict, i);
      const char *name = var_get_name (var);
      enum val_type type = var_get_type (var);
      int width = var_get_width (var);
      enum measure measure = var_get_measure (var);
      enum var_role role = var_get_role (var);
      enum alignment alignment = var_get_alignment (var);
      const struct fmt_spec *format = var_get_print_format (var);

      if (var_has_missing_values (var))
        {
          const struct missing_values *mv = var_get_missing_values (var);
          size_t j;

          syntax_gen_pspp (s, "MISSING VALUES %ss (", name);
          for (j = 0; j < mv_n_values (mv); j++)
            {
              if (j)
                ds_put_cstr (s, ", ");
              syntax_gen_value (s, mv_get_value (mv, j), width, format);
            }

          if (mv_has_range (mv))
            {
              double low, high;
              if (mv_has_value (mv))
                ds_put_cstr (s, ", ");
              mv_get_range (mv, &low, &high);
              syntax_gen_num_range (s, low, high, format);
            }
          ds_put_cstr (s, ").\n");
        }
      if (var_has_value_labels (var))
        {
          const struct val_labs *vls = var_get_value_labels (var);
          const struct val_lab **labels = val_labs_sorted (vls);
          size_t n_labels = val_labs_count (vls);
          size_t i;

          syntax_gen_pspp (s, "VALUE LABELS %ss", name);
          for (i = 0; i < n_labels; i++)
            {
              const struct val_lab *vl = labels[i];
              ds_put_cstr (s, "\n  ");
              syntax_gen_value (s, &vl->value, width, format);
              ds_put_byte (s, ' ');
              syntax_gen_string (s, ss_cstr (val_lab_get_escaped_label (vl)));
            }
          free (labels);
          ds_put_cstr (s, ".\n");
        }
      if (var_has_label (var))
        syntax_gen_pspp (s, "VARIABLE LABELS %ss %sq.\n",
                         name, var_get_label (var));
      if (measure != var_default_measure (type))
        syntax_gen_pspp (s, "VARIABLE LEVEL %ss (%ss).\n",
                         name, measure_to_syntax (measure));
      if (role != ROLE_INPUT)
        syntax_gen_pspp (s, "VARIABLE ROLE /%ss %ss.\n",
                         var_role_to_syntax (role), name);
      if (alignment != var_default_alignment (type))
        syntax_gen_pspp (s, "VARIABLE ALIGNMENT %ss (%ss).\n",
                         name, alignment_to_syntax (alignment));
      if (var_get_display_width (var) != var_default_display_width (width))
        syntax_gen_pspp (s, "VARIABLE WIDTH %ss (%d).\n",
                         name, var_get_display_width (var));
    }
}



static void
sheet_spec_gen_syntax (PsppireImportAssistant *ia, struct string *s)
{
  GtkBuilder *builder = ia->builder;
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
		   (ia->spreadsheet->type == SPREADSHEET_GNUMERIC) ? "GNM" : "ODS",
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
      gchar *file_name = NULL;
      gchar *encoding = NULL;
      g_object_get (ia->text_file,
		    "file-name", &file_name,
		    "encoding", &encoding,
		    NULL);

      if (file_name == NULL)
	return NULL;

      syntax_gen_pspp (&s,
		       "GET DATA"
		       "\n  /TYPE=TXT"
		       "\n  /FILE=%sq\n",
		       file_name);
      if (encoding && strcmp (encoding, "Auto"))
	syntax_gen_pspp (&s, "  /ENCODING=%sq\n", encoding);

      ds_put_cstr (&s,
		   "  /ARRANGEMENT=DELIMITED\n"
		   "  /DELCASE=LINE\n");

      first_line_append_syntax (ia, &s);
      separators_append_syntax (ia, &s);

      formats_append_syntax (ia, &s);
      apply_dict (ia->dict, &s);
      intro_append_syntax (ia, &s);
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
