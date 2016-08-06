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

#include "psppire-dialog-action-barchart.h"
#include "psppire-value-entry.h"

#include "dialog-common.h"
#include <ui/syntax-gen.h>
#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include "psppire-dict.h"
#include "libpspp/str.h"

#include "language/stats/chart-category.h"

static void
psppire_dialog_action_barchart_class_init (PsppireDialogActionBarchartClass *class);

G_DEFINE_TYPE (PsppireDialogActionBarchart, psppire_dialog_action_barchart, PSPPIRE_TYPE_DIALOG_ACTION);

static gboolean
dialog_state_valid (gpointer rd_)
{
  PsppireDialogActionBarchart *rd = PSPPIRE_DIALOG_ACTION_BARCHART (rd_);

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->button_summary_func)) )
    {
      if (0 == g_strcmp0 ("", gtk_entry_get_text (GTK_ENTRY (rd->var))))
	return FALSE;
    }

  if (0 == g_strcmp0 ("", gtk_entry_get_text (GTK_ENTRY (rd->variable_xaxis))))
    return FALSE;

  return TRUE;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionBarchart *rd = PSPPIRE_DIALOG_ACTION_BARCHART (rd_);

  gtk_entry_set_text (GTK_ENTRY (rd->var), "");
  gtk_entry_set_text (GTK_ENTRY (rd->variable_xaxis), "");
  gtk_entry_set_text (GTK_ENTRY (rd->variable_cluster), "");

  /* Set summary_func to true, then let it get unset again.
     This ensures that the signal handler gets called.   */
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->button_summary_func), TRUE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (rd->button_freq_func[0]), TRUE);
				
  gtk_widget_set_sensitive (rd->combobox, FALSE);

  gtk_combo_box_set_active (GTK_COMBO_BOX (rd->combobox), 0);
}

static void
on_summary_toggle (PsppireDialogActionBarchart *act)
{
  gboolean status = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->button_summary_func));

  gtk_widget_set_sensitive (act->summary_variables, status);
  gtk_widget_set_sensitive (act->combobox, status);
}

static void
populate_combo_model (GtkComboBox *cb)
{
  int i;
  GtkListStore *list =  gtk_list_store_new (4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT);
  GtkTreeIter iter;
  GtkCellRenderer *renderer ;

  for (i = 0; i < N_AG_FUNCS;  ++i)
    {
      const struct ag_func *af = ag_func + i;

      if (af->arity == 0)
	continue;

      gtk_list_store_append (list, &iter);
      gtk_list_store_set (list, &iter,
                          0, af->description,
			  1, af->name,
                          -1);
    }

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (cb), renderer, FALSE);

  gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (cb), renderer, "text", 0);

  gtk_combo_box_set_model (GTK_COMBO_BOX (cb), GTK_TREE_MODEL (list));
  g_object_unref (list);
}


static void
psppire_dialog_action_barchart_activate (PsppireDialogAction *a)
{
  PsppireDialogActionBarchart *act = PSPPIRE_DIALOG_ACTION_BARCHART (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  GHashTable *thing = psppire_dialog_action_get_hash_table (pda);
  GtkBuilder *xml = g_hash_table_lookup (thing, a);
  if (!xml)
    {
      xml = builder_new ("barchart.ui");
      g_hash_table_insert (thing, a, xml);

      pda->dialog = get_widget_assert (xml, "barchart-dialog");
      pda->source = get_widget_assert (xml, "dict-view");

      act->variable_xaxis = get_widget_assert (xml, "entry1");
      act->variable_cluster = get_widget_assert (xml, "entry3");
      act->var = get_widget_assert (xml, "entry2");
      act->button_freq_func[0] = get_widget_assert (xml, "radiobutton-count");
      act->button_freq_func[1] = get_widget_assert (xml, "radiobutton-percent");
      act->button_freq_func[2] = get_widget_assert (xml, "radiobutton-cum-count");
      act->button_freq_func[3] = get_widget_assert (xml, "radiobutton-cum-percent");
  
      act->button_summary_func = get_widget_assert (xml, "radiobutton3");
      act->summary_variables = get_widget_assert (xml, "hbox1");
      act->combobox = get_widget_assert (xml, "combobox1");

      populate_combo_model (GTK_COMBO_BOX(act->combobox));
  
      g_signal_connect_swapped (act->button_summary_func, "toggled",
				G_CALLBACK (on_summary_toggle), act);

      psppire_dialog_action_set_refresh (pda, refresh);

      psppire_dialog_action_set_valid_predicate (pda,
						 dialog_state_valid);
    }

}

static char *
generate_syntax (const PsppireDialogAction *a)
{
  PsppireDialogActionBarchart *rd = PSPPIRE_DIALOG_ACTION_BARCHART (a);
  gchar *text;
  const gchar *var_name_xaxis = gtk_entry_get_text (GTK_ENTRY (rd->variable_xaxis));
  const gchar *var_name_cluster = gtk_entry_get_text (GTK_ENTRY (rd->variable_cluster));

  GString *string = g_string_new ("GRAPH /BAR = ");

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->button_summary_func)))
    {
      GtkTreeIter iter;
      if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (rd->combobox), &iter))
	{
	  GValue value = {0};
	  GtkTreeModel *model = gtk_combo_box_get_model (GTK_COMBO_BOX (rd->combobox));
	  gtk_tree_model_get_value (model, &iter, 1, &value);
	  g_string_append (string, g_value_get_string (&value));
	  g_value_unset (&value);
	}
      g_string_append (string, " (");
      g_string_append (string, gtk_entry_get_text (GTK_ENTRY (rd->var)));
      g_string_append (string, ")");
    }
  else
    {
      int b;
      for (b = 0; b < 4; ++b)
	{
	  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->button_freq_func[b])))
	    break;
	}
      switch (b)
	{
	case 0:
	  g_string_append (string, "COUNT");
	  break;
	case 1:
	  g_string_append (string, "PCT");
	  break;
	case 2:
	  g_string_append (string, "CUFREQ");
	  break;
	case 3:
	  g_string_append (string, "CUPCT");
	  break;
	default:
	  g_assert_not_reached ();
	  break;
	}
    }

  g_string_append (string, " BY ");
  g_string_append (string, var_name_xaxis);

  if (g_strcmp0 (var_name_cluster, ""))
  {
    g_string_append (string, " BY ");
    g_string_append (string, var_name_cluster);
  }
  
  g_string_append (string, ".\n");

  text = string->str;

  g_string_free (string, FALSE);

  return text;
}

static void
psppire_dialog_action_barchart_class_init (PsppireDialogActionBarchartClass *class)
{
  psppire_dialog_action_set_activation (class, psppire_dialog_action_barchart_activate);

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_barchart_init (PsppireDialogActionBarchart *act)
{
}
