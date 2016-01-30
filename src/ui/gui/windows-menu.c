/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2016 Free Software Foundation

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

#include "windows-menu.h"
#include "psppire-window-register.h"
#include "psppire-data-window.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void
minimise (gpointer key, gpointer value, gpointer user_data)
{
  PsppireWindow *pw = PSPPIRE_WINDOW (value);
  gtk_window_iconify (GTK_WINDOW (pw));
}

static void
min_all (GtkWidget *widget, gpointer ud)
{
  PsppireWindowRegister *reg = psppire_window_register_new ();

  psppire_window_register_foreach (reg, minimise, NULL);
}

static void
reset_check_state (GtkWidget *widget, gpointer ud)
{
  GtkWindow *win = GTK_WINDOW (ud);
  gboolean state = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget));

  if (state == TRUE)
    gtk_window_present (win);
  
  /* Prevent the state from actually changing */
  g_signal_handlers_block_by_func (widget, reset_check_state, ud);
  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (widget), !state);
  g_signal_handlers_unblock_by_func (widget, reset_check_state, ud);
}

static void
add_menuitem (gpointer key, gpointer value, gpointer user_data)
{
  GtkMenu *menu = GTK_MENU (user_data);
  PsppireWindow *pw = PSPPIRE_WINDOW (value);

  GtkWidget *mi = gtk_check_menu_item_new_with_label (key);

  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (mi),
				  pw == g_object_get_data (G_OBJECT (menu), "toplevel"));

  g_signal_connect (mi, "toggled", G_CALLBACK (reset_check_state), pw);

  gtk_container_add (GTK_CONTAINER(menu), mi);
}

static void
toggle_split_window (PsppireDataWindow  *de, GtkCheckMenuItem *ta)
{
  psppire_data_editor_split_window (de->data_editor,
                                   gtk_check_menu_item_get_active (ta));
}



static void
repopulate_windows_menu (GObject *inst, gchar *name, gpointer data)
{
  PsppireWindowRegister *reg = psppire_window_register_new ();
  GtkMenuItem *mi = GTK_MENU_ITEM (data);

  GtkWidget *menu = gtk_menu_new ();

  GtkWindow *toplevel = g_object_get_data (G_OBJECT (mi), "toplevel");

  GtkWidget *minimize = gtk_menu_item_new_with_mnemonic (_("_Minimize all Windows"));
  GtkWidget *split = gtk_check_menu_item_new_with_mnemonic (_("_Split"));

  
  GtkWidget *sep = gtk_separator_menu_item_new ();
    
  gtk_menu_attach (GTK_MENU (menu), minimize, 0, 1, 0, 1);

  if (PSPPIRE_DATA_WINDOW_TYPE == G_OBJECT_TYPE (toplevel) )
    {
      gtk_menu_attach (GTK_MENU (menu), split, 0, 1, 1, 2);
      g_signal_connect_swapped (split, "toggled",
				G_CALLBACK (toggle_split_window), toplevel);
    }
    
  gtk_container_add (GTK_CONTAINER (menu), sep);

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (mi), menu);

  g_object_set_data (G_OBJECT (menu), "toplevel", toplevel);
  
  g_hash_table_foreach (reg->name_table, add_menuitem, menu);

  g_signal_connect (minimize, "activate", G_CALLBACK (min_all), NULL);
  
  gtk_widget_show_all (GTK_WIDGET (mi));
}

static void
on_destroy (GtkWindow *w, gpointer data)
{
  PsppireWindowRegister *reg = psppire_window_register_new ();

  g_signal_handlers_disconnect_by_func (reg, repopulate_windows_menu, w);
}

GtkWidget *
create_windows_menu (GtkWindow *toplevel)
{
  PsppireWindowRegister *reg = psppire_window_register_new ();
  
  GtkWidget *menuitem = gtk_menu_item_new_with_mnemonic (_("_Windows"));

  g_object_set_data (G_OBJECT (menuitem), "toplevel", toplevel);
  
  g_signal_connect (reg, "removed", G_CALLBACK (repopulate_windows_menu), menuitem);
  g_signal_connect (reg, "inserted", G_CALLBACK (repopulate_windows_menu), menuitem);

  g_signal_connect (menuitem, "destroy", G_CALLBACK (on_destroy), NULL);

  return menuitem;
}
