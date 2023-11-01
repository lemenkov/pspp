/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2016,
   2021  Free Software Foundation

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

#include "ui/gui/psppire-output-window.h"

#include <errno.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/string-map.h"
#include "output/driver-provider.h"
#include "output/output-item.h"
#include "ui/gui/help-menu.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/psppire-output-view.h"
#include "ui/gui/psppire-conf.h"
#include "ui/gui/windows-menu.h"

#include "gl/xalloc.h"

#include "helper.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

G_DEFINE_TYPE (PsppireOutputWindow, psppire_output_window, PSPPIRE_TYPE_WINDOW)

static GObjectClass *parent_class;

static void
psppire_output_window_finalize (GObject *object)
{
  if (G_OBJECT_CLASS (parent_class)->finalize)
    (*G_OBJECT_CLASS (parent_class)->finalize) (object);
}


static void
psppire_output_window_dispose (GObject *obj)
{
  PsppireOutputWindow *window = PSPPIRE_OUTPUT_WINDOW (obj);

  if (window->dispose_has_run)
    return;

  window->dispose_has_run = TRUE;
  psppire_output_view_destroy (window->view);
  window->view = NULL;

  /* Chain up to the parent class */
  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
psppire_output_window_class_init (PsppireOutputWindowClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);
  object_class->dispose = psppire_output_window_dispose;

  object_class->finalize = psppire_output_window_finalize;
}

/* Output driver class. */

struct psppire_output_driver
  {
    struct output_driver driver;
    PsppireOutputWindow *window;
  };

static struct output_driver_class psppire_output_class;

static struct psppire_output_driver *
psppire_output_cast (struct output_driver *driver)
{
  assert (driver->class == &psppire_output_class);
  return UP_CAST (driver, struct psppire_output_driver, driver);
}

static void
psppire_output_submit (struct output_driver *this,
                       const struct output_item *item)
{
  struct psppire_output_driver *pod = psppire_output_cast (this);
  PsppireOutputWindow *window;
  bool new;

  new = pod->window == NULL;
  if (new)
    {
      pod->window = PSPPIRE_OUTPUT_WINDOW (psppire_output_window_new ());
      GApplication *app = g_application_get_default ();
      gtk_application_add_window (GTK_APPLICATION (app),
                                  GTK_WINDOW (pod->window));

      pod->window->driver = pod;
    }
  window = pod->window;

  psppire_output_view_put (window->view, item);

  if (new)
    {
      /* We could have called this earlier in the previous "if (new)" block,
         but doing it here finds, in a plain GTK+ environment, a bug that
         otherwise only showed up on an Ubuntu Unity desktop.  See bug
         #43362. */
      gtk_widget_show_all (GTK_WIDGET (pod->window));
    }

  {
    gboolean status = true;
    psppire_conf_get_boolean ("OutputWindowAction", "alert", &status);
    gtk_window_set_urgency_hint (GTK_WINDOW (pod->window), status);
  }

  {
    gboolean status ;
    if (psppire_conf_get_boolean ("OutputWindowAction", "maximize", &status)
        && status)
      gtk_window_maximize (GTK_WINDOW (pod->window));
  }

  {
    gboolean status ;
    if (psppire_conf_get_boolean ("OutputWindowAction", "raise", &status)
        && status)
      gtk_window_present (GTK_WINDOW (pod->window));
  }
}

static struct output_driver_class psppire_output_class =
  {
    .name = "PSPPIRE",
    .submit = psppire_output_submit,
    .handles_groups = true,
    .handles_show = true,
  };

void
psppire_output_window_setup (void)
{
  struct psppire_output_driver *pod = xmalloc (sizeof *pod);
  *pod = (struct psppire_output_driver) {
    .driver = {
      .class = &psppire_output_class,
      .name = xstrdup ("PSPPIRE"),
      .device_type = SETTINGS_DEVICE_UNFILTERED,
    },
  };
  output_driver_register (&pod->driver);
}



/* Callback for the "delete" action (clicking the x on the top right
   hand corner of the window) */
static gboolean
on_delete (GtkWidget *w, GdkEvent *event, gpointer user_data)
{
  PsppireOutputWindow *ow = PSPPIRE_OUTPUT_WINDOW (user_data);

  gtk_widget_destroy (GTK_WIDGET (ow));

  ow->driver->window = NULL;

  return FALSE;
}



static void
cancel_urgency (GtkWindow *window,  gpointer data)
{
  gtk_window_set_urgency_hint (window, FALSE);
}

static void psppire_output_window_print (PsppireOutputWindow *window);


static void
export_output (PsppireOutputWindow *window, struct string_map *options,
               const char *format)
{
  string_map_insert (options, "format", format);
  psppire_output_view_export (window->view, options);
}


struct file_types
{
  const gchar *label;
  const gchar *ext;
};

enum
  {
    FT_AUTO = 0,
    FT_SPV,
    FT_PDF,
    FT_HTML,
    FT_ODT,
    FT_TXT,
    FT_ASCII,
    FT_PS,
    FT_CSV,
    FT_PNG,
    FT_SVG,
    n_FT
  };

static const struct file_types ft[n_FT] = {
  {N_("Infer file type from extension"),  NULL},
  {N_("SPSS Viewer (*.spv)"),             ".spv"},
  {N_("PDF (*.pdf)"),                     ".pdf"},
  {N_("HTML (*.html)"),                   ".html"},
  {N_("OpenDocument (*.odt)"),            ".odt"},
  {N_("Text (*.txt)"),                    ".txt"},
  {N_("Text [plain] (*.txt)"),            ".txt"},
  {N_("PostScript (*.ps)"),               ".ps"},
  {N_("Comma-Separated Values (*.csv)"),  ".csv"},
  {N_("Portable Network Graphics (*.png)"),  ".png"},
  {N_("Scalable Vector Graphics (*.svg)"),   ".svg"}
};


static void
on_combo_change (GtkFileChooser *chooser)
{
  gboolean sensitive = FALSE;
  GtkWidget *combo = gtk_file_chooser_get_extra_widget (chooser);

  int file_type = FT_AUTO;
  gchar *fn = gtk_file_chooser_get_filename (chooser);

  if (combo &&  gtk_widget_get_realized (combo))
    file_type = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));

  if (fn != NULL && file_type == FT_AUTO)
    {
      for (gint i = 1 ; i < n_FT ; ++i)
        {
          if (g_str_has_suffix (fn, ft[i].ext))
            {
              sensitive = TRUE;
              break;
            }
        }
    }
  else
    sensitive = (fn != NULL);

  g_free (fn);

  gtk_dialog_set_response_sensitive (GTK_DIALOG (chooser), GTK_RESPONSE_ACCEPT, sensitive);
}


static GtkListStore *
create_file_type_list (void)
{
  int i;
  GtkTreeIter iter;
  GtkListStore *list = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

  for (i = 0 ; i < n_FT ; ++i)
    {
      gtk_list_store_append (list, &iter);
      gtk_list_store_set (list, &iter,
                          0,  gettext (ft[i].label),
                          1,  ft[i].ext,
                          -1);
    }

  return list;
}

static void
psppire_output_window_export (PsppireOutputWindow *window)
{
  gint response;
  GtkWidget *combo;
  GtkListStore *list;

  GtkFileChooser *chooser;

  GtkWidget *dialog = gtk_file_chooser_dialog_new (_("Export Output"),
                                        GTK_WINDOW (window),
                                        GTK_FILE_CHOOSER_ACTION_SAVE,
                                        _("Cancel"), GTK_RESPONSE_CANCEL,
                                        _("Save"),   GTK_RESPONSE_ACCEPT,
                                        NULL);

  g_object_set (dialog, "local-only", FALSE, NULL);

  chooser = GTK_FILE_CHOOSER (dialog);

  list = create_file_type_list ();

  combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (list));


  {
    /* Create text cell renderer */
    GtkCellRenderer *cell = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), cell, FALSE);

    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (combo), cell,  "text", 0);
  }

  g_signal_connect_swapped (combo, "changed", G_CALLBACK (on_combo_change), chooser);

  gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);

  gtk_file_chooser_set_extra_widget (chooser, combo);

  g_signal_connect (chooser, "selection-changed", G_CALLBACK (on_combo_change), NULL);
  gtk_file_chooser_set_do_overwrite_confirmation (chooser, TRUE);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_ACCEPT)
    {
      gint file_type = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));
      gchar *filename = gtk_file_chooser_get_filename (chooser);
      struct string_map options;

      g_return_if_fail (filename);

      if (file_type == FT_AUTO)
        {
          /* If the "Infer file type from extension" option was chosen,
             search for the respective type in the list.
             (It's a O(n) search, but fortunately n is small). */
          gint i;
          for (i = 1 ; i < n_FT ; ++i)
            {
              if (g_str_has_suffix (filename, ft[i].ext))
                {
                  file_type = i;
                  break;
                }
            }
        }
      else if (! g_str_has_suffix (filename, ft[file_type].ext))
        {
          /* If an explicit document format was chosen, and if the chosen
             filename does not already have that particular "extension",
             then append it.
           */

          gchar *of = filename;
          filename = g_strconcat (filename, ft[file_type].ext, NULL);
          g_free (of);
        }

      string_map_init (&options);
      string_map_insert (&options, "output-file", filename);

      switch (file_type)
        {
        case FT_SPV:
          export_output (window, &options, "spv");
          break;
        case FT_PDF:
          export_output (window, &options, "pdf");
          break;
        case FT_HTML:
          export_output (window, &options, "html");
          break;
        case FT_ODT:
          export_output (window, &options, "odt");
          break;
        case FT_PS:
          export_output (window, &options, "ps");
          break;
        case FT_CSV:
          export_output (window, &options, "csv");
          break;
        case FT_PNG:
          export_output (window, &options, "png");
          break;
        case FT_SVG:
          export_output (window, &options, "svg");
          break;

        case FT_TXT:
          string_map_insert (&options, "box", "unicode");
          /* Fall through */

        case FT_ASCII:
          string_map_insert (&options, "charts", "none");
          export_output (window, &options, "txt");
          break;
        default:
          g_assert_not_reached ();
        }

      string_map_destroy (&options);

      free (filename);
    }

  gtk_widget_destroy (dialog);
}

static void
psppire_output_window_init (PsppireOutputWindow *window)
{
  GtkBuilder *xml = builder_new ("output-window.ui");
  GtkApplication *app = GTK_APPLICATION (g_application_get_default());
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add (GTK_CONTAINER (window), box);

  GtkWidget *paned = get_widget_assert (xml, "paned1");

  window->dispose_has_run = FALSE;

  window->view = psppire_output_view_new (
    GTK_LAYOUT (get_widget_assert (xml, "output")),
    GTK_TREE_VIEW (get_widget_assert (xml, "overview")));

  g_signal_connect (window,
                    "focus-in-event",
                    G_CALLBACK (cancel_urgency),
                    NULL);

  GObject *menu = get_object_assert (xml, "output-window-menu", G_TYPE_MENU);
  GtkWidget *menubar = gtk_menu_bar_new_from_model (G_MENU_MODEL (menu));
  gtk_box_pack_start (GTK_BOX (box), menubar, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), paned, TRUE, TRUE, 0);

  gtk_menu_shell_append (GTK_MENU_SHELL (menubar),
                         create_windows_menu (GTK_WINDOW (window)));

  gtk_menu_shell_append (GTK_MENU_SHELL (menubar),
                         create_help_menu (GTK_WINDOW (window)));

  {
    GSimpleAction *print = g_simple_action_new ("print", NULL);
    g_signal_connect_swapped (print, "activate", G_CALLBACK (psppire_output_window_print), window);
    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (print));


    const gchar *accels[2] = { "<Primary>P", NULL};
    gtk_application_set_accels_for_action (app,
                                           "win.print",
                                           accels);
  }


  {
    GSimpleAction *export = g_simple_action_new ("export", NULL);
    g_signal_connect_swapped (export, "activate", G_CALLBACK (psppire_output_window_export), window);
    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (export));
  }

  {
    GSimpleAction *close = g_simple_action_new ("close", NULL);
    g_signal_connect_swapped (close, "activate", G_CALLBACK (gtk_window_close), window);
    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (close));

    const gchar *accels[2] = { "<Primary>W", NULL};
    gtk_application_set_accels_for_action (app, "win.close", accels);
  }

  {
    GSimpleAction *select_all = g_simple_action_new ("select-all", NULL);
    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (select_all));
  }

  {
    GSimpleAction *copy = g_simple_action_new ("copy", NULL);
    g_action_map_add_action (G_ACTION_MAP (window), G_ACTION (copy));

    const gchar *accels[2] = { "<Primary>C", NULL};
    gtk_application_set_accels_for_action (app,
                                           "win.copy",
                                           accels);
  }


  g_object_unref (xml);

  g_signal_connect (window, "delete-event",
                    G_CALLBACK (on_delete), window);
}


GtkWidget*
psppire_output_window_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_output_window_get_type (),
                                   /* TRANSLATORS: This will be part of a filename.  Please avoid whitespace. */
                                   "filename", _("Output"),
                                   "description", _("Output Viewer"),
                                   NULL));
}

static void
psppire_output_window_print (PsppireOutputWindow *window)
{
  psppire_output_view_print (window->view, GTK_WINDOW (window));
}
