/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005, 2006, 2009, 2010, 2011, 2012, 2013, 2014, 2016  Free Software Foundation

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


#include <assert.h>
#include <gsl/gsl_errno.h>
#include <gtk/gtk.h>
#include <libintl.h>
#include <unistd.h>

#include "data/any-reader.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/datasheet.h"
#include "data/file-handle-def.h"
#include "data/session.h"
#include "data/settings.h"

#include "language/lexer/lexer.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/version.h"

#include "output/driver.h"
#include "output/journal.h"
#include "output/message-item.h"

#include "ui/gui/dict-display.h"
#include "ui/gui/executor.h"
#include "ui/gui/psppire-data-store.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dict.h"
#include "ui/gui/psppire.h"
#include "ui/gui/psppire-output-window.h"
#include "ui/gui/psppire-syntax-window.h"
#include "ui/gui/psppire-selector.h"
#include "ui/gui/psppire-var-view.h"
#include "ui/gui/psppire-means-layer.h"
#include "ui/gui/psppire-window-register.h"
#include "ui/gui/widgets.h"
#include "ui/source-init-opts.h"
#include "ui/syntax-gen.h"


#include "gl/configmake.h"
#include "gl/xalloc.h"
#include "gl/relocatable.h"

void create_icon_factory (void);

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

void
register_selection_functions (void)
{
  psppire_selector_set_default_selection_func (GTK_TYPE_ENTRY, insert_source_row_into_entry);
  psppire_selector_set_default_selection_func (PSPPIRE_VAR_VIEW_TYPE, insert_source_row_into_tree_view);
  psppire_selector_set_default_selection_func (GTK_TYPE_TREE_VIEW, insert_source_row_into_tree_view);
  psppire_selector_set_default_selection_func (PSPPIRE_TYPE_MEANS_LAYER, insert_source_row_into_layers);
}

bool
initialize (const struct init_source *is)
{
  switch (is->state)
    {
    case 0:
      i18n_init ();
      break;
    case 1:
      preregister_widgets ();
      break;
    case 2:
      gsl_set_error_handler_off ();
      break;
    case 3:
      output_engine_push ();
      break;
    case 4:
      settings_init ();
      break;
    case 5:
      fh_init ();
      break;
    case 6:
      psppire_set_lexer (NULL);
      break;
    case 7:
      bind_textdomain_codeset (PACKAGE, "UTF-8");
      break;
    case 8:
      if ( ! gtk_parse_args (is->argc, is->argv) )
	{
	  perror ("Error parsing arguments");
	  exit (1);
	}
      break;
    case 9:
      journal_init ();
      break;
    case 10:
      textdomain (PACKAGE);
      break;
    default:
      return TRUE;
      break;
    }
  return FALSE;
}


void
de_initialize (void)
{
  settings_done ();
  output_engine_pop ();
  i18n_done ();
}

void
psppire_quit (GApplication *app)
{
  g_application_quit (app);
}

struct icon_size
{
  int resolution;  /* The dimension of the images which will be used */
  size_t n_sizes;  /* The number of items in the array below. */
  const GtkIconSize *usage; /* An array determining for what the icon set is used */
};

static void
handle_msg (const struct msg *m_, void *lexer_)
{
  struct lexer *lexer = lexer_;
  struct msg m = *m_;

  if (lexer != NULL && m.file_name == NULL)
    {
      m.file_name = CONST_CAST (char *, lex_get_file_name (lexer));
      m.first_line = lex_get_first_line_number (lexer, 0);
      m.last_line = lex_get_last_line_number (lexer, 0);
      m.first_column = lex_get_first_column (lexer, 0);
      m.last_column = lex_get_last_column (lexer, 0);
    }

  message_item_submit (message_item_create (&m));
}

void
psppire_set_lexer (struct lexer *lexer)
{
  msg_set_handler (handle_msg, lexer);
}


GtkWindow *
psppire_preload_file (const gchar *file)
{
  const gchar *local_encoding = "UTF-8";
  
  struct file_handle *fh = fh_create_file (NULL,
					   file,
					   local_encoding,
					   fh_default_properties ());
  const char *filename = fh_get_file_name (fh);
  
  int retval = any_reader_detect (fh, NULL);

  GtkWindow *w = NULL;
  /* Check to see if the file is a .sav or a .por file.  If not
     assume that it is a syntax file */
  if (retval == 1)
    w = open_data_window (NULL, filename, NULL, NULL);
  else if (retval == 0)
    {
      create_data_window ();
      w = open_syntax_window (filename, NULL);
    }

  fh_unref (fh);
  return w;
}
