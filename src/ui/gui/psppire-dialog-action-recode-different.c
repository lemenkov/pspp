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

#include "psppire-dialog-action-recode-different.h"
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
  PsppireDialogActionRecodeDifferent *rdd = PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT (rd);
  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));
  
  if (g_hash_table_size (rdd->varmap) != gtk_tree_model_iter_n_children (model, NULL) )
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
psppire_dialog_action_recode_different_class_init (PsppireDialogActionRecodeDifferentClass *class);

G_DEFINE_TYPE (PsppireDialogActionRecodeDifferent, psppire_dialog_action_recode_different, PSPPIRE_TYPE_DIALOG_ACTION_RECODE);

static void
refresh (PsppireDialogAction *act)
{
  PsppireDialogActionRecode *rd = PSPPIRE_DIALOG_ACTION_RECODE (act);
  PsppireDialogActionRecodeDifferent *rdd = PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT (rd);

  psppire_dialog_action_recode_refresh (act);

  if (rdd->varmap)
    g_hash_table_remove_all (rdd->varmap);
}


static void
on_old_new_show (PsppireDialogActionRecode *rd)
{
  gtk_toggle_button_set_active
    (GTK_TOGGLE_BUTTON (rd->toggle[BUTTON_NEW_VALUE]), TRUE);

  g_signal_emit_by_name (rd->toggle[BUTTON_NEW_VALUE], "toggled");

  gtk_widget_show (rd->toggle[BUTTON_NEW_COPY]);
  gtk_widget_show (rd->new_copy_label);
  gtk_widget_show (rd->strings_box);
}



/* Name-Label pair */
struct nlp
{
  char *name;
  char *label;
};


static struct nlp *
nlp_create (const char *name, const char *label)
{
  struct nlp *nlp = xmalloc (sizeof *nlp);

  nlp->name = g_strdup (name);

  nlp->label = NULL;

  if ( 0 != strcmp ("", label))
    nlp->label = g_strdup (label);

  return nlp;
}

static void
nlp_destroy (gpointer data)
{
  struct nlp *nlp = data ;
  if ( ! nlp )
    return;

  g_free (nlp->name);
  g_free (nlp->label);
  g_free (nlp);
}



static void
render_new_var_name (GtkTreeViewColumn *tree_column,
		     GtkCellRenderer *cell,
		     GtkTreeModel *tree_model,
		     GtkTreeIter *iter,
		     gpointer data)
{
  struct nlp *nlp = NULL;
  PsppireDialogActionRecode *rd = data;
  PsppireDialogActionRecodeDifferent *rdd = PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT (rd);

  struct variable *var = NULL;

  gtk_tree_model_get (tree_model, iter, 
		      0, &var,
		      -1);

  nlp = g_hash_table_lookup (rdd->varmap, var);

  if ( nlp )
    g_object_set (cell, "text", nlp->name, NULL);
  else
    g_object_set (cell, "text", "", NULL);
}

static void
on_change_clicked (GObject *obj, gpointer data)
{
  PsppireDialogActionRecode *rd = data;
  PsppireDialogActionRecodeDifferent *rdd = PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT (rd);

  struct variable *var = NULL;
  struct nlp *nlp;

  GtkTreeModel *model =  gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

  GtkTreeIter iter;
  GtkTreeSelection *selection =
    gtk_tree_view_get_selection (GTK_TREE_VIEW (rd->variable_treeview));

  GList *rows = gtk_tree_selection_get_selected_rows (selection, &model);

  const gchar *dest_var_name =
    gtk_entry_get_text (GTK_ENTRY (rd->new_name_entry));

  const gchar *dest_var_label =
    gtk_entry_get_text (GTK_ENTRY (rd->new_label_entry));

  if ( NULL == rows || rows->next != NULL)
    goto finish;

  gtk_tree_model_get_iter (model, &iter, rows->data);

  gtk_tree_model_get (model, &iter, 0, &var, -1);

  g_hash_table_remove (rdd->varmap, var);

  nlp = nlp_create (dest_var_name, dest_var_label);

  g_hash_table_insert (rdd->varmap, var, nlp);

  gtk_tree_model_row_changed (model, rows->data, &iter);

 finish:
  g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (rows);
}



/* Callback which gets called when a new row is selected
   in the variable treeview.
   It sets the name and label entry widgets to reflect the
   currently selected row.
*/
static void
on_selection_change (GtkTreeSelection *selection, gpointer data)
{
  PsppireDialogActionRecode *rd = data;
  PsppireDialogActionRecodeDifferent *rdd = PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT (rd);

  GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

  GList *rows = gtk_tree_selection_get_selected_rows (selection, &model);

  if ( rows && !rows->next)
    {
      /* Exactly one row is selected */
      struct nlp *nlp;
      struct variable *var;
      gboolean ok;
      GtkTreeIter iter;

      gtk_widget_set_sensitive  (rd->change_button, TRUE);
      gtk_widget_set_sensitive  (rd->new_name_entry, TRUE);
      gtk_widget_set_sensitive  (rd->new_label_entry, TRUE);

      ok = gtk_tree_model_get_iter (model, &iter, (GtkTreePath*) rows->data);
      g_return_if_fail (ok);

      gtk_tree_model_get (model, &iter,
			  0, &var, 
			  -1);

      nlp = g_hash_table_lookup (rdd->varmap, var);

      if (nlp)
	{
	  gtk_entry_set_text (GTK_ENTRY (rd->new_name_entry), nlp->name ? nlp->name : "");
	  gtk_entry_set_text (GTK_ENTRY (rd->new_label_entry), nlp->label ? nlp->label : "");
	}
      else
	{
	  gtk_entry_set_text (GTK_ENTRY (rd->new_name_entry), "");
	  gtk_entry_set_text (GTK_ENTRY (rd->new_label_entry), "");
	}
    }
  else
    {
      gtk_widget_set_sensitive  (rd->change_button, FALSE);
      gtk_widget_set_sensitive  (rd->new_name_entry, FALSE);
      gtk_widget_set_sensitive  (rd->new_label_entry, FALSE);

      gtk_entry_set_text (GTK_ENTRY (rd->new_name_entry), "");
      gtk_entry_set_text (GTK_ENTRY (rd->new_label_entry), "");
    }


  g_list_foreach (rows, (GFunc) gtk_tree_path_free, NULL);
  g_list_free (rows);
}




static void
populate_treeview (PsppireDialogActionRecode *act)
{
  GtkTreeSelection *sel;
  PsppireDialogActionRecodeDifferent *rdd = PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT (act);
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
  GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes (_("New"),
								     renderer,
								     "text", NULL,
								     NULL);

  gtk_tree_view_column_set_cell_data_func (col, renderer,
					   render_new_var_name,
					   act, NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW (act->variable_treeview), col);

  col = gtk_tree_view_get_column (GTK_TREE_VIEW (act->variable_treeview), 0);

  g_object_set (col, "title", _("Old"), NULL);

  g_object_set (act->variable_treeview, "headers-visible", TRUE, NULL);

  rdd->varmap = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, nlp_destroy);

  sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (act->variable_treeview));

  g_signal_connect (sel, "changed",
		    G_CALLBACK (on_selection_change), act);

  g_signal_connect (act->change_button, "clicked",
		    G_CALLBACK (on_change_clicked),  act);
}


static void
psppire_dialog_action_recode_different_activate (PsppireDialogAction *a)
{
  PsppireDialogActionRecode *act = PSPPIRE_DIALOG_ACTION_RECODE (a);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);

  psppire_dialog_action_recode_pre_activate (act, populate_treeview);

  gtk_window_set_title (GTK_WINDOW (pda->dialog),
			_("Recode into Different Variables"));

  gtk_window_set_title (GTK_WINDOW (act->old_and_new_dialog),
			_("Recode into Different Variables: Old and New Values "));

  gtk_widget_show (act->output_variable_box);
  
  g_signal_connect_swapped (act->old_and_new_dialog, "show",
			    G_CALLBACK (on_old_new_show), act);

  psppire_dialog_action_set_refresh (pda, refresh);

  psppire_dialog_action_set_valid_predicate (pda,
					     dialog_state_valid);
}

static void
append_into_clause (const PsppireDialogActionRecode *rd, struct string *dds)
{
  PsppireDialogActionRecodeDifferent *rdd = PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT (rd);

  /* If applicable set the INTO clause which determines into which variables the new values go */
  GtkTreeIter iter;
  ds_put_cstr (dds, "\n\tINTO ");
  gboolean ok;
      
  for (ok = psppire_var_view_get_iter_first (PSPPIRE_VAR_VIEW (rd->variable_treeview), &iter);
       ok;
       ok = psppire_var_view_get_iter_next (PSPPIRE_VAR_VIEW (rd->variable_treeview), &iter))
    {
      struct nlp *nlp = NULL;
      const struct variable *var = psppire_var_view_get_variable (PSPPIRE_VAR_VIEW (rd->variable_treeview), 0, &iter);

      nlp = g_hash_table_lookup (rdd->varmap, var);
	    
      ds_put_cstr (dds, nlp->name);
      ds_put_cstr (dds, " ");
    }
}

static void
append_string_declarations (const PsppireDialogActionRecode *rd, struct string *dds)
{
  PsppireDialogActionRecodeDifferent *rdd = PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT (rd);

  /* Declare new string variables if applicable */
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->string_button)))
    {
      GHashTableIter iter;

      struct variable *var = NULL;
      struct nlp *nlp = NULL;

      g_hash_table_iter_init (&iter, rdd->varmap);
      while (g_hash_table_iter_next (&iter, (void**) &var, (void**) &nlp))
	{
	  ds_put_cstr (dds, "\nSTRING ");
	  ds_put_cstr (dds, nlp->name);
	  ds_put_c_format (dds, " (A%d).",
			   (int)
			   gtk_spin_button_get_value (GTK_SPIN_BUTTON (rd->width_entry)));
	}
    }
}

static void
append_new_value_labels (const PsppireDialogActionRecode *rd, struct string *dds)
{
  PsppireDialogActionRecodeDifferent *rdd = PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT (rd);

  /* If applicable, set labels for the new variables. */
  GHashTableIter iter;

  struct variable *var = NULL;
  struct nlp *nlp = NULL;

  g_hash_table_iter_init (&iter, rdd->varmap);
  while (g_hash_table_iter_next (&iter, (void**) &var, (void**) &nlp))
    {
      if (nlp->label)
	{
	  struct string sl;
	  ds_init_empty (&sl);
	  syntax_gen_string (&sl, ss_cstr (nlp->label));
	  ds_put_c_format (dds, "\nVARIABLE LABELS %s %s.",
			   nlp->name, ds_cstr (&sl));

	  ds_destroy (&sl);
	}
    }
}

static char *
diff_generate_syntax (const PsppireDialogAction *act)
{
  return psppire_dialog_action_recode_generate_syntax (act,
						       append_string_declarations,
						       append_into_clause,
						       append_new_value_labels);
}

static gboolean
target_is_string (const PsppireDialogActionRecode *rd)
{
  return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->string_button));
}

static void
psppire_dialog_action_recode_different_class_init (PsppireDialogActionRecodeDifferentClass *class)
{
  psppire_dialog_action_set_activation (class, psppire_dialog_action_recode_different_activate);

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = diff_generate_syntax;
  PSPPIRE_DIALOG_ACTION_RECODE_CLASS (class)->target_is_string = target_is_string;
}


static void
psppire_dialog_action_recode_different_init (PsppireDialogActionRecodeDifferent *act)
{
}

