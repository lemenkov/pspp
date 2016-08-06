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

#include "psppire-dialog-action-histogram.h"
#include "psppire-value-entry.h"

#include "dialog-common.h"
#include <ui/syntax-gen.h>
#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include "psppire-dict.h"
#include "libpspp/str.h"

static void
psppire_dialog_action_histogram_class_init (PsppireDialogActionHistogramClass *class);

G_DEFINE_TYPE (PsppireDialogActionHistogram, psppire_dialog_action_histogram, PSPPIRE_TYPE_DIALOG_ACTION);

static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionHistogram *rd = data;

  const gchar *var_name = gtk_entry_get_text (GTK_ENTRY (rd->variable));
  const struct variable *var = psppire_dict_lookup_var (PSPPIRE_DIALOG_ACTION (rd)->dict, var_name);

  if ( var == NULL)
    return FALSE;


  return TRUE;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionHistogram *rd = PSPPIRE_DIALOG_ACTION_HISTOGRAM (rd_);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->curve), FALSE);
  gtk_entry_set_text (GTK_ENTRY (rd->variable), "");
}

static void
psppire_dialog_action_histogram_activate (PsppireDialogAction *a)
{
  PsppireDialogActionHistogram *act = PSPPIRE_DIALOG_ACTION_HISTOGRAM (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  GHashTable *thing = psppire_dialog_action_get_hash_table (pda);
  GtkBuilder *xml = g_hash_table_lookup (thing, a);
  if (!xml)
    {
      xml = builder_new ("histogram.ui");
      g_hash_table_insert (thing, a, xml);
    }

  pda->dialog = get_widget_assert (xml, "histogram-dialog");
  pda->source = get_widget_assert (xml, "dict-view");

  act->variable = get_widget_assert (xml, "entry1");
  act->curve = get_widget_assert (xml, "curve");

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda,
					dialog_state_valid);

}



static char *
generate_syntax (const PsppireDialogAction *a)
{
  PsppireDialogActionHistogram *rd = PSPPIRE_DIALOG_ACTION_HISTOGRAM (a);
  gchar *text;
  const gchar *var_name = gtk_entry_get_text (GTK_ENTRY (rd->variable));
  GString *string = g_string_new ("GRAPH /HISTOGRAM ");

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->curve)))
    {
      g_string_append (string, "(NORMAL)");
    }

  g_string_append (string, " = ");
  g_string_append (string, var_name);

  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}

static void
psppire_dialog_action_histogram_class_init (PsppireDialogActionHistogramClass *class)
{
  psppire_dialog_action_set_activation (class, psppire_dialog_action_histogram_activate);

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_histogram_init (PsppireDialogActionHistogram *act)
{
}

