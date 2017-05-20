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


#ifndef __PSPPIRE_IMPORT_ASSISTANT_H__
#define __PSPPIRE_IMPORT_ASSISTANT_H__

#include <glib-object.h>
#include <glib.h>

#include <gtk/gtk.h>

#include "libpspp/str.h"
#include "psppire-dict.h"
#include "data/spreadsheet-reader.h"
#include "psppire-text-file.h"

G_BEGIN_DECLS

struct spreadsheet;


#define PSPPIRE_TYPE_IMPORT_ASSISTANT (psppire_import_assistant_get_type ())

#define PSPPIRE_IMPORT_ASSISTANT(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_IMPORT_ASSISTANT, PsppireImportAssistant))

#define PSPPIRE_IMPORT_ASSISTANT_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_IMPORT_ASSISTANT, \
                                 PsppireImportAssistantClass))

#define PSPPIRE_IS_IMPORT_ASSISTANT(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_IMPORT_ASSISTANT))

#define PSPPIRE_IS_IMPORT_ASSISTANT_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_IMPORT_ASSISTANT))


#define PSPPIRE_IMPORT_ASSISTANT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_IMPORT_ASSISTANT, \
				   PsppireImportAssistantClass))

typedef struct _PsppireImportAssistant       PsppireImportAssistant;
typedef struct _PsppireImportAssistantClass  PsppireImportAssistantClass;


typedef void page_func (PsppireImportAssistant *, GtkWidget *page);

struct _PsppireImportAssistant
{
  GtkAssistant parent;

  GtkBuilder *builder;

  gint current_page;

  gchar *file_name;

  /* START The chooser page of the assistant. */
  GtkWidget *encoding_selector;
  GtkFileFilter *default_filter;
  /* END The chooser page of the assistant. */


  /* START The introduction page of the assistant. */
    GtkWidget *all_cases_button;
    GtkWidget *n_cases_button;
    GtkWidget *n_cases_spin;
    GtkWidget *percent_button;
    GtkWidget *percent_spin;
  /* END The introduction page of the assistant. */


/* START Page where the user chooses field separators. */

  /* How to break lines into columns. */
  //  struct string separators;   /* Field separators. */
  struct string quotes;       /* Quote characters. */

  GtkWidget *custom_cb;
  GtkWidget *custom_entry;
  GtkWidget *quote_cb;
  GtkWidget *quote_combo;

  GtkEntry *quote_entry;
  GtkWidget *fields_tree_view;

/* END Page where the user chooses field separators. */


/* START Page where the user verifies and adjusts input formats. */
   struct variable **modified_vars;
   size_t modified_var_cnt;
/* END Page where the user verifies and adjusts input formats. */


  /* START first line page */
  GtkWidget *first_line_tree_view;
  GtkWidget *variable_names_cb;
  /* END first line page */

  GMainLoop *main_loop;
  GtkWidget *paste_button;
  GtkWidget *reset_button;
  int response;
  int watch_cursor;

  GtkCellRenderer *prop_renderer;
  GtkCellRenderer *fixed_renderer;

  PsppireTextFile *text_file;

  GtkTreeModel *delimiters_model;

  struct sheet_spec_page *sheet_spec;

#if MERGE_SHEET
  /* The columns produced. */
  struct column *columns;     /* Information about each column. */
  size_t column_cnt;          /* Number of columns. */

  int skip_lines;             /* Number of initial lines to skip? */
  gboolean variable_names;        /* Variable names above first line of data? */
#endif

  struct dictionary *dict;

  GtkWidget *var_sheet;
  GtkWidget *data_sheet;

  struct spreadsheet *spreadsheet;
};

struct _PsppireImportAssistantClass
{
  GtkAssistantClass parent_class;
};

GType psppire_import_assistant_get_type (void) ;


GtkWidget *psppire_import_assistant_new (GtkWindow *toplevel);

gchar *psppire_import_assistant_generate_syntax (PsppireImportAssistant *);

G_END_DECLS

#endif /* __PSPPIRE_IMPORT_ASSISTANT_H__ */
