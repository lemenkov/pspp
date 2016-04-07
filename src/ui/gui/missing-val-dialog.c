/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2005, 2006, 2009, 2011, 2012, 2015, 2016  Free Software Foundation

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

/*  This module describes the behaviour of the Missing Values dialog box,
    used for input of the missing values in the variable sheet */

#include <config.h>

#include "ui/gui/missing-val-dialog.h"

#include "builder-wrapper.h"
#include "helper.h"
#include <data/format.h>
#include "missing-val-dialog.h"
#include <data/missing-values.h>
#include <data/variable.h>
#include <data/data-in.h>

#include <gtk/gtk.h>

#include <string.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static GObject *psppire_missing_val_dialog_constructor (
  GType type, guint, GObjectConstructParam *);
static void psppire_missing_val_dialog_finalize (GObject *);

G_DEFINE_TYPE (PsppireMissingValDialog,
               psppire_missing_val_dialog,
               PSPPIRE_TYPE_DIALOG);
enum
  {
    PROP_0,
    PROP_VARIABLE,
    PROP_MISSING_VALUES
  };

static void
psppire_missing_val_dialog_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  PsppireMissingValDialog *obj = PSPPIRE_MISSING_VAL_DIALOG (object);

  switch (prop_id)
    {
    case PROP_VARIABLE:
      psppire_missing_val_dialog_set_variable (obj,
                                               g_value_get_pointer (value));
      break;
    case PROP_MISSING_VALUES:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_missing_val_dialog_get_property (GObject      *object,
                                         guint         prop_id,
                                         GValue       *value,
                                         GParamSpec   *pspec)
{
  PsppireMissingValDialog *obj = PSPPIRE_MISSING_VAL_DIALOG (object);

  switch (prop_id)
    {
    case PROP_MISSING_VALUES:
      g_value_set_pointer (value, &obj->mvl);
      break;
    case PROP_VARIABLE:
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
psppire_missing_val_dialog_class_init (PsppireMissingValDialogClass *class)
{
  GObjectClass *gobject_class;
  gobject_class = G_OBJECT_CLASS (class);

  gobject_class->constructor = psppire_missing_val_dialog_constructor;
  gobject_class->finalize = psppire_missing_val_dialog_finalize;
  gobject_class->set_property = psppire_missing_val_dialog_set_property;
  gobject_class->get_property = psppire_missing_val_dialog_get_property;

  g_object_class_install_property (
    gobject_class, PROP_VARIABLE,
    g_param_spec_pointer ("variable",
                          "Variable",
                          "Variable whose missing values are to be edited.  "
                          "The variable's print format and encoding are also "
                          "used for editing.",
                          G_PARAM_WRITABLE));

  g_object_class_install_property (
    gobject_class, PROP_MISSING_VALUES,
    g_param_spec_pointer ("missing-values",
                          "Missing Values",
                          "Edited missing values.",
                          G_PARAM_READABLE));
}

static void
psppire_missing_val_dialog_init (PsppireMissingValDialog *dialog)
{
  /* We do all of our work on widgets in the constructor function, because that
     runs after the construction properties have been set.  Otherwise
     PsppireDialog's "orientation" property hasn't been set and therefore we
     have no box to populate. */
  mv_init (&dialog->mvl, 0);
  dialog->encoding = NULL;
}

static void
psppire_missing_val_dialog_finalize (GObject *obj)
{
  PsppireMissingValDialog *dialog = PSPPIRE_MISSING_VAL_DIALOG (obj);

  mv_destroy (&dialog->mvl);
  g_free (dialog->encoding);

  G_OBJECT_CLASS (psppire_missing_val_dialog_parent_class)->finalize (obj);
}

PsppireMissingValDialog *
psppire_missing_val_dialog_new (const struct variable *var)
{
  return PSPPIRE_MISSING_VAL_DIALOG (
    g_object_new (PSPPIRE_TYPE_MISSING_VAL_DIALOG,
                  "variable", var,
                  NULL));
}

void
psppire_missing_val_dialog_run (GtkWindow *parent_window,
                                const struct variable *var,
                                struct missing_values *mv)
{
  PsppireMissingValDialog *dialog;

  dialog = psppire_missing_val_dialog_new (var);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), parent_window);
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
  gtk_widget_show (GTK_WIDGET (dialog));

  if (psppire_dialog_run (PSPPIRE_DIALOG (dialog)) == GTK_RESPONSE_OK)
    mv_copy (mv, psppire_missing_val_dialog_get_missing_values (dialog));
  else
    mv_copy (mv, var_get_missing_values (var));

  gtk_widget_destroy (GTK_WIDGET (dialog));
}


/* A simple (sub) dialog box for displaying user input errors */
static void
err_dialog (const gchar *msg, GtkWindow *window)
{
  GtkWidget *dialog =
    gtk_message_dialog_new (window,
			    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			    GTK_MESSAGE_ERROR,
			    GTK_BUTTONS_CLOSE,
			    "%s",msg);

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

/* Interpret text, display error dialog
   If parsing is o.k., the value is initialized and it is the responsibility of
   the caller to destroy the variable. */
static gboolean
try_missing_value(const PsppireMissingValDialog *dialog, const gchar *text, union value *vp)
{
  const int var_width = fmt_var_width (&dialog->format);
  char *error_txt = NULL;

  value_init(vp, var_width);
  error_txt = data_in (ss_cstr(text), "UTF-8", dialog->format.type,
		       vp, var_width, dialog->encoding);
  if (error_txt)
    {
      err_dialog (error_txt, GTK_WINDOW (dialog));
      free (error_txt);
      goto error;
    }
  else
    {
      if (mv_is_acceptable (vp, var_width))
	return TRUE;
      else
	{
	  err_dialog (_("The maximum length of a missing value"
			" for a string variable is 8 in UTF-8."),
		      GTK_WINDOW (dialog));
	  goto error;
	}
    }
 error:
  value_destroy (vp, var_width);
  return FALSE;
}

/* Acceptability predicate for PsppireMissingValDialog.

   This function is also the only place that dialog->mvl gets updated. */
static gboolean
missing_val_dialog_acceptable (gpointer data)
{
  PsppireMissingValDialog *dialog = data;
  int var_width = fmt_var_width (&dialog->format);

  if ( gtk_toggle_button_get_active (dialog->button_discrete))
    {
      gint nvals = 0;
      gint i;

      mv_clear(&dialog->mvl);
      for(i = 0 ; i < 3 ; ++i )
	{
	  gchar *text =
	    g_strdup (gtk_entry_get_text (GTK_ENTRY (dialog->mv[i])));

	  union value v;
	  if ( !text || strlen (g_strstrip (text)) == 0 )
	    {
	      g_free (text);
	      continue;
	    }

	  if (!try_missing_value (dialog, text, &v))
	    {
	      g_free (text);
	      gtk_widget_grab_focus (dialog->mv[i]);
	      return FALSE;
	    }
	  mv_add_value (&dialog->mvl, &v);
	  nvals++;
	  g_free (text);
	  value_destroy (&v, var_width);
	}
      if ( nvals == 0 )
	{
	  err_dialog (_("At least one value must be specified"),
		      GTK_WINDOW (dialog));
	  gtk_widget_grab_focus (dialog->mv[0]);
	  return FALSE;
	}
    }

  if (gtk_toggle_button_get_active (dialog->button_range))
    {
      gchar *discrete_text;
      union value low_val ;
      union value high_val;
      const gchar *low_text = gtk_entry_get_text (GTK_ENTRY (dialog->low));
      const gchar *high_text = gtk_entry_get_text (GTK_ENTRY (dialog->high));

      assert (var_width == 0); /* Ranges are only for numeric variables */

      if (!try_missing_value(dialog, low_text, &low_val))
	{
	  gtk_widget_grab_focus (dialog->low);
	  return FALSE;
	}
      if (!try_missing_value (dialog, high_text, &high_val))
	{
	  gtk_widget_grab_focus (dialog->high);
	  value_destroy (&low_val, var_width);
	  return FALSE;
	}
      if (low_val.f > high_val.f)
	{
	  err_dialog (_("Incorrect range specification"),
		      GTK_WINDOW (dialog));
	  value_destroy (&low_val, var_width);
	  value_destroy (&high_val, var_width);
	  gtk_widget_grab_focus (dialog->low);
	  return FALSE;
	}
      mv_clear (&dialog->mvl);
      mv_add_range (&dialog->mvl, low_val.f, high_val.f);
      value_destroy (&low_val, var_width);
      value_destroy (&high_val, var_width);

      discrete_text = g_strdup (gtk_entry_get_text (GTK_ENTRY (dialog->discrete)));

      if ( discrete_text && strlen (g_strstrip (discrete_text)) > 0 )
	{
	  union value discrete_val;
	  if (!try_missing_value (dialog, discrete_text, &discrete_val))
	    {
	      g_free (discrete_text);
	      gtk_widget_grab_focus (dialog->discrete);
	      return FALSE;
	    }
	  mv_add_value (&dialog->mvl, &discrete_val);
	  value_destroy (&discrete_val, var_width);
	}
      g_free (discrete_text);
    }

  if (gtk_toggle_button_get_active (dialog->button_none))
    mv_clear (&dialog->mvl);

  return TRUE;
}


/* Callback which occurs when the 'discrete' radiobutton is toggled */
static void
discrete (GtkToggleButton *button, gpointer data)
{
  gint i;
  PsppireMissingValDialog *dialog = data;

  for (i = 0 ; i < 3 ; ++i )
    {
      gtk_widget_set_sensitive (dialog->mv[i],
			       gtk_toggle_button_get_active (button));
    }
}

/* Callback which occurs when the 'range' radiobutton is toggled */
static void
range (GtkToggleButton *button, gpointer data)
{
  PsppireMissingValDialog *dialog = data;

  const gboolean active = gtk_toggle_button_get_active (button);

  gtk_widget_set_sensitive (dialog->low, active);
  gtk_widget_set_sensitive (dialog->high, active);
  gtk_widget_set_sensitive (dialog->discrete, active);
}



/* Shows the dialog box and sets default values */
static GObject *
psppire_missing_val_dialog_constructor (GType                  type,
                                        guint                  n_properties,
                                        GObjectConstructParam *properties)
{
  PsppireMissingValDialog *dialog;
  GtkContainer *content_area;
  GtkBuilder *xml;
  GObject *obj;

  obj = G_OBJECT_CLASS (psppire_missing_val_dialog_parent_class)->constructor (
    type, n_properties, properties);
  dialog = PSPPIRE_MISSING_VAL_DIALOG (obj);

  content_area = GTK_CONTAINER (PSPPIRE_DIALOG (dialog));
  xml = builder_new ("missing-val-dialog.ui");
  gtk_container_add (GTK_CONTAINER (content_area),
                     get_widget_assert (xml, "missing-values-dialog"));

  dialog->mv[0] = get_widget_assert (xml, "mv0");
  dialog->mv[1] = get_widget_assert (xml, "mv1");
  dialog->mv[2] = get_widget_assert (xml, "mv2");

  dialog->low = get_widget_assert (xml, "mv-low");
  dialog->high = get_widget_assert (xml, "mv-high");
  dialog->discrete = get_widget_assert (xml, "mv-discrete");


  dialog->button_none     =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "no_missing"));

  dialog->button_discrete =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "discrete_missing"));

  dialog->button_range    =
    GTK_TOGGLE_BUTTON (get_widget_assert (xml, "range_missing"));

  psppire_dialog_set_accept_predicate (PSPPIRE_DIALOG (dialog),
                                       missing_val_dialog_acceptable,
                                       dialog);

  g_signal_connect (dialog->button_discrete, "toggled",
		   G_CALLBACK (discrete), dialog);

  g_signal_connect (dialog->button_range, "toggled",
		   G_CALLBACK (range), dialog);

  g_object_unref (xml);

  return obj;
}

void
psppire_missing_val_dialog_set_variable (PsppireMissingValDialog *dialog,
                                         const struct variable *var)
{
  enum val_type var_type;
  gint i;

  mv_destroy (&dialog->mvl);
  g_free (dialog->encoding);

  if (var != NULL)
    {
      const struct missing_values *vmv = var_get_missing_values (var);
      if (mv_is_empty(vmv))
	mv_init (&dialog->mvl, var_get_width(var));
      else
	mv_copy (&dialog->mvl, vmv);
      dialog->encoding = g_strdup (var_get_encoding (var));
      dialog->format = *var_get_print_format (var);
    }
  else
    {
      mv_init (&dialog->mvl, 0);
      dialog->encoding = NULL;
      dialog->format = F_8_0;
    }

  /* Blank all entry boxes and make them insensitive */
  gtk_entry_set_text (GTK_ENTRY (dialog->low), "");
  gtk_entry_set_text (GTK_ENTRY (dialog->high), "");
  gtk_entry_set_text (GTK_ENTRY (dialog->discrete), "");
  gtk_widget_set_sensitive (dialog->low, FALSE);
  gtk_widget_set_sensitive (dialog->high, FALSE);
  gtk_widget_set_sensitive (dialog->discrete, FALSE);

  var_type = val_type_from_width (fmt_var_width (&dialog->format));
  gtk_widget_set_sensitive (GTK_WIDGET (dialog->button_range),
			    var_type == VAL_NUMERIC);

  if (var == NULL)
    return;

  for (i = 0 ; i < 3 ; ++i )
    {
      gtk_entry_set_text (GTK_ENTRY (dialog->mv[i]), "");
      gtk_widget_set_sensitive (dialog->mv[i], FALSE);
    }

  if ( mv_has_range (&dialog->mvl))
    {
      union value low, high;
      gchar *low_text;
      gchar *high_text;
      mv_get_range (&dialog->mvl, &low.f, &high.f);


      low_text = value_to_text__ (low, &dialog->format, dialog->encoding);
      high_text = value_to_text__ (high, &dialog->format, dialog->encoding);

      gtk_entry_set_text (GTK_ENTRY (dialog->low), low_text);
      gtk_entry_set_text (GTK_ENTRY (dialog->high), high_text);
      g_free (low_text);
      g_free (high_text);

      if ( mv_has_value (&dialog->mvl))
	{
	  gchar *text;
	  text = value_to_text__ (*mv_get_value (&dialog->mvl, 0),
                                  &dialog->format, dialog->encoding);
	  gtk_entry_set_text (GTK_ENTRY (dialog->discrete), text);
	  g_free (text);
	}

      gtk_toggle_button_set_active (dialog->button_range, TRUE);
      gtk_widget_set_sensitive (dialog->low, TRUE);
      gtk_widget_set_sensitive (dialog->high, TRUE);
      gtk_widget_set_sensitive (dialog->discrete, TRUE);

    }
  else if ( mv_has_value (&dialog->mvl))
    {
      const int n = mv_n_values (&dialog->mvl);

      for (i = 0 ; i < 3 ; ++i )
	{
	  if ( i < n)
	    {
	      gchar *text ;

	      text = value_to_text__ (*mv_get_value (&dialog->mvl, i),
                                      &dialog->format, dialog->encoding);
	      gtk_entry_set_text (GTK_ENTRY (dialog->mv[i]), text);
	      g_free (text);
	    }
	  gtk_widget_set_sensitive (dialog->mv[i], TRUE);
	}
      gtk_toggle_button_set_active (dialog->button_discrete, TRUE);
    }
  else if ( mv_is_empty (&dialog->mvl))
    {
      gtk_toggle_button_set_active (dialog->button_none, TRUE);
    }
}

const struct missing_values *
psppire_missing_val_dialog_get_missing_values (
  const PsppireMissingValDialog *dialog)
{
  return &dialog->mvl;
}
