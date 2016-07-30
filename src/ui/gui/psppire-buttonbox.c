/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2007, 2010, 2011, 2012  Free Software Foundation

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

#include <glib.h>
#include <gtk/gtk.h>
#include "psppire-buttonbox.h"
#include "psppire-dialog.h"

#include "helper.h"

#include <gettext.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

GType psppire_button_flags_get_type (void);


static void psppire_button_box_class_init          (PsppireButtonboxClass *);
static void psppire_button_box_init                (PsppireButtonbox      *);


GType
psppire_buttonbox_get_type (void)
{
  static GType button_box_type = 0;

  if (!button_box_type)
    {
      static const GTypeInfo button_box_info =
      {
	sizeof (PsppireButtonboxClass),
	NULL, /* base_init */
        NULL, /* base_finalize */
	(GClassInitFunc) psppire_button_box_class_init,
        NULL, /* class_finalize */
	NULL, /* class_data */
        sizeof (PsppireButtonbox),
	0,
	(GInstanceInitFunc) psppire_button_box_init,
      };

      button_box_type = g_type_register_static (GTK_TYPE_BUTTON_BOX,
					    "PsppireButtonbox", &button_box_info, 0);
    }

  return button_box_type;
}

enum {
  PROP_BUTTONS = 1,
  PROP_DEFAULT = 2
};

static void
set_default (PsppireButtonbox *bb)
{
  int i;

  for (i = 0 ; i < n_PsppireButtonboxButtons ; ++i )
    if (bb->def == (1 << i))
      {
        gtk_widget_set_can_default (bb->button[i], TRUE);
        gtk_widget_grab_default (bb->button[i]);
      }
}

static void
psppire_buttonbox_set_property (GObject         *object,
                                guint            prop_id,
                                const GValue    *value,
                                GParamSpec      *pspec)
{
  gint i;
  guint flags;
  PsppireButtonbox *bb = PSPPIRE_BUTTONBOX (object);

  switch (prop_id)
    {
    case PROP_BUTTONS:
      flags = g_value_get_flags (value);
      for (i = 0 ; i < n_PsppireButtonboxButtons ; ++i )
        g_object_set (bb->button[i], "visible", 0x01 & (flags >> i)  , NULL);
      break;

    case PROP_DEFAULT:
      bb->def = g_value_get_flags (value);
      if (gtk_widget_get_realized (GTK_WIDGET (bb)))
        set_default (bb);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
psppire_buttonbox_get_property (GObject         *object,
                                guint            prop_id,
                                GValue          *value,
                                GParamSpec      *pspec)
{
  guint flags = 0;
  gint i;

  PsppireButtonbox *bb = PSPPIRE_BUTTONBOX (object);

  switch (prop_id)
    {
    case PROP_BUTTONS:
      for (i = 0 ; i < n_PsppireButtonboxButtons ; ++i )
        {
          gboolean visibility;
          g_object_get (bb->button[i], "visible", &visibility, NULL);

          if ( visibility )
            flags |= (0x01 << i);
        }

      g_value_set_flags (value, flags);
      break;

    case PROP_DEFAULT:
      g_value_set_flags (value, bb->def);

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


typedef enum
  {
    PSPPIRE_BUTTON_OK_MASK     = (1 << PSPPIRE_BUTTON_OK),
    PSPPIRE_BUTTON_GOTO_MASK   = (1 << PSPPIRE_BUTTON_GOTO),
    PSPPIRE_BUTTON_CONTINUE_MASK = (1 << PSPPIRE_BUTTON_CONTINUE),
    PSPPIRE_BUTTON_CANCEL_MASK = (1 << PSPPIRE_BUTTON_CANCEL),
    PSPPIRE_BUTTON_CLOSE_MASK  = (1 << PSPPIRE_BUTTON_CLOSE),
    PSPPIRE_BUTTON_HELP_MASK   = (1 << PSPPIRE_BUTTON_HELP),
    PSPPIRE_BUTTON_RESET_MASK  = (1 << PSPPIRE_BUTTON_RESET),
    PSPPIRE_BUTTON_PASTE_MASK  = (1 << PSPPIRE_BUTTON_PASTE)
  } PsppireButtonMask;

static GParamSpec *button_flags;
static GParamSpec *default_flags;

static void
psppire_button_box_class_init (PsppireButtonboxClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->set_property = psppire_buttonbox_set_property;
  object_class->get_property = psppire_buttonbox_get_property;

  button_flags =
    g_param_spec_flags ("buttons",
			"Buttons",
			"The mask that decides what buttons appear in the button box",
			PSPPIRE_TYPE_BUTTON_MASK,
			PSPPIRE_BUTTON_OK_MASK |
			PSPPIRE_BUTTON_CANCEL_MASK |
			PSPPIRE_BUTTON_RESET_MASK |
			PSPPIRE_BUTTON_HELP_MASK |
			PSPPIRE_BUTTON_PASTE_MASK,
			G_PARAM_READWRITE);
  g_object_class_install_property (object_class,
				   PROP_BUTTONS,
				   button_flags);

  default_flags =
    g_param_spec_flags ("default",
			"Default",
			"The mask that decides what what button grabs the default",
			PSPPIRE_TYPE_BUTTON_MASK,
			0,
			G_PARAM_READWRITE);
  g_object_class_install_property (object_class,
				   PROP_DEFAULT,
				   default_flags);
}

static void
close_and_respond (GtkWidget *w, gint response)
{
  PsppireDialog *dialog;

  GtkWidget *toplevel = gtk_widget_get_toplevel (w);

  /* If we're not in a psppire dialog (for example when in glade)
     then do nothing */
  if ( ! PSPPIRE_IS_DIALOG (toplevel))
    return;

  dialog = PSPPIRE_DIALOG (toplevel);

  dialog->response = response;

  psppire_dialog_close (dialog);
}

static gboolean
is_acceptable (GtkWidget *w)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (w);

  return (PSPPIRE_IS_DIALOG (toplevel)
          && psppire_dialog_is_acceptable (PSPPIRE_DIALOG (toplevel)));
}

static void
close_dialog (GtkWidget *w, gpointer data)
{
  close_and_respond (w, GTK_RESPONSE_CLOSE);
}

static void
continue_button_clicked (GtkWidget *w, gpointer data)
{
  if (is_acceptable (w))
    close_and_respond (w, PSPPIRE_RESPONSE_CONTINUE);
}


static void
ok_button_clicked (GtkWidget *w, gpointer data)
{
  if (is_acceptable (w))
    close_and_respond (w, GTK_RESPONSE_OK);
}


static void
paste_button_clicked (GtkWidget *w, gpointer data)
{
  if (is_acceptable (w))
    close_and_respond (w, PSPPIRE_RESPONSE_PASTE);
}

static void
goto_button_clicked (GtkWidget *w, gpointer data)
{
  if (is_acceptable (w))
    close_and_respond (w, PSPPIRE_RESPONSE_GOTO);
}


static void
refresh_clicked (GtkWidget *w, gpointer data)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (w);
  PsppireDialog *dialog;

  if ( ! PSPPIRE_IS_DIALOG (toplevel))
    return;

  dialog = PSPPIRE_DIALOG (toplevel);

  psppire_dialog_reload (dialog);
}

static void
help_clicked (GtkWidget *w, gpointer data)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (w);
  PsppireDialog *dialog;

  if ( ! PSPPIRE_IS_DIALOG (toplevel))
    return;

  dialog = PSPPIRE_DIALOG (toplevel);

  psppire_dialog_help (dialog);
}

static void
on_validity_change (GtkWidget *toplevel, gboolean valid, gpointer data)
{
  PsppireButtonbox *bb = data;

  /* Set the sensitivity of all the 'executive order' buttons */
  gtk_widget_set_sensitive (GTK_WIDGET (bb->button[PSPPIRE_BUTTON_OK]), valid);
  gtk_widget_set_sensitive (GTK_WIDGET (bb->button[PSPPIRE_BUTTON_PASTE]), valid);
  gtk_widget_set_sensitive (GTK_WIDGET (bb->button[PSPPIRE_BUTTON_GOTO]), valid);
  gtk_widget_set_sensitive (GTK_WIDGET (bb->button[PSPPIRE_BUTTON_CONTINUE]), valid);
}

static void
on_realize (GtkWidget *buttonbox, gpointer data)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (buttonbox);

  if ( PSPPIRE_IS_DIALOG (toplevel))
    {
      g_signal_connect (toplevel, "validity-changed",
			G_CALLBACK (on_validity_change), buttonbox);
    }
  set_default (PSPPIRE_BUTTONBOX (buttonbox));
}

static void
psppire_button_box_init (PsppireButtonbox *bb)
{
  bb->def = PSPPIRE_BUTTON_CONTINUE;

  bb->button[PSPPIRE_BUTTON_OK] = gtk_button_new_with_label (_("OK"));
  psppire_box_pack_start_defaults (GTK_BOX (bb), bb->button[PSPPIRE_BUTTON_OK]);
  g_signal_connect (bb->button[PSPPIRE_BUTTON_OK], "clicked",
		    G_CALLBACK (ok_button_clicked), NULL);
  g_object_set (bb->button[PSPPIRE_BUTTON_OK], "no-show-all", TRUE, NULL);


  bb->button[PSPPIRE_BUTTON_GOTO] =
    gtk_button_new_with_label (_("Go To"));
  psppire_box_pack_start_defaults (GTK_BOX (bb), bb->button[PSPPIRE_BUTTON_GOTO]);
  g_signal_connect (bb->button[PSPPIRE_BUTTON_GOTO], "clicked",
		    G_CALLBACK (goto_button_clicked), NULL);
  g_object_set (bb->button[PSPPIRE_BUTTON_GOTO], "no-show-all", TRUE, NULL);


  bb->button[PSPPIRE_BUTTON_CONTINUE] =
    gtk_button_new_with_mnemonic (_("Continue"));

  psppire_box_pack_start_defaults (GTK_BOX (bb),
			       bb->button[PSPPIRE_BUTTON_CONTINUE]);
  g_signal_connect (bb->button[PSPPIRE_BUTTON_CONTINUE], "clicked",
		    G_CALLBACK (continue_button_clicked), NULL);

  g_object_set (bb->button[PSPPIRE_BUTTON_CONTINUE],
		"no-show-all", TRUE, NULL);



  bb->button[PSPPIRE_BUTTON_PASTE] = gtk_button_new_with_label (_("Paste"));
  g_signal_connect (bb->button[PSPPIRE_BUTTON_PASTE], "clicked",
		    G_CALLBACK (paste_button_clicked), NULL);
  psppire_box_pack_start_defaults (GTK_BOX (bb), bb->button[PSPPIRE_BUTTON_PASTE]);
  g_object_set (bb->button[PSPPIRE_BUTTON_PASTE], "no-show-all", TRUE, NULL);

  bb->button[PSPPIRE_BUTTON_CANCEL] = gtk_button_new_with_label (_("Cancel"));
  g_signal_connect (bb->button[PSPPIRE_BUTTON_CANCEL], "clicked",
		    G_CALLBACK (close_dialog), NULL);
  psppire_box_pack_start_defaults (GTK_BOX (bb), bb->button[PSPPIRE_BUTTON_CANCEL]);
  g_object_set (bb->button[PSPPIRE_BUTTON_CANCEL], "no-show-all", TRUE, NULL);

  bb->button[PSPPIRE_BUTTON_CLOSE] = gtk_button_new_with_label (_("Close"));
  g_signal_connect (bb->button[PSPPIRE_BUTTON_CLOSE], "clicked",
		    G_CALLBACK (close_dialog), NULL);
  psppire_box_pack_start_defaults (GTK_BOX (bb), bb->button[PSPPIRE_BUTTON_CLOSE]);
  g_object_set (bb->button[PSPPIRE_BUTTON_CLOSE], "no-show-all", TRUE, NULL);


  bb->button[PSPPIRE_BUTTON_RESET] = gtk_button_new_with_label (_("Reset"));
  g_signal_connect (bb->button[PSPPIRE_BUTTON_RESET], "clicked",
		    G_CALLBACK (refresh_clicked), NULL);
  psppire_box_pack_start_defaults (GTK_BOX (bb), bb->button[PSPPIRE_BUTTON_RESET]);
  g_object_set (bb->button[PSPPIRE_BUTTON_RESET], "no-show-all", TRUE, NULL);


  bb->button[PSPPIRE_BUTTON_HELP] = gtk_button_new_with_label (_("Help"));
  g_signal_connect (bb->button[PSPPIRE_BUTTON_HELP], "clicked",
		    G_CALLBACK (help_clicked), NULL);
  psppire_box_pack_start_defaults (GTK_BOX (bb), bb->button[PSPPIRE_BUTTON_HELP]);
  g_object_set (bb->button[PSPPIRE_BUTTON_HELP], "no-show-all", TRUE, NULL);


  /* Set the default visibilities */
  {
    GValue value = { 0 };
    guint flags;
    gint i;
    g_value_init (&value, button_flags->value_type);
    g_param_value_set_default(button_flags, &value);


    flags = g_value_get_flags (&value);

    for (i = 0 ; i < n_PsppireButtonboxButtons ; ++i )
      g_object_set (bb->button[i], "visible", 0x01 & (flags >> i)  , NULL);

    g_value_unset (&value);
  }


  g_signal_connect (bb, "realize", G_CALLBACK (on_realize), NULL);
}

GType
psppire_button_flags_get_type (void)
{
  static GType ftype = 0;
  if (ftype == 0)
    {
      static const GFlagsValue values[] =
	{
	  { PSPPIRE_BUTTON_OK_MASK,      "PSPPIRE_BUTTON_OK_MASK",       "Accept dialog and run it" },
	  { PSPPIRE_BUTTON_GOTO_MASK,    "PSPPIRE_BUTTON_GOTO_MASK",     "Goto case/variable" },
	  { PSPPIRE_BUTTON_CONTINUE_MASK,"PSPPIRE_BUTTON_CONTINUE_MASK", "Accept and close the subdialog" },
	  { PSPPIRE_BUTTON_CANCEL_MASK,  "PSPPIRE_BUTTON_CANCEL_MASK",   "Close dialog and discard settings" },
	  { PSPPIRE_BUTTON_CLOSE_MASK,   "PSPPIRE_BUTTON_CLOSE_MASK",    "Close dialog" },
	  { PSPPIRE_BUTTON_HELP_MASK,    "PSPPIRE_BUTTON_HELP_MASK",     "Invoke context sensitive help" },
	  { PSPPIRE_BUTTON_RESET_MASK,   "PSPPIRE_BUTTON_RESET_MASK",    "Restore dialog to its default settings" },
	  { PSPPIRE_BUTTON_PASTE_MASK,   "PSPPIRE_BUTTON_PASTE_MASK",    "Accept dialog and paste syntax" },
	  { 0, NULL, NULL }
	};
      
      ftype = g_flags_register_static
	(g_intern_static_string ("PsppireButtonFlags"), values);

    }
  return ftype;
}

