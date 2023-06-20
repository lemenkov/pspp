/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2006, 2007, 2010, 2011, 2012, 2013, 2015, 2016,
   2021 Free Software Foundation

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

#include "libpspp/cast.h"
#include "libpspp/copyleft.h"
#include "libpspp/message.h"
#include "libpspp/version.h"
#include "ui/gui/executor.h"
#include "ui/gui/help-menu.h"
#include "ui/gui/psppire-data-window.h"

#include "gl/configmake.h"
#include "gl/relocatable.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Try to open html documentation uri via the default
   browser on the operating system */
#ifdef __APPLE__
#define HTMLOPENAPP "open"
#elif  _WIN32
#define HTMLOPENAPP "wscript"
#else
#define HTMLOPENAPP "xdg-open"
#endif

static const gchar *artists[] = { "Bastián Díaz", "Hugo Alejandro", NULL};

/* Opening the htmluri in windows via cmd /start uri opens
   the windows command shell for a moment. The alternative is
   to start a script via wscript. This will not be visible*/
#ifdef _WIN32
static gboolean
open_windows_help (const gchar *helpuri, GError **err)
{
  SHELLEXECUTEINFOA info;
  memset (&info, 0, sizeof (info));

  info.cbSize = sizeof (info);
  info.fMask = SEE_MASK_FLAG_NO_UI;
  info.lpVerb = "open";
  info.lpFile = helpuri;
  info.nShow = SW_SHOWNORMAL;

  BOOL ret = ShellExecuteExA (&info);

  if (ret)
    return TRUE;

  /* Contrary to what the microsoft documentation indicates, ShellExecuteExA does
     not seem to setLastError.  So we have to deal with errors ourselves here.  */
  const char *msg = 0;
  switch (GPOINTER_TO_INT (info.hInstApp))
    {
    case SE_ERR_FNF:
      msg = "File not found";
      break;
    case SE_ERR_PNF:
      msg = "Path not found";
      break;
    case SE_ERR_ACCESSDENIED:
      msg = "Access denied";
      break;
    case SE_ERR_OOM:
      msg = "Out of memory";
      break;
    case SE_ERR_DLLNOTFOUND:
      msg = "Dynamic-link library not found";
      break;
    case SE_ERR_SHARE:
      msg = "Cannot share an open file";
      break;
    case SE_ERR_ASSOCINCOMPLETE:
      msg = "File association information not complete";
      break;
    case SE_ERR_DDETIMEOUT:
      msg = "DDE operation timed out";
      break;
    case SE_ERR_DDEFAIL:
      msg = "DDE operation failed";
      break;
    case SE_ERR_DDEBUSY:
      msg = "DDE operation is busy";
      break;
    case SE_ERR_NOASSOC:
      msg = "File association not available";
      break;
    default:
      msg = "Unknown error";
      break;
    }

  *err = g_error_new_literal (g_quark_from_static_string ("pspp-help-error"),
                       0,
                       msg);

  return FALSE;
}

static gboolean
on_activate_link (GtkAboutDialog *label,
               gchar          *uri,
               gpointer        user_data)
{
  return  open_windows_help (uri, NULL);
}
#endif

static void
about_system_info (GtkMenuItem *mmm, GtkWindow *parent)
{
  execute_const_syntax_string (psppire_default_data_window (), "SHOW SYSTEM.");
}

static void
about_new (GtkMenuItem *mmm, GtkWindow *parent)
{
  GtkWidget *about =  gtk_about_dialog_new ();

#ifdef _WIN32
  /* The default handler for Windows doesn't appear to work.  */
  g_signal_connect (about, "activate-link", G_CALLBACK (on_activate_link), parent);
#endif

  gtk_about_dialog_set_logo_icon_name (GTK_ABOUT_DIALOG (about), "org.gnu.pspp");

  gtk_window_set_icon_name (GTK_WINDOW (about), "org.gnu.pspp");

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



/* Open the manual at PAGE with the following priorities
   First: local yelp help system
   Second: browser with local html doc dir in path pspp.html/<helppage>.html
   Third:  browers with Internet html help at gnu.org */
void
online_help (const char *page)
{
  GError *htmlerr = NULL;
  gchar *htmluri = NULL;

  char *htmlfilename;
  if (page == NULL)
    htmlfilename = xstrdup ("index.html");
  else
    {
      gchar **tokens = NULL;
      const int maxtokens = 5;
      int idx ;
      /* The page will be translated to the htmlfilename
         page                   htmlfilename
         GRAPH#SCATTERPLOT      SCATTERPLOT.html
         QUICK-CLUSTER          QUICK-CLUSTER.html
         which is valid for the multiple page html doc*/
      tokens = g_strsplit (page, "#", maxtokens);
      for (idx = 0; idx < maxtokens && tokens[idx]; idx++)
        ;
      htmlfilename = xasprintf ("%s.html", tokens[idx-1]);
      g_strfreev (tokens);
    }
  /* Hint: pspp.html is a directory...*/
  char *htmldir = relocate_clone (DOCDIR "/pspp.html");
  char *htmlfullname = xasprintf ("%s/%s", htmldir, htmlfilename);
  if (g_file_test (htmldir, G_FILE_TEST_IS_DIR))
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
  free (htmlfullname);
  free (htmldir);
  free (htmlfilename);

#ifdef _WIN32
  bool ok = open_windows_help (htmluri, &htmlerr);
#else
  gchar *htmlargv[3] = {CONST_CAST (char *, HTMLOPENAPP), htmluri, 0};
  bool ok = g_spawn_async (NULL, htmlargv,
                           NULL, G_SPAWN_SEARCH_PATH,
                           NULL, NULL, NULL, &htmlerr);
#endif
  if (!ok)
    {
      msg (ME, _("Cannot open via html: %s "
                 "with uri: %s "
                 "The PSSP manual is also available at %s"),
                  htmlerr->message,
                  htmluri,
                  PACKAGE_URL "documentation.html");
    }

  g_free (htmluri);
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
  GtkWidget *help_system_info = gtk_menu_item_new_with_mnemonic (_("_System Information"));
  GtkWidget *help_ref = gtk_menu_item_new_with_mnemonic (_("_Reference Manual"));

  GtkAccelGroup *accel_group = gtk_accel_group_new ();

  gtk_window_add_accel_group (toplevel, accel_group);

  gtk_widget_add_accelerator (help_ref,
                              "activate", accel_group,
                              GDK_KEY_F1, 0,
                              GTK_ACCEL_VISIBLE);

  gtk_menu_attach (GTK_MENU (menu), help_ref, 0, 1, 0, 1);
  gtk_menu_attach (GTK_MENU (menu), help_system_info, 0, 1, 1, 2);
  gtk_menu_attach (GTK_MENU (menu), help_about, 0, 1, 2, 3);

  g_signal_connect (help_about, "activate", G_CALLBACK (about_new), toplevel);
  g_signal_connect (help_system_info, "activate", G_CALLBACK (about_system_info), toplevel);
  g_signal_connect (help_ref, "activate", G_CALLBACK (reference_manual), NULL);

  g_object_set (menuitem, "submenu", menu, NULL);

  gtk_widget_show_all (menuitem);

  g_object_unref (accel_group);

  return menuitem;
}
