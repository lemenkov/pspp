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

#ifndef __PSPPIRE_DIALOG_ACTION_RECODE_H__
#define __PSPPIRE_DIALOG_ACTION_RECODE_H__

G_BEGIN_DECLS

enum
  {
    BUTTON_NEW_VALUE,
    BUTTON_NEW_COPY,
    BUTTON_NEW_SYSMIS,
    n_BUTTONS
  };

#define PSPPIRE_TYPE_DIALOG_ACTION_RECODE (psppire_dialog_action_recode_get_type ())

#define PSPPIRE_DIALOG_ACTION_RECODE(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_RECODE, PsppireDialogActionRecode))

#define PSPPIRE_DIALOG_ACTION_RECODE_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_RECODE, \
                                 PsppireDialogActionRecodeClass))


#define PSPPIRE_IS_DIALOG_ACTION_RECODE(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_RECODE))

#define PSPPIRE_IS_DIALOG_ACTION_RECODE_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_RECODE))


#define PSPPIRE_DIALOG_ACTION_RECODE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_RECODE, \
				   PsppireDialogActionRecodeClass))

typedef struct _PsppireDialogActionRecode       PsppireDialogActionRecode;
typedef struct _PsppireDialogActionRecodeClass  PsppireDialogActionRecodeClass;


struct _PsppireDialogActionRecode
{
  PsppireDialogAction parent;

  PsppireDialog *old_and_new_dialog;

  GtkWidget *dict_treeview;
  GtkWidget *variable_treeview;
  GtkWidget *toggle[n_BUTTONS];

  GtkWidget *strings_box;
  GtkWidget *convert_button;
  GtkWidget *new_copy_label;


  GtkWidget *new_value_entry;

  GtkWidget *old_value_chooser;

  GtkListStore *value_map;

  GtkWidget *acr;

  gboolean input_var_is_string;

  GtkWidget *new_name_entry;
  GtkWidget *new_label_entry;
  GtkWidget *change_button;

  GtkWidget *output_variable_box;
  
  GtkWidget *string_button;
  GtkWidget *width_entry;
};


struct _PsppireDialogActionRecodeClass
{
  PsppireDialogActionClass parent_class;

  gboolean (*target_is_string) (const PsppireDialogActionRecode *);
};

GType psppire_dialog_action_recode_get_type (void) ;

void psppire_dialog_action_recode_refresh (PsppireDialogAction *);

void psppire_dialog_action_recode_pre_activate (PsppireDialogActionRecode *act, void (*populate_treeview) (PsppireDialogActionRecode *) );


GType new_value_get_type (void);


char *psppire_dialog_action_recode_generate_syntax (const PsppireDialogAction *act,
						    void (*add_string_decls) (const PsppireDialogActionRecode *, struct string *),
						    void (*add_into_clause) (const PsppireDialogActionRecode *, struct string *),
						    void (*add_new_value_labels) (const PsppireDialogActionRecode *, struct string *));

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_RECODE_H__ */
