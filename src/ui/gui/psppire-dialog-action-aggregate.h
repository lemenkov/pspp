/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2015  Free Software Foundation

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

#ifndef __PSPPIRE_DIALOG_ACTION_AGGREGATE_H__
#define __PSPPIRE_DIALOG_ACTION_AGGREGATE_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_AGGREGATE (psppire_dialog_action_aggregate_get_type ())

#define PSPPIRE_DIALOG_ACTION_AGGREGATE(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_AGGREGATE, PsppireDialogActionAggregate))

#define PSPPIRE_DIALOG_ACTION_AGGREGATE_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_AGGREGATE, \
                                 PsppireDialogActionAggregateClass))


#define PSPPIRE_IS_DIALOG_ACTION_AGGREGATE(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_AGGREGATE))

#define PSPPIRE_IS_DIALOG_ACTION_AGGREGATE_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_AGGREGATE))


#define PSPPIRE_DIALOG_ACTION_AGGREGATE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_AGGREGATE, \
				   PsppireDialogActionAggregateClass))

typedef struct _PsppireDialogActionAggregate       PsppireDialogActionAggregate;
typedef struct _PsppireDialogActionAggregateClass  PsppireDialogActionAggregateClass;


struct _PsppireDialogActionAggregate
{
  PsppireDialogAction parent;

  /*< private >*/
  gboolean dispose_has_run ;

  GtkWidget *break_variables;

  GtkWidget *replace_radiobutton;
  GtkWidget *add_radiobutton;
  GtkWidget *filename_radiobutton;
  GtkWidget *filename_button;
  GtkWidget *filename_box;
  GtkWidget *filename_label;

  GtkWidget *function_combo;

  GtkWidget *summary_acr;
  GtkWidget *summary_var_name_entry;
  GtkWidget *summary_var_label_entry;

  GtkWidget *summary_sv;
  GtkWidget *summary_sv_entry;

  GtkWidget *summary_arg1;
  GtkWidget *summary_arg2;

  GtkWidget *summary_arg1_entry;
  GtkWidget *summary_arg2_entry;

  GtkWidget *sorted_button;
  GtkWidget *needs_sort_button;

  GtkWidget *pane;
};


struct _PsppireDialogActionAggregateClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_aggregate_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_AGGREGATE_H__ */
