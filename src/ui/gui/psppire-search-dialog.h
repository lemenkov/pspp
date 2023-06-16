/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2023  Free Software Foundation

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

#ifndef __PSPPIRE_SEARCH_DIALOG_H__
#define __PSPPIRE_SEARCH_DIALOG_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include "psppire-dialog.h"


G_BEGIN_DECLS

#define PSPPIRE_TYPE_SEARCH_DIALOG            (psppire_search_dialog_get_type ())
#define PSPPIRE_SEARCH_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_TYPE_SEARCH_DIALOG, PsppireSearchDialog))
#define PSPPIRE_SEARCH_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_TYPE_SEARCH_DIALOG, PsppireSearchDialogClass))
#define PSPPIRE_IS_SEARCH_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_SEARCH_DIALOG))
#define PSPPIRE_IS_SEARCH_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_SEARCH_DIALOG))


typedef struct _PsppireSearchDialog       PsppireSearchDialog;
typedef struct _PsppireSearchDialogClass  PsppireSearchDialogClass;

struct _PsppireSearchDialog
{
  PsppireDialog parent;

  GtkWidget *entry;
  GtkWidget *bbo;
  GtkWidget *ignore_case;
  GtkWidget *wrap;
  GtkWidget *whole;
  GtkWidget *forward;
  GtkWidget *backward;
};

struct _PsppireSearchDialogClass
{
  PsppireDialogClass parent_class;
};

GType          psppire_search_dialog_get_type        (void);
GtkWidget*     psppire_search_dialog_new             (void);

G_END_DECLS

#endif /* __PSPPIRE_SEARCH_DIALOG_H__ */
