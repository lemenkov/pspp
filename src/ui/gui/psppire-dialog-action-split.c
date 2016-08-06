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

#include "psppire-dialog-action-split.h"
#include "psppire-selector.h"
#include "psppire-var-view.h"
#include "dict-display.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_dialog_action_split_init            (PsppireDialogActionSplit      *act);
static void psppire_dialog_action_split_class_init      (PsppireDialogActionSplitClass *class);

G_DEFINE_TYPE (PsppireDialogActionSplit, psppire_dialog_action_split, PSPPIRE_TYPE_DIALOG_ACTION);


static char *
generate_syntax (const PsppireDialogAction *pda)
{
  PsppireDialogActionSplit *act = PSPPIRE_DIALOG_ACTION_SPLIT (pda);
  gchar *text;

  GString *string = g_string_new ("SPLIT FILE OFF.");

  if ( ! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->off)))
    {
      GString * varlist = g_string_sized_new (80);
      gint n_vars = psppire_var_view_append_names (PSPPIRE_VAR_VIEW (act->tv), 0, varlist);

      if ( n_vars > 0 )
	{
	  g_string_assign (string, "");

	  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(act->sort)))
	    {
	      g_string_append (string, "SORT CASES BY");
	      g_string_append (string, varlist->str);
	      g_string_append (string, ".\n");
	    }

	  g_string_append (string, "SPLIT FILE ");

	  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->layered)))
	    g_string_append (string, "LAYERED ");
	  else
	    g_string_append (string, "SEPARATE ");

	  g_string_append (string, "BY ");
	  g_string_append (string, varlist->str);
	  g_string_append (string, ".");
	}
      g_string_free (varlist, TRUE);
    }

  text =  string->str;

  g_string_free (string, FALSE);

  return text;
}


static gboolean
dialog_state_valid (gpointer data)
{
  return TRUE;
}

static void
refresh (PsppireDialogAction *pda)
{
  PsppireDialogActionSplit *act = PSPPIRE_DIALOG_ACTION_SPLIT (pda);

  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (act->tv));

  gint n_vars = dict_get_split_cnt (pda->dict->dict);

  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  if ( n_vars == 0 )
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (act->off), TRUE);
  else
    {
      GtkTreeIter iter;
      gint i;
      const struct variable *const *vars = dict_get_split_vars (pda->dict->dict);

      for (i = 0 ; i < n_vars; ++i )
	{
	  gtk_list_store_append (GTK_LIST_STORE (liststore), &iter);

	  gtk_list_store_set (GTK_LIST_STORE (liststore), &iter,
			      0, vars[i],
			      -1);
	}

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (act->layered), TRUE);
    }

  gtk_toggle_button_toggled (GTK_TOGGLE_BUTTON (act->off));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (act->sort), TRUE);
}

static void
on_off_toggled (GtkToggleButton *togglebutton,
		gpointer         user_data)
{
  PsppireDialogActionSplit *act = PSPPIRE_DIALOG_ACTION_SPLIT (user_data);

  gboolean state = !gtk_toggle_button_get_active (togglebutton);

  gtk_widget_set_sensitive (act->dest, state);
  gtk_widget_set_sensitive (act->selector, state);
  gtk_widget_set_sensitive (act->source, state);
}

static void
psppire_dialog_action_split_activate (PsppireDialogAction *pda)
{
  PsppireDialogActionSplit *act = PSPPIRE_DIALOG_ACTION_SPLIT (pda);

  GHashTable *thing = psppire_dialog_action_get_hash_table (pda);
  GtkBuilder *xml = g_hash_table_lookup (thing, pda);
  if (!xml)
    {
      xml = builder_new ("split-file.ui");
      g_hash_table_insert (thing, pda, xml);

      pda->dialog = get_widget_assert   (xml, "split-file-dialog");
      pda->source = get_widget_assert   (xml, "split-file-dict-treeview");
      act->selector = get_widget_assert (xml, "split-file-selector");

      act->dest =   get_widget_assert (xml, "split-file-grouping-vars");
      act->source = get_widget_assert (xml, "split-file-dict-treeview");
      act->sort = get_widget_assert (xml, "split-sort");

      act->off = get_widget_assert   (xml, "split-off");
      act->layered = get_widget_assert   (xml, "split-layered");

      act->tv = get_widget_assert (xml, "split-file-grouping-vars");

      g_signal_connect (act->off, "toggled", G_CALLBACK (on_off_toggled), pda);
      g_signal_connect_swapped (pda->dialog, "show", G_CALLBACK (refresh), pda);
    }
  
  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

}

static void
psppire_dialog_action_split_class_init (PsppireDialogActionSplitClass *class)
{
  psppire_dialog_action_set_activation (class, psppire_dialog_action_split_activate);
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_split_init (PsppireDialogActionSplit *act)
{
}

