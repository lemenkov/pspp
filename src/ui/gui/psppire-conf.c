/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009, 2010  Free Software Foundation

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

/*
   This module provides an interface for simple user preference config
   parameters.
*/

#include <config.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include <glib.h>

#include "psppire-conf.h"

G_DEFINE_TYPE (PsppireConf, psppire_conf, G_TYPE_OBJECT)

static void psppire_conf_finalize        (GObject   *object);
static void psppire_conf_dispose        (GObject   *object);

static GObjectClass *parent_class = NULL;

static PsppireConf *psppire_conf_get (void);

void
psppire_conf_save (void)
{
  PsppireConf *conf = psppire_conf_get ();
  if (!conf->dirty)
    return;
  conf->dirty = FALSE;

  gsize length = 0;

  gchar *new_contents = g_key_file_to_data  (conf->keyfile, &length, NULL);

  GError *err = NULL;
  if (g_strcmp0 (new_contents, conf->contents)
      && ! g_file_set_contents (conf->filename, new_contents, length, &err))
    {
      g_warning ("Cannot open %s for writing: %s", conf->filename, err->message);
      g_error_free (err);
    }

  g_free (conf->contents);
  conf->contents = new_contents;
}

static void
conf_dirty (PsppireConf *conf)
{
  conf->dirty = TRUE;
}

static void
psppire_conf_dispose  (GObject *object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
psppire_conf_finalize (GObject *object)
{
  PsppireConf *conf = PSPPIRE_CONF (object);
  g_key_file_free (conf->keyfile);
  g_free (conf->filename);
}


static PsppireConf *the_instance = NULL;

static GObject*
psppire_conf_construct   (GType                  type,
                                     guint                  n_construct_params,
                                     GObjectConstructParam *construct_params)
{
  GObject *object;

  if (!the_instance)
    {
      object = G_OBJECT_CLASS (parent_class)->constructor (type,
                                                           n_construct_params,
                                                           construct_params);
      the_instance = PSPPIRE_CONF (object);
    }
  else
    object = g_object_ref (G_OBJECT (the_instance));

  return object;
}

static void
psppire_conf_class_init (PsppireConfClass *class)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent (class);
  object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_conf_finalize;
  object_class->dispose = psppire_conf_dispose;
  object_class->constructor = psppire_conf_construct;
}


static void
psppire_conf_init (PsppireConf *conf)
{
  const gchar *dirname;
  struct stat s;

  /* Get the name of the directory for user configuration files, then, if it
     doesn't already exist, create it, since we might be the first program
     to want to put files there. */
  dirname = g_get_user_config_dir ();
  if (stat (dirname, &s) == -1 && errno == ENOENT)
    mkdir (dirname, 0700);

  conf->filename = g_strdup_printf ("%s/%s", dirname, "psppirerc");

  conf->keyfile = g_key_file_new ();
  g_key_file_load_from_file (conf->keyfile,
                             conf->filename,
                             G_KEY_FILE_KEEP_COMMENTS,
                             NULL);

  conf->dirty = FALSE;
}

/* Gets the singleton PsppireConf object.  The caller should not unref the
   object.  The caller should call psppire_conf_save() if it makes changes. */
static PsppireConf *
psppire_conf_get (void)
{
  return (the_instance
          ? the_instance
          : g_object_new (psppire_conf_get_type (), NULL));
}

gboolean
psppire_conf_get_int (const gchar *base, const gchar *name, gint *value)
{
  PsppireConf *conf = psppire_conf_get ();

  gboolean ok;
  GError *err = NULL;
  *value = g_key_file_get_integer (conf->keyfile,
                                   base,
                                   name, &err);

  ok = (err == NULL);
  if (err != NULL)
    g_error_free (err);

  return ok;
}

gboolean
psppire_conf_get_boolean (const gchar *base, const gchar *name, gboolean *value)
{
  PsppireConf *conf = psppire_conf_get ();

  gboolean ok;
  gboolean b;
  GError *err = NULL;
  b = g_key_file_get_boolean (conf->keyfile,
                              base,
                              name, &err);

  ok = (err == NULL);
  if (err != NULL)
    g_error_free (err);

  if (ok)
    *value = b;

  return ok;
}



gboolean
psppire_conf_get_string (const gchar *base, const gchar *name, gchar **value)
{
  PsppireConf *conf = psppire_conf_get ();
  gboolean ok;
  gchar *b;
  GError *err = NULL;
  b = g_key_file_get_string (conf->keyfile,
                             base,
                             name, &err);

  ok = (err == NULL);
  if (err != NULL)
    g_error_free (err);

  if (ok)
    *value = b;

  return ok;
}




gboolean
psppire_conf_get_variant (const gchar *base, const gchar *name, GVariant **v)
{
  PsppireConf *conf = psppire_conf_get ();
  gboolean ok;
  gchar *b;
  GError *err = NULL;
  b = g_key_file_get_string (conf->keyfile,
                             base,
                             name, &err);

  ok = (err == NULL);
  if (err != NULL)
    g_error_free (err);

  if (ok)
    {
      *v = g_variant_parse (NULL, b, NULL, NULL, NULL);
      g_free (b);
    }

  return ok;
}

gboolean
psppire_conf_get_enum (const gchar *base, const gchar *name, GType t, int *v)
{
  PsppireConf *conf = psppire_conf_get ();
  gboolean ok;
  gchar *b;
  GError *err = NULL;
  b = g_key_file_get_string (conf->keyfile,
                             base,
                             name, &err);

  ok = (err == NULL);
  if (err != NULL)
    g_error_free (err);

  if (ok)
    {
      GEnumClass *ec = g_type_class_ref (t);
      GEnumValue *ev = g_enum_get_value_by_nick (ec, b);
      *v = ev->value;
      g_type_class_unref (ec);
      g_free (b);
    }

  return ok;
}

void
psppire_conf_set_int (const gchar *base, const gchar *name,
                      gint value)
{
  PsppireConf *conf = psppire_conf_get ();
  g_key_file_set_integer (conf->keyfile, base, name, value);
  conf_dirty (conf);
}

void
psppire_conf_set_boolean (const gchar *base, const gchar *name,
                          gboolean value)
{
  PsppireConf *conf = psppire_conf_get ();
  g_key_file_set_boolean (conf->keyfile, base, name, value);
  conf_dirty (conf);
}


void
psppire_conf_set_string (const gchar *base, const gchar *name,
                         const gchar *value)
{
  PsppireConf *conf = psppire_conf_get ();
  g_key_file_set_string (conf->keyfile, base, name, value);
  conf_dirty (conf);
}

void
psppire_conf_set_variant (const gchar *base, const gchar *name, GVariant *value)
{
  PsppireConf *conf = psppire_conf_get ();
  gchar *v = g_variant_print (value, FALSE);
  g_key_file_set_string (conf->keyfile, base, name, v);
  conf_dirty (conf);
  g_free (v);
}

void
psppire_conf_set_enum (const gchar *base, const gchar *name,
                       GType enum_type,
                       int value)
{
  PsppireConf *conf = psppire_conf_get ();
  GEnumClass *ec = g_type_class_ref (enum_type);
  GEnumValue *ev = g_enum_get_value (ec, value);

  g_key_file_set_string (conf->keyfile, base, name,
                         ev->value_nick);

  g_type_class_unref (ec);

  conf_dirty (conf);
}



/*
  A convenience function to get the geometry of a
  window from from a saved config
*/
void
psppire_conf_get_window_geometry (const gchar *base, GtkWindow *window)
{
  gint height, width;
  gint x, y;
  gboolean maximize;

  if (psppire_conf_get_int (base, "height", &height)
      &&
      psppire_conf_get_int (base, "width", &width))
    {
      gtk_window_set_default_size (window, width, height);
    }

  if (psppire_conf_get_int (base, "x", &x)
       &&
       psppire_conf_get_int (base, "y", &y))
    {
      gtk_window_move (window, x, y);
    }

  if (psppire_conf_get_boolean (base, "maximize", &maximize))
    {
      if (maximize)
        gtk_window_maximize (window);
      else
        gtk_window_unmaximize (window);
    }
}


/*
   A convenience function to save the window geometry.
   This should typically be called from a window's
   "configure-event" and "window-state-event" signal handlers
 */
void
psppire_conf_set_window_geometry (const gchar *base, GtkWindow *gtk_window)
{
  gboolean maximized;
  GdkWindow *w;

  w = gtk_widget_get_window (GTK_WIDGET (gtk_window));
  if (w == NULL)
    return;

  maximized = (gdk_window_get_state (w) & GDK_WINDOW_STATE_MAXIMIZED) != 0;
  psppire_conf_set_boolean (base, "maximize", maximized);

  if (!maximized)
    {
      gint x, y;

      gint width = gdk_window_get_width (w);
      gint height= gdk_window_get_height (w);

      gdk_window_get_position (w, &x, &y);

      psppire_conf_set_int (base, "height", height);
      psppire_conf_set_int (base, "width", width);
      psppire_conf_set_int (base, "x", x);
      psppire_conf_set_int (base, "y", y);
    }
}
