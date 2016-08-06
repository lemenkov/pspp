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

#include "psppire-dialog-action-recode.h"
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


/* This might need to be changed to something less naive.
   In particular, what happends with dates, etc?
 */
static gchar *
num_to_string (gdouble x)
{
  return g_strdup_printf ("%.*g", DBL_DIG + 1, x);
}

/* Define a boxed type to represent a value which is a candidate
   to replace an existing value */

enum new_value_type
 {
   NV_NUMERIC,
   NV_STRING,
   NV_SYSMIS,
   NV_COPY
 };


struct new_value
{
  enum new_value_type type;
  union {
    double v;
    gchar *s;
  } v;
};


static struct new_value *
new_value_copy (struct new_value *nv)
{
  struct new_value *copy = g_memdup (nv, sizeof (*copy));

  if ( nv->type == NV_STRING )
    copy->v.s = xstrdup (nv->v.s);

  return copy;
}


static void
new_value_free (struct new_value *nv)
{
  if ( nv->type == NV_STRING )
    g_free (nv->v.s);

  g_free (nv);
}


static void
new_value_to_string (const GValue *src, GValue *dest)
{
  const struct new_value *nv = g_value_get_boxed (src);

  g_assert (nv);

  switch (nv->type)
    {
    case NV_NUMERIC:
      {
	gchar *text = g_strdup_printf ("%.*g", DBL_DIG + 1, nv->v.v);
	g_value_set_string (dest, text);
	g_free (text);
      }
      break;
    case NV_STRING:
      g_value_set_string (dest, nv->v.s);
      break;
    case NV_COPY:
      g_value_set_string (dest, "COPY");
      break;
    case NV_SYSMIS:
      g_value_set_string (dest, "SYSMIS");
      break;
    default:
      /* Shouldn't ever happen */
      g_warning ("Invalid type in new recode value");
      g_value_set_string (dest, "???");
      break;
    }
}

GType
new_value_get_type (void)
{
  static GType t = 0;

  if (t == 0 )
    {
      t = g_boxed_type_register_static  ("psppire-recode-new-values",
					 (GBoxedCopyFunc) new_value_copy,
					 (GBoxedFreeFunc) new_value_free);

      g_value_register_transform_func (t, G_TYPE_STRING,
				       new_value_to_string);
    }

  return t;
}



static void
on_string_toggled (GtkToggleButton *b, PsppireDialogActionRecode *rd)
{
  gboolean active;
  if (! rd->input_var_is_string )
    return ;

  active = gtk_toggle_button_get_active (b);
  gtk_widget_set_sensitive (rd->convert_button, !active);
}


static void
on_convert_toggled (GtkToggleButton *b, PsppireDialogActionRecode *rd)
{
  gboolean active;

  g_return_if_fail (rd->input_var_is_string);

  active = gtk_toggle_button_get_active (b);
  gtk_widget_set_sensitive (rd->string_button, !active);
}

static void
focus_value_entry (GtkWidget *w, PsppireDialogActionRecode *rd)
{
  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)))
    gtk_widget_grab_focus (rd->new_value_entry);
}


/* Callback for the new_value_entry and new_value_togglebutton widgets.
   It's used to enable/disable the acr. */
static void
set_acr (PsppireDialogActionRecode *rd)
{
  const gchar *text;

  if ( !gtk_toggle_button_get_active
       (GTK_TOGGLE_BUTTON (rd->toggle[BUTTON_NEW_VALUE])))
    {
      psppire_acr_set_enabled (PSPPIRE_ACR (rd->acr), TRUE);
      return;
    }

  text = gtk_entry_get_text (GTK_ENTRY (rd->new_value_entry));

  psppire_acr_set_enabled (PSPPIRE_ACR (rd->acr), !g_str_equal (text, ""));
}

enum
  {
    COL_VALUE_OLD,
    COL_VALUE_NEW,
    n_COL_VALUES
  };

/* Callback which gets called when a new row is selected
   in the acr's variable treeview.
   We use if to set the togglebuttons and entries to correspond to the
   selected row.
*/
static void
on_acr_selection_change (GtkTreeSelection *selection, gpointer data)
{
  PsppireDialogActionRecode *rd = data;
  GtkTreeModel *model = NULL;
  GtkTreeIter iter;

  GValue ov_value = {0};
  GValue nv_value = {0};
  struct old_value *ov = NULL;
  struct new_value *nv = NULL;

  if ( ! gtk_tree_selection_get_selected (selection, &model, &iter) )
    return;


  gtk_tree_model_get_value (GTK_TREE_MODEL (model), &iter,
			    COL_VALUE_OLD, &ov_value);

  gtk_tree_model_get_value (GTK_TREE_MODEL (model), &iter,
			    COL_VALUE_NEW, &nv_value);

  ov = g_value_get_boxed (&ov_value);
  nv = g_value_get_boxed (&nv_value);

  if (nv)
    {
      switch (nv->type)
	{
	case NV_NUMERIC:
	  {
	    gchar *str = num_to_string (nv->v.v);

	    gtk_toggle_button_set_active
	      (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_VALUE]), TRUE);

	    gtk_entry_set_text (GTK_ENTRY (rd->new_value_entry), str);
	    g_free (str);
	  }
	  break;
	case NV_STRING:
	  gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_VALUE]), TRUE);

	  gtk_entry_set_text (GTK_ENTRY (rd->new_value_entry), nv->v.s);
	  break;
	case NV_SYSMIS:
	  gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_SYSMIS]), TRUE);

	  break;
	case NV_COPY:
	  gtk_toggle_button_set_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_COPY]), TRUE);

	  break;
	default:
	  g_warning ("Invalid new value type");
	  break;
	}

      g_value_unset (&nv_value);
    }

  psppire_val_chooser_set_status (PSPPIRE_VAL_CHOOSER (rd->old_value_chooser), ov);
}

/* Initialise VAL to reflect the current status of RD */
static gboolean
set_old_value (GValue *val, const PsppireDialogActionRecode *rd)
{
  PsppireValChooser *vc = PSPPIRE_VAL_CHOOSER (rd->old_value_chooser);

  struct old_value ov;

  psppire_val_chooser_get_status (vc, &ov);

  g_value_init (val, old_value_get_type ());
  g_value_set_boxed (val, &ov);

  return TRUE;
}


/* Initialse VAL to reflect the current status of RD */
static gboolean
set_new_value (GValue *val, const PsppireDialogActionRecode *rd)
{
  const gchar *text = NULL;
  struct new_value nv;

  if (gtk_toggle_button_get_active
      (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_VALUE])))
    {
      text = gtk_entry_get_text (GTK_ENTRY (rd->new_value_entry));
      nv.type = NV_NUMERIC;

      if (PSPPIRE_DIALOG_ACTION_RECODE_CLASS (G_OBJECT_GET_CLASS (rd))->target_is_string (rd))
	nv.type = NV_STRING;

      if ( nv.type == NV_STRING )
	nv.v.s = g_strdup (text);
      else
	nv.v.v = g_strtod (text, 0);
    }
  else if (gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_COPY])))
    {
      nv.type = NV_COPY;
    }
  else if (gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (rd->toggle [BUTTON_NEW_SYSMIS])))
    {
      nv.type = NV_SYSMIS;
    }
  else
    return FALSE;

  g_value_init (val, new_value_get_type ());
  g_value_set_boxed (val, &nv);

  return TRUE;
}

/* A function to set a value in a column in the ACR */
static gboolean
set_value (gint col, GValue  *val, gpointer data)
{
  PsppireDialogActionRecode *rd = data;

  switch ( col )
    {
    case COL_VALUE_OLD:
      set_old_value (val, rd);
      break;
    case COL_VALUE_NEW:
      set_new_value (val, rd);
      break;
    default:
      return FALSE;
    }

  return TRUE;
}

static void
run_old_and_new_dialog (PsppireDialogActionRecode *rd)
{
  gint response;
  GtkListStore *local_store = clone_list_store (rd->value_map);
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (rd);

  psppire_acr_set_model (PSPPIRE_ACR (rd->acr), local_store);
  psppire_acr_set_get_value_func (PSPPIRE_ACR (rd->acr), set_value, rd);

  {
    /* Find the type of the first variable (it's invariant that
       all variables are of the same type) */
    const struct variable *v;
    GtkTreeIter iter;
    GtkTreeModel *model =
      gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

    gboolean not_empty = gtk_tree_model_get_iter_first (model, &iter);

    g_return_if_fail (not_empty);

    gtk_tree_model_get (model, &iter, 0, &v, -1);

    rd->input_var_is_string = var_is_alpha (v);

    g_object_set (rd->old_value_chooser, "is-string", rd->input_var_is_string, NULL);

    gtk_widget_set_sensitive (rd->toggle [BUTTON_NEW_SYSMIS],
			      var_is_numeric (v));

    gtk_widget_set_sensitive (rd->convert_button, var_is_alpha (v));
  }


  response = psppire_dialog_run (rd->old_and_new_dialog);
  psppire_acr_set_model (PSPPIRE_ACR (rd->acr), NULL);


  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      g_object_unref (rd->value_map);
      rd->value_map = clone_list_store (local_store);
    }
  else
    g_object_unref (local_store);


  psppire_dialog_notify_change (PSPPIRE_DIALOG (pda->dialog));
}


/* Sets the sensitivity of TARGET dependent upon the active status
   of BUTTON */
static void
toggle_sensitivity (GtkToggleButton *button, GtkWidget *target)
{
  gboolean state = gtk_toggle_button_get_active (button);

  /*  g_print ("%s Setting %p (%s) to %d because of %p\n",
      __FUNCTION__, target, gtk_widget_get_name (target), state, button); */

  gtk_widget_set_sensitive (target, state);
}




static void
psppire_dialog_action_recode_class_init (PsppireDialogActionRecodeClass *class);

G_DEFINE_TYPE (PsppireDialogActionRecode, psppire_dialog_action_recode, PSPPIRE_TYPE_DIALOG_ACTION);

void
psppire_dialog_action_recode_refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionRecode *rd = PSPPIRE_DIALOG_ACTION_RECODE (rd_);

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (rd->variable_treeview));

  gtk_list_store_clear (GTK_LIST_STORE (vars));

  gtk_widget_set_sensitive (rd->change_button, FALSE);
  gtk_widget_set_sensitive (rd->new_name_entry, FALSE);
  gtk_widget_set_sensitive (rd->new_label_entry, FALSE);

  gtk_list_store_clear (GTK_LIST_STORE (rd->value_map));
}


void
psppire_dialog_action_recode_pre_activate (PsppireDialogActionRecode *act, void (*populate_treeview) (PsppireDialogActionRecode *))
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (act);

  GHashTable *thing = psppire_dialog_action_get_hash_table (pda);
  GtkBuilder *xml = g_hash_table_lookup (thing, act);
  if (!xml)
    {
      xml = builder_new ("recode.ui");
      g_hash_table_insert (thing, act, xml);

      pda->dialog = get_widget_assert   (xml, "recode-dialog");
      pda->source = get_widget_assert   (xml, "treeview1");

  
      GtkWidget *selector = get_widget_assert (xml, "psppire-selector1");
      GtkWidget *oldandnew = get_widget_assert (xml, "button1");


      act->output_variable_box = get_widget_assert (xml,"frame4");

      act->change_button = get_widget_assert (xml, "change-button");
      act->variable_treeview =   get_widget_assert (xml, "treeview2");
      act->new_name_entry = get_widget_assert (xml, "dest-name-entry");
      act->new_label_entry = get_widget_assert (xml, "dest-label-entry");

      act->value_map = gtk_list_store_new (2,
					   old_value_get_type (),
					   new_value_get_type ());

      if (populate_treeview)
	populate_treeview (act);
      
      psppire_selector_set_allow (PSPPIRE_SELECTOR (selector), homogeneous_types);

      /* Set up the Old & New Values subdialog */
      {
	act->string_button = get_widget_assert (xml, "checkbutton1");
	act->width_entry   = get_widget_assert (xml, "spinbutton1");

	act->convert_button = get_widget_assert (xml, "checkbutton2");

	act->old_value_chooser = get_widget_assert (xml, "val-chooser");

	act->new_value_entry = get_widget_assert (xml, "entry1");


	act->toggle[BUTTON_NEW_VALUE]  = get_widget_assert (xml, "radiobutton1");
	act->toggle[BUTTON_NEW_SYSMIS] = get_widget_assert (xml, "radiobutton2");
	act->toggle[BUTTON_NEW_COPY]   = get_widget_assert (xml, "radiobutton3");

	act->new_copy_label = get_widget_assert (xml, "label3");
	act->strings_box    = get_widget_assert (xml, "table3");

	act->old_and_new_dialog =
	  PSPPIRE_DIALOG (get_widget_assert (xml, "old-new-values-dialog"));

	act->acr = get_widget_assert (xml, "psppire-acr1");

	g_signal_connect_swapped (act->toggle[BUTTON_NEW_VALUE], "toggled",
				  G_CALLBACK (set_acr), act);

	g_signal_connect_after (act->toggle[BUTTON_NEW_VALUE], "toggled",
				G_CALLBACK (focus_value_entry), act);

	g_signal_connect_swapped (act->new_value_entry, "changed",
				  G_CALLBACK (set_acr), act);

	{
	  GtkTreeSelection *sel;
	  /* Remove the ACR's default column.  We don't like it */
	  GtkTreeViewColumn *column = gtk_tree_view_get_column (PSPPIRE_ACR(act->acr)->tv, 0);
	  gtk_tree_view_remove_column (PSPPIRE_ACR (act->acr)->tv, column);


	  column =
	    gtk_tree_view_column_new_with_attributes (_("Old"),
						      gtk_cell_renderer_text_new (),
						      "text", 0,
						      NULL);

	  gtk_tree_view_append_column (PSPPIRE_ACR (act->acr)->tv, column);

	  column =
	    gtk_tree_view_column_new_with_attributes (_("New"),
						      gtk_cell_renderer_text_new (),
						      "text", 1,
						      NULL);

	  gtk_tree_view_append_column (PSPPIRE_ACR(act->acr)->tv, column);
	  g_object_set (PSPPIRE_ACR (act->acr)->tv, "headers-visible", TRUE, NULL);


	  sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (PSPPIRE_ACR(act->acr)->tv));
	  g_signal_connect (sel, "changed",
			    G_CALLBACK (on_acr_selection_change), act);
	}


	g_signal_connect_swapped (oldandnew, "clicked",
				  G_CALLBACK (run_old_and_new_dialog), act);


	g_signal_connect (act->toggle[BUTTON_NEW_VALUE], "toggled",
			  G_CALLBACK (toggle_sensitivity), act->new_value_entry);

	g_signal_connect (act->string_button, "toggled",
			  G_CALLBACK (toggle_sensitivity), act->width_entry);

	g_signal_connect (act->string_button, "toggled",
			  G_CALLBACK (on_string_toggled), act);

	g_signal_connect (act->convert_button, "toggled",
			  G_CALLBACK (on_convert_toggled), act);
      }
    }
}

/* Generate a syntax fragment for NV and append it to STR */
static void
new_value_append_syntax (struct string *dds, const struct new_value *nv)
{
  switch (nv->type)
    {
    case NV_NUMERIC:
      ds_put_c_format (dds, "%.*g", DBL_DIG + 1, nv->v.v);
      break;
    case NV_STRING:
      syntax_gen_string (dds, ss_cstr (nv->v.s));
      break;
    case NV_COPY:
      ds_put_cstr (dds, "COPY");
      break;
    case NV_SYSMIS:
      ds_put_cstr (dds, "SYSMIS");
      break;
    default:
      /* Shouldn't ever happen */
      g_warning ("Invalid type in new recode value");
      ds_put_cstr (dds, "???");
      break;
    }
}


char *
psppire_dialog_action_recode_generate_syntax (const PsppireDialogAction *act,
					      void (*append_string_decls) (const PsppireDialogActionRecode *, struct string *),
					      void (*append_into_clause) (const PsppireDialogActionRecode *, struct string *),
					      void (*append_new_value_labels) (const PsppireDialogActionRecode *, struct string *))
{
  PsppireDialogActionRecode *rd = PSPPIRE_DIALOG_ACTION_RECODE (act);
  gboolean ok;
  GtkTreeIter iter;
  gchar *text;
  struct string dds;

  ds_init_empty (&dds);

  append_string_decls (rd, &dds);
  
  ds_put_cstr (&dds, "\nRECODE ");

  psppire_var_view_append_names_str (PSPPIRE_VAR_VIEW (rd->variable_treeview), 0, &dds);

  ds_put_cstr (&dds, "\n\t");

  if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (rd->convert_button)))
    {
      ds_put_cstr (&dds, "(CONVERT) ");
    }

  for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (rd->value_map),
					   &iter);
       ok;
       ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (rd->value_map), &iter))
    {
      GValue ov_value = {0};
      GValue nv_value = {0};
      struct old_value *ov;
      struct new_value *nv;
      gtk_tree_model_get_value (GTK_TREE_MODEL (rd->value_map), &iter,
				COL_VALUE_OLD, &ov_value);

      gtk_tree_model_get_value (GTK_TREE_MODEL (rd->value_map), &iter,
				COL_VALUE_NEW, &nv_value);

      ov = g_value_get_boxed (&ov_value);
      nv = g_value_get_boxed (&nv_value);

      ds_put_cstr (&dds, "(");

      old_value_append_syntax (&dds, ov);
      ds_put_cstr (&dds, " = ");
      new_value_append_syntax (&dds, nv);

      ds_put_cstr (&dds, ") ");
      g_value_unset (&ov_value);
      g_value_unset (&nv_value);
    }

  append_into_clause (rd, &dds);

  ds_put_cstr (&dds, ".");

  append_new_value_labels (rd, &dds);
  
  ds_put_cstr (&dds, "\nEXECUTE.\n");


  text = ds_steal_cstr (&dds);

  ds_destroy (&dds);

  return text;
}


static void
psppire_dialog_action_recode_class_init (PsppireDialogActionRecodeClass *class)
{
}


static void
psppire_dialog_action_recode_init (PsppireDialogActionRecode *act)
{
}

