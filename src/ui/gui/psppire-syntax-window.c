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

#include "language/lexer/command-segmenter.h"
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
#include "ui/gui/psppire-search-dialog.h"
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

  char *pkg_data_dir = relocate_clone (PKGDATADIR);
  new_paths = g_realloc (new_paths, (n + 2) * sizeof (*new_paths));
  new_paths[n] = g_strdup (pkg_data_dir);
  new_paths[n+1] = NULL;
  free (pkg_data_dir);

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

typedef gboolean search_function (GtkSourceSearchContext *search,
                                  const GtkTextIter *iter,
                                  GtkTextIter *match_start,
                                  GtkTextIter *match_end,
                                   gboolean *has_wrapped_around);


/* This function is called when the user clicks the Find button */
static void on_find (PsppireSyntaxWindow *sw, gboolean backwards, gpointer data, gpointer junk)
{
  PsppireSearchDialog *dialog = PSPPIRE_SEARCH_DIALOG (data);

  const char *search_text = gtk_entry_get_text (GTK_ENTRY (dialog->entry));
  if (search_text == NULL)
    return;

  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (sw->buffer);
  GtkTextIter begin;
  GtkTextIter loc;

  GtkTextMark *mark = gtk_text_buffer_get_insert (buffer);

  GtkSourceSearchSettings *sss = gtk_source_search_context_get_settings (sw->search_context);
  gtk_source_search_settings_set_search_text (sss, search_text);
  gtk_source_search_settings_set_case_sensitive (sss,
                                                 !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->ignore_case)));

  gtk_source_search_settings_set_at_word_boundaries (sss,
                                                     gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->whole)));

  gtk_source_search_settings_set_wrap_around (sss,
                                              gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->wrap)));

  search_function *func = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->forward))
                                                        ? gtk_source_search_context_forward
                                                        : gtk_source_search_context_backward;

  gtk_text_buffer_get_iter_at_mark (buffer, &begin, mark);
  gtk_text_iter_forward_char (&begin);
  if (func (sw->search_context, &begin, &loc, NULL, NULL))
    {
      gtk_text_buffer_place_cursor (buffer, &loc);
    }
}

/* What to do when the Find menuitem is called.  */
static void
on_edit_find (PsppireSyntaxWindow *sw)
{
  GtkWidget *ww = psppire_search_dialog_new ();
  gtk_window_set_transient_for (GTK_WINDOW (ww), GTK_WINDOW (sw));

  g_signal_connect_swapped (ww, "find", G_CALLBACK (on_find), sw);

  sw->search_context = gtk_source_search_context_new (sw->buffer, NULL);

  psppire_dialog_run (PSPPIRE_DIALOG (ww));

  g_object_unref (sw->search_context);
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

static bool
overlaps (int a[2], int b[2])
{
  return (b[0] <= a[0] && a[0] < b[1]) || (a[0] <= b[0] && b[0] < a[1]);
}

/* Parse and execute the commands that overlap [START, END). */
static void
run_commands (PsppireSyntaxWindow *se, GtkTextIter start, GtkTextIter end)
{
  GtkTextBuffer *buf = GTK_TEXT_BUFFER (se->buffer);

  /* Convert the iterator range into a line number range.  Both ranges are
     half-open (they exclude the end), but it's OK for them to be empty. */
  int in_lines[2] = {
    gtk_text_iter_get_line (&start),
    gtk_text_iter_get_line (&end),
  };
  if (in_lines[0] == in_lines[1] || gtk_text_iter_get_line_index (&end) > 0)
    in_lines[1]++;

  /* These are the lines that we're going to run.  */
  int run_lines[2] = { -1, -1 };

  /* Iterate through all the text in the buffer until we find a command that
     spans the line we're on. */
  struct command_segmenter *cs = command_segmenter_create (se->syntax_mode);
  GtkTextIter begin;
  gtk_text_buffer_get_start_iter (buf, &begin);
  while (!gtk_text_iter_is_end (&begin))
    {
      GtkTextIter next = begin;
      gtk_text_iter_forward_line (&next);

      gchar *text = gtk_text_iter_get_text (&begin, &next);
      command_segmenter_push (cs, text, strlen (text));
      g_free (text);

      if (gtk_text_iter_is_end (&next))
        command_segmenter_eof (cs);

      int cmd_lines[2];
      while (command_segmenter_get (cs, cmd_lines))
        {
          if (overlaps (cmd_lines, in_lines))
            {
              /* This command's lines overlap with the lines we want to run.
                 If we don't have any lines yet, take this command's lines;
                 otherwise, extend the lines we have with this command's
                 lines. */
              if (run_lines[0] == -1)
                {
                  run_lines[0] = cmd_lines[0];
                  run_lines[1] = cmd_lines[1];
                }
              else
                run_lines[1] = cmd_lines[1];
            }
          else if (cmd_lines[0] >= in_lines[1])
            {
              /* We're moved past the lines that could possibly overlap with
                 those that we want to run.

                 If we don't have anything to run, we need to make some guess.
                 If we were just given a single position, then probably it
                 makes sense to run the next command.  Otherwise, we were given
                 a nonempty selection that didn't contain any commands, and it
                 seems reasonable to not run any. */
              if (run_lines[0] == -1 && gtk_text_iter_equal (&start, &end))
                {
                  run_lines[0] = cmd_lines[0];
                  run_lines[1] = cmd_lines[1];
                }
              break;
            }
        }

      begin = next;
    }
  command_segmenter_destroy (cs);

  if (run_lines[0] != -1)
    {
      GtkTextIter begin, end;
      gtk_text_buffer_get_iter_at_line (buf, &begin, run_lines[0]);
      gtk_text_buffer_get_iter_at_line (buf, &end, run_lines[1]);

      editor_execute_syntax (se, begin, end);
    }
}

static GtkTextIter
get_iter_for_cursor (PsppireSyntaxWindow *se)
{
  GtkTextBuffer *buf = GTK_TEXT_BUFFER (se->buffer);
  GtkTextIter iter;
  gtk_text_buffer_get_iter_at_mark (
    buf, &iter, gtk_text_buffer_get_insert (buf));
  return iter;
}

/* Parse and execute the currently selected syntax, if there is any, and
   otherwise the command that the cursor is in. */
static void
on_run_selection (PsppireSyntaxWindow *se)
{
  GtkTextBuffer *buf = GTK_TEXT_BUFFER (se->buffer);

  GtkTextIter begin, end;
  if (gtk_text_buffer_get_selection_bounds (buf, &begin, &end))
    run_commands (se, begin, end);
  else
    {
      GtkTextIter iter = get_iter_for_cursor (se);
      run_commands (se, iter, iter);
    }
}


/* Parse and execute the syntax from the current line, to the end of the
   buffer. */
static void
on_run_to_end (PsppireSyntaxWindow *se)
{
  GtkTextBuffer *buf = GTK_TEXT_BUFFER (se->buffer);
  GtkTextIter end;
  gtk_text_buffer_get_end_iter (buf, &end);

  run_commands (se, get_iter_for_cursor (se), end);
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

  window->search_context = NULL;

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
    GSimpleAction *run_selection = g_simple_action_new ("run-selection", NULL);

    g_signal_connect_swapped (run_selection, "activate",
                              G_CALLBACK (on_run_selection), window);

    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (run_selection));

    GtkApplication *app = GTK_APPLICATION (g_application_get_default ());
    const gchar *accels[2] = { "<Primary>R", NULL};
    gtk_application_set_accels_for_action (app,
                                           "win.run-selection",
                                           accels);
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
