/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2012  Free Software Foundation

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

#include "psppire-dialog-action.h"

/*
   This module provides a subclass of GtkTreeView, designed for dialogs
   which need lists of annotated checkbox items.
   The object contains the necessary model and renderers, which means that
   the user does not have to create these herself.
 */


#ifndef __PSPPIRE_DIALOG_ACTION_CROSSTABS_H__
#define __PSPPIRE_DIALOG_ACTION_CROSSTABS_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_CROSSTABS (psppire_dialog_action_crosstabs_get_type ())

#define PSPPIRE_DIALOG_ACTION_CROSSTABS(obj)        \
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                                                  PSPPIRE_TYPE_DIALOG_ACTION_CROSSTABS, PsppireDialogActionCrosstabs))

#define PSPPIRE_DIALOG_ACTION_CROSSTABS_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
                                 PSPPIRE_TYPE_DIALOG_ACTION_CROSSTABS, \
                                 PsppireDialogActionCrosstabsClass))


#define PSPPIRE_IS_DIALOG_ACTION_CROSSTABS(obj) \
                     (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_CROSSTABS))

#define PSPPIRE_IS_DIALOG_ACTION_CROSSTABS_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_CROSSTABS))


#define PSPPIRE_DIALOG_ACTION_CROSSTABS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                                   PSPPIRE_TYPE_DIALOG_ACTION_CROSSTABS, \
                                   PsppireDialogActionCrosstabsClass))

typedef struct _PsppireDialogActionCrosstabs       PsppireDialogActionCrosstabs;
typedef struct _PsppireDialogActionCrosstabsClass  PsppireDialogActionCrosstabsClass;


struct _PsppireDialogActionCrosstabs
{
  PsppireDialogAction parent;

  /*< private >*/
  gboolean dispose_has_run ;

  GtkWidget *col_vars;
  GtkWidget *row_vars;

  GtkWidget *dest_rows;
  GtkWidget *dest_cols ;
  GtkWidget *format_button ;
  GtkWidget *stat_button ;
  GtkWidget *cell_button ;

  GtkWidget *stat_view ;

  GtkWidget *cell_view ;
  GtkTreeModel *cell ;
  GtkWidget *cell_dialog ;
  GtkTreeModel *stat;
  GtkWidget *stat_dialog ;

  gboolean format_options_avalue;
  gboolean format_options_pivot;
  gboolean format_options_table;

  GtkWidget *table_button;
  GtkWidget *pivot_button;

  GtkWidget *format_dialog;
  GtkWidget *avalue_button;
};


struct _PsppireDialogActionCrosstabsClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_crosstabs_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_CROSSTABS_H__ */
