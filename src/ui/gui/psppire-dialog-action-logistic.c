/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2010, 2011, 2012, 2016  Free Software Foundation

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

#include "psppire-dialog-action-logistic.h"
#include "psppire-value-entry.h"

#include "dialog-common.h"
#include "helper.h"
#include <ui/syntax-gen.h>
#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"
#include "psppire-dict.h"
#include "libpspp/str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void
psppire_dialog_action_logistic_class_init (PsppireDialogActionLogisticClass *class);

G_DEFINE_TYPE (PsppireDialogActionLogistic, psppire_dialog_action_logistic, PSPPIRE_TYPE_DIALOG_ACTION);

static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionLogistic *rd = PSPPIRE_DIALOG_ACTION_LOGISTIC (data);

  const gchar *text = gtk_entry_get_text (GTK_ENTRY (rd->dep_var));

  GtkTreeModel *indep_vars = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->indep_vars));

  GtkTreeIter notused;

  return 0 != strcmp ("", text) &&
    gtk_tree_model_get_iter_first (indep_vars, &notused);
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionLogistic *rd = PSPPIRE_DIALOG_ACTION_LOGISTIC (rd_);

  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->indep_vars));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_entry_set_text (GTK_ENTRY (rd->dep_var), "");
}


static void
on_opts_clicked (PsppireDialogActionLogistic *act)
{
  int ret;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(act->conf_checkbox), act->conf);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (act->conf_entry), act->conf_level);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(act->const_checkbox), act->constant);

  gtk_spin_button_set_value (GTK_SPIN_BUTTON (act->cut_point_entry), act->cut_point);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (act->iterations_entry), act->max_iterations);

  
  ret = psppire_dialog_run (PSPPIRE_DIALOG (act->opts_dialog));

  if ( ret == PSPPIRE_RESPONSE_CONTINUE )
    {
      act->conf = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(act->conf_checkbox));
      act->conf_level = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->conf_entry));
      
      act->constant = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(act->const_checkbox));

      act->cut_point = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->cut_point_entry));
      act->max_iterations = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->iterations_entry));
    }
}


static void
psppire_dialog_action_logistic_activate (PsppireDialogAction *a)
{
  PsppireDialogActionLogistic *act = PSPPIRE_DIALOG_ACTION_LOGISTIC (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  GtkWidget *opts_button;

  GHashTable *thing = psppire_dialog_action_get_hash_table (pda);
  GtkBuilder *xml = g_hash_table_lookup (thing, a);
  if (!xml)
    {
      xml = builder_new ("logistic.ui");
      g_hash_table_insert (thing, a, xml);
    }

  pda->dialog = get_widget_assert   (xml, "logistic-dialog");
  pda->source = get_widget_assert   (xml, "dict-view");
  act->cut_point = 0.5;
  act->max_iterations = 20;
  act->constant = true;
  act->conf = false;
  act->conf_level = 95;

  act->dep_var  = get_widget_assert   (xml, "dependent-entry");
  act->indep_vars  = get_widget_assert   (xml, "indep-view");
  act->opts_dialog = get_widget_assert (xml, "options-dialog");
  act->conf_checkbox = get_widget_assert (xml, "checkbutton2");
  act->conf_entry = get_widget_assert (xml, "spinbutton1");
  act->const_checkbox = get_widget_assert (xml, "checkbutton1");

  act->iterations_entry = get_widget_assert (xml, "spinbutton3");
  act->cut_point_entry = get_widget_assert (xml, "spinbutton2");

  opts_button = get_widget_assert (xml, "options-button");

  g_signal_connect_swapped (opts_button, "clicked",
			    G_CALLBACK (on_opts_clicked),  act);

  g_signal_connect (act->conf_checkbox, "toggled",
		    G_CALLBACK (set_sensitivity_from_toggle),  
		    act->conf_entry);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(act->conf_checkbox), TRUE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(act->conf_checkbox), FALSE);

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda,
					dialog_state_valid);

}



static char *
generate_syntax (const PsppireDialogAction *a)
{
  PsppireDialogActionLogistic *rd = PSPPIRE_DIALOG_ACTION_LOGISTIC (a);
  gchar *text = NULL;

  const gchar *dep = gtk_entry_get_text (GTK_ENTRY (rd->dep_var));

  GString *strx = g_string_new ("LOGISTIC REGRESSION ");

  g_string_append (strx, dep);

  g_string_append (strx, " WITH");

  GSList *vars = psppire_var_view_list_names (PSPPIRE_VAR_VIEW (rd->indep_vars), 0);
  GSList *node = vars;

  GString *var_names = g_string_new ("");
  while (node)
    {
      g_string_prepend (var_names, var_get_name (node->data));
      g_string_prepend (var_names, " ");
      node = node->next;
    }

  g_string_append (strx, var_names->str);
  g_string_free (var_names, TRUE);


  GString *categoricals = g_string_new ("");
  for (node = vars; node; node = node->next)
    {
      const struct variable *v = node->data;
      enum measure m = var_get_measure (v);

      if (m == MEASURE_NOMINAL || m == MEASURE_ORDINAL || var_is_alpha (v))
	{
	  g_string_prepend (categoricals, var_get_name (v));
	  g_string_prepend (categoricals, " ");
	}
    }
  if (0 != strcmp (categoricals->str, ""))
    g_string_prepend (categoricals, "\n\t/CATEGORICAL =");

  g_string_append (strx, categoricals->str);
  g_string_free (categoricals, TRUE);
  g_slist_free (vars);

  struct string opt_str;
  ds_init_cstr (&opt_str, "\n\t/CRITERIA =");
  syntax_gen_pspp (&opt_str, " CUT(%g)", rd->cut_point);
  syntax_gen_pspp (&opt_str, " ITERATE(%d)", rd->max_iterations);
  g_string_append (strx, ds_cstr (&opt_str));
  ds_destroy (&opt_str);

  if (rd->conf)
    {
      g_string_append_printf (strx, "\n\t/PRINT = CI(%g)", rd->conf_level);
    }

  if (rd->constant) 
    g_string_append (strx, "\n\t/NOORIGIN");
  else
    g_string_append (strx, "\n\t/ORIGIN");

  g_string_append (strx, ".\n");

  text = strx->str;

  g_string_free (strx, FALSE);

  return text;
}

static void
psppire_dialog_action_logistic_class_init (PsppireDialogActionLogisticClass *class)
{
  psppire_dialog_action_set_activation (class, psppire_dialog_action_logistic_activate);

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_logistic_init (PsppireDialogActionLogistic *act)
{
}

