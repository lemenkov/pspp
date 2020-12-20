/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2009, 2010, 2011, 2012, 2013  Free Software Foundation

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


/* This file is a rubbish bin where stuff gets put when it doesn't seem to
   belong anywhere else.
*/
#include <config.h>

#include	<glib-object.h>

#include <glib.h>
#include "helper.h"
#include <data/format.h>
#include <data/data-in.h>
#include <data/data-out.h>
#include <data/dictionary.h>
#include <data/casereader-provider.h>
#include <libpspp/message.h>
#include "psppire-syntax-window.h"
#include <gtk/gtk.h>
#include <libpspp/i18n.h>

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <data/settings.h>

#include "psppire-data-store.h"

#include "gl/configmake.h"
#include "xalloc.h"

#include <gettext.h>

/* Formats a value according to VAR's print format and strips white space
   appropriately for VAR's type.  That is, if VAR is numeric, strips leading
   white space (because numbers are right-justified within their fields), and
   if VAR is string, strips trailing white space (because spaces pad out string
   values on the right).

   Returns an allocated string.  The returned string must be freed when no
   longer required. */
gchar *
value_to_text (union value v, const struct variable *var)
{
  return value_to_text__ (v, var_get_print_format (var),
                          var_get_encoding (var));
}

/* Formats a value with format FORMAT and strips white space appropriately for
   FORMATs' type.  That is, if FORMAT is numeric, strips leading white space
   (because numbers are right-justified within their fields), and if FORMAT is
   string, strips trailing white space (because spaces pad out string values on
   the right).

   Returns an allocated string.  The returned string must be freed when no
   longer required. */
gchar *
value_to_text__ (union value v,
                 const struct fmt_spec *format, const char *encoding)
{
  gchar *s;

  s = data_out_stretchy (&v, encoding, format, NULL);
  if (fmt_is_numeric (format->type))
    g_strchug (s);
  else
    g_strchomp (s);

  return s;
}

/* Converts TEXT to a value.

   VAL will be initialised and filled by this function.
   It is the caller's responsibility to destroy VAL when no longer needed.
   VAR must be the variable with which VAL is associated.

   On success, VAL is returned, NULL otherwise.
*/
union value *
text_to_value (const gchar *text,
	       const struct variable *var,
	       union value *val)
{
  return text_to_value__ (text, var_get_print_format (var),
                          var_get_encoding (var), val);
}

/* Converts TEXT, which contains a value in the given FORMAT encoding in
   ENCODING, into a value.

   VAL will be initialised and filled by this function.
   It is the caller's responsibility to destroy VAL when no longer needed.

   On success, VAL is returned, NULL otherwise.
*/
union value *
text_to_value__ (const gchar *text,
                 const struct fmt_spec *format,
                 const gchar *encoding,
                 union value *val)
{
  int width = fmt_var_width (format);

  if (format->type != FMT_A)
    {
      if (! text) return NULL;

      {
	const gchar *s = text;
	while (*s)
	  {
	    if (!isspace (*s))
	      break;
	    s++;
	  }

	if (!*s) return NULL;
      }
    }

  value_init (val, width);
  char *err = data_in (ss_cstr (text), UTF8, format->type, val, width, encoding);

  if (err)
    {
      value_destroy (val, width);
      val = NULL;
      free (err);
    }

  return val;
}


#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* Create a deep copy of SRC */
GtkListStore *
clone_list_store (const GtkListStore *src)
{
  GtkTreeIter src_iter;
  gboolean ok;
  gint i;
  const gint n_cols =  gtk_tree_model_get_n_columns (GTK_TREE_MODEL (src));
  GType *types = g_malloc (sizeof (*types) *  n_cols);

  int row = 0;
  GtkListStore *dest;

  for (i = 0 ; i < n_cols; ++i)
    types[i] = gtk_tree_model_get_column_type (GTK_TREE_MODEL (src), i);

  dest = gtk_list_store_newv (n_cols, types);

  for (ok = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (src),
					   &src_iter);
       ok;
       ok = gtk_tree_model_iter_next (GTK_TREE_MODEL (src), &src_iter))
    {
      GtkTreeIter dest_iter;
      gtk_list_store_append  (dest, &dest_iter);

      for (i = 0 ; i < n_cols; ++i)
	{
	  GValue val = {0};

	  gtk_tree_model_get_value (GTK_TREE_MODEL (src), &src_iter, i, &val);
	  gtk_list_store_set_value (dest, &dest_iter, i, &val);

	  g_value_unset (&val);
	}
      row++;
    }

  g_free (types);

  return dest;
}




static gboolean
on_delete (GtkWindow *window, GdkEvent *e, GtkWindow **addr)
{
  *addr = NULL;

  return FALSE;
}

char *
paste_syntax_to_window (gchar *syntax)
{
  static GtkWidget *the_syntax_pasteboard = NULL;

  GtkTextBuffer *buffer = NULL;

  if (NULL == the_syntax_pasteboard)
    {
      the_syntax_pasteboard = psppire_syntax_window_new (NULL);
      g_signal_connect (the_syntax_pasteboard, "delete-event", G_CALLBACK (on_delete),
			&the_syntax_pasteboard);
    }

  buffer = GTK_TEXT_BUFFER (PSPPIRE_SYNTAX_WINDOW (the_syntax_pasteboard)->buffer);

  gtk_text_buffer_begin_user_action (buffer);
  gtk_text_buffer_insert_at_cursor (buffer, syntax, -1);
  gtk_text_buffer_insert_at_cursor (buffer, "\n", 1);
  gtk_text_buffer_end_user_action (buffer);

  gtk_widget_show (the_syntax_pasteboard);

  return syntax;
}


/* Return the width of an upper case M (in pixels) when rendered onto
   WIDGET with its current style.  */
gdouble
width_of_m (GtkWidget *widget)
{
  PangoContext *context = gtk_widget_create_pango_context (widget);
  PangoLayout *layout = pango_layout_new (context);
  PangoRectangle rect;

  pango_layout_set_text (layout, "M", 1);
  pango_layout_get_extents (layout, NULL, &rect);

  g_object_unref (G_OBJECT (layout));
  g_object_unref (G_OBJECT (context));

  return rect.width / (gdouble) PANGO_SCALE;
}

