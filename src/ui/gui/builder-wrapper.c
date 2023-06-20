/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2009, 2010, 2011, 2012  Free Software Foundation

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


#include <config.h>

#include "builder-wrapper.h"

#include "libpspp/str.h"


static GtkBuilder *
builder_new_real (const gchar *name)
{
  GtkBuilder *builder = gtk_builder_new ();

  GError *err = NULL;
  if (! gtk_builder_add_from_file (builder, name,  &err))
    {
      g_critical ("Couldn\'t open user interface  file %s: %s", name, err->message);
      g_clear_error (&err);
    }

  return builder;
}

GtkBuilder *
builder_new (const gchar *name)
{
  char *full_name = relocate_format ("%s/%s", PKGDATADIR, name);
  GtkBuilder *builder = builder_new_real (full_name);
  free (full_name);

  return builder;
}

GObject *
get_object_assert (GtkBuilder *builder, const gchar *name, GType type)
{
  GObject *o = NULL;
  g_assert (name);

  o = gtk_builder_get_object (builder, name);

  if (!o)
    g_critical ("Object `%s' could not be found\n", name);
  else if (! g_type_is_a (G_OBJECT_TYPE (o), type))
   {
     g_critical ("Object `%s' was expected to have type %s, but in fact has type %s",
        name, g_type_name (type), G_OBJECT_TYPE_NAME (o));
   }

  return o;
}


GtkWidget *
get_widget_assert (GtkBuilder *builder, const gchar *name)
{
  GtkWidget *w = GTK_WIDGET (get_object_assert (builder, name, GTK_TYPE_WIDGET));

  g_object_set (w, "name", name, NULL);

  return w;
}
