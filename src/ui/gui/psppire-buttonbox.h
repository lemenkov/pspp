/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010, 2011, 2012  Free Software Foundation

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


#ifndef __PSPPIRE_BUTTON_BOX_H__
#define __PSPPIRE_BUTTON_BOX_H__


#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PSPPIRE_BUTTON_BOX_TYPE            (psppire_button_box_get_type ())
#define PSPPIRE_BUTTON_BOX(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_BUTTON_BOX_TYPE, PsppireButtonBox))
#define PSPPIRE_BUTTON_BOX_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_BUTTON_BOX_TYPE, PsppireButtonBoxClass))
#define PSPPIRE_IS_BUTTON_BOX(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_BUTTON_BOX_TYPE))
#define PSPPIRE_IS_BUTTON_BOX_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_BUTTON_BOX_TYPE))


typedef struct _PsppireButtonBox       PsppireButtonBox;
typedef struct _PsppireButtonBoxClass  PsppireButtonBoxClass;


enum
  {
    PSPPIRE_BUTTON_OK = 0,
    PSPPIRE_BUTTON_GOTO,
    PSPPIRE_BUTTON_CONTINUE,
    PSPPIRE_BUTTON_CANCEL,
    PSPPIRE_BUTTON_CLOSE,
    PSPPIRE_BUTTON_HELP,
    PSPPIRE_BUTTON_RESET,
    PSPPIRE_BUTTON_PASTE,
    n_PsppireButtonBoxButtons
  };

typedef enum
  {
    PSPPIRE_BUTTON_OK_MASK     = (1 << PSPPIRE_BUTTON_OK),
    PSPPIRE_BUTTON_GOTO_MASK   = (1 << PSPPIRE_BUTTON_GOTO),
    PSPPIRE_BUTTON_CONTINUE_MASK = (1 << PSPPIRE_BUTTON_CONTINUE),
    PSPPIRE_BUTTON_CANCEL_MASK = (1 << PSPPIRE_BUTTON_CANCEL),
    PSPPIRE_BUTTON_CLOSE_MASK  = (1 << PSPPIRE_BUTTON_CLOSE),
    PSPPIRE_BUTTON_HELP_MASK   = (1 << PSPPIRE_BUTTON_HELP),
    PSPPIRE_BUTTON_RESET_MASK  = (1 << PSPPIRE_BUTTON_RESET),
    PSPPIRE_BUTTON_PASTE_MASK  = (1 << PSPPIRE_BUTTON_PASTE)
  } PsppireButtonMask;

struct _PsppireButtonBox
{
  GtkButtonBox parent;

  /* <private> */
  GtkWidget *button[n_PsppireButtonBoxButtons];
  guint def;
};

struct _PsppireButtonBoxClass
{
  GtkButtonBoxClass parent_class;
};

GType          psppire_button_box_get_type        (void);
GtkWidget*     psppire_button_box_new (void);


#define PSPPIRE_TYPE_BUTTON_MASK psppire_button_flags_get_type()

G_END_DECLS

#endif /* __PSPPIRE_BUTTON_BOX_H__ */

