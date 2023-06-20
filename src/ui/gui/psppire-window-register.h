/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008  Free Software Foundation

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


#include <glib-object.h>
#include <glib.h>

#include "psppire-window.h"

#ifndef __PSPPIRE_WINDOW_REGISTER_H__
#define __PSPPIRE_WINDOW_REGISTER_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_WINDOW_REGISTER (psppire_window_register_get_type ())

#define PSPPIRE_WINDOW_REGISTER(obj)        \
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                    PSPPIRE_TYPE_WINDOW_REGISTER, PsppireWindowRegister))

#define PSPPIRE_WINDOW_REGISTER_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
                                 PSPPIRE_TYPE_WINDOW_REGISTER, \
                                 PsppireWindowRegisterClass))


#define PSPPIRE_IS_WINDOW_REGISTER(obj) \
                     (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_WINDOW_REGISTER))

#define PSPPIRE_IS_WINDOW_REGISTER_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_WINDOW_REGISTER))


#define PSPPIRE_WINDOW_REGISTER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                                   PSPPIRE_TYPE_WINDOW_REGISTER, \
                                   PsppireWindowRegisterClass))

typedef struct _PsppireWindowRegister       PsppireWindowRegister;
typedef struct _PsppireWindowRegisterClass  PsppireWindowRegisterClass;


struct _PsppireWindowRegister
{
  GObject parent;

  /*< private >*/
  GHashTable *name_table;
};


struct _PsppireWindowRegisterClass
{
  GObjectClass parent_class;
};


GType psppire_window_register_get_type (void) G_GNUC_CONST;

PsppireWindowRegister * psppire_window_register_new (void);

void psppire_window_register_insert (PsppireWindowRegister *wr, PsppireWindow *window,
                                     const gchar *name);

void psppire_window_register_remove (PsppireWindowRegister *wr, const gchar *name);


PsppireWindow *psppire_window_register_lookup (PsppireWindowRegister *wr, const gchar *name);


void psppire_window_register_foreach (PsppireWindowRegister *wr, GHFunc func,
                                      gpointer);

gint psppire_window_register_n_items (PsppireWindowRegister *wr);

GtkWidget *create_windows_menu (GtkWindow *toplevel);


G_END_DECLS

#endif /* __PSPPIRE_WINDOW_REGISTER_H__ */
