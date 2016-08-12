/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2007, 2010, 2011, 2012, 2013, 2015, 2016  Free Software Foundation

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

#include <libpspp/copyleft.h>
#include <libpspp/version.h>
#include "help-menu.h"
#include <libpspp/message.h>

#include "gl/configmake.h"
#include "gl/relocatable.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Try to open html documentation uri via the default
   browser on the operating system */
#ifdef __APPLE__
#define HTMLOPENARGV {"open", 0, 0}
#elif  _WIN32
#define HTMLOPENARGV {"wscript", 0, 0}
#else
#define HTMLOPENARGV {"xdg-open", 0, 0}
#endif

static const gchar *artists[] = { "Bastián Díaz", "Hugo Alejandro", NULL};

static void
about_new (GtkMenuItem *mmm, GtkWindow *parent)
{
  GtkWidget *about =  gtk_about_dialog_new ();

  gtk_about_dialog_set_logo_icon_name (GTK_ABOUT_DIALOG (about), "pspp");

  gtk_window_set_icon_name (GTK_WINDOW (about), "pspp");

  gtk_about_dialog_set_website (GTK_ABOUT_DIALOG (about), PACKAGE_URL);

  gtk_about_dialog_set_version (GTK_ABOUT_DIALOG (about),
				announced_version);

  gtk_about_dialog_set_authors (GTK_ABOUT_DIALOG (about),
				(const gchar **) authors);

  gtk_about_dialog_set_artists (GTK_ABOUT_DIALOG (about),
				artists);

  gtk_about_dialog_set_license (GTK_ABOUT_DIALOG (about),
				copyleft);

  gtk_about_dialog_set_comments (GTK_ABOUT_DIALOG (about),
				 _("A program for the analysis of sampled data"));

  gtk_about_dialog_set_copyright (GTK_ABOUT_DIALOG (about),
				  "Free Software Foundation");

  gtk_about_dialog_set_translator_credits 
    (
     GTK_ABOUT_DIALOG (about),
     /* TRANSLATORS: Do not translate this string.  Instead, put the names of the people
	who have helped in the translation. */
     _("translator-credits")
     );

  gtk_window_set_transient_for (GTK_WINDOW (about), parent);

  gtk_window_set_modal (GTK_WINDOW (about), TRUE);

  gtk_dialog_run (GTK_DIALOG (about));

  gtk_widget_hide (about);
}


/* Opening the htmluri in windows via cmd /start uri opens
   the windows command shell for a moment. The alternative is
   to start a script via wscript. This will not be visible*/
#ifdef _WIN32
static gboolean open_windows_help (const gchar *helpuri,
                                   GError **err)
{
  gchar *vbsfilename = NULL;
  gchar *vbs = NULL;
  gboolean result;
  vbsfilename = g_build_filename (g_get_tmp_dir (),
                                  "pspp-help-open.vbs",
                                  NULL);
  vbs = g_strdup_printf("CreateObject(\"WScript.Shell\").Run \"%s\"",
                        helpuri);
  result = g_file_set_contents (vbsfilename,
                                vbs,
                                strlen(vbs),
                                err);
  g_free (vbs);
  if (!result)
    goto error;

  gchar *argv[] = {"wscript",vbsfilename,0};

  result = g_spawn_async (NULL, argv,
                          NULL, G_SPAWN_SEARCH_PATH,
                          NULL, NULL, NULL, err);
 error:
  g_free (vbsfilename);
  return result;
}
#endif

/* Open the manual at PAGE with the following priorities
   First: local yelp help system
   Second: browser with local html doc dir in path pspp.html/<helppage>.html
   Third:  browers with Internet html help at gnu.org */
void
online_help (const char *page)
{
  GError *err = NULL;
  GError *htmlerr = NULL;
  gchar *argv[3] = { "yelp", 0, 0};
  gchar *htmlargv[3] = HTMLOPENARGV;
  gchar *htmlfilename = NULL;
  gchar *htmlfullname = NULL;
  gchar *htmluri = NULL;

  if (page == NULL)
    {
      argv[1] = g_strdup_printf ("file://%s", relocate (DOCDIR "/pspp.xml"));
      htmlfilename = g_strdup ("index.html");
    }
  else
    {
      gchar **tokens = NULL;
      const int maxtokens = 5;
      int idx ;
      argv[1] = g_strdup_printf ("file://%s#%s",
                                 relocate (DOCDIR "/pspp.xml"), page);
      /* The page will be translated to the htmlfilename
         page                   htmlfilename
         GRAPH#SCATTERPLOT      SCATTERPLOT.html
         QUICK-CLUSTER          QUICK-CLUSTER.html
         which is valid for the multiple page html doc*/
      tokens = g_strsplit (page, "#", maxtokens);
      for (idx = 0; idx < maxtokens && tokens[idx]; idx++)
	;
      htmlfilename = g_strdup_printf ("%s.html", tokens[idx-1]);
      g_strfreev (tokens);
    }
  /* Hint: pspp.html is a directory...*/
  htmlfullname = g_strdup_printf ("%s/%s", relocate (DOCDIR "/pspp.html"),
                                  htmlfilename);
  if (g_file_test (relocate (DOCDIR "/pspp.html"), G_FILE_TEST_IS_DIR))
    {
      GError *urierr = NULL;
      htmluri =  g_filename_to_uri (htmlfullname,NULL, &urierr);
      if (!htmluri)
        {
          msg (ME, _("Help path conversion error: %s"), urierr->message);
          htmluri = htmlfullname;
        }
      g_clear_error (&urierr);
    }
  else
    htmluri = g_strdup_printf (PACKAGE_URL "manual/html_node/%s",
                               htmlfilename);
  g_free (htmlfullname);
  g_free (htmlfilename);
  htmlargv[1] = htmluri;

  /* The following **SHOULD** work but it does not on 28.5.2016
     g_app_info_launch_default_for_uri (htmluri, NULL, &err);
     osx: wine is started to launch the uri...
     windows: not so bad, but the first access does not work*/

  if (! (g_spawn_async (NULL, argv,
                        NULL, G_SPAWN_SEARCH_PATH,
                        NULL, NULL,   NULL,   &err) ||
#ifdef _WIN32
         open_windows_help (htmluri, &htmlerr))
#else
         g_spawn_async (NULL, htmlargv,
                        NULL, G_SPAWN_SEARCH_PATH,
                        NULL, NULL,   NULL,   &htmlerr))
#endif
      )
    {
      msg (ME, _("Cannot open reference manual via yelp: %s. "
                 "Cannot open via html: %s "
                 "with uri: %s "
                 "The PSSP manual is also available at %s"),
                  err->message,
                  htmlerr->message,
                  htmluri,
                  PACKAGE_URL "documentation.html");
    }

  g_free (argv[1]);
  g_free (htmluri);
  g_clear_error (&err);
  g_clear_error (&htmlerr);
}

static void
reference_manual (GtkMenuItem *menu, gpointer data)
{
  online_help (NULL);
}

GtkWidget *
create_help_menu (GtkWindow *toplevel)
{
  GtkWidget *menuitem = gtk_menu_item_new_with_mnemonic (_("_Help"));
  GtkWidget *menu = gtk_menu_new ();

  GtkWidget *help_about = gtk_menu_item_new_with_mnemonic (_("_About"));
  GtkWidget *help_ref = gtk_menu_item_new_with_mnemonic (_("_Reference Manual"));

  GtkAccelGroup *accel_group = gtk_accel_group_new ();
  
  gtk_window_add_accel_group (toplevel, accel_group);

  gtk_widget_add_accelerator (help_ref,
			      "activate", accel_group,
			      GDK_KEY_F1, 0,
			      GTK_ACCEL_VISIBLE);

  gtk_menu_attach (GTK_MENU (menu), help_ref, 0, 1, 0, 1);
  gtk_menu_attach (GTK_MENU (menu), help_about, 0, 1, 1, 2);

  g_signal_connect (help_about, "activate", G_CALLBACK (about_new), toplevel);
  g_signal_connect (help_ref, "activate", G_CALLBACK (reference_manual), NULL);
  
  g_object_set (menuitem, "submenu", menu, NULL);

  gtk_widget_show_all (menuitem);
  
  return menuitem;
}
