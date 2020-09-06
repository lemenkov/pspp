/* PSPPIRE - a graphical user interface for PSPP.
    Copyright (C) 2017  Free Software Foundation

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _PSPPIRE_VARIABLE_SHEET_H
#define _PSPPIRE_VARIABLE_SHEET_H

#include <gtk/gtk.h>
#include <ssw-sheet.h>


struct dispatch;

struct _PsppireVariableSheet
{
  SswSheet parent_instance;

  GtkCellRenderer *value_label_renderer;
  GtkCellRenderer *missing_values_renderer;
  GtkCellRenderer *var_type_renderer;

  struct dispatch *value_label_dispatch;
  struct dispatch *missing_values_dispatch;
  struct dispatch *var_type_dispatch;

  /* Row header popup menu */
  GtkWidget *row_popup;
  GtkWidget *clear_variables_menu_item;

  gboolean dispose_has_run;
};

struct _PsppireVariableSheetClass
{
  SswSheetClass parent_class;
};


#define PSPPIRE_TYPE_VARIABLE_SHEET psppire_variable_sheet_get_type ()

G_DECLARE_FINAL_TYPE (PsppireVariableSheet, psppire_variable_sheet, PSPPIRE, VARIABLE_SHEET, SswSheet)

GtkWidget *psppire_variable_sheet_new (void);

#endif
