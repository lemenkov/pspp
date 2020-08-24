/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005, 2006, 2010, 2011, 2012, 2013, 2014, 2015, 2016  Free Software Foundation

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

#include "ui/gui/psppire.h"

#include <gtk/gtk.h>
#include <stdlib.h>
#if ENABLE_RELOCATABLE && defined(__APPLE__)
#include <sys/stat.h>
#endif

#include "language/lexer/include-path.h"
#include "libpspp/argv-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/copyleft.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"
#include "libpspp/version.h"
#include "ui/source-init-opts.h"
#include "ui/gui/psppire-syntax-window.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-output-window.h"

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
  { init_prepare, init_check, init_dispatch, NULL };



GtkWidget *wsplash = 0;
gint64 start_time = 0;


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


static void
on_startup (GApplication * app, gpointer ud)
{
  GMainContext *context = g_main_context_new ();

  if (start_time != 0)
    {
      wsplash = create_splash_window ();
      gtk_application_add_window (GTK_APPLICATION (app),
                                  GTK_WINDOW (wsplash));
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

static void
on_activate (GApplication * app, gpointer ud)
{
  post_initialise (app);

  GtkWindow *x = create_data_window ();
  gtk_application_add_window (GTK_APPLICATION (app), x);

  wait_for_splash (app, x);
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
    {NULL}
  };

  GOptionContext *oc = g_option_context_new ("");
  g_option_context_set_help_enabled (oc, FALSE);
  g_option_context_set_ignore_unknown_options (oc, FALSE);
  g_option_context_add_main_entries (oc, oe, NULL);
  g_option_context_parse (oc, argc, argv, NULL);
  g_option_context_free (oc);
}

#if ENABLE_RELOCATABLE && defined(__APPLE__)
static void
pspp_macos_setenv (const char * progname)
{
  /* helper to set environment variables for pspp to be relocatable.
   * Due to the latest changes it is not recommended to set it in the shell
   * wrapper anymore.
   */
  gchar resolved_path[PATH_MAX];
  /* on some OSX installations open file limit is 256 and GIMP needs more */
  struct rlimit limit;
  limit.rlim_cur = 10000;
  limit.rlim_max = 10000;
  setrlimit (RLIMIT_NOFILE, &limit);
  if (realpath (progname, resolved_path))
    {
      gchar  tmp[PATH_MAX];
      gchar *app_dir;
      gchar  res_dir[PATH_MAX];
      struct stat sb;

      app_dir = g_path_get_dirname (resolved_path);
      g_snprintf (tmp, sizeof(tmp), "%s/../../Resources", app_dir);
      if (realpath (tmp, res_dir) && !stat (res_dir,&sb) && S_ISDIR (sb.st_mode))
        g_print ("pspp is started as MacOS application\n");
      else
        return;
      g_free (app_dir);

      g_snprintf (tmp, sizeof(tmp), "%s/lib/gtk-3.0/3.0.0", res_dir);
      g_setenv ("GTK_PATH", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/etc/gtk-3.0/gtk.immodules", res_dir);
      g_setenv ("GTK_IM_MODULE_FILE", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/lib/gegl-0.4", res_dir);
      g_setenv ("GEGL_PATH", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/lib/babl-0.1", res_dir);
      g_setenv ("BABL_PATH", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache", res_dir);
      g_setenv ("GDK_PIXBUF_MODULE_FILE", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/etc/fonts", res_dir);
      g_setenv ("FONTCONFIG_PATH", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/lib/gio/modules", res_dir);
      g_setenv ("GIO_MODULE_DIR", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/etc/xdg", res_dir);
      g_setenv ("XDG_CONFIG_DIRS", tmp, TRUE);
      g_snprintf (tmp, sizeof(tmp), "%s/share", res_dir);
      g_setenv ("XDG_DATA_DIRS", tmp, TRUE);

      if (g_getenv ("HOME")!=NULL)
        {
          g_snprintf (tmp, sizeof(tmp),
                      "%s/Library/Application Support/pspp/1.3/cache",
                      g_getenv("HOME"));
          g_setenv ("XDG_CACHE_HOME", tmp, TRUE);
        }
    }
}
#endif

int
main (int argc, char *argv[])
{

#if ENABLE_RELOCATABLE && defined(__APPLE__)
  /* remove MacOS session identifier from the command line args */
  gint newargc = 0;
  for (gint i = 0; i < argc; i++)
    {
      if (!g_str_has_prefix (argv[i], "-psn_"))
        {
          argv[newargc] = argv[i];
          newargc++;
        }
    }
  if (argc > newargc)
    {
      argv[newargc] = NULL; /* glib expects NULL terminated array */
      argc = newargc;
    }
  pspp_macos_setenv (argv[0]);
#endif

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
