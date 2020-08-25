/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2017 Free Software Foundation

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

#include "psppire-dialog-action-k-independent.h"

#include "psppire-var-view.h"
#include "psppire-value-entry.h"
#include "psppire-acr.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"
#include "helper.h"


#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid



static void psppire_dialog_action_k_independent_init            (PsppireDialogActionKIndependent      *act);
static void psppire_dialog_action_k_independent_class_init      (PsppireDialogActionKIndependentClass *class);

G_DEFINE_TYPE (PsppireDialogActionKIndependent, psppire_dialog_action_k_independent, PSPPIRE_TYPE_DIALOG_ACTION);

static const char *keyword[n_KIDS] =
  {
    "KRUSKAL-WALLIS",
    "MEDIAN"
  };

static char *
generate_syntax (const PsppireDialogAction *act)
{
  gchar *text;
  PsppireDialogActionKIndependent *kid = PSPPIRE_DIALOG_ACTION_K_INDEPENDENT (act);

  GString *string = g_string_new ("NPAR TEST");
  int i;
  for (i = 0; i < n_KIDS; ++i)
    {
      g_string_append (string, "\n\t");

      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (kid->checkbutton[i])))
	{
	  g_string_append_printf (string, "/%s = ", keyword[i]);
	  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (kid->vars_treeview),
					 0, string);

	  g_string_append (string, " BY ");

	  g_string_append (string,
			   gtk_entry_get_text (GTK_ENTRY (kid->groupvar_entry)));


	  g_string_append_printf (string, " (%g, %g)",
				  kid->lower_limit_value.f,
				  kid->upper_limit_value.f);
	}
    }

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}


static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionKIndependent *kid = PSPPIRE_DIALOG_ACTION_K_INDEPENDENT (data);

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (kid->vars_treeview));

  GtkTreeIter notused;

  if (!gtk_tree_model_get_iter_first (vars, &notused))
    return FALSE;

  if (0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (kid->groupvar_entry))))
    return FALSE;

  gboolean method_set = FALSE;
  gint i;
  for (i = 0; i < n_KIDS; ++i)
    {
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (kid->checkbutton[i])))
	method_set = TRUE;
    }

  return method_set;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionKIndependent *kid = PSPPIRE_DIALOG_ACTION_K_INDEPENDENT (rd_);

  GtkTreeModel *model =
    gtk_tree_view_get_model (GTK_TREE_VIEW (kid->vars_treeview));

  gtk_entry_set_text (GTK_ENTRY (kid->groupvar_entry), "");

  gtk_list_store_clear (GTK_LIST_STORE (model));

  gint i;
  for (i = 0; i < n_KIDS; ++i)
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (kid->checkbutton[i]),
				    FALSE);
    }
}

static void
run_define_groups_dialog (PsppireDialogActionKIndependent *kid)
{
  if (kid->lower_limit_value.f != SYSMIS)
    psppire_value_entry_set_value (PSPPIRE_VALUE_ENTRY (kid->lower_limit_entry),
				   &kid->lower_limit_value, 0);

  if (kid->upper_limit_value.f != SYSMIS)
    psppire_value_entry_set_value (PSPPIRE_VALUE_ENTRY (kid->upper_limit_entry),
				   &kid->upper_limit_value, 0);

  if (PSPPIRE_RESPONSE_CONTINUE ==
      psppire_dialog_run (PSPPIRE_DIALOG (kid->subdialog)))
    {
      psppire_value_entry_get_value (PSPPIRE_VALUE_ENTRY (kid->lower_limit_entry),
				     &kid->lower_limit_value, 0);

      psppire_value_entry_get_value (PSPPIRE_VALUE_ENTRY (kid->upper_limit_entry),
				     &kid->upper_limit_value, 0);
    }
}


static void
set_value_entry_variable (PsppireDialogActionKIndependent *kid, GtkEntry *entry)
{
  PsppireDialogAction *da = PSPPIRE_DIALOG_ACTION (kid);
  const gchar *text = gtk_entry_get_text (entry);

  const struct variable *v = da->dict ?
    psppire_dict_lookup_var (da->dict, text) : NULL;

  psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (kid->lower_limit_entry), v);
  psppire_value_entry_set_variable (PSPPIRE_VALUE_ENTRY (kid->upper_limit_entry), v);
}


static GtkBuilder *
psppire_dialog_action_k_independent_activate (PsppireDialogAction *a, GVariant *param)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionKIndependent *kid = PSPPIRE_DIALOG_ACTION_K_INDEPENDENT (a);

  GtkBuilder *xml = builder_new ("k-independent.ui");

  pda->dialog = get_widget_assert   (xml, "k-independent-dialog");
  pda->source = get_widget_assert   (xml, "k-independent-treeview1");

  kid->vars_treeview =  get_widget_assert (xml, "k-independent-treeview2");
  kid->groupvar_entry = get_widget_assert (xml, "k-independent-entry");

  kid->subdialog = get_widget_assert (xml, "define-groups-dialog");

  kid->lower_limit_entry = get_widget_assert (xml, "lower-limit-entry");
  kid->upper_limit_entry = get_widget_assert (xml, "upper-limit-entry");

  kid->checkbutton[KID_KRUSKAL_WALLIS] = get_widget_assert (xml,
							    "kruskal-wallis");

  kid->checkbutton[KID_MEDIAN] = get_widget_assert (xml, "median");

  g_signal_connect_swapped (get_widget_assert (xml, "define-groups-button"),
			    "clicked",
			    G_CALLBACK (run_define_groups_dialog), kid);

  g_signal_connect_swapped (kid->groupvar_entry, "changed",
			    G_CALLBACK (set_value_entry_variable), kid);

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

  return xml;
}

static void
psppire_dialog_action_k_independent_class_init (PsppireDialogActionKIndependentClass *class)
{
  PSPPIRE_DIALOG_ACTION_CLASS (class)->initial_activate = psppire_dialog_action_k_independent_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}

static void
psppire_dialog_action_k_independent_init (PsppireDialogActionKIndependent *kid)
{
  kid->lower_limit_value.f = SYSMIS;
  kid->upper_limit_value.f = SYSMIS;
}
