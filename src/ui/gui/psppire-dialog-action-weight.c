/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2015  Free Software Foundation

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

#include "psppire-dialog-action-weight.h"
#include "psppire-selector.h"
#include "psppire-var-view.h"
#include "dict-display.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_dialog_action_weight_init            (PsppireDialogActionWeight      *act);
static void psppire_dialog_action_weight_class_init      (PsppireDialogActionWeightClass *class);

G_DEFINE_TYPE (PsppireDialogActionWeight, psppire_dialog_action_weight, PSPPIRE_TYPE_DIALOG_ACTION);


static char *
generate_syntax (const PsppireDialogAction *pda)
{
  gchar *syntax = NULL;
  PsppireDialogActionWeight *wcd = PSPPIRE_DIALOG_ACTION_WEIGHT (pda);

  const gchar *text  = gtk_entry_get_text (GTK_ENTRY (wcd->entry));

  const struct variable *var = psppire_dict_lookup_var (pda->dict, text);

  if ( var == NULL)
    syntax = g_strdup ("WEIGHT OFF.\n");
  else
    syntax = g_strdup_printf ("WEIGHT BY %s.\n",
			      var_get_name (var));

  return syntax;
}


static gboolean
dialog_state_valid (gpointer data)
{
  return TRUE;
}

static void
refresh (PsppireDialogAction *pda)
{
  PsppireDialogActionWeight *wcd = PSPPIRE_DIALOG_ACTION_WEIGHT (pda);

  const struct variable *var = dict_get_weight (pda->dict->dict);

  if ( ! var )
    {
      gtk_entry_set_text (GTK_ENTRY (wcd->entry), "");
      gtk_label_set_text (GTK_LABEL (wcd->status), _("Do not weight cases"));
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (wcd->off), TRUE);
    }
  else
    {
      gchar *text =
	g_strdup_printf (_("Weight cases by %s"), var_get_name (var));

      gtk_entry_set_text (GTK_ENTRY (wcd->entry), var_get_name (var));
      gtk_label_set_text (GTK_LABEL (wcd->status), text);

      g_free (text);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (wcd->on), TRUE);
    }

  g_signal_emit_by_name (wcd->entry, "activate");
}

static void
on_select (PsppireSelector *sel, gpointer data)
{
  PsppireDialogActionWeight *wcd = data;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (wcd->on), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (wcd->on), TRUE);
}

static void
on_deselect (PsppireSelector *sel, gpointer data)
{
  PsppireDialogActionWeight *wcd = data;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (wcd->off), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (wcd->on), FALSE);
}

static void
on_toggle (GtkToggleButton *off, gpointer data)
{
  PsppireDialogActionWeight *wcd = data;

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (wcd->off)))
    {
      gtk_entry_set_text (GTK_ENTRY (wcd->entry), "");
    }
}


static void
psppire_dialog_action_weight_activate (PsppireDialogAction *pda)
{
  PsppireDialogActionWeight *act = PSPPIRE_DIALOG_ACTION_WEIGHT (pda);

  GHashTable *thing = psppire_dialog_action_get_hash_table (pda);
  GtkBuilder *xml = g_hash_table_lookup (thing, pda);
  if (!xml)
    {
      xml = builder_new ("weight.ui");
      g_hash_table_insert (thing, pda, xml);

      pda->dialog = get_widget_assert   (xml, "weight-cases-dialog");
      pda->source = get_widget_assert   (xml, "weight-cases-treeview");

      act->entry = get_widget_assert (xml, "weight-cases-entry");
      act->off = get_widget_assert (xml,"weight-cases-radiobutton1");
      act->on = get_widget_assert (xml, "radiobutton2");
      act->status  = get_widget_assert (xml, "weight-status-label");
      GtkWidget *selector = get_widget_assert (xml, "weight-cases-selector");

      g_signal_connect (selector, "selected", G_CALLBACK (on_select), act);
      g_signal_connect (selector, "de-selected", G_CALLBACK (on_deselect), act);
      g_signal_connect (act->off, "toggled", G_CALLBACK (on_toggle), act);

      g_object_set (pda->source,
		    "selection-mode", GTK_SELECTION_SINGLE,
		    "predicate", var_is_numeric,
		    NULL);
      
      psppire_selector_set_filter_func (PSPPIRE_SELECTOR (selector),
					is_currently_in_entry);
    }
  
  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

}

static void
psppire_dialog_action_weight_class_init (PsppireDialogActionWeightClass *class)
{
  psppire_dialog_action_set_activation (class, psppire_dialog_action_weight_activate);
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_weight_init (PsppireDialogActionWeight *act)
{
}

