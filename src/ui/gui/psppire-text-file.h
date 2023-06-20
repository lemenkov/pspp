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
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#ifndef __PSPPIRE_TEXT_FILE_H__
#define __PSPPIRE_TEXT_FILE_H__

#include "libpspp/str.h"

#include <glib-object.h>

G_BEGIN_DECLS



#define PSPPIRE_TYPE_TEXT_FILE               (psppire_text_file_get_type ())

#define PSPPIRE_TEXT_FILE(obj)        \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               PSPPIRE_TYPE_TEXT_FILE, PsppireTextFile))

#define PSPPIRE_TEXT_FILE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            PSPPIRE_TYPE_TEXT_FILE,                    \
                            PsppireTextFileClass))


#define PSPPIRE_IS_TEXT_FILE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_TEXT_FILE))

#define PSPPIRE_IS_TEXT_FILE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_TEXT_FILE))

#define PSPPIRE_TEXT_FILE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              PSPPIRE_TYPE_TEXT_FILE,                  \
                              PsppireTextFileClass))

enum { MAX_PREVIEW_LINES = 1000 }; /* Max number of lines to read. */

struct _PsppireTextFile
{
  GObject parent;

  gchar *file_name;
  gchar *encoding;

  gint maximum_lines;

  /* The first several lines of the file.   These copies which are UTF8 encoded,
     regardless of the file encoding.  */
  struct substring lines[MAX_PREVIEW_LINES];
  size_t n_lines;

  gulong total_lines; /* Number of lines in file. */
  gboolean total_is_exact;    /* Is total_lines exact (or an estimate)? */

  /*< private >*/
  gboolean dispose_has_run ;
  gint stamp;
};

struct _PsppireTextFileClass
{
  GObjectClass parent_class;
};


typedef struct _PsppireTextFile       PsppireTextFile;
typedef struct _PsppireTextFileClass  PsppireTextFileClass;

GType psppire_text_file_get_type (void) G_GNUC_CONST;
PsppireTextFile *psppire_text_file_new (const gchar *file_name, const gchar *encoding);

gboolean psppire_text_file_get_total_exact (PsppireTextFile *tf);
gulong psppire_text_file_get_n_lines (PsppireTextFile *tf);

G_END_DECLS

#endif /* __PSPPIRE_TEXT_FILE_H__ */
