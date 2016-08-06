/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2009, 2010, 2011, 2012, 2014, 2016  Free Software Foundation

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

#include "psppire-var-view.h"

#include "psppire-dialog-action-recode-same.h"
#include "builder-wrapper.h"
#include <ui/gui/dialog-common.h>

#include "psppire-acr.h"

#include "psppire-selector.h"
#include "psppire-val-chooser.h"

#include "helper.h"
#include <ui/syntax-gen.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static gboolean
difx_variable_treeview_is_populated (PsppireDialogActionRecode *rd)
{
  GtkTreeIter not_used;

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

  if ( !gtk_tree_model_get_iter_first (vars, &not_used))
    return FALSE;

  return TRUE;
}


/* Dialog is valid iff at least one variable has been selected,
   AND the list of mappings is not empty.
 */
static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionRecode *rd = data;
  GtkTreeIter not_used;
      
  if ( ! rd->value_map )
    return FALSE;

  if ( ! gtk_tree_model_get_iter_first (GTK_TREE_MODEL (rd->value_map),
					&not_used) )
    return FALSE;

  return difx_variable_treeview_is_populated (rd);
}




static void
psppire_dialog_action_recode_same_class_init (PsppireDialogActionRecodeSameClass *class);

G_DEFINE_TYPE (PsppireDialogActionRecodeSame, psppire_dialog_action_recode_same, PSPPIRE_TYPE_DIALOG_ACTION_RECODE);

static void
refresh (PsppireDialogAction *rd)
{
  psppire_dialog_action_recode_refresh (rd);
}

static void
on_old_new_show (PsppireDialogActionRecode *rd)
{
  gtk_toggle_button_set_active
    (GTK_TOGGLE_BUTTON (rd->toggle[BUTTON_NEW_VALUE]), TRUE);

  g_signal_emit_by_name (rd->toggle[BUTTON_NEW_VALUE], "toggled");

  gtk_widget_hide (rd->toggle[BUTTON_NEW_COPY]);
  gtk_widget_hide (rd->new_copy_label);
  gtk_widget_hide (rd->strings_box);
}

static void
psppire_dialog_action_recode_same_activate (PsppireDialogAction *a)
{
  PsppireDialogActionRecode *act = PSPPIRE_DIALOG_ACTION_RECODE (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  psppire_dialog_action_recode_pre_activate (act, NULL);

  gtk_window_set_title (GTK_WINDOW (pda->dialog),
			_("Recode into Same Variables"));
  
  g_signal_connect_swapped (act->old_and_new_dialog, "show",
			    G_CALLBACK (on_old_new_show), act);

  gtk_window_set_title (GTK_WINDOW (act->old_and_new_dialog),
			_("Recode into Same Variables: Old and New Values"));

  gtk_widget_hide (act->output_variable_box);
  
  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda,
					dialog_state_valid);
}

static void
null_op (const PsppireDialogActionRecode *rd, struct string *dds)
{
}

static char *
same_generate_syntax (const PsppireDialogAction *act)
{
  return psppire_dialog_action_recode_generate_syntax (act, null_op, null_op, null_op);
}

static gboolean
target_is_string (const PsppireDialogActionRecode *rd)
{
  return rd->input_var_is_string;
}


static void
psppire_dialog_action_recode_same_class_init (PsppireDialogActionRecodeSameClass *class)
{
  psppire_dialog_action_set_activation (class, psppire_dialog_action_recode_same_activate);

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = same_generate_syntax;
  PSPPIRE_DIALOG_ACTION_RECODE_CLASS (class)->target_is_string = target_is_string;
}


static void
psppire_dialog_action_recode_same_init (PsppireDialogActionRecodeSame *act)
{
}

