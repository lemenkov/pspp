/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005, 2006, 2010, 2011, 2012, 2013, 2014, 2015,
   2016, 2020, 2021  Free Software Foundation

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

#include "pre-initialisation.h"

#include "ui/gui/psppire.h"

#include <gtk/gtk.h>
#include <stdlib.h>

#include "language/lexer/include-path.h"
#include "libpspp/argv-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/copyleft.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"
#include "libpspp/version.h"
#include "ui/source-init-opts.h"
#include "ui/gui/psppire-syntax-window.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-output-window.h"
#include "ui/gui/psppire-conf.h"
#include "ui/gui/helper.h"

#include "gl/configmake.h"
#include "gl/progname.h"
#include "gl/relocatable.h"
#include "gl/version-etc.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid



static gboolean
show_version_and_exit (void)
{
  version_etc (stdout, "psppire", PACKAGE_NAME, PACKAGE_VERSION,
               "Ben Pfaff", "John Darrington", "Jason Stover", NULL_SENTINEL);

  exit (0);

  return TRUE;
}



static gboolean
init_prepare (GSource * source, gint * timeout_)
{
  return TRUE;
}

static gboolean
init_check (GSource * source)
{
  return TRUE;
}

static gboolean
init_dispatch (GSource * ss, GSourceFunc callback, gpointer user_data)
{
  struct init_source *is = (struct init_source *) ss;

  bool finished = initialize (is);
  is->state++;

  if (finished)
    {
      g_main_loop_quit (is->loop);
      return FALSE;
    }

  return TRUE;
}

static GSourceFuncs init_funcs =
  {init_prepare, init_check, init_dispatch, NULL, NULL, NULL };

static GtkWidget *wsplash = 0;
static gint64 start_time = 0;


static GtkWidget *
create_splash_window (void)
{
  GtkWidget *sp = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  const gchar *filename = PKGDATADIR "/splash.png";
  const char *relocated_filename = relocate (filename);
  GtkWidget *l = gtk_image_new_from_file (relocated_filename);
  if (filename != relocated_filename)
    free (CONST_CAST (char *, relocated_filename));

  gtk_container_add (GTK_CONTAINER (sp), l);
  gtk_window_set_type_hint (GTK_WINDOW (sp),
                            GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);
  gtk_window_set_position (GTK_WINDOW (sp), GTK_WIN_POS_CENTER);
  gtk_window_set_skip_pager_hint (GTK_WINDOW (sp), TRUE);
  gtk_window_set_skip_taskbar_hint (GTK_WINDOW (sp), TRUE);
  gtk_window_set_focus_on_map (GTK_WINDOW (sp), FALSE);
  gtk_window_set_accept_focus (GTK_WINDOW (sp), FALSE);

  GdkGeometry hints;
  hints.max_height = 100;
  hints.max_width = 200;
  gtk_window_set_geometry_hints (GTK_WINDOW (sp),
                                 NULL, &hints, GDK_HINT_MAX_SIZE);


  gtk_window_set_gravity (GTK_WINDOW (sp), GDK_GRAVITY_CENTER);

  gtk_window_set_modal (GTK_WINDOW (sp), TRUE);
  gtk_window_set_decorated (GTK_WINDOW (sp), FALSE);
  gtk_window_set_keep_above (GTK_WINDOW (sp), TRUE);
  gtk_widget_show_all (sp);
  return sp;
}


static gint
on_local_options (GApplication * application,
                  GVariantDict * options, gpointer user_data)
{
  {
    GVariant *b =
      g_variant_dict_lookup_value (options, "no-unique",
				   G_VARIANT_TYPE_BOOLEAN);
    if (b)
      {
	GApplicationFlags flags =  g_application_get_flags (application);
	flags |= G_APPLICATION_NON_UNIQUE;
	g_application_set_flags (application, flags);
	g_variant_unref (b);
      }
  }
  {
    GVariant *b =
      g_variant_dict_lookup_value (options, "no-splash",
				   G_VARIANT_TYPE_BOOLEAN);
    if (b)
      g_variant_unref (b);
    else
      start_time = g_get_monotonic_time ();
  }


  return -1;
}

/* Use the imperitive mood for all entries in this table.
   Each entry should end with a period.   */
static const char *tips[] =
  {
#ifdef _WIN32
   N_("PSPP runs best on free platforms such as GNU and GNU/Linux.  Windows is a non-free system.  As such, certain features might work sub-optimally.  For best results use a free system instead."),
#endif
   N_("Right click on variable lists to change between viewing the variables' names and their labels."),
   N_("Click \"Paste\" instead of \"OK\" when running procedures.  This allows you to edit your commands before running them and you have better control over your work."),
   N_("Directly import your spreadsheets using the \"File | Import Data\" menu."),
   N_("For an easy way to convert string variables into numerically encoded variables, use \"Automatic Recode\"  which preserves the variable names as labels."),
   N_("When browsing large data sets, use \"Windows | Split\" to see both ends of the data in the same view."),
   N_("Export your reports to ODT format for easy editing with the Libreoffice.org suite."),
   N_("Use \"Edit | Options\" to have your Output window automatically appear when statistics are generated."),
   N_("To easily reorder your variables, drag and drop them in the Variable View or the Data View.")
  };

#define N_TIPS  (sizeof tips / sizeof tips[0])

static void
user_tip (GApplication *app)
{
  PsppireConf *conf = psppire_conf_new ();

  gboolean show_tip = TRUE;
  psppire_conf_get_boolean (conf, "startup", "show-user-tips", &show_tip);

  if (!show_tip)
    return;

  GtkWindow *parent = gtk_application_get_active_window (GTK_APPLICATION (app));

  GtkWidget *d =
    gtk_dialog_new_with_buttons (_("Psppire User Hint"), parent,
                                 GTK_DIALOG_MODAL,
                                 GTK_MESSAGE_INFO,
                                 0, 0,
                                 NULL);

  GtkWidget *pictogram = gtk_image_new_from_icon_name ("user-info", GTK_ICON_SIZE_DIALOG);

  GtkWidget *next = gtk_button_new_with_mnemonic (_("_Next Tip"));
  gtk_dialog_add_action_widget (GTK_DIALOG (d), next, 1);

  GtkWidget *close = gtk_button_new_with_mnemonic (_("_Close"));
  gtk_dialog_add_action_widget (GTK_DIALOG (d), close, GTK_RESPONSE_CLOSE);

  gtk_window_set_transient_for (GTK_WINDOW (d), parent);

  g_object_set (d,
                "decorated", FALSE,
                "skip-taskbar-hint", TRUE,
                "skip-pager-hint", TRUE,
                "application", app,
                NULL);

  GtkWidget *ca = gtk_dialog_get_content_area (GTK_DIALOG (d));

  g_object_set (ca, "margin", 5, NULL);

  GtkWidget *check = gtk_check_button_new_with_mnemonic ("_Show tips at startup");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), show_tip);

  srand (time(0));
  gint x = rand () % N_TIPS;
  GtkWidget *label = gtk_label_new (gettext (tips[x]));

  /* Make the font of the label a little larger than the other widgets.  */
  {
    GtkStyleContext *sc = gtk_widget_get_style_context (label);
    GtkCssProvider *p = gtk_css_provider_new ();
    const gchar *css = "* {font-size: 130%;}";
    if (gtk_css_provider_load_from_data (p, css, strlen (css), NULL))
      {
        gtk_style_context_add_provider (sc, GTK_STYLE_PROVIDER (p),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
      }
    g_object_unref (p);
  }

  /* It's more readable if the text is not all in one long line.  */
  g_object_set (label, "wrap", TRUE, NULL);
  gint width = PANGO_PIXELS (50.0 * width_of_m (label) * PANGO_SCALE);
  gtk_window_set_default_size (GTK_WINDOW (d), width, -1);


  if (pictogram)
    gtk_box_pack_start (GTK_BOX (ca), pictogram, FALSE, FALSE, 5);
  gtk_box_pack_start (GTK_BOX (ca), label, FALSE, FALSE, 5);
  gtk_box_pack_end (GTK_BOX (ca), check, FALSE, FALSE, 5);

  gtk_widget_show_all (d);

  g_object_set (close,
                "has-focus", TRUE,
                "is-focus", TRUE,
                NULL);

  while (1 == gtk_dialog_run (GTK_DIALOG (d)))
    {
      if (++x >= N_TIPS) x = 0;
      g_object_set (label, "label", gettext (tips[x]), NULL);
    }

  show_tip = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check));
  psppire_conf_set_boolean (conf,
                            "startup", "show-user-tips",
                            show_tip);

  g_object_unref (conf);

  gtk_widget_destroy (d);
}


static void
on_startup (GApplication * app, gpointer ud)
{
  GMainContext *context = g_main_context_new ();

  if (start_time != 0)
    {
      wsplash = create_splash_window ();
      gtk_application_add_window (GTK_APPLICATION (app),
                                  GTK_WINDOW (wsplash));

      g_signal_connect_swapped (wsplash, "destroy", G_CALLBACK (user_tip), app);
    }
  else
    {
      g_signal_connect (app, "activate", G_CALLBACK (user_tip), NULL);
    }

  GMainLoop *loop = g_main_loop_new (context, FALSE);

  GSource *ss = g_source_new (&init_funcs, sizeof (struct init_source));

  ((struct init_source *) ss)->loop = loop;
  ((struct init_source *) ss)->state = 0;

  g_source_set_priority (ss, G_PRIORITY_DEFAULT);

  g_source_attach (ss, context);
  g_main_loop_run (loop);
}


static void
post_initialise (GApplication * app)
{
  register_selection_functions ();
  psppire_output_window_setup ();

  GSimpleAction *quit = g_simple_action_new ("quit", NULL);
  g_signal_connect_swapped (quit, "activate", G_CALLBACK (psppire_quit), app);
  g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (quit));
}


#define SPLASH_DURATION 1000

static gboolean
destroy_splash (gpointer ud)
{
  GtkWidget *sp = GTK_WIDGET (ud);
  gtk_widget_destroy (sp);
  wsplash = NULL;
  return G_SOURCE_REMOVE;
}


static void
wait_for_splash (GApplication *app, GtkWindow *x)
{
  if (wsplash)
    {
      gtk_window_set_transient_for (GTK_WINDOW (wsplash), x);
      gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (wsplash));
      gtk_window_set_keep_above (GTK_WINDOW (wsplash), TRUE);
      gtk_window_present (GTK_WINDOW (wsplash));

      /* Remove the splash screen after SPLASH_DURATION milliseconds */
      gint64 elapsed_time = (g_get_monotonic_time () - start_time) / 1000;
      if (SPLASH_DURATION - elapsed_time <= 0)
	destroy_splash (wsplash);
      else
	g_timeout_add (SPLASH_DURATION - elapsed_time, destroy_splash, wsplash);
    }
}

static GtkWidget *fatal_error_dialog = NULL;
static GtkWidget *fatal_error_label;
static const char *diagnostic_info;

static void
fatal_error_handler (int sig)
{
  /* Reset SIG to its default handling so that if it happens again we won't
     recurse. */
  signal (sig, SIG_DFL);

  static char message [1024];
  strcpy (message, "proximate cause:    ");
  switch (sig)
    {
    case SIGABRT:
      strcat (message, "Assertion Failure/Abort");
      break;
    case SIGFPE:
      strcat (message, "Floating Point Exception");
      break;
    case SIGSEGV:
      strcat (message, "Segmentation Violation");
      break;
    default:
      strcat (message, "Unknown");
      break;
    }
  strcat (message, "\n");
  strcat (message, diagnostic_info);

  g_object_set (fatal_error_label,
                "label", message,
                NULL);

  gtk_dialog_run (GTK_DIALOG (fatal_error_dialog));

  /* Re-raise the signal so that we terminate with the correct status. */
  raise (sig);
}

static void
on_activate (GApplication * app, gpointer ud)
{
  struct sigaction fatal_error_action;
  sigset_t sigset;
  g_return_if_fail (0 == sigemptyset (&sigset));
  fatal_error_dialog =
    gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                            _("Psppire: Fatal Error"));

  diagnostic_info = prepare_diagnostic_information ();

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (fatal_error_dialog),
                                            _("You have discovered a bug in PSPP.  "
                                              "Please report this to %s including all of the following information, "
                                              "and a description of what you were doing when this happened."),
                                            PACKAGE_BUGREPORT);

  g_return_if_fail (fatal_error_dialog != NULL);

  GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG (fatal_error_dialog));
  fatal_error_label = gtk_label_new ("");
  g_object_set (fatal_error_label,
                "selectable", TRUE,
                "wrap", TRUE,
                NULL);
  gtk_container_add (GTK_CONTAINER (content_area), fatal_error_label);

  gtk_widget_show_all (content_area);

  fatal_error_action.sa_handler = fatal_error_handler;
  fatal_error_action.sa_mask = sigset;
  fatal_error_action.sa_flags = 0;

  post_initialise (app);

  GtkWindow *x = create_data_window ();
  gtk_application_add_window (GTK_APPLICATION (app), x);

  wait_for_splash (app, x);
  sigaction (SIGABRT, &fatal_error_action, NULL);
  sigaction (SIGSEGV, &fatal_error_action, NULL);
  sigaction (SIGFPE,  &fatal_error_action, NULL);
}

static GtkWindow *
find_empty_data_window (GApplication *app)
{
  GList *wl = gtk_application_get_windows (GTK_APPLICATION (app));
  while (wl)
    {
      if (wl->data && PSPPIRE_IS_DATA_WINDOW (GTK_WINDOW (wl->data)) &&
          psppire_data_window_is_empty (PSPPIRE_DATA_WINDOW (wl->data)))
        return GTK_WINDOW (wl->data);
      wl = wl->next;
    }
  return NULL;
}

static GtkWindow *
find_psppire_window (GApplication *app)
{
  GList *wl = gtk_application_get_windows (GTK_APPLICATION (app));
  while (wl)
    {
      if (wl->data && PSPPIRE_IS_WINDOW (GTK_WINDOW (wl->data)))
        return GTK_WINDOW (wl->data);
      wl = wl->next;
    }
  return NULL;
}

static void
on_open (GApplication *app, GFile **files, gint n_files, gchar * hint,
         gpointer ud)
{
  /* If the application is already open and we open another file
     via xdg-open on GNU/Linux or via the file manager, then open is
     called. Check if we already have a psppire window. */
  if (find_psppire_window (app) == NULL)
    post_initialise (app);

  /* When a new data file is opened, then try to find an empty
     data window which will then be replaced as in the open file
     dialog */
  GtkWindow *victim = find_empty_data_window (app);

  gchar *file = g_file_get_parse_name (files[0]);
  GtkWindow *x = psppire_preload_file (file, victim);
  g_free (file);

  wait_for_splash (app, x);
}


/* These are arguments which must be processed BEFORE the X server has been initialised */
static void
process_pre_start_arguments (int *argc, char ***argv)
{
  GOptionEntry oe[] = {
    {"version", 'V', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
     show_version_and_exit, N_("Show version information and exit"), 0},
    {NULL, 0, 0, 0, NULL, "", 0}
  };

  GOptionContext *oc = g_option_context_new ("");
  g_option_context_set_help_enabled (oc, FALSE);
  g_option_context_set_ignore_unknown_options (oc, FALSE);
  g_option_context_add_main_entries (oc, oe, NULL);
  g_option_context_parse (oc, argc, argv, NULL);
  g_option_context_free (oc);
}

int
main (int argc, char *argv[])
{
  /* Some operating systems need to munge the arguments.  */
  pre_initialisation (&argc, argv);

  set_program_name (argv[0]);

  GtkApplication *app =
    gtk_application_new ("gnu.pspp", G_APPLICATION_HANDLES_OPEN);

  process_pre_start_arguments (&argc, &argv);

  GOptionEntry oe[] = {
    {"no-splash", 'q', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, NULL,
      N_("Do not display the splash screen"), 0},
    {"no-unique", 'n', G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, NULL,
      N_("Do not attempt single instance negotiation"), 0},
    {NULL}
  };

  g_application_add_main_option_entries (G_APPLICATION (app), oe);

  g_signal_connect (app, "startup", G_CALLBACK (on_startup), NULL);
  g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
  g_signal_connect (app, "handle-local-options",
                    G_CALLBACK (on_local_options), NULL);
  g_signal_connect (app, "open", G_CALLBACK (on_open), NULL);

  {
    GSimpleAction *act_new_syntax = g_simple_action_new ("new-syntax", NULL);
    g_signal_connect_swapped (act_new_syntax, "activate",
                              G_CALLBACK (create_syntax_window), NULL);
    g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (act_new_syntax));
  }

  {
    GSimpleAction *act_new_data = g_simple_action_new ("new-data", NULL);
    g_signal_connect_swapped (act_new_data, "activate",
                              G_CALLBACK (create_data_window), NULL);
    g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (act_new_data));
  }

  g_object_set (G_OBJECT (app), "register-session", TRUE, NULL);
  return g_application_run (G_APPLICATION (app), argc, argv);
}
