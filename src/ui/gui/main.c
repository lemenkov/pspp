/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005, 2006, 2010, 2011, 2012, 2013, 2014, 2015  Free Software Foundation

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

#include "gl/configmake.h"
#include "gl/progname.h"
#include "gl/relocatable.h"
#include "gl/version-etc.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


GdkWindow *create_splash_window (GMainContext *context);
gboolean destroy_splash_window (gpointer ud);




/* Arguments to be interpreted before the X server gets initialised */

enum
  {
    OPT_HELP,
    OPT_VERSION,
    OPT_NO_SPLASH,
    OPT_MEASURE_STARTUP,
    N_STARTUP_OPTIONS
  };

static const struct argv_option startup_options[N_STARTUP_OPTIONS] =
  {
    {"help",      'h', no_argument, OPT_HELP},
    {"version",   'V', no_argument, OPT_VERSION},
    {"no-splash", 'q', no_argument, OPT_NO_SPLASH},
    {"measure-startup", 0, no_argument, OPT_MEASURE_STARTUP},
  };

/* --measure-startup: Prints the elapsed time to start up and load any file
   specified on the command line. */
static gboolean measure_startup;
static GTimer *startup;

static void
usage (void)
{
  char *inc_path = string_array_join (include_path_default (), " ");
  GOptionGroup *gtk_options;
  GOptionContext *ctx;
  gchar *gtk_help_base, *gtk_help;

  /* Get help text for GTK+ options.  */
  ctx = g_option_context_new ("psppire");
  gtk_options = gtk_get_option_group (FALSE);
  gtk_help_base = g_option_context_get_help (ctx, FALSE, gtk_options);
  g_option_context_free (ctx);

  /* The GTK+ help text starts with usage instructions that we don't want,
     followed by a blank line.  Trim off everything up to and including the
     first blank line. */
  gtk_help = strstr (gtk_help_base, "\n\n");
  gtk_help = gtk_help != NULL ? gtk_help + 2 : gtk_help_base;

  printf (_("\
PSPPIRE, a GUI for PSPP, a program for statistical analysis of sampled data.\n\
Usage: %s [OPTION]... FILE\n\
\n\
Arguments to long options also apply to equivalent short options.\n\
\n\
GUI options:\n\
  -q, --no-splash           don't show splash screen during startup\n\
\n\
%s\
Language options:\n\
  -I, --include=DIR         append DIR to search path\n\
  -I-, --no-include         clear search path\n\
  -a, --algorithm={compatible|enhanced}\n\
                            set to `compatible' if you want output\n\
                            calculated from broken algorithms\n\
  -x, --syntax={compatible|enhanced}\n\
                            set to `compatible' to disable PSPP extensions\n\
  -i, --interactive         interpret syntax in interactive mode\n\
  -s, --safer               don't allow some unsafe operations\n\
Default search path: %s\n\
\n\
Informative output:\n\
  -h, --help                display this help and exit\n\
  -V, --version             output version information and exit\n\
\n\
A non-option argument is interpreted as a data file in .sav or .zsav or .por\n\
format or a syntax file to load.\n"),
          program_name, gtk_help, inc_path);

  free (inc_path);
  g_free (gtk_help_base);

  emit_bug_reporting_address ();
  exit (EXIT_SUCCESS);
}

static void
startup_option_callback (int id, void *show_splash_)
{
  gboolean *show_splash = show_splash_;

  switch (id)
    {
    case OPT_HELP:
      usage ();
      break;

    case OPT_VERSION:
      version_etc (stdout, "psppire", PACKAGE_NAME, PACKAGE_VERSION,
                   "Ben Pfaff", "John Darrington", "Jason Stover",
                   NULL_SENTINEL);
      exit (EXIT_SUCCESS);

    case OPT_NO_SPLASH:
      *show_splash = FALSE;
      break;

    case OPT_MEASURE_STARTUP:
      measure_startup = TRUE;
      break;

    default:
      NOT_REACHED ();
    }
}

static gboolean UNUSED
print_startup_time (gpointer data)
{
  g_timer_stop (startup);
  printf ("%.3f seconds elapsed\n", g_timer_elapsed (startup, NULL));
  g_timer_destroy (startup);
  startup = NULL;

  return FALSE;
}

#ifdef __APPLE__
static const bool apple = true;
#else
static const bool apple = false;
#endif

/* Searches ARGV for the -psn_xxxx option that the desktop application
   launcher passes in, and removes it if it finds it.  Returns the new value
   of ARGC. */
static inline int
remove_psn (int argc, char **argv)
{
  if (apple)
    {
      int i;

      for (i = 0; i < argc; i++)
	{
	  if (!strncmp (argv[i], "-psn", 4))
	    {
	      remove_element (argv, argc + 1, sizeof *argv, i);
	      return argc - 1;
	    }
	}
    }
  return argc;
}


gboolean
init_prepare (GSource *source, gint *timeout_)
{
  return TRUE;
}



gboolean
init_check (GSource *source)
{
  return TRUE;
}


gboolean
init_dispatch (GSource *ss,
	       GSourceFunc callback,
	       gpointer user_data)
{
  struct init_source *is = (struct init_source *)ss;

  bool finished = initialize (is);
  is->state++;
  
  if (finished)
    {
      g_main_loop_quit (is->loop);
      return FALSE;
    }

  return TRUE;
}

static GSourceFuncs init_funcs = {init_prepare, init_check, init_dispatch, NULL};



int
main (int argc, char *argv[])
{
  gboolean show_splash = TRUE;
  struct argv_parser *parser;
  const gchar *vers;

  set_program_name (argv[0]);

#if !GLIB_CHECK_VERSION(2,32,0)
  /* g_thread_init() was required before glib 2.32, but it is deprecated since
     then and calling it yields a compile-time warning. */
  g_thread_init (NULL);
#endif

  gtk_disable_setlocale ();

  startup = g_timer_new ();
  g_timer_start (startup);

  if ( (vers = gtk_check_version (GTK_MAJOR_VERSION,
				 GTK_MINOR_VERSION,
				 GTK_MICRO_VERSION)) )
    {
      g_warning ("%s", vers);
    }

  argc = remove_psn (argc, argv);

  /* Parse our own options. 
     This must come BEFORE gdk_init otherwise options such as 
     --help --version which ought to work without an X server, won't.
  */
  parser = argv_parser_create ();
  argv_parser_add_options (parser, startup_options, N_STARTUP_OPTIONS,
                           startup_option_callback, &show_splash);
  source_init_register_argv_parser (parser);
  if (!argv_parser_run (parser, argc, argv))
    exit (EXIT_FAILURE);
  argv_parser_destroy (parser);

  /* Initialise GDK.  GTK gets initialized later. */
  gdk_init (&argc, &argv);

  GMainContext *context = g_main_context_new ();
  
  GdkWindow *win = show_splash ? create_splash_window (context) : NULL;

  GMainLoop *loop = g_main_loop_new (context, FALSE);

  GSource *ss = g_source_new (&init_funcs,
			      sizeof (struct init_source));
  
  ((struct init_source *) ss)->state = 0;
  
  g_source_set_priority (ss, G_PRIORITY_DEFAULT);
    
  g_source_attach (ss, context);

  ((struct init_source *) ss)->argc = &argc;
  ((struct init_source *) ss)->argv = &argv;
  ((struct init_source *) ss)->loop = loop;
  ((struct init_source *) ss)->filename_arg = optind < argc ? optind : -1;
  
  g_source_unref (ss);

  g_main_loop_run (loop);

  g_main_loop_unref (loop);
  g_main_context_unref (context);

  if (win)
    g_timeout_add (500, destroy_splash_window, win);

  gtk_main ();

  /* Not much point in this except to check for memory leaks */
  de_initialize ();
  
  return 0;
}





struct splash_source
{
  GSource parent;
  cairo_surface_t *sfc;
};

void
fill_splash_window (GdkWindow *win, cairo_surface_t *sfce)
{
  cairo_t *cr = gdk_cairo_create (win);
  
  cairo_set_source_surface (cr, sfce, 0, 0);
  
  cairo_paint (cr);
  cairo_destroy (cr);
}

gboolean
splash_prepare  (GSource    *source,
	    gint       *timeout_)
{
  GdkEvent *e = gdk_event_peek ();
  if (!e)
    return FALSE;

  gdk_event_free (e);
  return TRUE;
}

gboolean
splash_check   (GSource    *source)
{
  GdkEvent *e = gdk_event_peek ();
  if (!e)
    return FALSE;

  gdk_event_free (e);
  return TRUE;
}


gboolean
splash_dispatch (GSource *ss,
	    GSourceFunc callback,
	    gpointer    user_data)
{
  struct splash_source *source = (struct splash_source *) ss;
  GdkEvent *e = gdk_event_get ();
  if (!e)
    return TRUE;

  GdkWindow *w = ((GdkEventAny *)e)->window;

  if (!w)
    {
      gdk_event_free (e);
      return TRUE;
    }

  fill_splash_window (w, source->sfc);
  gdk_display_flush (gdk_window_get_display (w));

  gdk_event_free (e);

  return TRUE;
}


gboolean
destroy_splash_window (gpointer ud)
{
  GdkWindow *win = GDK_WINDOW (ud);
  gdk_window_withdraw (win);
  gdk_display_flush (gdk_window_get_display (win));
  gdk_window_destroy (win);
  
  return FALSE;
}

GSourceFuncs splash_funcs = {splash_prepare, splash_check, splash_dispatch, NULL};


GdkWindow *
create_splash_window (GMainContext *context)
{
  const gchar *filename = PKGDATADIR "/splash.png";

  const char *relocated_filename = relocate (filename);
  cairo_surface_t *the_surface = 
    cairo_image_surface_create_from_png  (relocated_filename);

  if (filename != relocated_filename)
    free (CONST_CAST (char *, relocated_filename));

  
  g_return_val_if_fail (the_surface, NULL);
    
  int attr_mask = GDK_WA_TYPE_HINT;
  GdkWindowAttr attr;
    
  attr.width =  cairo_image_surface_get_width (the_surface);
  attr.height = cairo_image_surface_get_height (the_surface);
  attr.wclass = GDK_INPUT_OUTPUT; 
  attr.window_type = GDK_WINDOW_TOPLEVEL;
    
  attr.type_hint = GDK_WINDOW_TYPE_HINT_SPLASHSCREEN;
    
  GdkWindow *win = gdk_window_new (NULL, &attr, attr_mask);
    
  gdk_window_set_events (win, GDK_EXPOSURE_MASK);
  gdk_window_set_keep_above (win, TRUE);
  gdk_window_show (win);
  

  GSource *ss = g_source_new (&splash_funcs,
			      sizeof (struct splash_source));
  
  ((struct splash_source *) ss)->sfc = the_surface;
  g_source_set_priority (ss, G_PRIORITY_HIGH);
    
  g_source_attach (ss, context);

  g_source_unref (ss);
    
  return win;
}


