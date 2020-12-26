/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2020  Free Software Foundation

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

#ifndef PSPPIRE_IMPORT_TEXTFILE_H
#define PSPPIRE_IMPORT_TEXTFILE_H

#include "psppire-import-assistant.h"

struct separator
{
  const char *name;           /* Name (for use with get_widget_assert). */
  gunichar c;                 /* Separator character. */
};

/* All the separators in the dialog box. */
static const struct separator separators[] =
  {
    {"space",     ' '},
    {"tab",       '\t'},
    {"bang",      '!'},
    {"colon",     ':'},
    {"comma",     ','},
    {"hyphen",    '-'},
    {"pipe",      '|'},
    {"semicolon", ';'},
    {"slash",     '/'},
  };

#define SEPARATOR_CNT (sizeof separators / sizeof *separators)

/* Initializes IA's intro substructure. */
void intro_page_create (PsppireImportAssistant *ia);
void first_line_page_create (PsppireImportAssistant *ia);
void separators_page_create (PsppireImportAssistant *ia);

/* Set the data model for both the data sheet and the variable sheet.  */
void textfile_set_data_models (PsppireImportAssistant *ia);

void text_spec_gen_syntax (PsppireImportAssistant *ia, struct string *s);


#endif
