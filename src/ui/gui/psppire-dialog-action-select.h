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

#ifndef __PSPPIRE_DIALOG_ACTION_SELECT_H__
#define __PSPPIRE_DIALOG_ACTION_SELECT_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_SELECT (psppire_dialog_action_select_get_type ())

#define PSPPIRE_DIALOG_ACTION_SELECT(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_SELECT, PsppireDialogActionSelect))

#define PSPPIRE_DIALOG_ACTION_SELECT_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_SELECT, \
                                 PsppireDialogActionSelectClass))


#define PSPPIRE_IS_DIALOG_ACTION_SELECT(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_SELECT))

#define PSPPIRE_IS_DIALOG_ACTION_SELECT_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_SELECT))


#define PSPPIRE_DIALOG_ACTION_SELECT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_SELECT, \
				   PsppireDialogActionSelectClass))

typedef struct _PsppireDialogActionSelect       PsppireDialogActionSelect;
typedef struct _PsppireDialogActionSelectClass  PsppireDialogActionSelectClass;


struct _PsppireDialogActionSelect
{
  PsppireDialogAction parent;

  /*< private >*/
  GtkWidget *spinbutton ;
  GtkWidget *spin_sample_size ;
  GtkWidget *spin_sample_limit ;

  GtkWidget *hbox1;
  GtkWidget *hbox2;

  GtkWidget *rsample_dialog;
  GtkWidget *percent        ; 
  GtkWidget *sample_n_cases ; 
  GtkWidget *table          ;
  GtkWidget *l0 ;
  GtkWidget *l1 ;
  GtkWidget *radiobutton_range ;
  GtkWidget *first ;
  GtkWidget *last ;
  GtkWidget *radiobutton_sample;
  GtkWidget *radiobutton_all;
  GtkWidget *entry;
  GtkWidget *radiobutton_filter_variable;
  GtkWidget *radiobutton_delete;
  GtkWidget *radiobutton_filter;
  GtkWidget *range_subdialog;

};


struct _PsppireDialogActionSelectClass
{
  PsppireDialogActionClass parent_class;
};


GType psppire_dialog_action_select_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_SELECT_H__ */
