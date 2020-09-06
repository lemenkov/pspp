/*  PSPPIRE - a graphical user interface for PSPP.
    Copyright (C) 2016  Free Software Foundation

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

#ifndef _PSPPIRE_VAR_SHEET_HEADER_H
#define _PSPPIRE_VAR_SHEET_HEADER_H


G_DECLARE_FINAL_TYPE (PsppireVarSheetHeader, psppire_var_sheet_header, PSPPIRE, VAR_SHEET_HEADER, GObject)


struct _PsppireVarSheetHeader
{
  GObject parent_instance;
};

struct _PsppireVarSheetHeaderClass
{
  GObjectClass parent_instance;
};



#define PSPPIRE_TYPE_VAR_SHEET_HEADER psppire_var_sheet_header_get_type ()

#endif
