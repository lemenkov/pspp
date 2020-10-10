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


#endif
