/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010, 2011, 2012, 2015  Free Software Foundation

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
#include "psppire-dialog.h"
#include "psppire-buttonbox.h"
#include "psppire-selector.h"
#include <string.h>
#include "builder-wrapper.h"
#include "help-menu.h"

#include "psppire-window-base.h"

static void psppire_dialog_class_init          (PsppireDialogClass *);
static void psppire_dialog_init                (PsppireDialog      *);


enum  {DIALOG_REFRESH,
       RESPONSE,
       VALIDITY_CHANGED,
       DIALOG_HELP,
       n_SIGNALS};

static guint signals [n_SIGNALS];

static GObjectClass     *parent_class = NULL;

static void
psppire_dialog_finalize (GObject *object)
{
  PsppireDialog *dialog = PSPPIRE_DIALOG (object);

  g_free (dialog->help_page);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}


G_DEFINE_TYPE (PsppireDialog, psppire_dialog, PSPPIRE_TYPE_WINDOW_BASE);

/* Properties */
enum
{
  PROP_0,
  PROP_ORIENTATION,
  PROP_SLIDING,
  PROP_HELP_PAGE,
};


static void
psppire_dialog_get_property (GObject         *object,
                             guint            prop_id,
                             GValue          *value,
                             GParamSpec      *pspec)
{
  PsppireDialog *dialog = PSPPIRE_DIALOG (object);

  switch (prop_id)
    {
    case PROP_SLIDING:
      g_value_set_boolean (value, dialog->slidable);
      break;
    case PROP_HELP_PAGE:
      g_value_set_string (value, dialog->help_page);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
psppire_dialog_set_property (GObject         *object,
                             guint            prop_id,
                             const GValue    *value,
                             GParamSpec      *pspec)

{
  PsppireDialog *dialog = PSPPIRE_DIALOG (object);

  switch (prop_id)
    {
    case PROP_SLIDING:
      dialog->slidable = g_value_get_boolean (value);
      break;
    case PROP_HELP_PAGE:
      dialog->help_page = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
psppire_dialog_class_init (PsppireDialogClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_dialog_finalize;

  GParamSpec *sliding_spec ;
  GParamSpec *help_page_spec ;

  help_page_spec =
    g_param_spec_string ("help-page",
                         "Help Page",
                         "The section of the manual to load when the Help button is clicked",
                         NULL,
                         G_PARAM_READWRITE);

  sliding_spec =
    g_param_spec_boolean ("slidable",
                          "Slidable",
                          "Can the container be sized by the user",
                          FALSE,
                          G_PARAM_CONSTRUCT_ONLY |G_PARAM_READWRITE);

  object_class->set_property = psppire_dialog_set_property;
  object_class->get_property = psppire_dialog_get_property;

  g_object_class_install_property (object_class,
                                   PROP_SLIDING,
                                   sliding_spec);

  g_object_class_install_property (object_class,
                                   PROP_HELP_PAGE,
                                   help_page_spec);


  signals [DIALOG_REFRESH] =
    g_signal_new ("refresh",
                  G_TYPE_FROM_CLASS (class),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE,
                  0);


  signals [RESPONSE] =
    g_signal_new ("response",
                  G_TYPE_FROM_CLASS (class),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__INT,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_INT);


  signals [VALIDITY_CHANGED] =
    g_signal_new ("validity-changed",
                  G_TYPE_FROM_CLASS (class),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_BOOLEAN);


  signals [DIALOG_HELP] =
    g_signal_new ("help",
                  G_TYPE_FROM_CLASS (class),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__STRING,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_STRING);

  parent_class = g_type_class_peek_parent (class);
}

static void
close_dialog (GtkWidget *w, gpointer data)
{
  PsppireDialog *dialog = data;

  psppire_dialog_close (dialog);
}

void
psppire_dialog_close (PsppireDialog *dialog)
{
  g_main_loop_quit (dialog->loop);
  gtk_widget_hide (GTK_WIDGET (dialog));
}

static void
delete_event_callback (GtkWidget *w, GdkEvent *e, gpointer data)
{
  close_dialog (w, data);
}


static void
psppire_dialog_init (PsppireDialog *dialog)
{
  dialog->contents_are_valid = NULL;
  dialog->validity_data = NULL;
  dialog->contents_are_acceptable = NULL;
  dialog->acceptable_data = NULL;
  dialog->slidable = FALSE;
  dialog->help_page = NULL;

  gtk_window_set_type_hint (GTK_WINDOW (dialog),
        GDK_WINDOW_TYPE_HINT_DIALOG);

  g_signal_connect (dialog, "delete-event",
                    G_CALLBACK (delete_event_callback),
                    dialog);

  gtk_window_set_type_hint (GTK_WINDOW (dialog),
        GDK_WINDOW_TYPE_HINT_DIALOG);

  g_object_set (dialog, "icon-name", "org.gnu.pspp", NULL);
}

GtkWidget*
psppire_dialog_new (void)
{
  PsppireDialog *dialog ;

  dialog = g_object_new (psppire_dialog_get_type (),
                         NULL);

  return GTK_WIDGET (dialog) ;
}


void
psppire_dialog_notify_change (PsppireDialog *dialog)
{
  if (dialog->contents_are_valid)
    {
      gboolean valid = dialog->contents_are_valid (dialog->validity_data);

      g_signal_emit (dialog, signals [VALIDITY_CHANGED], 0, valid);
    }
}


/* Descend the widget tree, connecting appropriate signals to the
   psppire_dialog_notify_change callback */
static void
connect_notify_signal (GtkWidget *w, gpointer data)
{
  PsppireDialog *dialog = data;

  if (PSPPIRE_IS_BUTTON_BOX (w))
    return;

  if (GTK_IS_CONTAINER (w))
    {
      gtk_container_foreach (GTK_CONTAINER (w),
                             connect_notify_signal,
                             dialog);
    }

  /* It's unfortunate that GTK+ doesn't have a generic
     "user-modified-state-changed" signal.  Instead, we have to try and
     predict what widgets and signals are likely to exist in our dialogs. */

  if (GTK_IS_DRAWING_AREA (w))
    {
      g_signal_connect_swapped (w, "draw",
                                G_CALLBACK (psppire_dialog_notify_change),
                                dialog);
    }

  if (GTK_IS_TOGGLE_BUTTON (w))
    {
      g_signal_connect_swapped (w, "toggled",
                                G_CALLBACK (psppire_dialog_notify_change),
                                dialog);
    }

  if (PSPPIRE_IS_SELECTOR (w))
    {
      g_signal_connect_swapped (w, "selected",
                                G_CALLBACK (psppire_dialog_notify_change),
                                dialog);

      g_signal_connect_swapped (w, "de-selected",
                                G_CALLBACK (psppire_dialog_notify_change),
                                dialog);

      psppire_selector_update_subjects (PSPPIRE_SELECTOR (w));
    }

  if (GTK_IS_EDITABLE (w))
    {
      g_signal_connect_swapped (w, "changed",
                                G_CALLBACK (psppire_dialog_notify_change),
                                dialog);
    }

  if (GTK_IS_CELL_EDITABLE (w))
    {
      g_signal_connect_swapped (w, "editing-done",
                                G_CALLBACK (psppire_dialog_notify_change),
                                dialog);
    }

  if (GTK_IS_TEXT_VIEW (w))
    {
      GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (w));

      g_signal_connect_swapped (buffer, "changed",
                                G_CALLBACK (psppire_dialog_notify_change),
                                dialog);
    }

  if (GTK_IS_TREE_VIEW (w))
    {
      gint i = 0;
      GtkTreeView *tv = GTK_TREE_VIEW (w);
      GtkTreeSelection *selection =
        gtk_tree_view_get_selection (tv);
      GtkTreeViewColumn *col;
      GtkTreeModel *model = gtk_tree_view_get_model (tv);

      if (model)
        {
          g_signal_connect_swapped (model, "row-changed",
                                    G_CALLBACK (psppire_dialog_notify_change),
                                    dialog);

          g_signal_connect_swapped (model, "row-deleted",
                                    G_CALLBACK (psppire_dialog_notify_change),
                                    dialog);

          g_signal_connect_swapped (model, "row-inserted",
                                    G_CALLBACK (psppire_dialog_notify_change),
                                    dialog);

        }

      g_signal_connect_swapped (selection, "changed",
                                G_CALLBACK (psppire_dialog_notify_change),
                                dialog);

      while ((col = gtk_tree_view_get_column (tv, i++)))
        {
          GList *renderers = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (col));
          GList *start = renderers;
          while (renderers)
            {
              if (GTK_IS_CELL_RENDERER_TOGGLE (renderers->data))
                g_signal_connect_swapped (renderers->data, "toggled",
                                          G_CALLBACK (psppire_dialog_notify_change), dialog);
              renderers = renderers->next;
            }
          g_list_free (start);
        }
    }
}


gint
psppire_dialog_run (PsppireDialog *dialog)
{
  gchar *title = NULL;
  g_object_get (dialog, "title", &title, NULL);

  if (title == NULL)
    g_warning ("PsppireDialog %s has no title", gtk_widget_get_name (GTK_WIDGET (dialog)));

  if (dialog->contents_are_valid != NULL)
    gtk_container_foreach (GTK_CONTAINER (gtk_bin_get_child(GTK_BIN(dialog))),
                           connect_notify_signal,
                           dialog);

  dialog->loop = g_main_loop_new (NULL, FALSE);

  gtk_widget_show (GTK_WIDGET (dialog));
  psppire_dialog_notify_change (dialog);

  g_main_loop_run (dialog->loop);

  g_main_loop_unref (dialog->loop);

  g_signal_emit (dialog, signals [RESPONSE], 0, dialog->response);

  g_free (title);

  return dialog->response;
}


void
psppire_dialog_reload (PsppireDialog *dialog)
{
  g_signal_emit (dialog, signals [DIALOG_REFRESH], 0);
}


void
psppire_dialog_help (PsppireDialog *dialog)
{
  const char *page = NULL;

  g_object_get (dialog, "help-page", &page, NULL);

  online_help (page);

  g_signal_emit (dialog, signals [DIALOG_HELP], 0, page);
}

/* Sets a predicate function that is checked after each change that the user
   makes to the dialog's state.  If the predicate function returns false, then
   "OK" and other buttons that accept the dialog's settings will be
   disabled. */
void
psppire_dialog_set_valid_predicate (PsppireDialog *dialog,
                                    ContentsAreValid contents_are_valid,
                                    gpointer data)
{
  dialog->contents_are_valid = contents_are_valid;
  dialog->validity_data = data;
}

/* Sets a predicate function that is called after "OK" or another button that
   accepts the dialog's settings is pushed.  If the predicate function returns
   false, then the button push is ignored.  (If the predicate function returns
   false, then it should take some action to notify the user why the contents
   are unacceptable, e.g. pop up a dialog box.)

   An accept predicate is preferred over a validity predicate when the reason
   why the dialog settings are unacceptable may not be obvious to the user, so
   that the user needs a helpful message to explain. */
void
psppire_dialog_set_accept_predicate (PsppireDialog *dialog,
                                     ContentsAreValid contents_are_acceptable,
                                     gpointer data)
{
  dialog->contents_are_acceptable = contents_are_acceptable;
  dialog->acceptable_data = data;
}

gboolean
psppire_dialog_is_acceptable (const PsppireDialog *dialog)
{
  return (dialog->contents_are_acceptable == NULL
          || dialog->contents_are_acceptable (dialog->acceptable_data));
}
