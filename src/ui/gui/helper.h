/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2004, 2009, 2010, 2011, 2012  Free Software Foundation

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


#ifndef __MISC_H__
#define __MISC_H__


#include <data/format.h>
#include <data/value.h>

#include <gtk/gtk.h>

#include "psppire-dict.h"

gchar *paste_syntax_to_window (gchar *syntax);

struct fmt_spec;

/* Returns a new GParamSpec for a string.  An attempt to store the empty string
   in the parameter will be silently translated into storing a null pointer. */
static inline GParamSpec *
null_if_empty_param (const gchar *name, const gchar *nick,
                     const gchar *blurb, const gchar *default_value,
                     GParamFlags flags)
{
  GParamSpec *param;

  param = g_param_spec_string (name, nick, blurb, default_value, flags);
  ((GParamSpecString *) param)->null_fold_if_empty = TRUE;
  return param;
}

gchar * value_to_text (union value v, const struct variable *);
gchar * value_to_text__ (union value v, const struct fmt_spec *,
                         const char *encoding);


union value *text_to_value (const gchar *text, const struct variable *,
                            union value *);
union value *text_to_value__ (const gchar *text, const struct fmt_spec *,
                              const gchar *encoding, union value *);

/* Create a deep copy of SRC */
GtkListStore * clone_list_store (const GtkListStore *src);

/* gtk_box_pack_start_defaults is deprecated.
   Therefore we roll our own until a better solution is found */
static inline void
psppire_box_pack_start_defaults (GtkBox *box, GtkWidget *widget)
{
  gtk_box_pack_start (box, widget, TRUE, TRUE, 0);
}

/* Starting with gcc8 the warning Wcast-function-type will
   trigger if no intermediate (void (*)(void)) cast is done
   for a function cast to GFunc when the number of parameters
   is not 2. The reason is that the compiler behaviour in this
   situation is undefined according to C standard although many
   implementations rely on this. */
#define GFUNC_COMPAT_CAST(x) ((GFunc) (void (*)(void)) (x))


/* Return the width of an upper case M (in pixels) when rendered onto
   WIDGET with its current style.  */
gdouble width_of_m (GtkWidget *widget);

#endif

