/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012, 2013, 2014, 2019  Free Software Foundation

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

#include "psppire-dialog-action-oneway.h"

#include <float.h>

#include "psppire-var-view.h"
#include "psppire-acr.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"
#include "helper.h"


#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#define     POSTHOC_BONFERRONI  0x01
#define     POSTHOC_GH          0x02
#define     POSTHOC_LSD         0x04
#define     POSTHOC_SCHEFFE     0x08
#define     POSTHOC_SIDAK       0x10
#define     POSTHOC_TUKEY       0x20

static void next (GtkWidget *widget, PsppireDialogActionOneway *);
static void prev (GtkWidget *widget, PsppireDialogActionOneway *);
static void run_posthoc_dialog (PsppireDialogActionOneway *ow);
static void run_contrasts_dialog (PsppireDialogActionOneway *csd);
static void push_new_store (GArray *contrast_stack, PsppireDialogActionOneway *csd);


static void psppire_dialog_action_oneway_init            (PsppireDialogActionOneway      *act);
static void psppire_dialog_action_oneway_class_init      (PsppireDialogActionOnewayClass *class);

G_DEFINE_TYPE (PsppireDialogActionOneway, psppire_dialog_action_oneway, PSPPIRE_TYPE_DIALOG_ACTION);


static char *
generate_syntax (const PsppireDialogAction *act)
{
  PsppireDialogActionOneway *ow = PSPPIRE_DIALOG_ACTION_ONEWAY (act);
  gchar *text;
  gint i;

  gboolean descriptives = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ow->descriptives));
  gboolean homogeneity = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ow->homogeneity));
  struct string dss;

  ds_init_cstr (&dss, "ONEWAY /VARIABLES=");

  psppire_var_view_append_names_str (PSPPIRE_VAR_VIEW (ow->vars_treeview), 0, &dss);

  ds_put_cstr (&dss, " BY ");

  ds_put_cstr (&dss, gtk_entry_get_text (GTK_ENTRY (ow->factor_entry)));

  if (descriptives || homogeneity)
    {
      ds_put_cstr (&dss, "\n\t/STATISTICS=");
      if (descriptives)
	ds_put_cstr (&dss, "DESCRIPTIVES ");
      if (homogeneity)
	ds_put_cstr (&dss, "HOMOGENEITY ");
    }
  if (ow->posthoc)
    {
      ds_put_cstr (&dss, "\n\t/POSTHOC=");
      if ( ow->posthoc & POSTHOC_BONFERRONI)
	ds_put_cstr (&dss, "BONFERRONI ");
      if ( ow->posthoc & POSTHOC_GH)
	ds_put_cstr (&dss, "GH ");
      if ( ow->posthoc & POSTHOC_LSD)
	ds_put_cstr (&dss, "LSD ");
      if ( ow->posthoc & POSTHOC_SCHEFFE)
	ds_put_cstr (&dss, "SCHEFFE ");
      if ( ow->posthoc & POSTHOC_SIDAK)
	ds_put_cstr (&dss, "SIDAK ");
      if ( ow->posthoc & POSTHOC_TUKEY)
	ds_put_cstr (&dss, "TUKEY ");
      }
  for (i = 0 ; i < ow->contrasts_array->len ; ++i )
    {
      GtkListStore *ls = g_array_index (ow->contrasts_array, GtkListStore*, i);
      GtkTreeIter iter;
      gboolean ok;

      ds_put_cstr (&dss, "\n\t/CONTRAST=");

      for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL(ls),
					       &iter);
 	   ok;
	   ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (ls), &iter))
	{
	  gdouble v;

	  gtk_tree_model_get (GTK_TREE_MODEL (ls), &iter, 0, &v, -1);

	  ds_put_c_format (&dss, " %.*g", DBL_DIG + 1, v);
	}
    }

  ds_put_cstr (&dss, ".\n");

  text = ds_steal_cstr (&dss);
  ds_destroy (&dss);

  return text;
}


static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionOneway *ow = PSPPIRE_DIALOG_ACTION_ONEWAY (data);

  GtkTreeModel *vars =
    gtk_tree_view_get_model (GTK_TREE_VIEW (ow->vars_treeview));

  GtkTreeIter notused;

  if ( !gtk_tree_model_get_iter_first (vars, &notused) )
    return FALSE;

  if ( 0 == strcmp ("", gtk_entry_get_text (GTK_ENTRY (ow->factor_entry))))
    return FALSE;


  return TRUE;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionOneway *ow = PSPPIRE_DIALOG_ACTION_ONEWAY (rd_);

  GtkTreeModel *model =
    gtk_tree_view_get_model (GTK_TREE_VIEW (ow->vars_treeview));

  gtk_entry_set_text (GTK_ENTRY (ow->factor_entry), "");

  gtk_list_store_clear (GTK_LIST_STORE (model));
}


/* Callback for when the list store currently associated with the
   treeview has changed.  It sets the widgets of the subdialog
   to reflect the store's new state.
*/
static void
list_store_changed (PsppireDialogActionOneway *csd)
{
  gboolean ok;
  gdouble total = 0.0;
  GtkTreeIter iter;
  GtkTreeModel *ls = NULL;
  gchar *text =
    g_strdup_printf (_("Contrast %d of %d"),
		     csd->c, csd->temp_contrasts->len);

  gtk_label_set_label (GTK_LABEL (csd->stack_label), text);

  g_free (text);

  gtk_widget_set_sensitive (csd->prev, csd->c > 1);

  if ( csd->c > 0 )
    ls = g_array_index (csd->temp_contrasts, GtkTreeModel*, csd->c - 1);

  psppire_acr_set_model (PSPPIRE_ACR (csd->acr), GTK_LIST_STORE (ls));

  /* Sensitive iff the liststore has two items or more */
  gtk_widget_set_sensitive (csd->next,
			    gtk_tree_model_iter_nth_child
			    (ls, &iter,  NULL, 1));

  for (ok = gtk_tree_model_get_iter_first (ls, &iter);
       ok;
       ok = gtk_tree_model_iter_next (ls, &iter)
       )
    {
      gdouble v;
      gtk_tree_model_get (ls, &iter, 0, &v, -1);
      total += v;
    }

  text = g_strdup_printf ("%.*g", DBL_DIG + 1, total);

  gtk_entry_set_text (GTK_ENTRY (csd->ctotal), text);

  g_free (text);
}


/* Copy the contrasts array into the local array */
static GArray *
clone_contrasts_array (GArray *src_array)
{
  gint i;

  GArray *dest_array =
    g_array_sized_new (FALSE, FALSE, sizeof (GtkListStore *),
		       src_array->len);

  for (i = 0 ; i < src_array->len ; ++i )
    {

      GtkTreeIter src_iter;
      GtkListStore *src = g_array_index (src_array, GtkListStore*, i);
      GtkListStore *dest;

      /* Refuse to copy empty stores */
      if (! gtk_tree_model_get_iter_first (GTK_TREE_MODEL (src),
					   &src_iter))
	continue;

      dest = clone_list_store (src);

      g_array_append_val (dest_array, dest);
    }

  return dest_array;
}


static GtkBuilder *
psppire_dialog_action_oneway_activate (PsppireDialogAction *a, GVariant *param)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionOneway *act = PSPPIRE_DIALOG_ACTION_ONEWAY (a);

  GtkBuilder *xml = builder_new ( "oneway.ui");

  GtkWidget *contrasts_button = get_widget_assert (xml, "contrasts-button");
/* Posthoc button */
  GtkWidget *posthoc_button= get_widget_assert (xml, "posthoc-button");
  GtkEntry *entry = GTK_ENTRY (get_widget_assert (xml, "entry1"));

  pda->dialog = get_widget_assert   (xml, "oneway-anova-dialog");
  pda->source = get_widget_assert   (xml, "oneway-anova-treeview1");

  act->vars_treeview =  get_widget_assert (xml, "oneway-anova-treeview2");
  act->factor_entry = get_widget_assert (xml, "oneway-anova-entry");

  act->descriptives =  get_widget_assert (xml, "checkbutton1");
  act->homogeneity =  get_widget_assert (xml, "checkbutton2");

/* Posthoc tests dialog */
  act->posthoc_dialog = get_widget_assert (xml, "posthoc-dialog");

  act->bonferroni_button = get_widget_assert (xml, "bonferroni-button");
  act->gh_button = get_widget_assert (xml, "gh-button");
  act->lsd_button = get_widget_assert (xml, "lsd-button");
  act->scheffe_button = get_widget_assert (xml, "scheffe-button");
  act->sidak_button = get_widget_assert (xml, "sidak-button");
  act->tukey_button = get_widget_assert (xml, "tukey-button");

  g_signal_connect_swapped (posthoc_button, "clicked",
		    G_CALLBACK (run_posthoc_dialog), act);

/* Contrast dialog */
  act->contrasts_dialog = get_widget_assert (xml, "contrasts-dialog");

  act->next = get_widget_assert (xml, "next-button");
  act->prev = get_widget_assert (xml, "prev-button");
  act->ctotal = get_widget_assert (xml, "entry2");
  act->acr = get_widget_assert (xml, "psppire-acr1");
  act->stack_label = get_widget_assert (xml, "contrast-stack-label");
  act->contrasts_array = g_array_new (FALSE, FALSE, sizeof (GtkListStore *));


  g_signal_connect (act->next, "clicked", G_CALLBACK (next), act);
  g_signal_connect (act->prev, "clicked", G_CALLBACK (prev), act);

  psppire_acr_set_entry (PSPPIRE_ACR (act->acr), entry);

  gtk_window_set_transient_for (GTK_WINDOW (act->contrasts_dialog),
				  GTK_WINDOW (pda->toplevel));


  g_signal_connect_swapped (contrasts_button, "clicked",
		    G_CALLBACK (run_contrasts_dialog), act);


  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);
  return xml;
}

static void
psppire_dialog_action_oneway_class_init (PsppireDialogActionOnewayClass *class)
{
  PSPPIRE_DIALOG_ACTION_CLASS (class)->initial_activate = psppire_dialog_action_oneway_activate;
  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_oneway_init (PsppireDialogActionOneway *act)
{
  act->contrasts_array = NULL;
  act->c = -1;
  act->posthoc = 0;
}
/* Posthoc dialog */
static void
run_posthoc_dialog (PsppireDialogActionOneway *ow)
{
  gint response;

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ow->bonferroni_button),
				ow->posthoc & POSTHOC_BONFERRONI);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ow->gh_button),
				ow->posthoc & POSTHOC_GH);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ow->lsd_button),
				ow->posthoc & POSTHOC_LSD);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ow->scheffe_button),
				ow->posthoc & POSTHOC_SCHEFFE);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ow->sidak_button),
				ow->posthoc & POSTHOC_SIDAK);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ow->tukey_button),
				ow->posthoc & POSTHOC_TUKEY);

  response = psppire_dialog_run (PSPPIRE_DIALOG (ow->posthoc_dialog));

  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      ow->posthoc= 0;
      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ow->bonferroni_button) ))
	ow->posthoc |= POSTHOC_BONFERRONI;

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ow->gh_button) ))
	ow->posthoc |= POSTHOC_GH;

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ow->lsd_button) ))
	ow->posthoc |= POSTHOC_LSD;

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ow->scheffe_button) ))
	ow->posthoc |= POSTHOC_SCHEFFE;

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ow->sidak_button) ))
	ow->posthoc |= POSTHOC_SIDAK;

      if ( gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (ow->tukey_button) ))
	ow->posthoc |= POSTHOC_TUKEY;
    }
}

static void
run_contrasts_dialog (PsppireDialogActionOneway *csd)
{
  gint response;

  csd->temp_contrasts = clone_contrasts_array (csd->contrasts_array);

  csd->c = 1;

  push_new_store (csd->temp_contrasts, csd);

  response = psppire_dialog_run (PSPPIRE_DIALOG (csd->contrasts_dialog));

  if ( response == PSPPIRE_RESPONSE_CONTINUE )
    {
      csd->contrasts_array = clone_contrasts_array (csd->temp_contrasts);
    }
}


static void
push_new_store (GArray *contrast_stack, PsppireDialogActionOneway *csd)
{
  GtkListStore *ls = gtk_list_store_new (1, G_TYPE_DOUBLE);

  g_array_append_val (contrast_stack, ls);

  g_signal_connect_swapped (ls, "row-deleted",
			    G_CALLBACK (list_store_changed), csd);

  g_signal_connect_swapped (ls, "row-changed",
			    G_CALLBACK (list_store_changed), csd);

  list_store_changed (csd);
}


static void
next (GtkWidget *widget, PsppireDialogActionOneway *csd)
{
  if (csd->c >= csd->temp_contrasts->len)
    push_new_store (csd->temp_contrasts, csd);

  csd->c++;

  list_store_changed (csd);
}


static void
prev (GtkWidget *widget, PsppireDialogActionOneway *csd)
{
  if ( csd->c > 0 )
    --csd->c;

  list_store_changed (csd);
}
