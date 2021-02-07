/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2020, 2021 Free Software Foundation, Inc.

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
#include <gtk/gtk.h>

#include "psppire-var-info.h"
#include "data/value-labels.h"
#include "data/variable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

G_DEFINE_TYPE (PsppireVarInfo, psppire_var_info, GTK_TYPE_GRID);

static void
psppire_var_info_dispose (GObject *obj)
{
  PsppireVarInfo *var_info = PSPPIRE_VAR_INFO (obj);

  if (var_info->dispose_has_run)
    return;
  var_info->dispose_has_run = TRUE;

  G_OBJECT_CLASS (psppire_var_info_parent_class)->dispose (obj);
}

static const char *field_labels[n_VAR_INFO] =
  {
   N_("Name"),
   N_("Label"),
   N_("Position"),
   N_("Measurement Level"),
   N_("Role"),
   N_("Width"),
   N_("Alignment"),
   N_("Print Format"),
   N_("Write Format"),
   N_("Missing Values"),
   N_("Value Labels")
  };


void
psppire_var_info_init (PsppireVarInfo *var_info)
{
  g_object_set (var_info,
                "row-spacing", 3,
                "column-spacing", 3,
                "margin", 5,
                NULL);

  for (int r = 0; r < n_VAR_INFO; ++r)
    {
      GtkWidget *label = gtk_label_new (gettext (field_labels[r]));
      g_object_set (label,
                    "halign", GTK_ALIGN_START,
                    NULL);

      gtk_grid_attach (GTK_GRID (var_info), label, 0, r, 1, 1);
      gtk_widget_show (label);

      if (r >= n_VAR_INFO - 1)
        break;

      var_info->entry[r] = gtk_entry_new ();

      g_object_set (var_info->entry[r],
                    "visible", TRUE,
                    "double-buffered", FALSE,
                    "hexpand", TRUE,
                    "editable", FALSE,
                    NULL);

      gtk_grid_attach (GTK_GRID (var_info), var_info->entry[r], 1, r, 1, 1);
    }

  var_info->combo = gtk_combo_box_text_new_with_entry ();
  GtkWidget *entry = gtk_bin_get_child (GTK_BIN (var_info->combo));
  g_object_set (entry, "editable", FALSE, NULL);
  gtk_widget_show (var_info->combo);

  gtk_grid_attach (GTK_GRID (var_info), var_info->combo, 1, n_VAR_INFO - 1, 1, 1);
}

GtkWidget*
psppire_var_info_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_var_info_get_type (), NULL));
}


enum
  {
    PROP_0,
    PROP_VARIABLE,
  };

static void
__set_property (GObject      *object,
                guint         prop_id,
                const GValue *value,
                GParamSpec   *pspec)
{
  PsppireVarInfo *var_info = PSPPIRE_VAR_INFO (object);

  switch (prop_id)
    {
    case PROP_VARIABLE:
      {
        gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT (var_info->combo));
        gtk_combo_box_set_active  (GTK_COMBO_BOX (var_info->combo), 0);
        GtkWidget *entry = gtk_bin_get_child (GTK_BIN (var_info->combo));
        gtk_entry_set_text (GTK_ENTRY (entry), "");

        const struct variable *var = g_value_get_pointer (value);
        if (var == NULL)
          {
            for (int i = 0; i < n_VAR_INFO - 1; ++i)
              {
                gtk_entry_set_text (GTK_ENTRY (var_info->entry[i]), "");
              }
            gtk_combo_box_set_active  (GTK_COMBO_BOX (var_info->combo), -1);
            return;
          }

        char *str = NULL;
        gtk_entry_set_text (GTK_ENTRY (var_info->entry[VAR_INFO_NAME]),
                            var_get_name (var));

        str = g_strdup_printf ("%ld", var_get_dict_index (var));
        gtk_entry_set_text (GTK_ENTRY (var_info->entry[VAR_INFO_POSITION]),
                            str);
        g_free (str);

        const char *label = var_get_label (var);
        gtk_entry_set_text (GTK_ENTRY (var_info->entry[VAR_INFO_LABEL]),
                            label ? label : "");

        str = g_strdup_printf ("%d", var_get_width (var));
        gtk_entry_set_text (GTK_ENTRY (var_info->entry[VAR_INFO_WIDTH]),
                            str);
        g_free (str);

        gtk_entry_set_text (GTK_ENTRY (var_info->entry[VAR_INFO_MEASUREMENT_LEVEL]),
                            measure_to_string (var_get_measure (var)));

        gtk_entry_set_text (GTK_ENTRY (var_info->entry[VAR_INFO_ROLE]),
                            var_role_to_string (var_get_role (var)));

        gtk_entry_set_text (GTK_ENTRY (var_info->entry[VAR_INFO_ALIGNMENT]),
                            alignment_to_string (var_get_alignment (var)));

        const struct fmt_spec *pf = var_get_print_format (var);
        char xx[FMT_STRING_LEN_MAX + 1];
        gtk_entry_set_text (GTK_ENTRY (var_info->entry[VAR_INFO_PRINT_FORMAT]),
                            fmt_to_string (pf, xx));

        const struct fmt_spec *wf = var_get_write_format (var);
        gtk_entry_set_text (GTK_ENTRY (var_info->entry[VAR_INFO_WRITE_FORMAT]),
                            fmt_to_string (wf, xx));

        const struct missing_values *mv = var_get_missing_values (var);
        str = mv_to_string (mv, "UTF-8");
        gtk_entry_set_text (GTK_ENTRY (var_info->entry[VAR_INFO_MISSING_VALUES]),
                            str ? str : "");
        g_free (str);

        const struct val_labs *vls = var_get_value_labels (var);
        if (vls)
          {
            for (const struct val_lab *vl = val_labs_first (vls);
                 vl;
                 vl = val_labs_next (vls, vl))
              {
                struct string text;
                ds_init_empty (&text);

                const char *l = val_lab_get_label (vl);
                const union value *v = val_lab_get_value (vl);

                var_append_value_name__ (var, v, SETTINGS_VALUE_SHOW_VALUE, &text);
                ds_put_cstr (&text, ": ");
                ds_put_cstr (&text, l);

                gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (var_info->combo), ds_cstr (&text));
                gtk_combo_box_set_active  (GTK_COMBO_BOX (var_info->combo), 0);
              }
          }
      }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
__get_property (GObject      *object,
                guint         prop_id,
                GValue       *value,
                GParamSpec   *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
psppire_var_info_class_init (PsppireVarInfoClass *class)
{

  GObjectClass *gobject_class = G_OBJECT_CLASS (class);


  gobject_class->set_property = __set_property;
  gobject_class->get_property = __get_property;
  gobject_class->dispose = psppire_var_info_dispose;

  g_object_class_install_property (gobject_class, PROP_VARIABLE,
                                   g_param_spec_pointer
                                   ("variable",
                                    "Variable",
                                    "The variable whose parameters are to be displayed",
                                    G_PARAM_WRITABLE));
}
