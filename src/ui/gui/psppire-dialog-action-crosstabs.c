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

#include "psppire-dialog-action-crosstabs.h"
#include "psppire-value-entry.h"

#include "dialog-common.h"
#include "helper.h"
#include <ui/syntax-gen.h>
#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"
#include "psppire-checkbox-treeview.h"
#include "psppire-dict.h"
#include "libpspp/str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void
psppire_dialog_action_crosstabs_class_init (PsppireDialogActionCrosstabsClass *class);

G_DEFINE_TYPE (PsppireDialogActionCrosstabs, psppire_dialog_action_crosstabs, PSPPIRE_TYPE_DIALOG_ACTION);

static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionCrosstabs *cd = PSPPIRE_DIALOG_ACTION_CROSSTABS (data);

  GtkTreeModel *row_vars = gtk_tree_view_get_model (GTK_TREE_VIEW (cd->dest_rows));
  GtkTreeModel *col_vars = gtk_tree_view_get_model (GTK_TREE_VIEW (cd->dest_cols));

  GtkTreeIter notused;

  return (gtk_tree_model_get_iter_first (row_vars, &notused)
    && gtk_tree_model_get_iter_first (col_vars, &notused));
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionCrosstabs *cd = PSPPIRE_DIALOG_ACTION_CROSSTABS (rd_);

  GtkTreeModel *liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (cd->dest_rows));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));

  liststore = gtk_tree_view_get_model (GTK_TREE_VIEW (cd->dest_cols));
  gtk_list_store_clear (GTK_LIST_STORE (liststore));
}

#define CROSSTABS_STATS                                             \
  CS (CHISQ, N_("Chisq"), N_("Pearson chi-square, "                 \
   "likelihood ratio, Fisher’s exact test, continuity correction, " \
   "linear-by-linear association."))                                    \
  CS (PHI, N_("Phi and Cramer's V"), NULL)                          \
  CS (CC, N_("CC"), N_("Contingency coefficient"))                  \
  CS (LAMBDA, N_("Lambda"), NULL)                                   \
  CS (UC, N_("UC"), N_("Uncertainty coefficient"))                  \
  CS (BTAU, N_("BTau"), N_("Kendall's Tau-b"))                      \
  CS (CTAU, N_("CTau"), N_("Kendall's Tau-c"))                      \
  CS (RISK, N_("Risk"), N_("Relative Risk estimate"))               \
  CS (GAMMA, N_("Gamma"), NULL)                                     \
  CS (D, N_("D"), N_("Somer's d"))                                  \
  CS (KAPPA, N_("Kappa"), N_("Cohen's Kappa"))                      \
  CS (ETA, N_("Eta"), NULL)                                         \
  CS (CORR, N_("Corr"), N_("Spearman correlation, Pearson's r"))    \
  CS (STATS_NONE, N_("None"), NULL)


#define CROSSTABS_CELLS                                               \
  CS (COUNT, N_("Count"), N_("Frequency Count"))                      \
  CS (ROW, N_("Row"), N_("Row percent"))                              \
  CS (COLUMN, N_("Column"), N_("Column percent"))                     \
  CS (TOTAL, N_("Total"), N_("Total percent"))                        \
  CS (EXPECTED, N_("Expected"), N_("Expected value"))                 \
  CS (RESIDUAL, N_("Residual"), NULL)                                 \
  CS (SRESIDUAL, N_("Std. Residual"), N_("Standardized Residual"))    \
  CS (ASRESIDUAL, N_("Adjusted Std. Residual"), NULL)                 \
  CS (CELLS_NONE, N_("None"), NULL)

enum
  {
#define CS(NAME, LABEL, TOOLTIP) CS_##NAME,
    CROSSTABS_STATS
#undef CS
    N_CROSSTABS_STATS
  };

enum
  {
#define CS(NAME, LABEL, TOOLTIP) CS_##NAME,
    CROSSTABS_CELLS
#undef CS
    N_CROSSTABS_CELLS
  };

enum
  {
#define CS(NAME, LABEL, TOOLTIP) B_CS_##NAME = 1u << CS_##NAME,
    CROSSTABS_STATS
    CROSSTABS_CELLS
#undef CS
    B_CS_STATS_ALL = (1u << N_CROSSTABS_STATS) - 1,
    B_CS_CELLS_ALL = (1u << N_CROSSTABS_CELLS) - 1,
    B_CS_STATS_DEFAULT = B_CS_CHISQ,
    B_CS_CELL_DEFAULT = B_CS_COUNT | B_CS_ROW | B_CS_COLUMN | B_CS_TOTAL,
    B_CS_NONE
  };

static const struct checkbox_entry_item stats[] =
  {
#define CS(NAME, LABEL, TOOLTIP) {#NAME, LABEL, TOOLTIP},
    CROSSTABS_STATS \
    CS(NONE, N_("None"), NULL)
#undef CS
  };

static const struct checkbox_entry_item cells[] =
  {
#define CS(NAME, LABEL, TOOLTIP) {#NAME, LABEL, TOOLTIP},
    CROSSTABS_CELLS \
    CS(NONE, N_("None"), NULL)
#undef CS
  };

static void
on_format_clicked (PsppireDialogActionCrosstabs *cd)
{
  int ret;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cd->avalue_button), cd->format_options_avalue);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cd->table_button), cd->format_options_table);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cd->pivot_button), cd->format_options_pivot);

  ret = psppire_dialog_run (PSPPIRE_DIALOG (cd->format_dialog));

  if (ret == PSPPIRE_RESPONSE_CONTINUE)
    {
      cd->format_options_avalue =
        gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (cd->avalue_button));

      cd->format_options_table =
        gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (cd->table_button));

      cd->format_options_pivot =
        gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (cd->pivot_button));
    }
}

static void
on_cell_clicked (PsppireDialogActionCrosstabs *cd)
{
  GtkListStore *liststore = clone_list_store (GTK_LIST_STORE (cd->cell));

  gint ret = psppire_dialog_run (PSPPIRE_DIALOG (cd->cell_dialog));

  if (ret == PSPPIRE_RESPONSE_CONTINUE)
    {
      g_object_unref (liststore);
    }
  else
    {
      gtk_tree_view_set_model (GTK_TREE_VIEW (cd->cell_view) , GTK_TREE_MODEL (liststore));
      cd->cell = GTK_TREE_MODEL (liststore);
    }
}


static void
on_statistics_clicked (PsppireDialogActionCrosstabs *cd)
{
  GtkListStore *liststore = clone_list_store (GTK_LIST_STORE (cd->stat));

  gint ret = psppire_dialog_run (PSPPIRE_DIALOG (cd->stat_dialog));

  if (ret == PSPPIRE_RESPONSE_CONTINUE)
    {
      g_object_unref (liststore);
    }
  else
    {
      gtk_tree_view_set_model (GTK_TREE_VIEW (cd->stat_view) , GTK_TREE_MODEL (liststore));
      cd->stat = GTK_TREE_MODEL (liststore);
    }
}


static GtkBuilder *
psppire_dialog_action_crosstabs_activate (PsppireDialogAction *a, GVariant *param)
{
  PsppireDialogActionCrosstabs *act = PSPPIRE_DIALOG_ACTION_CROSSTABS (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  GtkBuilder *xml = builder_new ("crosstabs.ui");

  pda->dialog = get_widget_assert   (xml, "crosstabs-dialog");
  pda->source = get_widget_assert   (xml, "dict-treeview");

  act->dest_rows =   get_widget_assert   (xml, "rows");
  act->dest_cols =   get_widget_assert   (xml, "cols");
  act->format_button = get_widget_assert (xml, "format-button");
  act->stat_button = get_widget_assert (xml, "stats-button");
  act->cell_button = get_widget_assert (xml, "cell-button");
  act->stat_view =   get_widget_assert (xml, "stats-view");
  act->cell_view =   get_widget_assert (xml, "cell-view");
  act->cell_dialog = get_widget_assert (xml, "cell-dialog");
  act->stat_dialog = get_widget_assert (xml, "stat-dialog");
  act->format_dialog = get_widget_assert (xml, "format-dialog");

  act->avalue_button = get_widget_assert (xml, "ascending");
  act->table_button = get_widget_assert (xml, "print-tables");
  act->pivot_button = get_widget_assert (xml, "pivot");

  act->format_options_avalue = TRUE;
  act->format_options_table = TRUE;
  act->format_options_pivot = TRUE;

  psppire_checkbox_treeview_populate (PSPPIRE_CHECKBOX_TREEVIEW (act->cell_view),
                                      B_CS_CELL_DEFAULT,
                                      N_CROSSTABS_CELLS,
                                      cells);

  act->cell = gtk_tree_view_get_model (GTK_TREE_VIEW (act->cell_view));

  psppire_checkbox_treeview_populate (PSPPIRE_CHECKBOX_TREEVIEW (act->stat_view),
                                      B_CS_STATS_DEFAULT,
                                      N_CROSSTABS_STATS,
                                      stats);

  act->stat = gtk_tree_view_get_model (GTK_TREE_VIEW (act->stat_view));

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda,
                                             dialog_state_valid);

  g_signal_connect_swapped (act->cell_button, "clicked",
                            G_CALLBACK (on_cell_clicked), act);

  g_signal_connect_swapped (act->stat_button, "clicked",
                            G_CALLBACK (on_statistics_clicked), act);

  g_signal_connect_swapped (act->format_button, "clicked",
                            G_CALLBACK (on_format_clicked), act);

  return xml;
}



static char *
generate_syntax (const PsppireDialogAction *a)
{
  PsppireDialogActionCrosstabs *cd = PSPPIRE_DIALOG_ACTION_CROSSTABS (a);
  int i, n;
  guint selected;
  GString *string = g_string_new ("CROSSTABS ");
  gboolean ok;
  GtkTreeIter iter;

  g_string_append (string, "\n\t/TABLES=");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (cd->dest_rows), 0, string);
  g_string_append (string, "\tBY\t");
  psppire_var_view_append_names (PSPPIRE_VAR_VIEW (cd->dest_cols), 0, string);


  g_string_append (string, "\n\t/FORMAT=");

  if (cd->format_options_avalue)
    g_string_append (string, "AVALUE");
  else
    g_string_append (string, "DVALUE");
  g_string_append (string, " ");

  if (cd->format_options_table)
    g_string_append (string, "TABLES");
  else
    g_string_append (string, "NOTABLES");

  selected = 0;
  for (i = 0, ok = gtk_tree_model_get_iter_first (cd->stat, &iter); ok;
       i++, ok = gtk_tree_model_iter_next (cd->stat, &iter))
    {
      gboolean toggled;
      gtk_tree_model_get (cd->stat, &iter,
                          CHECKBOX_COLUMN_SELECTED, &toggled, -1);
      if (toggled)
        selected |= 1u << i;
      else
        selected &= ~(1u << i);
    }

  if (!(selected & (1u << CS_STATS_NONE)))
    {
      if (selected)
        {
          g_string_append (string, "\n\t/STATISTICS=");
          n = 0;
          for (i = 0; i < N_CROSSTABS_STATS; i++)
            if (selected & (1u << i))
              {
                if (n++)
                  g_string_append (string, " ");
                g_string_append (string, stats[i].name);
              }
        }
    }

  selected = 0;
  for (i = 0, ok = gtk_tree_model_get_iter_first (cd->cell, &iter); ok;
       i++, ok = gtk_tree_model_iter_next (cd->cell, &iter))
    {
      gboolean toggled;
      gtk_tree_model_get (cd->cell, &iter,
                          CHECKBOX_COLUMN_SELECTED, &toggled, -1);
      if (toggled)
        selected |= 1u << i;
      else
        selected &= ~(1u << i);
    }



  g_string_append (string, "\n\t/CELLS=");
  if (selected & (1u << CS_CELLS_NONE))
    g_string_append (string, "NONE");
  else
    {
      n = 0;
      for (i = 0; i < N_CROSSTABS_CELLS; i++)
        if (selected & (1u << i))
          {
            if (n++)
              g_string_append (string, " ");
            g_string_append (string, cells[i].name);
          }
    }

  g_string_append (string, ".\n");

  return g_string_free_and_steal (string);
}

static void
psppire_dialog_action_crosstabs_class_init (PsppireDialogActionCrosstabsClass *class)
{
  PSPPIRE_DIALOG_ACTION_CLASS (class)->initial_activate = psppire_dialog_action_crosstabs_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_crosstabs_init (PsppireDialogActionCrosstabs *act)
{
}
