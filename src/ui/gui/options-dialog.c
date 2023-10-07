/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2017, 2021 Free Software Foundation

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

#include "output/journal.h"
#include "ui/gui/helper.h"
#include "ui/gui/psppire-conf.h"
#include "ui/gui/builder-wrapper.h"
#include "ui/gui/psppire-data-store.h"
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

  GtkWidget *sort_names;
  GtkWidget *sort_labels;
  GtkWidget *sort_none;

  GtkWidget *maximize;
  GtkWidget *alert;
  GtkWidget *raise;

  GtkWidget *show_tips;

  GtkWidget *journal_disable;
  GtkWidget *journal_default;
  GtkWidget *journal_custom;
  GtkWidget *journal_custom_location;
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

GType
pspp_options_journal_location_get_type (void)
{
  static GType etype = 0;
  if (G_UNLIKELY(etype == 0)) {
    static const GEnumValue values[] =
      {
        { PSPP_OPTIONS_JOURNAL_LOCATION_DISABLED, "PSPP_OPTIONS_JOURNAL_LOCATION_DISABLED", "disabled" },
        { PSPP_OPTIONS_JOURNAL_LOCATION_DEFAULT, "PSPP_OPTIONS_JOURNAL_LOCATION_DEFAULT", "default" },
        { PSPP_OPTIONS_JOURNAL_LOCATION_CUSTOM, "PSPP_OPTIONS_JOURNAL_LOCATION_CUSTOM", "custom" },
        { 0, NULL, NULL }
      };
    etype = g_enum_register_static (g_intern_static_string ("PsppOptionsJournalLocation"), values);
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
  gboolean show_tips = true;

  fd.xml = builder_new ("options.ui");

  dialog = get_widget_assert (fd.xml, "options-dialog");

  fd.show_tips = get_widget_assert (fd.xml, "checkbutton-show-tips");

  fd.show_labels = get_widget_assert (fd.xml, "radiobutton-labels");
  fd.show_names  = get_widget_assert (fd.xml, "radiobutton-names");

  fd.sort_labels = get_widget_assert (fd.xml, "radiobutton-sort-by-label");
  fd.sort_names  = get_widget_assert (fd.xml, "radiobutton-sort-by-name");
  fd.sort_none   = get_widget_assert (fd.xml, "radiobutton-unsorted");

  fd.maximize = get_widget_assert (fd.xml, "checkbutton-maximize");
  fd.alert    = get_widget_assert (fd.xml, "checkbutton-alert");
  fd.raise    = get_widget_assert (fd.xml, "checkbutton-raise");

  fd.journal_disable = get_widget_assert (fd.xml, "journal-disable");
  fd.journal_default = get_widget_assert (fd.xml, "journal-default");
  fd.journal_custom = get_widget_assert (fd.xml, "journal-custom");
  fd.journal_custom_location = get_widget_assert (fd.xml, "journal-custom-location");

  GtkLabel *default_journal_location = GTK_LABEL (get_widget_assert (fd.xml, "default_journal_location"));
  gtk_label_set_text (default_journal_location, journal_get_default_file_name ());

  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (de));

  if (psppire_conf_get_boolean ("VariableLists", "display-labels", &disp_labels))
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.show_labels),
                                    disp_labels);

      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.show_names),
                                    !disp_labels);
    }

  if (psppire_conf_get_boolean ("startup", "show-user-tips", &show_tips))
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.show_tips),
                                    show_tips);
    }

  int location = -1;
  psppire_conf_get_enum ("Journal", "location",
                         PSPP_TYPE_OPTIONS_JOURNAL_LOCATION, &location);
  switch (location)
    {
    case PSPP_OPTIONS_JOURNAL_LOCATION_DISABLED:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.journal_disable), true);
      break;

    case PSPP_OPTIONS_JOURNAL_LOCATION_DEFAULT:
    default:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.journal_default), true);
      break;

    case PSPP_OPTIONS_JOURNAL_LOCATION_CUSTOM:
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.journal_custom), true);
      break;
    }

  char *custom_location;
  if (psppire_conf_get_string ("Journal", "custom-location", &custom_location))
    {
      gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (fd.journal_custom_location), custom_location);
      g_free (custom_location);
    }

  int what = -1;
  psppire_conf_get_enum ("VariableLists", "sort-order",
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
    if (psppire_conf_get_boolean ("OutputWindowAction", "maximize", &status))
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.maximize), status);
  }

  {
    gboolean status = true;
    psppire_conf_get_boolean ("OutputWindowAction", "alert", &status);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.alert), status);
  }

  {
    gboolean status;
    if (psppire_conf_get_boolean ("OutputWindowAction", "raise", &status))
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fd.raise), status);
  }

  const int result = psppire_dialog_run (PSPPIRE_DIALOG (dialog));

  if (result == GTK_RESPONSE_OK)
    {
      PsppOptionsVarOrder sort_order = -1;
      gboolean sl = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd.show_labels));

      psppire_conf_set_boolean ("VariableLists", "display-labels", sl);

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

      psppire_conf_set_enum ("VariableLists", "sort-order",
                             PSPP_TYPE_OPTIONS_VAR_ORDER,
                             sort_order);

      psppire_conf_set_boolean ("OutputWindowAction", "maximize",
                                gtk_toggle_button_get_active
                                (GTK_TOGGLE_BUTTON (fd.maximize)));

      psppire_conf_set_boolean ("OutputWindowAction", "raise",
                                gtk_toggle_button_get_active
                                (GTK_TOGGLE_BUTTON (fd.raise)));

      psppire_conf_set_boolean ("OutputWindowAction", "alert",
                                gtk_toggle_button_get_active
                                (GTK_TOGGLE_BUTTON (fd.alert)));

      psppire_conf_set_boolean ("startup", "show-user-tips",
                                gtk_toggle_button_get_active
                                (GTK_TOGGLE_BUTTON (fd.show_tips)));

      PsppOptionsJournalLocation journal_location;
      if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd.journal_disable)))
        journal_location = PSPP_OPTIONS_JOURNAL_LOCATION_DISABLED;
      else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (fd.journal_custom)))
        journal_location = PSPP_OPTIONS_JOURNAL_LOCATION_CUSTOM;
      else
        journal_location = PSPP_OPTIONS_JOURNAL_LOCATION_DEFAULT;
      psppire_conf_set_enum ("Journal", "location",
                             PSPP_TYPE_OPTIONS_JOURNAL_LOCATION,
                             journal_location);
      gchar *custom_location = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fd.journal_custom_location));
      if (custom_location)
        {
          psppire_conf_set_string ("Journal", "custom-location", custom_location);
          g_free (custom_location);
        }
      psppire_conf_save ();

      options_init ();
    }

  g_object_unref (fd.xml);
}

void
options_init (void)
{
  char *custom_location;
  if (!psppire_conf_get_string ("Journal", "custom-location",
                                &custom_location))
    custom_location = g_strdup (journal_get_default_file_name ());

  int location = -1;
  psppire_conf_get_enum ("Journal", "location",
                         PSPP_TYPE_OPTIONS_JOURNAL_LOCATION, &location);
  switch (location) {
  case PSPP_OPTIONS_JOURNAL_LOCATION_DISABLED:
    journal_disable ();
    break;

  case PSPP_OPTIONS_JOURNAL_LOCATION_DEFAULT:
  default:
    journal_set_file_name (journal_get_default_file_name ());
    journal_enable ();
    break;

  case PSPP_OPTIONS_JOURNAL_LOCATION_CUSTOM:
    journal_set_file_name (custom_location);
    journal_enable ();
    break;
  }
  g_free (custom_location);
}
