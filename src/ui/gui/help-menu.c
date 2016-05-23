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

#ifdef __APPLE__
#define HTMLOPENAPP "open"
#elif  _WIN32
#define HTMLOPENAPP "start"
#else
#define HTMLOPENAPP "xdg-open"
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

/* Open the manual at PAGE */
void
online_help (const char *page)
{
  GError *err = NULL;
  GError *htmlerr = NULL;
  gchar *argv[3] = { "yelp", 0, 0};
  gchar *htmlargv[3] = { HTMLOPENAPP, 0, 0};
  gchar *htmlfilename = NULL;
  gchar *htmlfullname = NULL;

  if (page == NULL)
    {
      argv[1] = g_strdup_printf ("file://%s", relocate (DOCDIR "/pspp.xml"));
      htmlfilename = g_strdup ("index.html");
      htmlargv[1] = g_strdup_printf ("file://%s", htmlfilename);
    }
  else
    {
      gchar **tokens = NULL;
      const int maxtokens = 5;
      int idx = 0;
      argv[1] = g_strdup_printf ("file://%s#%s",
                                 relocate (DOCDIR "/pspp.xml"), page);
      tokens = g_strsplit (page, "#", maxtokens);
      for(;tokens[idx] && idx < maxtokens;idx++);
      htmlfilename = g_strdup_printf ("%s.html", tokens[idx-1]);
      g_strfreev (tokens);
    }

  htmlfullname = g_strdup_printf ("%s/%s", relocate (DOCDIR "/pspp.html"),
                                  htmlfilename);
  if (g_file_test (relocate (DOCDIR "/pspp.html"), G_FILE_TEST_EXISTS))
    htmlargv[1] = g_strdup_printf ("file://%s", htmlfullname);
  else
    htmlargv[1] = g_strdup_printf (PACKAGE_URL "manual/html_node/%s",
                                   htmlfilename);

  g_free (htmlfullname);
  g_free (htmlfilename);

  if (! (g_spawn_async (NULL, argv,
                        NULL, G_SPAWN_SEARCH_PATH,
                        NULL, NULL,   NULL,   &err) ||
         g_spawn_async (NULL, htmlargv,
                        NULL, G_SPAWN_SEARCH_PATH,
                        NULL, NULL,   NULL,   &htmlerr))
      )
    {
      msg (ME, _("Cannot open reference manual via yelp: %s. "
                 "Cannot open via html: %s "
                 "The PSSP manual is also available at %s"),
                  err->message,
                  htmlerr->message,
                  PACKAGE_URL "documentation.html");
    }

  g_free (argv[1]);
  g_free (htmlargv[1]);
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
