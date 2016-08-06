/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2008, 2009, 2010, 2011, 2014, 2015 Free Software Foundation, Inc.

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


#include "builder-wrapper.h"
#include "dialog-common.h"
#include "dict-display.h"
#include "libpspp/str.h"
#include "psppire-data-store.h"
#include "psppire-data-window.h"
#include "psppire-dialog-action-select.h"
#include "psppire-dialog.h"
#include "psppire-dict.h"
#include "psppire-scanf.h"
#include "psppire-value-entry.h"
#include "psppire-var-view.h"
#include "widget-io.h"

#include <ui/syntax-gen.h>



#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_dialog_action_select_class_init (PsppireDialogActionSelectClass *class);

G_DEFINE_TYPE (PsppireDialogActionSelect, psppire_dialog_action_select, PSPPIRE_TYPE_DIALOG_ACTION);

static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionSelect *act = PSPPIRE_DIALOG_ACTION_SELECT (data);
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->radiobutton_all)))
    {
      return TRUE;
    }
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (act->radiobutton_filter_variable)))
    {
      const gchar *text = gtk_entry_get_text (GTK_ENTRY (act->entry));
      if (!psppire_dict_lookup_var (PSPPIRE_DIALOG_ACTION (act)->dict, text))
	return FALSE;
    }

  return TRUE;
}


static void
refresh (PsppireDialogAction *pda)
{
  PsppireDialogActionSelect *act = PSPPIRE_DIALOG_ACTION_SELECT (pda);

  gtk_entry_set_text (GTK_ENTRY (act->entry), "");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (act->radiobutton_all), TRUE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (act->radiobutton_filter), TRUE);

  gtk_label_set_text (GTK_LABEL (act->l1), "");
  gtk_label_set_text (GTK_LABEL (act->l0), "");
}


static void
set_radiobutton (GtkWidget *button, gpointer data)
{
  GtkToggleButton *toggle = data;
  gtk_toggle_button_set_active (toggle, TRUE);
}


static const gchar label1[] = N_("Approximately %3d%% of all cases.");
static const gchar label2[] = N_("Exactly %3d cases from the first %3d cases.");


/* Ensure that the range "first" and "last" spinbuttons are self consistent */
static void
sample_consistent (GtkSpinButton *spin, PsppireDialogActionSelect *act)
{
  gdouble size = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->spin_sample_size));
  gdouble limit  = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->spin_sample_limit));

  if (limit < size)
    {
      if (spin == GTK_SPIN_BUTTON (act->spin_sample_size))
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (act->spin_sample_limit), size);
      if (spin == GTK_SPIN_BUTTON (act->spin_sample_limit))
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (act->spin_sample_size), limit);
    }
}


static void
sample_subdialog (GtkButton *b, gpointer data)
{
  gint response;
  PsppireDialogActionSelect *scd = PSPPIRE_DIALOG_ACTION_SELECT (data);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (data);

  PsppireDataStore *data_store = NULL;
  g_object_get (PSPPIRE_DATA_WINDOW (pda->toplevel)->data_editor,
		"data-store", &data_store,
		NULL);

  gint case_count = psppire_data_store_get_case_count (data_store);

  if (!scd->hbox1)
    {
      scd->hbox1 = psppire_scanf_new (gettext (label1), &scd->spinbutton);

      gtk_widget_show (scd->hbox1);

      gtk_grid_attach (GTK_GRID (scd->table),
		       scd->hbox1,
		       1, 0,
		       1, 1);

      g_signal_connect (scd->percent, "toggled",
			G_CALLBACK (set_sensitivity_from_toggle), scd->hbox1);

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scd->percent), TRUE);
    }


  if (!scd->hbox2)
    {
      scd->hbox2 =
	psppire_scanf_new (gettext (label2), &scd->spin_sample_size, &scd->spin_sample_limit);

      gtk_spin_button_set_range (GTK_SPIN_BUTTON (scd->spin_sample_size),
				 1, case_count);

      gtk_spin_button_set_range (GTK_SPIN_BUTTON (scd->spin_sample_limit),
				 1, case_count);

      g_signal_connect (scd->spin_sample_size, "value-changed", G_CALLBACK (sample_consistent), scd);
      g_signal_connect (scd->spin_sample_limit, "value-changed", G_CALLBACK (sample_consistent), scd);

      
      gtk_widget_show (scd->hbox2);
      gtk_widget_set_sensitive (scd->hbox2, FALSE);

      gtk_grid_attach (GTK_GRID (scd->table),
		       scd->hbox2,
		       1, 1, 1, 1);

      g_signal_connect (scd->sample_n_cases, "toggled",
			G_CALLBACK (set_sensitivity_from_toggle), scd->hbox2);

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scd->sample_n_cases), FALSE);
    }


  gtk_window_set_transient_for (GTK_WINDOW (scd->rsample_dialog),
				GTK_WINDOW (pda->dialog));

  response = psppire_dialog_run (PSPPIRE_DIALOG (scd->rsample_dialog));

  if ( response != PSPPIRE_RESPONSE_CONTINUE)
    {
      g_signal_handlers_disconnect_by_func
	(G_OBJECT (scd->percent),
	 G_CALLBACK (set_sensitivity_from_toggle),
	 scd->hbox1);

      g_signal_handlers_disconnect_by_func
	(G_OBJECT (scd->sample_n_cases),
	 G_CALLBACK (set_sensitivity_from_toggle),
	 scd->hbox2);

      gtk_widget_destroy(scd->hbox1);
      gtk_widget_destroy(scd->hbox2);
      scd->hbox1 = scd->hbox2 = NULL;
    }
  else
    {
      gchar *text;

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scd->percent)))
	{
	  text = widget_printf (gettext(label1), scd->spinbutton);
	  gtk_label_set_text (GTK_LABEL (scd->l0), text);
	}
      else
	{
	  text =
	    widget_printf (gettext(label2), scd->spin_sample_size, scd->spin_sample_limit);
	  gtk_label_set_text (GTK_LABEL (scd->l0), text);

	}
      g_free (text);
    }
}



static void
range_subdialog (GtkButton *b, gpointer data)
{
  gint response;
  PsppireDialogActionSelect *scd = PSPPIRE_DIALOG_ACTION_SELECT (data);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (data);

  PsppireDataStore *data_store = NULL;
  g_object_get (PSPPIRE_DATA_WINDOW (pda->toplevel)->data_editor,
		"data-store", &data_store,
		NULL);

  gint n_cases = psppire_data_store_get_case_count (data_store);

  gtk_spin_button_set_range (GTK_SPIN_BUTTON (scd->last),  1,  n_cases);
  gtk_spin_button_set_range (GTK_SPIN_BUTTON (scd->first), 1,  n_cases);

  gtk_window_set_transient_for (GTK_WINDOW (scd->range_subdialog),
				GTK_WINDOW (pda->dialog));

  response = psppire_dialog_run (PSPPIRE_DIALOG (scd->range_subdialog));
  if ( response == PSPPIRE_RESPONSE_CONTINUE)
    {
      gchar *text = widget_printf (_("%d thru %d"), scd->first, scd->last);
      gtk_label_set_text (GTK_LABEL (scd->l1), text);
      g_free (text);
    }
}

/* Ensure that the range "first" and "last" spinbuttons are self consistent */
static void
consistency (GtkSpinButton *spin, PsppireDialogActionSelect *act)
{
  gdouble first = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->first));
  gdouble last  = gtk_spin_button_get_value (GTK_SPIN_BUTTON (act->last));

  if (last < first)
    {
      if (spin == GTK_SPIN_BUTTON (act->first))
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (act->last), first);
      if (spin == GTK_SPIN_BUTTON (act->last))
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (act->first), last);
    }
}

static void
psppire_dialog_action_select_activate (PsppireDialogAction *a)
{
  PsppireDialogActionSelect *act = PSPPIRE_DIALOG_ACTION_SELECT (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  GHashTable *thing = psppire_dialog_action_get_hash_table (pda);
  GtkBuilder *xml = g_hash_table_lookup (thing, a);
  if (!xml)
    {
      xml = builder_new ("select-cases.ui");
      g_hash_table_insert (thing, a, xml);


      pda->dialog = get_widget_assert (xml, "select-cases-dialog");
      pda->source = get_widget_assert   (xml, "select-cases-treeview");

      g_object_set (pda->source, 
		    "selection-mode", GTK_SELECTION_SINGLE,
		    NULL);
      
      act->entry = get_widget_assert (xml, "filter-variable-entry");

      GtkWidget *selector = get_widget_assert (xml, "psppire-selector-filter");
      psppire_selector_set_filter_func (PSPPIRE_SELECTOR (selector),
					is_currently_in_entry);

      act->rsample_dialog = get_widget_assert (xml, "select-cases-random-sample-dialog");
      act->percent = get_widget_assert (xml, "radiobutton-sample-percent");
      act->sample_n_cases = get_widget_assert (xml, "radiobutton-sample-n-cases");
      act->table = get_widget_assert (xml, "select-cases-random-sample-table");

      act->l0 = get_widget_assert (xml, "random-sample-label");;

      act->radiobutton_range = get_widget_assert (xml, "radiobutton-range");
      act->range_subdialog = get_widget_assert (xml, "select-cases-range-dialog");

      act->first = get_widget_assert (xml, "range-dialog-first");
      act->last = get_widget_assert (xml, "range-dialog-last");

      g_signal_connect (act->first, "value-changed", G_CALLBACK (consistency), act);
      g_signal_connect (act->last, "value-changed", G_CALLBACK (consistency), act);

      act->l1 = get_widget_assert (xml, "range-sample-label");
      act->radiobutton_sample =  get_widget_assert (xml, "radiobutton-sample");

      act->radiobutton_all = get_widget_assert (xml, "radiobutton-all");
      act->radiobutton_filter_variable =  get_widget_assert (xml, "radiobutton-filter-variable");

      act->radiobutton_filter =  get_widget_assert (xml, "radiobutton-filter");
      act->radiobutton_delete = get_widget_assert (xml,   "radiobutton-delete");


      GtkWidget	*button_range = get_widget_assert (xml, "button-range");
      GtkWidget *button_sample = get_widget_assert (xml, "button-sample");

      GtkWidget *button_if =get_widget_assert (xml, "button-if");

      GtkWidget *radiobutton_if = get_widget_assert (xml, "radiobutton-if");

      GtkWidget *sample_label = get_widget_assert (xml, "random-sample-label");

      g_signal_connect (act->radiobutton_all, "toggled",
			G_CALLBACK (set_sensitivity_from_toggle_invert),
			get_widget_assert (xml, "filter-delete-button-box"));

      g_signal_connect (button_if, "clicked",
			G_CALLBACK (set_radiobutton), radiobutton_if);

      g_signal_connect (button_sample, "clicked",
			G_CALLBACK (set_radiobutton), act->radiobutton_sample);

      g_signal_connect (button_range,  "clicked",
			G_CALLBACK (set_radiobutton), act->radiobutton_range);

      g_signal_connect (selector, "clicked",
			G_CALLBACK (set_radiobutton), act->radiobutton_filter_variable);

      g_signal_connect (selector, "selected",
			G_CALLBACK (set_radiobutton), act->radiobutton_filter_variable);

      g_signal_connect (act->radiobutton_range, "toggled",
			G_CALLBACK (set_sensitivity_from_toggle),
			act->l1);

      g_signal_connect (act->radiobutton_sample, "toggled",
			G_CALLBACK (set_sensitivity_from_toggle),
			sample_label);

      g_signal_connect (act->radiobutton_filter_variable, "toggled",
			G_CALLBACK (set_sensitivity_from_toggle),
			act->entry);

      g_signal_connect (button_range,
			"clicked", G_CALLBACK (range_subdialog), act);

      g_signal_connect (button_sample,
			"clicked", G_CALLBACK (sample_subdialog), act);
    }

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda,
					dialog_state_valid);

}


static char *
generate_syntax_filter (const PsppireDialogAction *a)
{
  PsppireDialogActionSelect *scd = PSPPIRE_DIALOG_ACTION_SELECT (a);

  gchar *text = NULL;
  struct string dss;

  const gchar *filter = "filter_$";
  const gchar key[]="case_$";

  ds_init_empty (&dss);

  if (gtk_toggle_button_get_active
      (GTK_TOGGLE_BUTTON (scd->radiobutton_range)))
    {
      ds_put_c_format (&dss,
		       "COMPUTE filter_$ = ($CASENUM >= %ld "
		       "AND $CASENUM <= %ld).\n",
		       (long) gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->first)),
		       (long) gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->last)));

      ds_put_cstr (&dss, "EXECUTE.\n");
    }
  else if ( gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (scd->radiobutton_sample)))
    {
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scd->percent)))
	{
	  const double percentage =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->spinbutton));

	  ds_put_c_format (&dss,
			   "COMPUTE %s = RV.UNIFORM (0,1) < %.*g.\n",
			   filter,
			   DBL_DIG + 1, percentage / 100.0 );
	}
      else
	{
	  const gint n_cases =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->spin_sample_size));
	  const gint from_n_cases =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->spin_sample_limit));


	  const gchar ranvar[]="rv_$";

	  ds_put_c_format (&dss,
			   "COMPUTE %s = $CASENUM.\n", key);

	  ds_put_c_format (&dss,
			   "COMPUTE %s = %s > %d.\n",
			   filter, key, from_n_cases);

	  ds_put_c_format (&dss,
			   "COMPUTE %s = RV.UNIFORM (0, 1).\n",
			   ranvar);

	  ds_put_c_format (&dss,
			   "SORT BY %s, %s.\n",
			   filter, ranvar);

	  ds_put_cstr (&dss, "EXECUTE.\n");
				  

	  ds_put_c_format (&dss,
			   "COMPUTE %s = $CASENUM.\n",
			   filter );

	  ds_put_c_format (&dss,
			   "COMPUTE %s = %s <= %d\n",
			   filter,
			   filter,
			   n_cases );

	  ds_put_cstr (&dss, "EXECUTE.\n");


	  ds_put_c_format (&dss,
			   "SORT BY %s.\n",
			   key);

	  ds_put_c_format (&dss,
			   "DELETE VARIABLES %s, %s.\n",
			   key, ranvar);
	}

      ds_put_cstr (&dss, "EXECUTE.\n");
    }
  else
    {
      filter = gtk_entry_get_text (GTK_ENTRY (scd->entry));
    }

  ds_put_c_format (&dss, "FILTER BY %s.\n", filter);

  text  = ds_steal_cstr (&dss);

  ds_destroy (&dss);

  return text;
}


static gchar *
generate_syntax_delete (const PsppireDialogAction *a)
{
  PsppireDialogActionSelect *scd = PSPPIRE_DIALOG_ACTION_SELECT (a);
  gchar *text = NULL;
  struct string dss;

  if ( gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (scd->radiobutton_all)))
    {
      return xstrdup ("\n");
    }

  ds_init_empty (&dss);

  if ( gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (scd->radiobutton_sample)))
    {
      ds_put_cstr (&dss, "SAMPLE ");
      
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scd->percent)))
	{
	  const double percentage =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->spinbutton));
	  ds_put_c_format (&dss, "%g.", percentage / 100.0);
	}
      else
	{
	  const gint n_cases =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->spin_sample_size));
	  const gint from_n_cases =
	    gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->spin_sample_limit));
	  
	  ds_put_c_format (&dss, "%d FROM %d .", n_cases, from_n_cases);
	}
      
    }
  else if ( gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (scd->radiobutton_range)))
    {
      ds_put_c_format (&dss,
		       "COMPUTE filter_$ = ($CASENUM >= %ld "
		       "AND $CASENUM <= %ld).\n",
		       (long) gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->first)),
		       (long) gtk_spin_button_get_value (GTK_SPIN_BUTTON (scd->last)));
      ds_put_cstr (&dss, "EXECUTE.\n");
      ds_put_c_format (&dss, "SELECT IF filter_$.\n");

    }
  else if (gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (scd->radiobutton_filter_variable)))
    {
      ds_put_c_format (&dss, "SELECT IF (%s <> 0).",
		       gtk_entry_get_text (GTK_ENTRY (scd->entry)));
    }


  ds_put_cstr (&dss, "\n");

  text = ds_steal_cstr (&dss);

  ds_destroy (&dss);

  return text;
}


static gchar *
generate_syntax (const PsppireDialogAction *a)
{
  PsppireDialogActionSelect *scd = PSPPIRE_DIALOG_ACTION_SELECT (a);

  /* In the simple case, all we need to do is cancel any existing filter */
  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scd->radiobutton_all)))
    {
      return g_strdup ("FILTER OFF.\n");
    }
  
  /* Are we filtering or deleting ? */
  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (scd->radiobutton_delete)))
    {
      return generate_syntax_delete (a);
    }
  else
    {
      return generate_syntax_filter (a);
    }
}


static void
psppire_dialog_action_select_class_init (PsppireDialogActionSelectClass *class)
{
  psppire_dialog_action_set_activation (class, psppire_dialog_action_select_activate);

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_select_init (PsppireDialogActionSelect *act)
{
}

