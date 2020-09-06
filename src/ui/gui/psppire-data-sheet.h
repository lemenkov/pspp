/* PSPPIRE - a graphical user interface for PSPP.
    Copyright (C) 2017, 2019  Free Software Foundation

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

#ifndef _PSPPIRE_DATA_SHEET_H
#define _PSPPIRE_DATA_SHEET_H

#include <gtk/gtk.h>
#include <ssw-sheet.h>

struct _PsppireDataSheet
{
  SswSheet parent_instance;

  GtkWidget *data_sheet_cases_column_popup;

  GtkWidget *data_clear_variables_menu_item;
  GtkWidget *data_clear_cases_menu_item;
  GtkWidget *data_sheet_cases_row_popup;

  /* Data sheet popup menu */
  GtkWidget *data_sort_ascending_menu_item;
  GtkWidget *data_sort_descending_menu_item;
};

struct _PsppireDataSheetClass
{
  SswSheetClass parent_class;
};

#define PSPPIRE_TYPE_DATA_SHEET psppire_data_sheet_get_type ()

G_DECLARE_FINAL_TYPE (PsppireDataSheet, psppire_data_sheet, PSPPIRE, DATA_SHEET, SswSheet)

GtkWidget *psppire_data_sheet_new (void);

void psppire_data_sheet_delete_variables (PsppireDataSheet *sheet);

void psppire_data_sheet_insert_new_variable_at_posn (PsppireDataSheet *sheet, gint posn);


#endif
