/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2020 Free Software Foundation, Inc.

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


#ifndef __PSPPIRE_VAR_INFO_H__
#define __PSPPIRE_VAR_INFO_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>


G_BEGIN_DECLS

#define PSPPIRE_VAR_INFO_TYPE            (psppire_var_info_get_type ())
#define PSPPIRE_VAR_INFO(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), PSPPIRE_VAR_INFO_TYPE, PsppireVarInfo))
#define PSPPIRE_VAR_INFO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), PSPPIRE_VAR_INFO_TYPE, PsppireVarInfoClass))
#define PSPPIRE_IS_VAR_INFO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PSPPIRE_VAR_INFO_TYPE))
#define PSPPIRE_IS_VAR_INFO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), PSPPIRE_VAR_INFO_TYPE))

enum
    {
     VAR_INFO_NAME,
     VAR_INFO_LABEL,
     VAR_INFO_POSITION,
     VAR_INFO_MEASUREMENT_LEVEL,
     VAR_INFO_ROLE,
     VAR_INFO_WIDTH,
     VAR_INFO_ALIGNMENT,
     VAR_INFO_PRINT_FORMAT,
     VAR_INFO_WRITE_FORMAT,
     VAR_INFO_MISSING_VALUES,
     VAR_INFO_VALUE_LABELS,
     n_VAR_INFO
    };

typedef struct _PsppireVarInfo       PsppireVarInfo;
typedef struct _PsppireVarInfoClass  PsppireVarInfoClass;

struct _PsppireVarInfo
{
  GtkGrid parent;

  gboolean dispose_has_run;

  GtkWidget *entry[n_VAR_INFO - 1];
  GtkWidget *combo;
};


struct _PsppireVarInfoClass
{
  GtkGridClass parent_class;
};



GType       psppire_var_info_get_type        (void);
GtkWidget*  psppire_var_info_new             (void);

G_END_DECLS

#endif /* __PSPPIRE_VAR_INFO_H__ */
