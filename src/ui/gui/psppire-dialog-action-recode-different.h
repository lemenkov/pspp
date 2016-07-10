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

#include "psppire-dialog-action-recode.h"

#ifndef __PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT_H__
#define __PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT_H__

G_BEGIN_DECLS


#define PSPPIRE_TYPE_DIALOG_ACTION_RECODE_DIFFERENT (psppire_dialog_action_recode_different_get_type ())

#define PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT(obj)	\
                     (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
						  PSPPIRE_TYPE_DIALOG_ACTION_RECODE_DIFFERENT, PsppireDialogActionRecodeDifferent))

#define PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_CAST ((klass), \
				 PSPPIRE_TYPE_DIALOG_ACTION_RECODE_DIFFERENT, \
                                 PsppireDialogActionRecodeDifferentClass))


#define PSPPIRE_IS_DIALOG_ACTION_RECODE_DIFFERENT(obj) \
	             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_TYPE_DIALOG_ACTION_RECODE_DIFFERENT))

#define PSPPIRE_IS_DIALOG_ACTION_RECODE_DIFFERENT_CLASS(klass) \
                     (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_TYPE_DIALOG_ACTION_RECODE_DIFFERENT))


#define PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), \
				   PSPPIRE_TYPE_DIALOG_ACTION_RECODE_DIFFERENT, \
				   PsppireDialogActionRecodeDifferentClass))

typedef struct _PsppireDialogActionRecodeDifferent       PsppireDialogActionRecodeDifferent;
typedef struct _PsppireDialogActionRecodeDifferentClass  PsppireDialogActionRecodeDifferentClass;


struct _PsppireDialogActionRecodeDifferent
{
  PsppireDialogActionRecode parent;

  /* A hash table of struct nlp's indexed by variable */
  GHashTable *varmap;
};


struct _PsppireDialogActionRecodeDifferentClass
{
  PsppireDialogActionRecodeClass parent_class;
};


GType psppire_dialog_action_recode_different_get_type (void) ;

G_END_DECLS

#endif /* __PSPPIRE_DIALOG_ACTION_RECODE_DIFFERENT_H__ */
