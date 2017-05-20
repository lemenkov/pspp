/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2015, 2016, 2017  Free Software Foundation

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

#include <errno.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <sys/stat.h>

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

#include "gl/intprops.h"

#include "libpspp/i18n.h"
#include "libpspp/line-reader.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "builder-wrapper.h"
#include "helper.h"
#include "psppire-import-assistant.h"
#include "psppire-scanf.h"
#include "psppire-dialog.h"
#include "psppire-empty-list-store.h"
#include "psppire-encoding-selector.h"
#include "psppire-spreadsheet-model.h"
#include "psppire-text-file.h"
#include "psppire-delimited-text.h"
#include "psppire-data-sheet.h"
#include "psppire-data-store.h"
#include "psppire-dict.h"
#include "psppire-variable-sheet.h"

#include "ui/syntax-gen.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum { MAX_LINE_LEN = 16384 }; /* Max length of an acceptable line. */


/* Sets IA's separators substructure to match the widgets. */
static void get_separators (PsppireImportAssistant *ia);
static void split_fields (PsppireImportAssistant *ia);

/* Chooses a name for each column on the separators page */
static void choose_column_names (PsppireImportAssistant *ia);



static void intro_page_create (PsppireImportAssistant *ia);
static void first_line_page_create (PsppireImportAssistant *ia);

static void separators_page_create (PsppireImportAssistant *ia);
static void formats_page_create (PsppireImportAssistant *ia);

static void push_watch_cursor (PsppireImportAssistant *ia);
static void pop_watch_cursor (PsppireImportAssistant *ia);



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

  g_object_unref (ia->builder);

  g_object_unref (ia->prop_renderer);
  g_object_unref (ia->fixed_renderer);

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
  push_watch_cursor (ia);

  //  get_separators (ia);
  //  split_fields (ia);
  choose_column_names (ia);

  pop_watch_cursor (ia);
}


#if SHEET_MERGE

/* Chooses the most common character among those in TARGETS,
   based on the frequency data in HISTOGRAM, and stores it in
   RESULT.  If there is a tie for the most common character among
   those in TARGETS, the earliest character is chosen.  If none
   of the TARGETS appear at all, then DEF is used as a
   fallback. */
static void
find_commonest_chars (unsigned long int histogram[UCHAR_MAX + 1],
                      const char *targets, const char *def,
                      struct string *result)
{
  unsigned char max = 0;
  unsigned long int max_count = 0;

  for (; *targets != '\0'; targets++)
    {
      unsigned char c = *targets;
      unsigned long int count = histogram[c];
      if (count > max_count)
        {
          max = c;
          max_count = count;
        }
    }
  if (max_count > 0)
    {
      ds_clear (result);
      ds_put_byte (result, max);
    }
  else
    ds_assign_cstr (result, def);
}

/* Picks the most likely separator and quote characters based on
   IA's file data. */
static void
choose_likely_separators (PsppireImportAssistant *ia)
{
  unsigned long int histogram[UCHAR_MAX + 1] = { 0 };

  /* Construct a histogram of all the characters used in the
     file. */
  gboolean valid;
  GtkTreeIter iter;
  for (valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (ia->text_file), &iter);
       valid;
       valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (ia->text_file), &iter))
    {
      gchar *xxx = 0;
      gtk_tree_model_get (GTK_TREE_MODEL (ia->text_file), &iter, 1, &xxx, -1);
      struct substring line = ss_cstr (xxx);
      size_t length = ss_length (line);
      size_t i;
      for (i = 0; i < length; i++)
        histogram[(unsigned char) line.string[i]]++;
      g_free (xxx);
    }

  find_commonest_chars (histogram, "\"'", "", &ia->quotes);
  find_commonest_chars (histogram, ",;:/|!\t-", ",", &ia->separators);
}

#endif

static void set_separators (PsppireImportAssistant *ia);


static void
repopulate_delimiter_columns (PsppireImportAssistant *ia)
{
  /* Remove all the columns */
  while (gtk_tree_view_get_n_columns (GTK_TREE_VIEW (ia->fields_tree_view)) > 0)
    {
      GtkTreeViewColumn *tvc = gtk_tree_view_get_column (GTK_TREE_VIEW (ia->fields_tree_view), 0);
      gtk_tree_view_remove_column (GTK_TREE_VIEW (ia->fields_tree_view), tvc);
    }

  gint n_fields = gtk_tree_model_get_n_columns (ia->delimiters_model);

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
  gtk_tree_view_set_model (GTK_TREE_VIEW (ia->fields_tree_view), ia->delimiters_model);

  g_signal_connect_swapped (ia->delimiters_model, "notify::delimiters",
  			G_CALLBACK (reset_tree_view_model), ia);


  repopulate_delimiter_columns (ia);

  revise_fields_preview (ia);
  //  choose_likely_separators (ia);
  //  set_separators (ia);
}

struct separator
{
  const char *name;           /* Name (for use with get_widget_assert). */
  gunichar c;                 /* Separator character. */
};

/* All the separators in the dialog box. */
static const struct separator separators[] =
  {
    {"space", ' '},
    {"tab", '\t'},
    {"bang", '!'},
    {"colon", ':'},
    {"comma", ','},
    {"hyphen", '-'},
    {"pipe", '|'},
    {"semicolon", ';'},
    {"slash", '/'},
  };
#define SEPARATOR_CNT (sizeof separators / sizeof *separators)


#if SHEET_MERGE

/* Sets the widgets to match IA's separators substructure. */
static void
set_separators (PsppireImportAssistant *ia)
{
  unsigned int seps;
  struct string custom;
  bool any_custom;
  bool any_quotes;
  size_t i;

  ds_init_empty (&custom);
  seps = 0;
  for (i = 0; i < ds_length (&ia->separators); i++)
    {
      unsigned char c = ds_at (&ia->separators, i);
      int j;

      for (j = 0; j < SEPARATOR_CNT; j++)
        {
          const struct separator *s = &separators[j];
          if (s->c == c)
            {
              seps += 1u << j;
              goto next;
            }
        }

      ds_put_byte (&custom, c);
    next:;
    }

  for (i = 0; i < SEPARATOR_CNT; i++)
    {
      const struct separator *s = &separators[i];
      GtkWidget *button = get_widget_assert (ia->builder, s->name);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button),
                                    (seps & (1u << i)) != 0);
    }
  any_custom = !ds_is_empty (&custom);
  gtk_entry_set_text (GTK_ENTRY (ia->custom_entry), ds_cstr (&custom));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->custom_cb),
                                any_custom);
  gtk_widget_set_sensitive (ia->custom_entry, any_custom);
  ds_destroy (&custom);

  any_quotes = !ds_is_empty (&ia->quotes);

  gtk_entry_set_text (ia->quote_entry,
                      any_quotes ? ds_cstr (&ia->quotes) : "\"");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->quote_cb),
                                any_quotes);
  gtk_widget_set_sensitive (ia->quote_combo, any_quotes);
}

#endif

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
  size_t i;

  for (i = 0; i < ia->modified_var_cnt; i++)
    var_destroy (ia->modified_vars[i]);
  free (ia->modified_vars);
  ia->modified_vars = NULL;
  ia->modified_var_cnt = 0;
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


/* Increments the "watch cursor" level, setting the cursor for
   the assistant window to a watch face to indicate to the user
   that the ongoing operation may take some time. */
static void
push_watch_cursor (PsppireImportAssistant *ia)
{
  if (++ia->watch_cursor == 1)
    {
      GtkWidget *widget = GTK_WIDGET (ia);
      GdkDisplay *display = gtk_widget_get_display (widget);
      GdkCursor *cursor = gdk_cursor_new_for_display (display, GDK_WATCH);
      gdk_window_set_cursor (gtk_widget_get_window (widget), cursor);
      g_object_unref (cursor);
      gdk_display_flush (display);
    }
}

/* Decrements the "watch cursor" level.  If the level reaches
   zero, the cursor is reset to its default shape. */
static void
pop_watch_cursor (PsppireImportAssistant *ia)
{
  if (--ia->watch_cursor == 0)
    {
      GtkWidget *widget = GTK_WIDGET (ia);
      gdk_window_set_cursor (gtk_widget_get_window (widget), NULL);
    }
}

#if SHEET_MERGE
static gint
get_string_width (GtkWidget *treeview, GtkCellRenderer *renderer,
		  const char *string)
{
  gint width;
  g_object_set (G_OBJECT (renderer), "text", string, (void *) NULL);
  gtk_cell_renderer_get_preferred_width (renderer, treeview,
					 NULL, &width);
  return width;
}

static gint
get_monospace_width (GtkWidget *treeview, GtkCellRenderer *renderer,
                     size_t char_cnt)
{
  struct string s;
  gint width;

  ds_init_empty (&s);
  ds_put_byte_multiple (&s, '0', char_cnt);
  ds_put_byte (&s, ' ');
  width = get_string_width (treeview, renderer, ds_cstr (&s));
  ds_destroy (&s);

  return width;
}

static void
set_model_on_treeview (PsppireImportAssistant *ia, GtkWidget *tree_view, size_t first_line)
{
  GtkTreeModel *model = GTK_TREE_MODEL (psppire_empty_list_store_new (ia->line_cnt - first_line));

  g_object_set_data (G_OBJECT (model), "lines", &ia->lines + first_line);
  g_object_set_data (G_OBJECT (model), "first-line", GINT_TO_POINTER (first_line));

  pspp_sheet_view_set_model (PSPP_SHEET_VIEW (tree_view), model);

  g_object_unref (model);
}

static GtkWidget *
make_tree_view (const PsppireImportAssistant *ia)
{
  GtkWidget *tree_view = pspp_sheet_view_new ();
  pspp_sheet_view_set_grid_lines (PSPP_SHEET_VIEW (tree_view), PSPP_SHEET_VIEW_GRID_LINES_BOTH);

  add_line_number_column (ia, tree_view);

  return tree_view;
}
#endif

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

  gtk_combo_box_set_model (GTK_COMBO_BOX (sheet_entry),
			   psppire_spreadsheet_model_new (ia->spreadsheet));

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

  if (f && !g_file_test (f, G_FILE_TEST_IS_DIR))
    {
      gtk_assistant_set_page_complete (GTK_ASSISTANT(ia), GTK_WIDGET (fc), TRUE);

      if (ia->spreadsheet)
	spreadsheet_unref (ia->spreadsheet);

      ia->spreadsheet = gnumeric_probe (f, FALSE);

      if (!ia->spreadsheet)
	ia->spreadsheet = ods_probe (f, FALSE);

      if (!ia->spreadsheet)
	{
	  intro_page_create (ia);
	  first_line_page_create (ia);
	  separators_page_create (ia);
	}
      else
	{
	  sheet_spec_page_create (ia);
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
  g_print ("%s:%d %s\n", __FILE__, __LINE__, __FUNCTION__);
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
chooser_page_create (PsppireImportAssistant *ia)
{
  GtkFileFilter *filter = NULL;

  GtkWidget *chooser = gtk_file_chooser_widget_new (GTK_FILE_CHOOSER_ACTION_OPEN);

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
  /* ia->column_cnt = 0; */
  /* ia->columns = NULL; */

  ia->file_name = NULL;

  ia->spreadsheet = NULL;
  ia->watch_cursor = 0;

  ia->prop_renderer = gtk_cell_renderer_text_new ();
  g_object_ref_sink (ia->prop_renderer);
  ia->fixed_renderer = gtk_cell_renderer_text_new ();
  g_object_ref_sink (ia->fixed_renderer);
  g_object_set (G_OBJECT (ia->fixed_renderer),
                "family", "Monospace",
                (void *) NULL);

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
                            gtk_toggle_button_get_active (
							  GTK_TOGGLE_BUTTON (p->n_cases_button)));

  gtk_widget_set_sensitive (p->percent_spin,
                            gtk_toggle_button_get_active (
							  GTK_TOGGLE_BUTTON (p->percent_button)));
}


#if SHEET_MERGE


/* Sets IA's first_line substructure to match the widgets. */
static void
set_first_line_options (PsppireImportAssistant *ia)
{
  GtkTreeIter iter;
  GtkTreeModel *model;

  PsppSheetSelection *selection = pspp_sheet_view_get_selection (PSPP_SHEET_VIEW (ia->tree_view));
  if (pspp_sheet_selection_get_selected (selection, &model, &iter))
    {
      GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
      int row = gtk_tree_path_get_indices (path)[0];
      gtk_tree_path_free (path);

      ia->skip_lines = row;
      ia->variable_names =
        (ia->skip_lines > 0
         && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->variable_names_cb)));
    }

  gtk_widget_set_sensitive (ia->variable_names_cb, ia->skip_lines > 0);
}



static void
reset_first_line_page (PsppireImportAssistant *ia)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ia->variable_names_cb), FALSE);
  PsppSheetSelection *selection = pspp_sheet_view_get_selection (PSPP_SHEET_VIEW (ia->tree_view));
  pspp_sheet_selection_unselect_all (selection);
  gtk_widget_set_sensitive (ia->variable_names_cb, FALSE);
}

#endif

static void
on_treeview_selection_change (PsppireImportAssistant *ia)
{
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (ia->first_line_tree_view));
  GtkTreeModel *model = NULL;
  GtkTreeIter iter;
  if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
      int n;
      GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
      gint *index = gtk_tree_path_get_indices (path);

      n = *index;

      gtk_tree_path_free (path);

      gtk_widget_set_sensitive (ia->variable_names_cb, n > 0);

      ia->delimiters_model
	= psppire_delimited_text_new (GTK_TREE_MODEL (ia->text_file));
      g_object_set (ia->delimiters_model, "first-line", n, NULL);

      //      ia->skip_lines = n;
    }
}


/* Initializes IA's first_line substructure. */
static void
first_line_page_create (PsppireImportAssistant *ia)
{
  g_print ("%s:%d %s\n", __FILE__, __LINE__, __FUNCTION__);
  GtkWidget *w =  get_widget_assert (ia->builder, "FirstLine");

  g_object_set_data (G_OBJECT (w), "on-entering", on_treeview_selection_change);

  add_page_to_assistant (ia, w,
			 GTK_ASSISTANT_PAGE_CONTENT, _("Select the First Line"));

  GtkWidget *scrolled_window = get_widget_assert (ia->builder, "first-line-scroller");

  if (ia->first_line_tree_view == NULL)
    {
      ia->first_line_tree_view = gtk_tree_view_new ();

      gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (ia->first_line_tree_view), TRUE);

      GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
      GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes (_("Line"), renderer,
									    "text", 0,
									    NULL);
      gtk_tree_view_append_column (GTK_TREE_VIEW (ia->first_line_tree_view), column);

      renderer = gtk_cell_renderer_text_new ();
      column = gtk_tree_view_column_new_with_attributes (_("Text"), renderer, "text", 1, NULL);
      gtk_tree_view_append_column (GTK_TREE_VIEW (ia->first_line_tree_view), column);

      gtk_container_add (GTK_CONTAINER (scrolled_window), ia->first_line_tree_view);

      g_signal_connect_swapped (ia->first_line_tree_view, "cursor-changed",
			G_CALLBACK (on_treeview_selection_change), ia);
    }
  gtk_widget_show_all (scrolled_window);

  ia->variable_names_cb = get_widget_assert (ia->builder, "variable-names");

#if SHEET_MERGE
  pspp_sheet_selection_set_mode (
				 pspp_sheet_view_get_selection (PSPP_SHEET_VIEW (ia->tree_view)),
				 PSPP_SHEET_SELECTION_BROWSE);
  pspp_sheet_view_set_rubber_banding (PSPP_SHEET_VIEW (ia->tree_view), TRUE);


  g_signal_connect_swapped (pspp_sheet_view_get_selection (PSPP_SHEET_VIEW (ia->tree_view)),
			    "changed", G_CALLBACK (set_first_line_options), ia);

  g_signal_connect_swapped (ia->variable_names_cb, "toggled",
			    G_CALLBACK (set_first_line_options), ia);

  g_object_set_data (G_OBJECT (w), "on-reset", reset_first_line_page);
#endif
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

  GtkWidget *w  =  gtk_grid_get_child_at (GTK_GRID (table), 1, 1);
  int old_value = w ? gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (ia->n_cases_spin)) : 1;
  if (w)
    gtk_container_remove (GTK_CONTAINER (table), w);

  w  =  gtk_grid_get_child_at (GTK_GRID (table), 1, 2);
  if (w)
    gtk_container_remove (GTK_CONTAINER (table), w);


  GtkWidget *hbox_n_cases = psppire_scanf_new (_("Only the first %4d cases"), &ia->n_cases_spin);

  GtkAdjustment *adj = gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (ia->n_cases_spin));
  gtk_adjustment_set_lower (adj, 1.0);

  if (ia->text_file)
    {
      if (psppire_text_file_get_total_exact (ia->text_file))
	{
	  gulong total_lines = psppire_text_file_get_n_lines (ia->text_file);
	  gtk_adjustment_set_upper (adj, total_lines);
	  gtk_adjustment_set_value (adj, old_value);
	}
      else
	gtk_adjustment_set_upper (adj, DBL_MAX);
    }
  gtk_grid_attach (GTK_GRID (table), hbox_n_cases,
		   1, 1,
		   1, 1);


  GtkWidget *hbox_percent = psppire_scanf_new (_("Only the first %3d %% of file (approximately)"),
					       &ia->percent_spin);

  gtk_grid_attach (GTK_GRID (table), hbox_percent,
		   1, 2,
		   1, 1);

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





struct column
{
  /* Variable name for this column.  This is the variable name
     used on the separators page; it can be overridden by the
     user on the formats page. */
  char *name;

  /* Maximum length of any row in this column. */
  size_t width;

  /* Contents of this column: contents[row] is the contents for
     the given row.

     A null substring indicates a missing column for that row
     (because the line contains an insufficient number of
     separators).

     contents[] elements may be substrings of the lines[]
     strings that represent the whole lines of the file, to
     save memory.  Other elements are dynamically allocated
     with ss_alloc_substring. */
  struct substring *contents;
};

#if SHEET_MERGE

/* Called to render one of the cells in the fields preview tree
   view. */
static void
render_input_cell (PsppSheetViewColumn *tree_column, GtkCellRenderer *cell,
                   GtkTreeModel *model, GtkTreeIter *iter,
                   gpointer ia_)
{
  PsppireImportAssistant *ia = ia_;
  struct substring field;
  size_t row;
  gint column;

  column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
                                               "column-number"));
  row = empty_list_store_iter_to_row (iter) + ia->skip_lines;
  field = ia->columns[column].contents[row];
  if (field.string != NULL)
    {
      GValue text = {0, };
      g_value_init (&text, G_TYPE_STRING);
      g_value_take_string (&text, ss_xstrdup (field));
      g_object_set_property (G_OBJECT (cell), "text", &text);
      g_value_unset (&text);
      g_object_set (cell, "background-set", FALSE, (void *) NULL);
    }
  else
    g_object_set (cell,
                  "text", "",
                  "background", "red",
                  "background-set", TRUE,
                  (void *) NULL);
}

/* Parses the contents of the field at (ROW,COLUMN) according to
   its variable format.  If OUTPUTP is non-null, then *OUTPUTP
   receives the formatted output for that field (which must be
   freed with free).  If TOOLTIPP is non-null, then *TOOLTIPP
   receives a message suitable for use in a tooltip, if one is
   needed, or a null pointer otherwise.  Returns TRUE if a
   tooltip message is needed, otherwise FALSE. */
static bool
parse_field (PsppireImportAssistant *ia,
             size_t row, size_t column,
             char **outputp, char **tooltipp)
{
  const struct fmt_spec *in;
  struct fmt_spec out;
  char *tooltip;
  bool ok;

  struct substring field = ia->columns[column].contents[row];
  const struct variable *var = dict_get_var (ia->dict, column);
  union value val;

  value_init (&val, var_get_width (var));
  in = var_get_print_format (var);
  out = fmt_for_output_from_input (in);
  tooltip = NULL;
  if (field.string != NULL)
    {
      char *error = data_in (field, "UTF-8", in->type, &val, var_get_width (var),
			     dict_get_encoding (ia->dict));
      if (error != NULL)
        {
          tooltip = xasprintf (_("Cannot parse field content `%.*s' as "
                                 "format %s: %s"),
                               (int) field.length, field.string,
                               fmt_name (in->type), error);
          free (error);
        }
    }
  else
    {
      tooltip = xstrdup (_("This input line has too few separators "
                           "to fill in this field."));
      value_set_missing (&val, var_get_width (var));
    }
  if (outputp != NULL)
    {
      *outputp = data_out (&val, dict_get_encoding (ia->dict),  &out);
    }
  value_destroy (&val, var_get_width (var));

  ok = tooltip == NULL;
  if (tooltipp != NULL)
    *tooltipp = tooltip;
  else
    free (tooltip);
  return ok;
}

/* Called to render one of the cells in the data preview tree
   view. */
static void
render_output_cell (PsppSheetViewColumn *tree_column,
                    GtkCellRenderer *cell,
                    GtkTreeModel *model,
                    GtkTreeIter *iter,
                    gpointer ia_)
{
  PsppireImportAssistant *ia = ia_;
  char *output;
  GValue gvalue = { 0, };
  bool ok = parse_field (ia,
			 (empty_list_store_iter_to_row (iter)
			  + ia->skip_lines),
			 GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
							     "column-number")),
			 &output, NULL);

  g_value_init (&gvalue, G_TYPE_STRING);
  g_value_take_string (&gvalue, output);
  g_object_set_property (G_OBJECT (cell), "text", &gvalue);
  g_value_unset (&gvalue);

  if (ok)
    g_object_set (cell, "background-set", FALSE, (void *) NULL);
  else
    g_object_set (cell,
                  "background", "red",
                  "background-set", TRUE,
                  (void *) NULL);
}


/* Utility functions used by multiple pages of the assistant. */

static gboolean
get_tooltip_location (GtkWidget *widget, gint wx, gint wy,
                      const PsppireImportAssistant *ia,
                      size_t *row, size_t *column)
{
  PsppSheetView *tree_view = PSPP_SHEET_VIEW (widget);
  gint bx, by;
  GtkTreePath *path;
  GtkTreeIter iter;
  PsppSheetViewColumn *tree_column;
  GtkTreeModel *tree_model;
  bool ok;

  pspp_sheet_view_convert_widget_to_bin_window_coords (tree_view,
                                                       wx, wy, &bx, &by);
  if (!pspp_sheet_view_get_path_at_pos (tree_view, bx, by,
					&path, &tree_column, NULL, NULL))
    return FALSE;

  *column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (tree_column),
                                                "column-number"));

  tree_model = pspp_sheet_view_get_model (tree_view);
  ok = gtk_tree_model_get_iter (tree_model, &iter, path);
  gtk_tree_path_free (path);
  if (!ok)
    return FALSE;

  *row = empty_list_store_iter_to_row (&iter) + ia->skip_lines;
  return TRUE;
}






/* Called to render a tooltip on one of the cells in the fields
   preview tree view. */
static gboolean
on_query_input_tooltip (GtkWidget *widget, gint wx, gint wy,
                        gboolean keyboard_mode UNUSED,
                        GtkTooltip *tooltip, PsppireImportAssistant *ia)
{
  size_t row, column;

  if (!get_tooltip_location (widget, wx, wy, ia, &row, &column))
    return FALSE;

  if (ia->columns[column].contents[row].string != NULL)
    return FALSE;

  gtk_tooltip_set_text (tooltip,
                        _("This input line has too few separators "
                          "to fill in this field."));
  return TRUE;
}


/* Called to render a tooltip for one of the cells in the data
   preview tree view. */
static gboolean
on_query_output_tooltip (GtkWidget *widget, gint wx, gint wy,
			 gboolean keyboard_mode UNUSED,
			 GtkTooltip *tooltip, PsppireImportAssistant *ia)
{
  size_t row, column;
  char *text;

  if (!gtk_widget_get_mapped (widget))
    return FALSE;

  if (!get_tooltip_location (widget, wx, wy, ia, &row, &column))
    return FALSE;

  if (parse_field (ia, row, column, NULL, &text))
    return FALSE;

  gtk_tooltip_set_text (tooltip, text);
  free (text);
  return TRUE;
}
#endif



static void
set_quote_list (GtkComboBox *cb)
{
  GtkListStore *list =  gtk_list_store_new (1, G_TYPE_STRING);
  GtkTreeIter iter;
  gint i;
  const gchar *seperator[3] = {"'\"", "\'", "\""};

  for (i = 0; i < 3; i++)
    {
      const gchar *s = seperator[i];

      /* Add a new row to the model */
      gtk_list_store_append (list, &iter);
      gtk_list_store_set (list, &iter,
                          0, s,
                          -1);

    }

  gtk_combo_box_set_model (GTK_COMBO_BOX (cb), GTK_TREE_MODEL (list));
  g_object_unref (list);

  gtk_combo_box_set_entry_text_column (cb, 0);
}


#if SHEET_MERGE

/* Sets IA's separators substructure to match the widgets. */
static void
get_separators (PsppireImportAssistant *ia)
{
  int i;

  ds_clear (&ia->separators);
  for (i = 0; i < SEPARATOR_CNT; i++)
    {
      const struct separator *sep = &separators[i];
      GtkWidget *button = get_widget_assert (ia->builder, sep->name);
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button)))
        ds_put_byte (&ia->separators, sep->c);
    }

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->custom_cb)))
    ds_put_cstr (&ia->separators,
                 gtk_entry_get_text (GTK_ENTRY (ia->custom_entry)));

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->quote_cb)))
    {
      const gchar *text = gtk_entry_get_text (GTK_ENTRY (gtk_bin_get_child (GTK_BIN (ia->quote_combo))));
      ds_assign_cstr (&ia->quotes, text);
    }
  else
    ds_clear (&ia->quotes);
}






/* Breaks the file data in IA into columns based on the
   separators set in IA's separators substructure. */
static void
split_fields (PsppireImportAssistant *ia)
{
  size_t columns_allocated;
  bool space_sep;
  size_t row = 0;

  /* Is space in the set of separators? */
  space_sep = ss_find_byte (ds_ss (&ia->separators), ' ') != SIZE_MAX;

  /* Split all the lines, not just those from
     ia->first_line.skip_lines on, so that we split the line that
     contains variables names if ia->first_line.variable_names is
     TRUE. */
  columns_allocated = 0;

  gint n_lines = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (ia->text_file), NULL);
  GtkTreeIter iter;
  gtk_tree_model_get_iter_first (GTK_TREE_MODEL (ia->text_file), &iter);
  while (gtk_tree_model_iter_next (GTK_TREE_MODEL (ia->text_file), &iter))
    {
      row++;
      gchar *xxx;
      gtk_tree_model_get (GTK_TREE_MODEL (ia->text_file), &iter, 1, &xxx, -1);
      struct substring text = ss_cstr (xxx);
      g_free (xxx);
      size_t column_idx;

      for (column_idx = 0; ;column_idx++)
        {
          struct substring field = SS_EMPTY_INITIALIZER;
          struct column *column;

          if (space_sep)
	    {
	      ss_ltrim (&text, ss_cstr (" "));
	    }
          if (ss_is_empty (text))
            {
              if (column_idx != 0)
                break;
              field = text;
            }
          else if (!ds_is_empty (&ia->quotes)
                   && ds_find_byte (&ia->quotes, text.string[0]) != SIZE_MAX)
            {
              int quote = ss_get_byte (&text);
              struct string s;
              int c;

              ds_init_empty (&s);
              while ((c = ss_get_byte (&text)) != EOF)
                if (c != quote)
                  ds_put_byte (&s, c);
                else if (ss_match_byte (&text, quote))
                  ds_put_byte (&s, quote);
                else
                  break;
              field = ds_ss (&s);
            }
          else
	    {
	      ss_get_bytes (&text, ss_cspan (text, ds_ss (&ia->separators)),
			    &field);
	    }

          if (column_idx >= ia->column_cnt)
            {
              struct column *column;

              if (ia->column_cnt >= columns_allocated)
		{
		  ia->columns = x2nrealloc (ia->columns, &columns_allocated,
					    sizeof *ia->columns);
		}
              column = &ia->columns[ia->column_cnt++];
              column->name = NULL;
              column->width = 0;
              column->contents = xcalloc (n_lines,
                                          sizeof *column->contents);
            }
          column = &ia->columns[column_idx];
          column->contents[row] = field;
          if (ss_length (field) > column->width)
            column->width = ss_length (field);

          if (space_sep)
            ss_ltrim (&text, ss_cstr (" "));
          if (ss_is_empty (text))
            break;
          if (ss_find_byte (ds_ss (&ia->separators), ss_first (text))
              != SIZE_MAX)
            ss_advance (&text, 1);
        }
    }
}

#endif

/* Chooses a name for each column on the separators page */
static void
choose_column_names (PsppireImportAssistant *ia)
{
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->variable_names_cb)))
    {
      int i;
      unsigned long int generated_name_count = 0;
      dict_clear (ia->dict);
      for (i = 0; i < gtk_tree_model_get_n_columns (ia->delimiters_model) - 1; ++i)
	{
	  const gchar *candidate_name =
	    psppire_delimited_text_get_header_title (PSPPIRE_DELIMITED_TEXT (ia->delimiters_model), i);

	  char *name = dict_make_unique_var_name (ia->dict, candidate_name, &generated_name_count);
	  dict_create_var_assert (ia->dict, name, 0);
	  free (name);
	}
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

  //  revise_fields_preview (ia);
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
  ia->quote_entry = GTK_ENTRY (gtk_bin_get_child (GTK_BIN (ia->quote_combo)));
  ia->quote_cb = get_widget_assert (builder, "quote-cb");

  set_quote_list (GTK_COMBO_BOX (ia->quote_combo));

  if (ia->fields_tree_view == NULL)
    {
      GtkWidget *scroller = get_widget_assert (ia->builder, "fields-scroller");
      ia->fields_tree_view = gtk_tree_view_new ();
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





#if SHEET_MERGE

/* Called when the user changes one of the variables in the
   dictionary. */
static void
on_variable_change (PsppireDict *dict, int dict_idx,
		    unsigned int what, const struct variable *oldvar,
                    PsppireImportAssistant *ia)
{
  PsppSheetView *tv = PSPP_SHEET_VIEW (ia->data_tree_view);
  gint column_idx = dict_idx + 1;

  push_watch_cursor (ia);

  /* Remove previous column and replace with new column. */
  pspp_sheet_view_remove_column (tv, pspp_sheet_view_get_column (PSPP_SHEET_VIEW (ia->data_tree_view), column_idx));
  pspp_sheet_view_insert_column (tv, PSPP_SHEET_VIEW_COLUMN (make_data_column (ia, ia->data_tree_view, FALSE, dict_idx)),
                                 column_idx);

  /* Save a copy of the modified variable in modified_vars, so
     that its attributes will be preserved if we back up to the
     previous page with the Prev button and then come back
     here. */
  if (dict_idx >= ia->modified_var_cnt)
    {
      size_t i;
      ia->modified_vars = xnrealloc (ia->modified_vars, dict_idx + 1,
				     sizeof *ia->modified_vars);
      for (i = 0; i <= dict_idx; i++)
        ia->modified_vars[i] = NULL;
      ia->modified_var_cnt = dict_idx + 1;
    }
  if (ia->modified_vars[dict_idx])
    var_destroy (ia->modified_vars[dict_idx]);
  ia->modified_vars[dict_idx]
    = var_clone (psppire_dict_get_variable (dict, dict_idx));

  pop_watch_cursor (ia);
}

#endif


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

	  const struct variable *var = dict_get_var (ia->dict, i);

	  const gchar *ss = g_value_get_string (&value);

	  union value *v = case_data_rw (c, var);
	  char *xx = data_in (ss_cstr (ss),
			      "UTF-8",
			      var_get_write_format (var)->type,
			      v, var_get_width (var), "UTF-8");

	  /* if (xx) */
	  /*   g_print ("%s:%d Err %s\n", __FILE__, __LINE__, xx); */
	  free (xx);
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


static void
foo (struct dictionary *dict, void *aux)
{
  PsppireImportAssistant *ia = PSPPIRE_IMPORT_ASSISTANT (aux);
  g_print ("%s:%d\n", __FILE__, __LINE__);

  struct caseproto *proto = caseproto_create ();

  int i;
  for (i = 0 ; i < dict_get_var_cnt (ia->dict); ++i)
    {
      const struct variable *var = dict_get_var (ia->dict, i);
      proto = caseproto_add_width (proto, var_get_width (var));
    }


  gint n_rows = gtk_tree_model_iter_n_children (ia->delimiters_model, NULL);

  struct casereader *reader =
    casereader_create_random (proto, n_rows, &my_casereader_class,  ia);


  PsppireDataStore *store = NULL;

  g_object_get (ia->data_sheet, "data-model", &store, NULL);

  psppire_data_store_set_reader (store, reader);
}

/* Called just before the formats page of the assistant is
   displayed. */
static void
prepare_formats_page (PsppireImportAssistant *ia)
{
  PsppireDict *dict = psppire_dict_new_from_dict (ia->dict);
  g_object_set (ia->var_sheet, "data-model", dict, NULL);

  my_casereader_class.read = my_read;
  my_casereader_class.destroy = my_destroy;
  my_casereader_class.advance = my_advance;

  struct caseproto *proto = caseproto_create ();
  int i;

  struct fmt_guesser **fg = xcalloc (sizeof *fg, dict_get_var_cnt (ia->dict));
  for (i = 0 ; i < dict_get_var_cnt (ia->dict); ++i)
    {
      fg[i] = fmt_guesser_create ();
    }

  gint n_rows = gtk_tree_model_iter_n_children (ia->delimiters_model, NULL);

  GtkTreeIter iter;
  gboolean ok;
  for (ok = gtk_tree_model_get_iter_first (ia->delimiters_model, &iter);
       ok;
       ok = gtk_tree_model_iter_next (ia->delimiters_model, &iter))
    {
      for (i = 0 ; i < dict_get_var_cnt (ia->dict); ++i)
	{
	  gchar *s = NULL;
	  gtk_tree_model_get (ia->delimiters_model, &iter, i+1, &s, -1);
	  fmt_guesser_add (fg[i], ss_cstr (s));
	  free (s);
	}
    }

  for (i = 0 ; i < dict_get_var_cnt (ia->dict); ++i)
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

  //  dict_set_change_callback (ia->dict, foo, ia);

  struct casereader *reader =
    casereader_create_random (proto, n_rows, &my_casereader_class,  ia);

  PsppireDataStore *store = psppire_data_store_new (dict);
  psppire_data_store_set_reader (store, reader);

  g_object_set (ia->data_sheet, "data-model", store, NULL);


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

      gtk_container_add (GTK_CONTAINER (data_scroller), ia->data_sheet);

      gtk_widget_show_all (data_scroller);
    }

  add_page_to_assistant (ia, w,
			 GTK_ASSISTANT_PAGE_CONFIRM, _("Adjust Variable Formats"));

  ia->modified_vars = NULL;
  ia->modified_var_cnt = 0;
}




static void
separators_append_syntax (const PsppireImportAssistant *ia, struct string *s)
{
  int i;

  ds_put_cstr (s, "  /DELIMITERS=\"");

  if (gtk_toggle_button_get_active (get_widget_assert (ia->builder, "tab")))
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
  if (!ds_is_empty (&ia->quotes))
    syntax_gen_pspp (s, "  /QUALIFIER=%sq\n", ds_cstr (&ia->quotes));
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
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ia->n_cases_button)))
    ds_put_format (s, "N OF CASES %d.\n",
		   gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (ia->n_cases_spin)));
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



static char *
sheet_spec_gen_syntax (PsppireImportAssistant *ia)
{
  GtkBuilder *builder = ia->builder;
  GtkWidget *range_entry = get_widget_assert (builder, "cell-range-entry");
  GtkWidget *sheet_entry = get_widget_assert (builder, "sheet-entry");
  GtkWidget *rnc = get_widget_assert (builder, "readnames-checkbox");
  const gchar *range = gtk_entry_get_text (GTK_ENTRY (range_entry));
  int sheet_index = 1 + gtk_combo_box_get_active (GTK_COMBO_BOX (sheet_entry));
  gboolean read_names = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rnc));

  struct string s = DS_EMPTY_INITIALIZER;

  char *filename;
  g_object_get (ia->text_file, "file-name", &filename, NULL);
  syntax_gen_pspp (&s,
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
      syntax_gen_pspp (&s,
		       "\n  /CELLRANGE=RANGE %sq", range);
    }
  else
    {
      syntax_gen_pspp (&s,
		       "\n  /CELLRANGE=FULL");
    }


  syntax_gen_pspp (&s, ".");


  return ds_cstr (&s);
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
      return sheet_spec_gen_syntax (ia);
    }

  return ds_cstr (&s);
}
