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

#include "psppire-dialog-action-scatterplot.h"

#include "psppire-var-view.h"

#include "psppire-dialog.h"
#include "builder-wrapper.h"
#include "helper.h"


#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


static void psppire_dialog_action_scatterplot_init            (PsppireDialogActionScatterplot      *act);
static void psppire_dialog_action_scatterplot_class_init      (PsppireDialogActionScatterplotClass *class);

G_DEFINE_TYPE (PsppireDialogActionScatterplot, psppire_dialog_action_scatterplot, PSPPIRE_TYPE_DIALOG_ACTION);


static char *
generate_syntax (const PsppireDialogAction *act)
{
  PsppireDialogActionScatterplot *ow = PSPPIRE_DIALOG_ACTION_SCATTERPLOT (act);
  gchar *text;
  struct string dss;

  ds_init_cstr (&dss, "GRAPH SCATTERPLOT(BIVARIATE) = ");

  ds_put_cstr (&dss, gtk_entry_get_text (GTK_ENTRY (ow->x_axis)));
  
  ds_put_cstr (&dss, " WITH ");

  ds_put_cstr (&dss, gtk_entry_get_text (GTK_ENTRY (ow->y_axis)));

  ds_put_cstr (&dss, ".\n");

  text = ds_steal_cstr (&dss);
  ds_destroy (&dss);

  return text;
}


static gboolean
dialog_state_valid (gpointer data)
{
  PsppireDialogActionScatterplot *ow = PSPPIRE_DIALOG_ACTION_SCATTERPLOT (data);

  const char *xvar = gtk_entry_get_text (GTK_ENTRY  (ow->x_axis));
  const char *yvar = gtk_entry_get_text (GTK_ENTRY  (ow->y_axis));

  if ( 0 == strcmp ("", xvar))
    return FALSE;

  if ( 0 == strcmp ("", yvar))
    return FALSE;

  
  return TRUE;
}

static void
refresh (PsppireDialogAction *rd_)
{
  PsppireDialogActionScatterplot *ow = PSPPIRE_DIALOG_ACTION_SCATTERPLOT (rd_);

  gtk_entry_set_text (GTK_ENTRY (ow->x_axis), "");
  gtk_entry_set_text (GTK_ENTRY (ow->y_axis), "");
}



static void
psppire_dialog_action_scatterplot_activate (PsppireDialogAction *a)
{
  PsppireDialogAction *pda = PSPPIRE_DIALOG_ACTION (a);
  PsppireDialogActionScatterplot *act = PSPPIRE_DIALOG_ACTION_SCATTERPLOT (a);

  GHashTable *thing = psppire_dialog_action_get_hash_table (pda);
  GtkBuilder *xml = g_hash_table_lookup (thing, a);
  if (!xml)
    {
      xml = builder_new ("scatterplot.ui");
      g_hash_table_insert (thing, a, xml);
    }

  pda->dialog = get_widget_assert   (xml, "scatterplot-dialog");
  pda->source = get_widget_assert   (xml, "scatterplot-treeview1");

  act->y_axis =  get_widget_assert (xml, "scatterplot-y-axis");
  act->x_axis =  get_widget_assert (xml, "scatterplot-x-axis");

  psppire_dialog_action_set_valid_predicate (pda, dialog_state_valid);
  psppire_dialog_action_set_refresh (pda, refresh);

}

static void
psppire_dialog_action_scatterplot_class_init (PsppireDialogActionScatterplotClass *class)
{
  psppire_dialog_action_set_activation (class, psppire_dialog_action_scatterplot_activate);

  PSPPIRE_DIALOG_ACTION_CLASS (class)->generate_syntax = generate_syntax;
}


static void
psppire_dialog_action_scatterplot_init (PsppireDialogActionScatterplot *act)
{
}
