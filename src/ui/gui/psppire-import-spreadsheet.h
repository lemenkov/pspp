#ifndef PSPPIRE_IMPORT_SPREADSHEET_H
#define PSPPIRE_IMPORT_SPREADSHEET_H

/* Initializes IA's sheet_spec substructure. */
void sheet_spec_page_create (PsppireImportAssistant *ia);

/* Set the data model for both the data sheet and the variable sheet.  */
void spreadsheet_set_data_models (PsppireImportAssistant *ia);

#endif
