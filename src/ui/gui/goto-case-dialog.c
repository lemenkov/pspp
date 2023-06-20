/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2011, 2012, 2016  Free Software Foundation

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
#include "goto-case-dialog.h"
#include "builder-wrapper.h"
#include "psppire-dialog.h"
#include "psppire-data-store.h"
#include "psppire-data-sheet.h"


static void
refresh (PsppireDataSheet *ds, GtkBuilder *xml)
{
  GtkTreeModel *tm = NULL;
  g_object_get (ds, "data-model", &tm, NULL);

  GtkWidget *case_num_entry = get_widget_assert (xml, "goto-case-case-num-entry");
  gint case_count =  gtk_tree_model_iter_n_children (tm, NULL);
  g_object_unref (tm);

  gtk_spin_button_set_range (GTK_SPIN_BUTTON (case_num_entry), 1, case_count);
}

void
goto_case_dialog (PsppireDataSheet *ds)
{
  GtkWindow *top_level;
  gint response;
  GtkBuilder *xml = builder_new ("goto-case.ui");
  GtkWidget *dialog = get_widget_assert   (xml, "goto-case-dialog");

  top_level = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (ds)));
  gtk_window_set_transient_for (GTK_WINDOW (dialog), top_level);

  refresh (ds, xml);

  response = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  if (response == PSPPIRE_RESPONSE_GOTO)
    {
      GtkTreeModel *tm  = NULL;
      g_object_get (ds, "data-model", &tm, NULL);

      GtkWidget *case_num_entry =
          get_widget_assert (xml, "goto-case-case-num-entry");

      gint case_num =
        gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (case_num_entry))
          - FIRST_CASE_NUMBER ;

      gint case_count = gtk_tree_model_iter_n_children (tm, NULL);
      g_object_unref (tm);

      if (case_num >= 0 && case_num < case_count)
      {
        ssw_sheet_scroll_to (SSW_SHEET (ds), -1, case_num);
        ssw_sheet_set_active_cell (SSW_SHEET (ds), -1, case_num, 0);
      }
    }
}
