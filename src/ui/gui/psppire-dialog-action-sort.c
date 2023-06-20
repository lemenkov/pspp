/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012  Free Software Foundation

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

#include "psppire-dialog-action-sort.h"

#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

static void psppire_dialog_action_sort_init            (PsppireDialogActionSort      *act);
static void psppire_dialog_action_sort_class_init      (PsppireDialogActionSortClass *class);

G_DEFINE_TYPE (PsppireDialogActionSort, psppire_dialog_action_sort, PSPPIRE_TYPE_DIALOG_ACTION);

static char *
generate_syntax (const PsppireDialogAction *act)
{
  PsppireDialogActionSort *scd = PSPPIRE_DIALOG_ACTION_SORT (act);
  GString *string = g_string_new ("SORT CASES BY ");

  PsppireVarView *var_view = PSPPIRE_VAR_VIEW (scd->variables);
  gint n_vars = psppire_var_view_append_names (var_view, 0, string);

  if (n_vars == 0)
    {
      g_string_assign (string, "");
    }
  else
    {
      const char up_down =
        (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scd->ascending))
         ? 'A' : 'D');
      g_string_append_printf (string, "(%c)", up_down);
      g_string_append (string, ".");
    }

  return g_string_free_and_steal (string);
}

static void
reset (PsppireDialogAction *act)
{
  PsppireDialogActionSort *scd = PSPPIRE_DIALOG_ACTION_SORT (act);

  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (scd->variables));

  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scd->ascending), TRUE);
}




static gboolean
dialog_state_valid (gpointer act)
{
  PsppireDialogActionSort *scd = PSPPIRE_DIALOG_ACTION_SORT (act);
  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (scd->variables));

  gint n_rows = gtk_tree_model_iter_n_children  (model, NULL);

  if (n_rows == 0)
    return FALSE;

  return TRUE;
}


static GtkBuilder *
psppire_dialog_action_sort_activate (PsppireDialogAction *a, GVariant *param)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionSort *act = PSPPIRE_DIALOG_ACTION_SORT (a);

  GtkBuilder *xml = builder_new ("sort.ui");

  pda->dialog = get_widget_assert (xml, "sort-cases-dialog");
  pda->source = get_widget_assert (xml, "sort-cases-treeview1");

  act->variables =  get_widget_assert (xml, "sort-cases-treeview2");
  act->ascending = get_widget_assert (xml, "sort-cases-radiobutton0");

  psppire_dialog_action_set_refresh (pda, reset);

  psppire_dialog_action_set_valid_predicate (pda,
                                      dialog_state_valid);
  return xml;
}

static void
psppire_dialog_action_sort_class_init (PsppireDialogActionSortClass *class)
{
 PsppireDialogActionClass *pdac = PSPPIRE_DIALOG_ACTION_CLASS (class);
  PSPPIRE_DIALOG_ACTION_CLASS (class)->initial_activate = psppire_dialog_action_sort_activate;

 pdac->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_sort_init (PsppireDialogActionSort *act)
{
}

