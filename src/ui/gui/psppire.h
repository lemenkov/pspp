/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2005, 2006, 2009, 2010, 2011  Free Software Foundation

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

#ifndef PSPPIRE_H
#define PSPPIRE_H

#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <stdbool.h>

struct lexer;


struct init_source
{
  GSource parent;
  int state;
  GMainLoop *loop;
  int filename_arg;
  int *argc;
  char ***argv;
};

bool initialize (const struct init_source *is);

void de_initialize (void);

void psppire_quit (GApplication *app);

const char * output_file_name (void);

void psppire_set_lexer (struct lexer *);

void register_selection_functions (void);

GtkWindow * psppire_preload_file (const gchar *file);


#endif /* PSPPIRE_H */
