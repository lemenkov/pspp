/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2017 Free Software Foundation

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

#include <config.h>

#include "options-dialog.h"

#include "ui/gui/helper.h"
#include "ui/gui/psppire-conf.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/psppire-data-window.h"
#include "ui/gui/psppire-dialog.h"

#include <gtk/gtk.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid


struct options_dialog
{
  GtkBuilder *xml;
  GtkWidget *show_labels;
  GtkWidget *show_names;
  PsppireConf *conf;

  GtkWidget *sort_names;
  GtkWidget *sort_labels;
  GtkWidget *sort_none;

  GtkWidget *maximize;
  GtkWidget *alert;
  GtkWidget *raise;
};

GType
pspp_options_var_order_get_type (void)
{
  static GType etype = 0;
  if (G_UNLIKELY(etype == 0)) {
    static const GEnumValue values[] =
      {
	{ PSPP_OPTIONS_VAR_ORDER_UNSORTED, "PSPP_OPTIONS_VAR_ORDER_UNSORTED", "unsorted" },
	{ PSPP_OPTIONS_VAR_ORDER_NAME,     "PSPP_OPTIONS_VAR_ORDER_NAME",     "name" },
	{ PSPP_OPTIONS_VAR_ORDER_LABEL,    "PSPP_OPTIONS_VAR_ORDER_LABEL",    "label" },
	{ 0, NULL, NULL }
      };
    etype = g_enum_register_static (g_intern_static_string ("PsppOptionsVarOrder"), values);
  }
  return etype;
}

/*
   Pops up the Options dialog box
 */
void
options_dialog (PsppireDataWindow *de)
{
  struct options_dialog fd;

  GtkWidget *dialog ;

  gboolean disp_labels = true;

  fd.xml = builder_new ("options.ui");

  dialog = get_widget_assert (fd.xml, "options-dialog");

  fd.show_labels = get_widget_assert (fd.xml, "radiobutton-labels");
  fd.show_names  = get_widget_assert (fd.xml, "radiobutton-names");

  fd.sort_labels = get_widget_assert (fd.xml, "radiobutton-sort-by-label");
  fd.sort_names  = get_widget_assert (fd.xml, "radiobutton-sort-by-name");
  fd.sort_none   = get_widget_assert (fd.xml, "radiobutton-unsorted");

  fd.maximize = get_widget_assert (fd.xml, "checkbutton-maximize");
  fd.alert    = get_widget_assert (fd.xml, "checkbutton-alert");
  fd.raise    = get_widget_assert (fd.xml, "checkbutton-raise");
  
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  fd.conf = psppire_conf_new ();

  if (psppire_conf_get_boolean (fd.conf,
				"VariableLists", "display-labels", &disp_labels))
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.show_labels),
				    disp_labels);

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.show_names),
				    !disp_labels);
    }


  int what = -1;
  psppire_conf_get_enum (fd.conf, "VariableLists", "sort-order",
			 PSPP_TYPE_OPTIONS_VAR_ORDER, &what);

  switch (what)
    {
    default:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.sort_none), true);
      break;
    case PSPP_OPTIONS_VAR_ORDER_NAME:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.sort_names), true);
      break;
    case PSPP_OPTIONS_VAR_ORDER_LABEL:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.sort_labels), true);
      break;
    }

  {
    gboolean status;
    if (psppire_conf_get_boolean (fd.conf, "OutputWindowAction", "maximize",
				  &status))
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.maximize), status);
  }
  
  {
    gboolean status = true;
    psppire_conf_get_boolean (fd.conf, "OutputWindowAction", "alert", &status);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.alert), status);
  }
  
  {
    gboolean status;
    if (psppire_conf_get_boolean (fd.conf, "OutputWindowAction", "raise",
				  &status))
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.raise), status);
  }
  
  const int result = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  if (result == GTK_RESPONSE_OK)
    {
      PsppOptionsVarOrder sort_order = -1;
      gboolean sl = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd.show_labels));

      psppire_conf_set_boolean (fd.conf,
				"VariableLists", "display-labels", sl);

      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd.sort_labels)))
	{
	  sort_order = PSPP_OPTIONS_VAR_ORDER_LABEL;
	}
      else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd.sort_names)))
	{
	  sort_order = PSPP_OPTIONS_VAR_ORDER_NAME;
	}
      else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd.sort_none)))
	{
	  sort_order = PSPP_OPTIONS_VAR_ORDER_UNSORTED;
	}
	
      psppire_conf_set_enum (fd.conf,
			     "VariableLists", "sort-order",
			     PSPP_TYPE_OPTIONS_VAR_ORDER,
			     sort_order);

      psppire_conf_set_boolean (fd.conf, "OutputWindowAction", "maximize",
				gtk_toggle_button_get_active
				(GTK_TOGGLE_BUTTON (fd.maximize)));
      
      psppire_conf_set_boolean (fd.conf, "OutputWindowAction", "raise",
				gtk_toggle_button_get_active
				(GTK_TOGGLE_BUTTON (fd.raise)));

      psppire_conf_set_boolean (fd.conf, "OutputWindowAction", "alert",
				gtk_toggle_button_get_active
				(GTK_TOGGLE_BUTTON (fd.alert)));
    }

  g_object_unref (fd.xml);
}
