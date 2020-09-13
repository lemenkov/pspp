/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012, 2020  Free Software Foundation

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

#include "psppire-dialog-action-examine.h"

#include "psppire-var-view.h"
#include "dialog-common.h"
#include "psppire-selector.h"
#include "psppire-dict.h"
#include "psppire-dialog.h"
#include "builder-wrapper.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_dialog_action_examine_class_init      (PsppireDialogActionExamineClass *class);

G_DEFINE_TYPE (PsppireDialogActionExamine, psppire_dialog_action_examine, PSPPIRE_TYPE_DIALOG_ACTION);


#define     STAT_DESCRIPTIVES  0x01
#define     STAT_EXTREMES      0x02
#define     STAT_PERCENTILES   0x04

static void
run_stats_dialog (PsppireDialogActionExamine *ed)
{
  gint response;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->descriptives_button),
				ed->stats & STAT_DESCRIPTIVES);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->extremes_button),
				ed->stats & STAT_EXTREMES);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->percentiles_button),
				ed->stats & STAT_PERCENTILES);

  response = psppire_dialog_run (PSPPIRE_DIALOG (ed->stats_dialog));

  if (response == PSPPIRE_RESPONSE_CONTINUE)
    {
      ed->stats = 0;
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->descriptives_button)))
	ed->stats |= STAT_DESCRIPTIVES;

      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->extremes_button)))
	ed->stats |= STAT_EXTREMES;

      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->percentiles_button)))
	ed->stats |= STAT_PERCENTILES;
    }
}

static void
run_opts_dialog (PsppireDialogActionExamine *ed)
{
  gint response;

  switch (ed->opts)
    {
    case OPT_LISTWISE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->listwise), TRUE);
      break;
    case OPT_PAIRWISE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->pairwise), TRUE);
      break;
    case OPT_REPORT:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->report), TRUE);
      break;
    default:
      g_assert_not_reached ();
      break;
    };

  response = psppire_dialog_run (PSPPIRE_DIALOG (ed->opts_dialog));

  if (response == PSPPIRE_RESPONSE_CONTINUE)
    {
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->listwise)))
	ed->opts = OPT_LISTWISE;
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->pairwise)))
	ed->opts = OPT_PAIRWISE;
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->report)))
	ed->opts = OPT_REPORT;
  }
}

static void
run_plots_dialog (PsppireDialogActionExamine *ed)
{
  gint response;

  switch (ed->boxplots)
    {
    case BOXPLOT_FACTORS:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->boxplot_factors_button), TRUE);
      break;
    case BOXPLOT_DEPENDENTS:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->boxplot_dependents_button), TRUE);
      break;
    case BOXPLOT_NONE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->boxplot_none_button), TRUE);
      break;
    default:
      g_assert_not_reached ();
      break;
    };

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->histogram_button), ed->histogram);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->npplots_button), ed->npplots);

  switch (ed->spreadlevel)
    {
    case SPREAD_NONE:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->spread_none_button), TRUE);
      break;
    case SPREAD_POWER:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->spread_power_button), TRUE);
      break;
    case SPREAD_TRANS:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->spread_trans_button), TRUE);
      break;
    case SPREAD_UNTRANS:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ed->spread_untrans_button), TRUE);
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  switch (ed->spreadpower)
    {
    case SPREADPOWER_NATLOG:
      gtk_combo_box_set_active_id (GTK_COMBO_BOX (ed->spread_power_combo), "natlog");
      break;
    case SPREADPOWER_CUBE:
      gtk_combo_box_set_active_id (GTK_COMBO_BOX (ed->spread_power_combo), "cube");
      break;
    case SPREADPOWER_SQUARE:
      gtk_combo_box_set_active_id (GTK_COMBO_BOX (ed->spread_power_combo), "square");
      break;
    case SPREADPOWER_SQUAREROOT:
      gtk_combo_box_set_active_id (GTK_COMBO_BOX (ed->spread_power_combo), "squareroot");
      break;
    case SPREADPOWER_RECROOT:
      gtk_combo_box_set_active_id (GTK_COMBO_BOX (ed->spread_power_combo), "recroot");
      break;
    case SPREADPOWER_RECIPROCAL:
      gtk_combo_box_set_active_id (GTK_COMBO_BOX (ed->spread_power_combo), "reciprocal");
      break;
    }

  response = psppire_dialog_run (PSPPIRE_DIALOG (ed->plots_dialog));

  if (response == PSPPIRE_RESPONSE_CONTINUE)
    {
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->boxplot_factors_button)))
	ed->boxplots = BOXPLOT_FACTORS;
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->boxplot_dependents_button)))
	ed->boxplots = BOXPLOT_DEPENDENTS;
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->boxplot_none_button)))
	ed->boxplots = BOXPLOT_NONE;

      ed->histogram = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->histogram_button));
      ed->npplots   = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->npplots_button));

      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->spread_none_button)))
	ed->spreadlevel = SPREAD_NONE;
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->spread_power_button)))
	ed->spreadlevel = SPREAD_POWER;
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->spread_trans_button)))
	ed->spreadlevel = SPREAD_TRANS;
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->spread_untrans_button)))
	ed->spreadlevel = SPREAD_UNTRANS;

      if (0 == strcmp (gtk_combo_box_get_active_id (GTK_COMBO_BOX (ed->spread_power_combo)), "natlog"))
	ed->spreadpower = SPREADPOWER_NATLOG;
      else if (0 == strcmp (gtk_combo_box_get_active_id (GTK_COMBO_BOX (ed->spread_power_combo)), "cube"))
	ed->spreadpower = SPREADPOWER_CUBE;
      else if (0 == strcmp (gtk_combo_box_get_active_id (GTK_COMBO_BOX (ed->spread_power_combo)), "square"))
	ed->spreadpower = SPREADPOWER_SQUARE;
      else if (0 == strcmp (gtk_combo_box_get_active_id (GTK_COMBO_BOX (ed->spread_power_combo)), "squareroot"))
	ed->spreadpower = SPREADPOWER_SQUAREROOT;
      else if (0 == strcmp (gtk_combo_box_get_active_id (GTK_COMBO_BOX (ed->spread_power_combo)), "recroot"))
	ed->spreadpower = SPREADPOWER_RECROOT;
      else if (0 == strcmp (gtk_combo_box_get_active_id (GTK_COMBO_BOX (ed->spread_power_combo)), "reciprocal"))
	ed->spreadpower = SPREADPOWER_RECIPROCAL;
    }
}

static char *
generate_syntax (const PsppireDialogAction *act)
{
  PsppireDialogActionExamine *ed  = PSPPIRE_DIALOG_ACTION_EXAMINE (act);

  const char *label;
  gchar *text = NULL;
  GString *str = g_string_new ("EXAMINE ");
  bool show_stats = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->display_stats_button));
  bool show_plots = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->display_plots_button));

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ed->display_both_button)))
    {
      show_stats = true;
      show_plots = true;
    }

  g_string_append (str, "\n\t/VARIABLES=");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (ed->variables), 0, str);

  if (0  < gtk_tree_model_iter_n_children
       (gtk_tree_view_get_model (GTK_TREE_VIEW (ed->factors)), NULL))
    {
      g_string_append (str, "\n\tBY ");
      psppire_var_view_append_names (PSPPIRE_VAR_VIEW (ed->factors), 0, str);
    }

  label = gtk_entry_get_text (GTK_ENTRY (ed->id_var));
  if (0 != strcmp (label, ""))
    {
      g_string_append (str, "\n\t/ID = ");
      g_string_append (str, label);
    }

  if (show_stats)
    {
      if (ed->stats & (STAT_DESCRIPTIVES | STAT_EXTREMES))
	{
	  g_string_append (str, "\n\t/STATISTICS =");

	  if (ed->stats & STAT_DESCRIPTIVES)
	    g_string_append (str, " DESCRIPTIVES");

	  if (ed->stats & STAT_EXTREMES)
	    g_string_append (str, " EXTREME");
	}

      if (ed->stats & STAT_PERCENTILES)
	g_string_append (str, "\n\t/PERCENTILES");
    }

  if (show_plots &&
      ((ed->boxplots != BOXPLOT_NONE) ||
       ed->histogram ||
       ed->npplots ||
       (ed->spreadlevel != SPREAD_NONE)))
    {
      g_string_append (str, "\n\t/PLOT =");

      if (ed->boxplots != BOXPLOT_NONE)
	g_string_append (str, " BOXPLOT");
      if (ed->histogram)
	g_string_append (str, " HISTOGRAM");
      if (ed->npplots)
	g_string_append (str, " NPPLOT");
      if (ed->spreadlevel != SPREAD_NONE)
	{
	  g_string_append (str, " SPREADLEVEL");
	  if (ed->spreadlevel != SPREAD_POWER)
	    {
	      gchar *power = NULL;
	      if (ed->spreadlevel == SPREAD_TRANS)
		switch (ed->spreadpower)
		  {
		  case SPREADPOWER_NATLOG:
		    power = "0";
		    break;
		  case SPREADPOWER_CUBE:
		    power = "3";
		    break;
		  case SPREADPOWER_SQUARE:
		    power = "2";
		    break;
		  case SPREADPOWER_SQUAREROOT:
		    power = "0.5";
		    break;
		  case SPREADPOWER_RECROOT:
		    power = "-0.5";
		    break;
		  case SPREADPOWER_RECIPROCAL:
		    power = "-1";
		    break;
		  default:
		    g_assert_not_reached ();
		    break;
		  }
	      else
		power = "1";
	      g_string_append_printf(str, " (%s)",power);
	    }
	}
      if (ed->boxplots == BOXPLOT_FACTORS)
	g_string_append (str, "\n\t/COMPARE = GROUPS");
      if (ed->boxplots == BOXPLOT_DEPENDENTS)
	g_string_append (str, "\n\t/COMPARE = VARIABLES");
    }

  g_string_append (str, "\n\t/MISSING=");
  switch (ed->opts)
    {
    case OPT_REPORT:
      g_string_append (str, "REPORT");
      break;
    case OPT_PAIRWISE:
      g_string_append (str, "PAIRWISE");
      break;
    default:
      g_string_append (str, "LISTWISE");
      break;
    };

  g_string_append (str, ".");
  text = str->str;

  g_string_free (str, FALSE);

  return text;
}

static gboolean
dialog_state_valid (PsppireDialogAction *da)
{
  PsppireDialogActionExamine *pae  = PSPPIRE_DIALOG_ACTION_EXAMINE (da);
  GtkTreeIter notused;
  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (pae->variables));

  return gtk_tree_model_get_iter_first (vars, &notused);
}

static void
dialog_refresh (PsppireDialogAction *da)
{
  PsppireDialogActionExamine *dae  = PSPPIRE_DIALOG_ACTION_EXAMINE (da);
  GtkTreeModel *liststore = NULL;

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (dae->variables));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (dae->factors));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  gtk_entry_set_text (GTK_ENTRY (dae->id_var), "");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (dae->display_both_button), TRUE);

  dae->stats = 0x00;
  dae->opts = OPT_LISTWISE;
  dae->boxplots = BOXPLOT_FACTORS;
  dae->histogram = TRUE;
  dae->npplots = FALSE;
  dae->spreadlevel = SPREAD_NONE;
  dae->spreadpower = SPREADPOWER_NATLOG;
}

static GtkBuilder *
psppire_dialog_action_examine_activate (PsppireDialogAction *a, GVariant *param)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionExamine *act = PSPPIRE_DIALOG_ACTION_EXAMINE (a);

  GtkBuilder *xml = builder_new ("examine.ui");

  GtkWidget *stats_button = get_widget_assert (xml, "stats-button");
  GtkWidget *opts_button = get_widget_assert (xml, "opts-button");
  GtkWidget *plots_button = get_widget_assert (xml, "plots-button");

  g_signal_connect_swapped (stats_button, "clicked",
			    G_CALLBACK (run_stats_dialog), act);

  g_signal_connect_swapped (opts_button, "clicked",
			    G_CALLBACK (run_opts_dialog), act);
  g_signal_connect_swapped (plots_button, "clicked",
			    G_CALLBACK (run_plots_dialog), act);

  GtkWidget *dep_sel = get_widget_assert (xml, "psppire-selector1");
  GtkWidget *dep_sel2 = get_widget_assert (xml, "psppire-selector2");
  GtkWidget *dep_sel3 = get_widget_assert (xml, "psppire-selector3");
  GtkWidget *table = get_widget_assert (xml, "table1");

  pda->dialog    = get_widget_assert   (xml, "examine-dialog");
  pda->source    = get_widget_assert   (xml, "treeview1");
  act->variables = get_widget_assert   (xml, "treeview2");
  act->factors   = get_widget_assert   (xml, "treeview3");
  act->id_var    = get_widget_assert   (xml, "entry1");
  act->display_both_button  = get_widget_assert (xml, "display-both-button");
  act->display_stats_button = get_widget_assert (xml, "display-stats-button");
  act->display_plots_button = get_widget_assert (xml, "display-plots-button");

  /* Setting the focus chain like this is a pain.
     But the default focus order seems to be somewhat odd. */
  GList *list = NULL;
  list = g_list_append (list, get_widget_assert (xml, "scrolledwindow1"));
  list = g_list_append (list, dep_sel);
  list = g_list_append (list, get_widget_assert (xml, "frame1"));
  list = g_list_append (list, dep_sel2);
  list = g_list_append (list, get_widget_assert (xml, "frame2"));
  list = g_list_append (list, dep_sel3);
  list = g_list_append (list, get_widget_assert (xml, "frame3"));
  gtk_container_set_focus_chain (GTK_CONTAINER (table), list);
  g_list_free (list);


  act->stats_dialog        = get_widget_assert (xml, "statistics-dialog");
  act->descriptives_button = get_widget_assert (xml, "descriptives-button");
  act->extremes_button     = get_widget_assert (xml, "extremes-button");
  act->percentiles_button  = get_widget_assert (xml, "percentiles-button");

  act->opts_dialog = get_widget_assert (xml, "options-dialog");
  act->listwise    = get_widget_assert (xml, "radiobutton1");
  act->pairwise    = get_widget_assert (xml, "radiobutton2");
  act->report      = get_widget_assert (xml, "radiobutton3");

  act->plots_dialog              = get_widget_assert (xml, "plots-dialog");
  act->boxplot_factors_button    = get_widget_assert (xml, "boxplot-factors-button");
  act->boxplot_dependents_button = get_widget_assert (xml, "boxplot-dependents-button");
  act->boxplot_none_button       = get_widget_assert (xml, "boxplot-none-button");
  act->histogram_button          = get_widget_assert (xml, "histogram-button");
  act->npplots_button            = get_widget_assert (xml, "npplots-button");
  act->spread_none_button        = get_widget_assert (xml, "spread-none-button");
  act->spread_power_button       = get_widget_assert (xml, "spread-power-button");
  act->spread_trans_button       = get_widget_assert (xml, "spread-trans-button");
  act->spread_untrans_button     = get_widget_assert (xml, "spread-untrans-button");
  act->spread_power_combo        = get_widget_assert (xml, "spread-power-combo");

  psppire_selector_set_allow (PSPPIRE_SELECTOR (dep_sel), numeric_only);

  psppire_dialog_action_set_valid_predicate (pda, (void *) dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, dialog_refresh);
  return xml;
}

static void
psppire_dialog_action_examine_class_init (PsppireDialogActionExamineClass *class)
{
  PSPPIRE_DIALOG_ACTION_CLASS (class)->initial_activate = psppire_dialog_action_examine_activate;

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}

static void
psppire_dialog_action_examine_init (PsppireDialogActionExamine *act)
{
}
