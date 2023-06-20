/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010, 2011, 2012, 2015  Free Software Foundation

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

#ifndef __PSPPIRE_DIALOG_H__
#define __PSPPIRE_DIALOG_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include "psppire-window-base.h"


#define PSPPIRE_RESPONSE_PASTE 1
#define PSPPIRE_RESPONSE_GOTO 2
#define PSPPIRE_RESPONSE_CONTINUE 3

G_BEGIN_DECLS

#define PSPPIRE_TYPE_DIALOG            (psppire_dialog_get_type ())
#define PSPPIRE_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_TYPE_DIALOG, PsppireDialog))
#define PSPPIRE_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_TYPE_DIALOG, PsppireDialogClass))
#define PSPPIRE_IS_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG))
#define PSPPIRE_IS_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG))


typedef struct _PsppireDialog       PsppireDialog;
typedef struct _PsppireDialogClass  PsppireDialogClass;

typedef gboolean (*ContentsAreValid) (gpointer);

struct _PsppireDialog
{
  PsppireWindowBase window;

  /* Private */
  GMainLoop *loop;
  gint response;

  ContentsAreValid contents_are_valid;
  gpointer validity_data;
  ContentsAreValid contents_are_acceptable;
  gpointer acceptable_data;
  gboolean slidable;
  gchar *help_page;
};

struct _PsppireDialogClass
{
  PsppireWindowBaseClass parent_class;
};


GType          psppire_dialog_get_type        (void);
GtkWidget*     psppire_dialog_new             (void);
void           psppire_dialog_reload          (PsppireDialog *);
void           psppire_dialog_help            (PsppireDialog *);
void           psppire_dialog_close           (PsppireDialog *);
gint           psppire_dialog_run             (PsppireDialog *);
void           psppire_dialog_set_valid_predicate (PsppireDialog *,
                                                   ContentsAreValid,
                                                   gpointer);
void           psppire_dialog_set_accept_predicate (PsppireDialog *,
                                                    ContentsAreValid,
                                                    gpointer);
gboolean       psppire_dialog_is_acceptable (const PsppireDialog *);
void           psppire_dialog_notify_change (PsppireDialog *);

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_H__ */
