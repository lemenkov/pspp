/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2012, 2014, 2016,
   2020  Free Software Foundation

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
#include <stdlib.h>

#include <gtksourceview/gtksource.h>

#include "language/lexer/lexer.h"
#include "libpspp/encoding-guesser.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "ui/gui/executor.h"
#include "ui/gui/help-menu.h"
#include "ui/gui/helper.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-encoding-selector.h"
#include "ui/gui/psppire-lex-reader.h"
#include "ui/gui/psppire-syntax-window.h"
#include "ui/gui/psppire.h"
#include "ui/gui/windows-menu.h"

#include "gl/localcharset.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_syntax_window_class_init    (PsppireSyntaxWindowClass *class);
static void psppire_syntax_window_init          (PsppireSyntaxWindow      *syntax_editor);


static void psppire_syntax_window_iface_init (PsppireWindowIface *iface);


/* Properties */
enum
{
  PROP_0,
  PROP_ENCODING
};

static void
psppire_syntax_window_set_property (GObject         *object,
                                    guint            prop_id,
                                    const GValue    *value,
                                    GParamSpec      *pspec)
{
  PsppireSyntaxWindow *window = PSPPIRE_SYNTAX_WINDOW (object);

  switch (prop_id)
    {
    case PROP_ENCODING:
      g_free (window->encoding);
      window->encoding = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}


static void
psppire_syntax_window_get_property (GObject         *object,
                                    guint            prop_id,
                                    GValue          *value,
                                    GParamSpec      *pspec)
{
  PsppireSyntaxWindow *window = PSPPIRE_SYNTAX_WINDOW (object);

  switch (prop_id)
    {
    case PROP_ENCODING:
      g_value_set_string (value, window->encoding);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

G_DEFINE_TYPE_WITH_CODE (PsppireSyntaxWindow, psppire_syntax_window, PSPPIRE_TYPE_WINDOW,
                         G_IMPLEMENT_INTERFACE (PSPPIRE_TYPE_WINDOW_MODEL, psppire_syntax_window_iface_init))

static GObjectClass *parent_class ;

static void
psppire_syntax_window_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (parent_class)->finalize)
    (*G_OBJECT_CLASS (parent_class)->finalize) (object);
}


static void
psppire_syntax_window_dispose (GObject *obj)
{
  PsppireSyntaxWindow *sw = PSPPIRE_SYNTAX_WINDOW (obj);

  GtkClipboard *clip_selection;
  GtkClipboard *clip_primary;

  if (sw->dispose_has_run)
    return;

  g_object_unref (sw->search_text_buffer);

  g_free (sw->encoding);
  sw->encoding = NULL;

  clip_selection = gtk_widget_get_clipboard (GTK_WIDGET (sw), GDK_SELECTION_CLIPBOARD);
  clip_primary =   gtk_widget_get_clipboard (GTK_WIDGET (sw), GDK_SELECTION_PRIMARY);

  g_signal_handler_disconnect (clip_primary, sw->sel_handler);

  g_signal_handler_disconnect (clip_selection, sw->ps_handler);

  /* Make sure dispose does not run twice. */
  sw->dispose_has_run = TRUE;

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}



static void
psppire_syntax_window_class_init (PsppireSyntaxWindowClass *class)
{
  GParamSpec *encoding_spec;
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  gobject_class->finalize = psppire_syntax_window_finalize;

  GtkSourceLanguageManager *lm = gtk_source_language_manager_get_default ();

  const gchar * const *existing_paths =  gtk_source_language_manager_get_search_path (lm);
  gchar **new_paths = g_strdupv ((gchar **)existing_paths);
  int n = g_strv_length ((gchar **) existing_paths);

  new_paths = g_realloc (new_paths, (n + 2) * sizeof (*new_paths));
  new_paths[n] = g_strdup (relocate (PKGDATADIR));
  new_paths[n+1] = NULL;

  lm = gtk_source_language_manager_new ();
  gtk_source_language_manager_set_search_path (lm, new_paths);

  class->lan = gtk_source_language_manager_get_language (lm, "pspp");

  if (class->lan == NULL)
    g_warning ("pspp.lang file not found.  Syntax highlighting will not be available.");

  parent_class = g_type_class_peek_parent (class);

  g_strfreev (new_paths);

  encoding_spec =
    null_if_empty_param ("encoding",
                         "Character encoding",
                         "IANA character encoding in this syntax file",
			 NULL,
			 G_PARAM_CONSTRUCT | G_PARAM_READWRITE);

  parent_class = g_type_class_peek_parent (class);

  gobject_class->set_property = psppire_syntax_window_set_property;
  gobject_class->get_property = psppire_syntax_window_get_property;
  gobject_class->dispose = psppire_syntax_window_dispose;

  g_object_class_install_property (gobject_class,
                                   PROP_ENCODING,
                                   encoding_spec);
}


static void
editor_execute_syntax (const PsppireSyntaxWindow *sw, GtkTextIter start,
		       GtkTextIter stop)
{
  PsppireWindow *win = PSPPIRE_WINDOW (sw);
  struct lex_reader *reader = lex_reader_for_gtk_text_buffer (
    GTK_TEXT_BUFFER (sw->buffer), start, stop, sw->syntax_mode);

  const gchar *filename = psppire_window_get_filename (win);
  /* TRANSLATORS: This is part of a filename.  Please avoid whitespace. */
  gchar *untitled = xasprintf ("%s.sps", _("Untitled"));
  lex_reader_set_file_name (reader, filename ? filename : untitled);
  free (untitled);

  execute_syntax (psppire_default_data_window (), reader);
}

/* Delete the currently selected text */
static void
on_edit_delete (PsppireSyntaxWindow *sw)
{
  GtkTextIter begin, end;
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (sw->buffer);

  if (gtk_text_buffer_get_selection_bounds (buffer, &begin, &end))
    gtk_text_buffer_delete (buffer, &begin, &end);
}

/* Create and run a dialog to collect the search string.
   In future this might be expanded to include options, for example
   backward searching, case sensitivity etc.  */
static const char *
get_search_text (PsppireSyntaxWindow *parent)
{
  const char *search_text = NULL;
  GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL;
  GtkWidget *dialog = gtk_dialog_new_with_buttons (_("Text Search"),
                                                   GTK_WINDOW (parent),
                                                   flags,
                                                   _("_OK"),
                                                   GTK_RESPONSE_OK,
                                                   _("_Cancel"),
                                                   GTK_RESPONSE_CANCEL,
                                                   NULL);

  GtkWidget *content_area =
    gtk_dialog_get_content_area (GTK_DIALOG (dialog));

  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
  GtkWidget *label = gtk_label_new (_("Text to search for:"));
  GtkWidget *entry = gtk_entry_new_with_buffer (parent->search_text_buffer);

  /* Add the label, and show everything we have added.  */
  gtk_container_add (GTK_CONTAINER (content_area), box);
  gtk_container_add (GTK_CONTAINER (box), label);
  gtk_container_add (GTK_CONTAINER (box), entry);
  gtk_widget_show_all (content_area);

  int result = gtk_dialog_run (GTK_DIALOG (dialog));
  switch (result)
    {
    case GTK_RESPONSE_OK:
      search_text = gtk_entry_get_text (GTK_ENTRY (entry));
      break;
    default:
      search_text = NULL;
    };

  gtk_widget_destroy (dialog);
  return search_text;
}


/* What to do when the Find menuitem is called.  */
static void
on_edit_find (PsppireSyntaxWindow *sw)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (sw->buffer);
  GtkTextIter begin;
  GtkTextIter loc;
  const char *target = get_search_text (sw);

  if (target == NULL)
    return;

  /* This is a wrap-around search.  So start searching one
     character after the current char.  */
  GtkTextMark *mark = gtk_text_buffer_get_insert (buffer);
  gtk_text_buffer_get_iter_at_mark (buffer, &begin, mark);
  gtk_text_iter_forward_char (&begin);
  if (gtk_text_iter_forward_search (&begin, target, 0,
                                    &loc, 0, 0))
    {
      gtk_text_buffer_place_cursor (buffer, &loc);
    }
  else
    {
      /* If not found, then continue the search from the top
         of the buffer.  */
      gtk_text_buffer_get_start_iter (buffer, &begin);
      if (gtk_text_iter_forward_search (&begin, target, 0,
                                        &loc, 0, 0))
        {
          gtk_text_buffer_place_cursor (buffer, &loc);
        }
    }
}


/* The syntax editor's clipboard deals only with text */
enum {
  SELECT_FMT_NULL,
  SELECT_FMT_TEXT,
};


static void
selection_changed (PsppireSyntaxWindow *sw)
{
  gboolean sel = gtk_text_buffer_get_has_selection (GTK_TEXT_BUFFER (sw->buffer));

  g_object_set (sw->edit_copy,    "enabled", sel, NULL);
  g_object_set (sw->edit_cut,     "enabled", sel, NULL);
  g_object_set (sw->edit_delete,  "enabled", sel, NULL);
}

/* The callback which runs when something request clipboard data */
static void
clipboard_get_cb (GtkClipboard     *clipboard,
		  GtkSelectionData *selection_data,
		  guint             info,
		  gpointer          data)
{
  PsppireSyntaxWindow *sw = data;
  g_assert (info == SELECT_FMT_TEXT);

  gtk_selection_data_set (selection_data, gtk_selection_data_get_target (selection_data),
			  8,
			  (const guchar *) sw->cliptext, strlen (sw->cliptext));

}

static void
clipboard_clear_cb (GtkClipboard *clipboard,
		    gpointer data)
{
  PsppireSyntaxWindow *sw = data;
  g_free (sw->cliptext);
  sw->cliptext = NULL;
}

static gchar tn1[] = "UTF8_STRING";
static gchar tn2[] = "STRING";
static gchar tn3[] = "TEXT";
static gchar tn4[] = "COMPOUND_TEXT";
static gchar tn5[] = "text/plain;charset=utf-8";
static gchar tn6[] = "text/plain";

static const GtkTargetEntry targets[] = {
  { tn1, 0, SELECT_FMT_TEXT },
  { tn2, 0, SELECT_FMT_TEXT },
  { tn3, 0, SELECT_FMT_TEXT },
  { tn4, 0, SELECT_FMT_TEXT },
  { tn5, 0, SELECT_FMT_TEXT },
  { tn6, 0, SELECT_FMT_TEXT }
};

/*
  Store a clip containing the currently selected text.
  Returns true iff something was set.
  As a side effect, begin and end will be set to indicate
  the limits of the selected text.
*/
static gboolean
set_clip (PsppireSyntaxWindow *sw, GtkTextIter *begin, GtkTextIter *end)
{
  GtkClipboard *clipboard ;
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (sw->buffer);

  if (! gtk_text_buffer_get_selection_bounds (buffer, begin, end))
    return FALSE;

  g_free (sw->cliptext);
  sw->cliptext = gtk_text_buffer_get_text  (buffer, begin, end, FALSE);

  clipboard =
    gtk_widget_get_clipboard (GTK_WIDGET (sw), GDK_SELECTION_CLIPBOARD);

  if (!gtk_clipboard_set_with_owner (clipboard, targets,
				     G_N_ELEMENTS (targets),
				     clipboard_get_cb, clipboard_clear_cb,
				     G_OBJECT (sw)))
    clipboard_clear_cb (clipboard, sw);

  return TRUE;
}

static void
on_edit_cut (PsppireSyntaxWindow *sw)
{
  GtkTextIter begin, end;

  if (set_clip (sw, &begin, &end))
    gtk_text_buffer_delete (GTK_TEXT_BUFFER (sw->buffer), &begin, &end);
}

static void
on_edit_copy (PsppireSyntaxWindow *sw)
{
  GtkTextIter begin, end;

  set_clip (sw, &begin, &end);
}

static void
on_edit_paste (PsppireSyntaxWindow *sw)
{
  GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (sw));
  GtkClipboard *clipboard =
    gtk_clipboard_get_for_display (display, GDK_SELECTION_CLIPBOARD);

  gtk_text_buffer_paste_clipboard (GTK_TEXT_BUFFER (sw->buffer), clipboard, NULL, TRUE);
}


/* Check to see if CLIP holds a target which we know how to paste,
   and set the sensitivity of the Paste action accordingly.
 */
static void
set_paste_sensitivity (GtkClipboard *clip, GdkEventOwnerChange *event, gpointer data)
{
  gint i;
  gboolean compatible_target = FALSE;
  PsppireSyntaxWindow *sw = PSPPIRE_SYNTAX_WINDOW (data);

  for (i = 0 ; i < sizeof (targets) / sizeof (targets[0]) ; ++i)
    {
      GdkAtom atom = gdk_atom_intern (targets[i].target, TRUE);
      if (gtk_clipboard_wait_is_target_available (clip, atom))
	{
	  compatible_target = TRUE;
	  break;
	}
    }

  g_object_set (sw->edit_paste, "enabled", compatible_target, NULL);
}




/* Parse and execute all the text in the buffer */
static void
on_run_all (PsppireSyntaxWindow *se)
{
  GtkTextIter begin, end;

  gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (se->buffer), &begin, 0);
  gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (se->buffer), &end, -1);

  editor_execute_syntax (se, begin, end);
}

/* Parse and execute the currently selected text */
static void
on_run_selection (PsppireSyntaxWindow *se)
{
  GtkTextIter begin, end;

  if (gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (se->buffer), &begin, &end))
    editor_execute_syntax (se, begin, end);
}


/* Parse and execute the from the current line, to the end of the
   buffer */
static void
on_run_to_end (PsppireSyntaxWindow *se)
{
  GtkTextIter begin, end;
  GtkTextIter here;
  gint line;

  /* Get the current line */
  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (se->buffer),
				    &here,
				    gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (se->buffer))
				);

  line = gtk_text_iter_get_line (&here) ;

  /* Now set begin and end to the start of this line, and end of buffer
     respectively */
  gtk_text_buffer_get_iter_at_line (GTK_TEXT_BUFFER (se->buffer), &begin, line);
  gtk_text_buffer_get_iter_at_line (GTK_TEXT_BUFFER (se->buffer), &end, -1);

  editor_execute_syntax (se, begin, end);
}



/* Parse and execute the current line */
static void
on_run_current_line (PsppireSyntaxWindow *se)
{
  GtkTextIter begin, end;
  GtkTextIter here;
  gint line;

  /* Get the current line */
  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (se->buffer),
				    &here,
				    gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (se->buffer))
				);

  line = gtk_text_iter_get_line (&here) ;

  /* Now set begin and end to the start of this line, and start of
     following line respectively */
  gtk_text_buffer_get_iter_at_line (GTK_TEXT_BUFFER (se->buffer), &begin, line);
  gtk_text_buffer_get_iter_at_line (GTK_TEXT_BUFFER (se->buffer), &end, line + 1);

  editor_execute_syntax (se, begin, end);
}



static void
on_syntax (GAction *action, GVariant *param, PsppireSyntaxWindow *sw)
{
  g_action_change_state (action, param);
  GVariant *new_state = g_action_get_state (action);

  const gchar *what = g_variant_get_string (new_state, NULL);
  if (0 == g_strcmp0 (what, "auto"))
    sw->syntax_mode = SEG_MODE_AUTO;
  else if (0 == g_strcmp0 (what, "interactive"))
    sw->syntax_mode = SEG_MODE_INTERACTIVE;
  else if (0 == g_strcmp0 (what, "batch"))
    sw->syntax_mode = SEG_MODE_BATCH;
  else
    g_warn_if_reached ();
}


/* Append ".sps" to FILENAME if necessary.
   The returned result must be freed when no longer required.
 */
static gchar *
append_suffix (const gchar *filename)
{
  if (! g_str_has_suffix (filename, ".sps") &&
       ! g_str_has_suffix (filename, ".SPS"))
    {
      return g_strdup_printf ("%s.sps", filename);
    }

  return xstrdup (filename);
}

/*
  Save BUFFER to the file called FILENAME.
  FILENAME must be encoded in Glib filename encoding.
  If successful, clears the buffer's modified flag.
*/
static gboolean
save_editor_to_file (PsppireSyntaxWindow *se,
		     const gchar *filename,
		     GError **err)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (se->buffer);
  struct substring text_locale;
  gboolean result ;
  GtkTextIter start, stop;
  gchar *text;

  gchar *suffixedname;
  g_assert (filename);

  suffixedname = append_suffix (filename);

  gtk_text_buffer_get_iter_at_line (buffer, &start, 0);
  gtk_text_buffer_get_iter_at_offset (buffer, &stop, -1);

  text = gtk_text_buffer_get_text (buffer, &start, &stop, FALSE);

  text_locale = recode_substring_pool (se->encoding, "UTF-8", ss_cstr (text),
                                       NULL);

  result =  g_file_set_contents (suffixedname, ss_data (text_locale),
                                 ss_length (text_locale), err);

  ss_dealloc (&text_locale);
  g_free (suffixedname);

  if (result)
    {
      char *fn = g_filename_display_name (filename);
      gchar *msg = g_strdup_printf (_("Saved file `%s'"), fn);
      g_free (fn);
      gtk_statusbar_push (GTK_STATUSBAR (se->sb), se->text_context, msg);
      gtk_text_buffer_set_modified (buffer, FALSE);
      g_free (msg);
    }

  return result;
}


/* PsppireWindow 'pick_Filename' callback. */
static void
syntax_pick_filename (PsppireWindow *window)
{
  PsppireSyntaxWindow *se = PSPPIRE_SYNTAX_WINDOW (window);
  const char *default_encoding;
  GtkFileFilter *filter;
  gint response;

  GtkWidget *dialog =
    gtk_file_chooser_dialog_new (_("Save Syntax"),
				 GTK_WINDOW (se),
				 GTK_FILE_CHOOSER_ACTION_SAVE,
				 _("Cancel"), GTK_RESPONSE_CANCEL,
				 _("Save"),   GTK_RESPONSE_ACCEPT,
				 NULL);

  g_object_set (dialog, "local-only", FALSE, NULL);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("Syntax Files (*.sps) "));
  gtk_file_filter_add_pattern (filter, "*.sps");
  gtk_file_filter_add_pattern (filter, "*.SPS");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog),
						  TRUE);

  default_encoding = se->encoding != NULL ? se->encoding : locale_charset ();
  gtk_file_chooser_set_extra_widget (
    GTK_FILE_CHOOSER (dialog),
    psppire_encoding_selector_new (default_encoding, false));

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_ACCEPT)
    {
      gchar *encoding;
      char *filename;

      filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
      psppire_window_set_filename (window, filename);
      free (filename);

      encoding = psppire_encoding_selector_get_encoding (
        gtk_file_chooser_get_extra_widget (GTK_FILE_CHOOSER (dialog)));
      if (encoding != NULL)
        {
          g_free (se->encoding);
          se->encoding = encoding;
        }
    }

  gtk_widget_destroy (dialog);
}


/* PsppireWindow 'save' callback. */
static void
syntax_save (PsppireWindow *se)
{
  const gchar *filename = psppire_window_get_filename (se);
  GError *err = NULL;
  save_editor_to_file (PSPPIRE_SYNTAX_WINDOW (se), filename, &err);
  if (err)
    {
      msg (ME, "%s", err->message);
      g_error_free (err);
    }
}


static void
load_and_show_syntax_window (GtkWidget *se, const gchar *filename,
                             const gchar *encoding)
{
  gboolean ok;

  gtk_source_buffer_begin_not_undoable_action (PSPPIRE_SYNTAX_WINDOW (se)->buffer);
  ok = psppire_window_load (PSPPIRE_WINDOW (se), filename, encoding, NULL);
  gtk_source_buffer_end_not_undoable_action (PSPPIRE_SYNTAX_WINDOW (se)->buffer);

  if (ok)
    gtk_widget_show (se);
  else
    gtk_widget_destroy (se);
}

void
create_syntax_window (void)
{
  GtkWidget *w = psppire_syntax_window_new (NULL);

  gtk_widget_show (w);
}

GtkWindow *
open_syntax_window (const char *file_name, const gchar *encoding)
{
  GtkWidget *se = psppire_syntax_window_new (NULL);

  if (file_name)
    load_and_show_syntax_window (se, file_name, encoding);

  return GTK_WINDOW (se);
}



static void psppire_syntax_window_print (PsppireSyntaxWindow *window);

static void
on_modified_changed (GtkTextBuffer *buffer, PsppireWindow *window)
{
  if (gtk_text_buffer_get_modified (buffer))
    psppire_window_set_unsaved (window);
}

static void undo_redo_update (PsppireSyntaxWindow *window);
static void undo_last_edit (PsppireSyntaxWindow *window);
static void redo_last_edit (PsppireSyntaxWindow *window);

static void
on_text_changed (GtkTextBuffer *buffer, PsppireSyntaxWindow *window)
{
  gtk_statusbar_pop (GTK_STATUSBAR (window->sb), window->text_context);
  undo_redo_update (window);
}

static void
psppire_syntax_window_init (PsppireSyntaxWindow *window)
{
  GtkBuilder *xml = builder_new ("syntax-editor.ui");
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  GObject *menu = get_object_assert (xml, "syntax-window-menu", G_TYPE_MENU);
  GtkWidget *menubar = gtk_menu_bar_new_from_model (G_MENU_MODEL (menu));

  GtkWidget *sw = get_widget_assert (xml, "scrolledwindow8");

  GtkWidget *text_view = get_widget_assert (xml, "syntax_text_view");

  PsppireSyntaxWindowClass *class
    = PSPPIRE_SYNTAX_WINDOW_CLASS (G_OBJECT_GET_CLASS (window));

  GtkClipboard *clip_selection = gtk_widget_get_clipboard (GTK_WIDGET (window), GDK_SELECTION_CLIPBOARD);
  GtkClipboard *clip_primary =   gtk_widget_get_clipboard (GTK_WIDGET (window), GDK_SELECTION_PRIMARY);

  window->print_settings = NULL;

  window->undo_menuitem = g_simple_action_new ("undo", NULL);
  window->redo_menuitem = g_simple_action_new ("redo", NULL);

  g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (window->undo_menuitem));
  g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (window->redo_menuitem));


  if (class->lan)
    window->buffer = gtk_source_buffer_new_with_language (class->lan);
  else
    window->buffer = gtk_source_buffer_new (NULL);

  gtk_text_view_set_buffer (GTK_TEXT_VIEW (text_view), GTK_TEXT_BUFFER (window->buffer));

  g_object_set (window->buffer,
		"highlight-matching-brackets", TRUE,
		NULL);

  g_object_set (text_view,
		"show-line-numbers", TRUE,
		"show-line-marks", TRUE,
		"auto-indent", TRUE,
		"indent-width", 4,
		"highlight-current-line", TRUE,
		NULL);

  window->encoding = NULL;
  window->syntax_mode = SEG_MODE_AUTO;

  window->cliptext = NULL;
  window->dispose_has_run = FALSE;

  window->search_text_buffer = gtk_entry_buffer_new (NULL, -1);

  window->edit_delete = g_simple_action_new ("delete", NULL);
  g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (window->edit_delete));

  window->edit_copy = g_simple_action_new ("copy", NULL);
  g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (window->edit_copy));

  window->edit_cut = g_simple_action_new ("cut", NULL);
  g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (window->edit_cut));

  window->edit_paste = g_simple_action_new ("paste", NULL);
  g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (window->edit_paste));

  window->edit_find = g_simple_action_new ("find", NULL);
  g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (window->edit_find));

  window->buffer = GTK_SOURCE_BUFFER (gtk_text_view_get_buffer (GTK_TEXT_VIEW (text_view)));

  window->sb = get_widget_assert (xml, "statusbar2");
  window->text_context = gtk_statusbar_get_context_id (GTK_STATUSBAR (window->sb), "Text Context");

  g_signal_connect (window->buffer, "changed",
		    G_CALLBACK (on_text_changed), window);

  g_signal_connect (window->buffer, "modified-changed",
		    G_CALLBACK (on_modified_changed), window);


  {
    GSimpleAction *print = g_simple_action_new ("print", NULL);

    g_signal_connect_swapped (print, "activate",
			      G_CALLBACK (psppire_syntax_window_print), window);

    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (print));
  }

  g_signal_connect_swapped (window->undo_menuitem,
			    "activate",
			    G_CALLBACK (undo_last_edit),
			    window);

  g_signal_connect_swapped (window->redo_menuitem,
			    "activate",
                            G_CALLBACK (redo_last_edit),
			    window);

  undo_redo_update (window);


  window->sel_handler = g_signal_connect_swapped (clip_primary, "owner-change",
						   G_CALLBACK (selection_changed), window);

  window->ps_handler = g_signal_connect (clip_selection, "owner-change",
					  G_CALLBACK (set_paste_sensitivity), window);

  gtk_container_add (GTK_CONTAINER (window), box);

  g_object_ref (sw);

  g_object_ref (window->sb);

  gtk_box_pack_start (GTK_BOX (box), menubar, FALSE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), sw, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (box), window->sb, FALSE, TRUE, 0);

  gtk_widget_show_all (box);

  GtkApplication *app = GTK_APPLICATION (g_application_get_default ());

  {
    GSimpleAction *open = g_simple_action_new ("open", NULL);

    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (open));

    g_signal_connect_swapped (open,
			      "activate",
			      G_CALLBACK (psppire_window_open),
			      window);
  }

  {
    GSimpleAction *save = g_simple_action_new ("save", NULL);

    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (save));

    g_signal_connect_swapped (save,
			      "activate",
			      G_CALLBACK (psppire_window_save),
			      window);

    const gchar *accels[2] = { "<Primary>S", NULL};
    gtk_application_set_accels_for_action (app,
					   "win.save",
					   accels);

  }

  {
    GSimpleAction *save_as = g_simple_action_new ("save_as", NULL);

    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (save_as));

    g_signal_connect_swapped (save_as,
			      "activate",
			      G_CALLBACK (psppire_window_save_as),
			      window);


    const gchar *accels[2] = { "<Shift><Primary>S", NULL};
    gtk_application_set_accels_for_action (app,
					   "win.save_as",
					   accels);


  }


  g_signal_connect_swapped (window->edit_delete,
		    "activate",
		    G_CALLBACK (on_edit_delete),
		    window);

  g_signal_connect_swapped (window->edit_copy,
		    "activate",
		    G_CALLBACK (on_edit_copy),
		    window);

  g_signal_connect_swapped (window->edit_cut,
		    "activate",
		    G_CALLBACK (on_edit_cut),
		    window);

  g_signal_connect_swapped (window->edit_paste,
		    "activate",
		    G_CALLBACK (on_edit_paste),
		    window);

  g_signal_connect_swapped (window->edit_find,
		    "activate",
		    G_CALLBACK (on_edit_find),
		    window);

  {
    GSimpleAction *run_all = g_simple_action_new ("run-all", NULL);

    g_signal_connect_swapped (run_all, "activate",
			      G_CALLBACK (on_run_all), window);

    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (run_all));
  }

  {
    GSimpleAction *run_current_line = g_simple_action_new ("run-current-line", NULL);

    g_signal_connect_swapped (run_current_line, "activate",
			      G_CALLBACK (on_run_current_line), window);

    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (run_current_line));

    GtkApplication *app = GTK_APPLICATION (g_application_get_default ());
    const gchar *accels[2] = { "<Primary>R", NULL};
    gtk_application_set_accels_for_action (app,
					   "win.run-current-line",
					   accels);
  }

  {
    GSimpleAction *run_selection = g_simple_action_new ("run-selection", NULL);

    g_signal_connect_swapped (run_selection, "activate",
			      G_CALLBACK (on_run_selection), window);

    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (run_selection));
  }

  {
    GSimpleAction *run_to_end = g_simple_action_new ("run-to-end", NULL);

    g_signal_connect_swapped (run_to_end, "activate",
			      G_CALLBACK (on_run_to_end), window);

    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (run_to_end));
  }

  {
    GSimpleAction *act_syntax = g_simple_action_new_stateful ("syntax", G_VARIANT_TYPE_STRING,
								 g_variant_new_string ("auto"));
    g_signal_connect (act_syntax, "activate", G_CALLBACK (on_syntax), window);
    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (act_syntax));
  }

  gtk_menu_shell_append (GTK_MENU_SHELL (menubar),
  			 create_windows_menu (GTK_WINDOW (window)));

  gtk_menu_shell_append (GTK_MENU_SHELL (menubar),
  			 create_help_menu (GTK_WINDOW (window)));

  g_object_unref (xml);
}





GtkWidget *
psppire_syntax_window_new (const char *encoding)
{
  GObject *sw = g_object_new (psppire_syntax_window_get_type (),
			      "description", _("Syntax Editor"),
			      "encoding", encoding,
			      NULL);

  GApplication *app = g_application_get_default ();
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (sw));

  return GTK_WIDGET (sw);
}

static void
error_dialog (GtkWindow *w, const gchar *filename,  GError *err)
{
  gchar *fn = g_filename_display_basename (filename);

  GtkWidget *dialog =
    gtk_message_dialog_new (w,
			    GTK_DIALOG_DESTROY_WITH_PARENT,
			    GTK_MESSAGE_ERROR,
			    GTK_BUTTONS_CLOSE,
			    _("Cannot load syntax file `%s'"),
			    fn);

  g_free (fn);

  g_object_set (dialog, "icon-name", "org.gnu.pspp", NULL);

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
					    "%s", err->message);

  gtk_dialog_run (GTK_DIALOG (dialog));

  gtk_widget_destroy (dialog);
}

/*
  Loads the buffer from the file called FILENAME
*/
static gboolean
syntax_load (PsppireWindow *window, const gchar *filename,
             const gchar *encoding, gpointer not_used)
{
  GError *err = NULL;
  gchar *text_locale = NULL;
  gchar *text_utf8 = NULL;
  gsize len_locale = -1;
  gsize len_utf8 = -1;
  GtkTextIter iter;
  PsppireSyntaxWindow *sw = PSPPIRE_SYNTAX_WINDOW (window);
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (sw->buffer);

  /* FIXME: What if it's a very big file ? */
  if (! g_file_get_contents (filename, &text_locale, &len_locale, &err))
    {
      error_dialog (GTK_WINDOW (window), filename, err);
      g_clear_error (&err);
      return FALSE;
    }

  if (!encoding || !encoding[0])
    {
      /* Determine the file's encoding and update sw->encoding.  (The ordering
         is important here because encoding_guess_whole_file() often returns
         its argument instead of a copy of it.) */
      char *guessed_encoding;

      guessed_encoding = g_strdup (encoding_guess_whole_file (sw->encoding,
                                                              text_locale,
                                                              len_locale));
      g_free (sw->encoding);
      sw->encoding = guessed_encoding;
    }
  else
    {
      g_free (sw->encoding);
      sw->encoding = g_strdup (encoding);
    }

  text_utf8 = recode_substring_pool ("UTF-8", sw->encoding,
                                     ss_buffer (text_locale, len_locale),
                                     NULL).string;
  free (text_locale);

  if (text_utf8 == NULL)
    {
      error_dialog (GTK_WINDOW (window), filename, err);
      g_clear_error (&err);
      return FALSE;
    }

  gtk_text_buffer_get_iter_at_line (buffer, &iter, 0);

  gtk_text_buffer_insert (buffer, &iter, text_utf8, len_utf8);

  gtk_text_buffer_set_modified (buffer, FALSE);

  free (text_utf8);

  add_most_recent (filename, "text/x-spss-syntax", sw->encoding);

  return TRUE;
}



static void
psppire_syntax_window_iface_init (PsppireWindowIface *iface)
{
  iface->save = syntax_save;
  iface->pick_filename = syntax_pick_filename;
  iface->load = syntax_load;
}




static void
undo_redo_update (PsppireSyntaxWindow *window)
{
  g_object_set (window->undo_menuitem, "enabled",
		gtk_source_buffer_can_undo (window->buffer), NULL);

  g_object_set  (window->redo_menuitem, "enabled",
		 gtk_source_buffer_can_redo (window->buffer), NULL);
}

static void
undo_last_edit (PsppireSyntaxWindow *window)
{
  gtk_source_buffer_undo (window->buffer);
  undo_redo_update (window);
}

static void
redo_last_edit (PsppireSyntaxWindow *window)
{
  gtk_source_buffer_redo (window->buffer);
  undo_redo_update (window);
}



/* Printing related stuff */


static void
begin_print (GtkPrintOperation *operation,
          GtkPrintContext   *context,
          PsppireSyntaxWindow *window)
{
  window->compositor =
    gtk_source_print_compositor_new (window->buffer);
}


static void
end_print (GtkPrintOperation *operation,
          GtkPrintContext   *context,
          PsppireSyntaxWindow *window)
{
  g_object_unref (window->compositor);
  window->compositor = NULL;
}



static gboolean
paginate (GtkPrintOperation *operation,
          GtkPrintContext   *context,
          PsppireSyntaxWindow *window)
{
  if (gtk_source_print_compositor_paginate (window->compositor, context))
    {
      gint n_pages = gtk_source_print_compositor_get_n_pages (window->compositor);
      gtk_print_operation_set_n_pages (operation, n_pages);

      return TRUE;
    }

  return FALSE;
}

static void
draw_page (GtkPrintOperation *operation,
           GtkPrintContext   *context,
           gint               page_nr,
          PsppireSyntaxWindow *window)
{
  gtk_source_print_compositor_draw_page (window->compositor,
					 context,
					 page_nr);
}



static void
psppire_syntax_window_print (PsppireSyntaxWindow *window)
{
  GtkPrintOperationResult res;

  GtkPrintOperation *print = gtk_print_operation_new ();

  if (window->print_settings != NULL)
    gtk_print_operation_set_print_settings (print, window->print_settings);


  g_signal_connect (print, "begin_print", G_CALLBACK (begin_print), window);
  g_signal_connect (print, "end_print", G_CALLBACK (end_print),     window);
  g_signal_connect (print, "draw_page", G_CALLBACK (draw_page),     window);
  g_signal_connect (print, "paginate", G_CALLBACK (paginate),       window);

  res = gtk_print_operation_run (print, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                                 GTK_WINDOW (window), NULL);

  if (res == GTK_PRINT_OPERATION_RESULT_APPLY)
    {
      if (window->print_settings != NULL)
        g_object_unref (window->print_settings);
      window->print_settings = g_object_ref (gtk_print_operation_get_print_settings (print));
    }

  g_object_unref (print);
}
