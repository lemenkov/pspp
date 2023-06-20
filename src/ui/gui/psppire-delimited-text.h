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

#ifndef __PSPPIRE_DELIMITED_TEXT_H__
#define __PSPPIRE_DELIMITED_TEXT_H__

#include "libpspp/str.h"
#include "libpspp/string-array.h"

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS



#define PSPPIRE_TYPE_DELIMITED_TEXT               (psppire_delimited_text_get_type ())

#define PSPPIRE_DELIMITED_TEXT(obj)        \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj),                                   \
                               PSPPIRE_TYPE_DELIMITED_TEXT, PsppireDelimitedText))

#define PSPPIRE_DELIMITED_TEXT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass),                                    \
                            PSPPIRE_TYPE_DELIMITED_TEXT,                    \
                            PsppireDelimitedTextClass))


#define PSPPIRE_IS_DELIMITED_TEXT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DELIMITED_TEXT))

#define PSPPIRE_IS_DELIMITED_TEXT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DELIMITED_TEXT))

#define PSPPIRE_DELIMITED_TEXT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj),                                    \
                              PSPPIRE_TYPE_DELIMITED_TEXT,                  \
                              PsppireDelimitedTextClass))

struct _PsppireDelimitedText
{
  GObject parent;

  GtkTreeModel *child;

  /* The first line of the file to be modelled */
  gint first_line;

  GSList *delimiters;
  gint max_fields;

  gunichar quote;

  /*< private >*/
  gboolean dispose_has_run ;
  gint stamp;

  /* caching */
  int cache_row;
  struct string_array cache;
  struct data_parser *parser;
};

struct _PsppireDelimitedTextClass
{
  GObjectClass parent_class;
};


typedef struct _PsppireDelimitedText       PsppireDelimitedText;
typedef struct _PsppireDelimitedTextClass  PsppireDelimitedTextClass;

GType psppire_delimited_text_get_type (void) G_GNUC_CONST;
PsppireDelimitedText *psppire_delimited_text_new (GtkTreeModel *);

const gchar *psppire_delimited_text_get_header_title (PsppireDelimitedText *file, gint column);


G_END_DECLS

#endif /* __PSPPIRE_DELIMITED_TEXT_H__ */
