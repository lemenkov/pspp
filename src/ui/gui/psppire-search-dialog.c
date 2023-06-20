/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2023  Free Software Foundation

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
#include "psppire-search-dialog.h"
#include "psppire-buttonbox.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_search_dialog_class_init    (PsppireSearchDialogClass *);
static void psppire_search_dialog_init          (PsppireSearchDialog      *);

enum  {FIND,
  n_SIGNALS};

static guint signals [n_SIGNALS];


static GObjectClass     *parent_class = NULL;

static void
psppire_search_dialog_finalize (GObject *object)
{
  //  PsppireSearchDialog *dialog = PSPPIRE_SEARCH_DIALOG (object);

  if (G_OBJECT_CLASS (parent_class)->finalize)
    G_OBJECT_CLASS (parent_class)->finalize (object);
}


G_DEFINE_TYPE (PsppireSearchDialog, psppire_search_dialog, PSPPIRE_TYPE_DIALOG);


static void
psppire_search_dialog_get_property (GObject         *object,
                             guint            prop_id,
                             GValue          *value,
                             GParamSpec      *pspec)
{
  //  PsppireSearchDialog *dialog = PSPPIRE_SEARCH_DIALOG (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
psppire_search_dialog_set_property (GObject         *object,
                             guint            prop_id,
                             const GValue    *value,
                             GParamSpec      *pspec)

{
  //  PsppireSearchDialog *dialog = PSPPIRE_SEARCH_DIALOG (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
psppire_search_dialog_class_init (PsppireSearchDialogClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = psppire_search_dialog_finalize;

  object_class->set_property = psppire_search_dialog_set_property;
  object_class->get_property = psppire_search_dialog_get_property;

  signals [FIND] =
    g_signal_new ("find",
                  G_TYPE_FROM_CLASS (class),
                  G_SIGNAL_RUN_FIRST,
                  0,
                  NULL, NULL,
                  g_cclosure_marshal_VOID__BOOLEAN,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_BOOLEAN);

  parent_class = g_type_class_peek_parent (class);
}

static void
search_forward (PsppireSearchDialog *dialog)
{
  g_signal_emit (dialog, signals [FIND], 0, FALSE);
}

static void
search_backward (PsppireSearchDialog *dialog)
{
  g_signal_emit (dialog, signals [FIND], 0, TRUE);
}

static void
on_find (PsppireSearchDialog *sd)
{
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (sd->backward)))
    search_backward (sd);
  else
    search_forward (sd);
}

static void
psppire_search_dialog_init (PsppireSearchDialog *dialog)
{
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);

  GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
  GtkWidget *label = gtk_label_new ( _("Search Text:"));
  dialog->entry = gtk_search_entry_new ();

  g_signal_connect_swapped (dialog->entry, "next-match", G_CALLBACK (search_forward),
                            dialog);
  g_signal_connect_swapped (dialog->entry, "previous-match", G_CALLBACK (search_backward),
                            dialog);

  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 5);
  gtk_box_pack_start (GTK_BOX (hbox), dialog->entry, TRUE, TRUE, 5);

  gtk_box_pack_start (GTK_BOX (box), hbox, FALSE, TRUE, 5);

  GtkWidget *bbo = gtk_button_box_new (GTK_ORIENTATION_VERTICAL);
  dialog->ignore_case = gtk_check_button_new_with_label ( _("Ignore case"));
  gtk_box_pack_start (GTK_BOX (bbo), dialog->ignore_case, FALSE, TRUE, 5);

  dialog->wrap = gtk_check_button_new_with_label ( _("Wrap around"));
  gtk_box_pack_start (GTK_BOX (bbo), dialog->wrap, FALSE, TRUE, 5);

  dialog->whole = gtk_check_button_new_with_label ( _("Match whole words only"));
  gtk_box_pack_start (GTK_BOX (bbo), dialog->whole, FALSE, TRUE, 5);

  dialog->forward = gtk_radio_button_new_with_label (NULL, _("Search forward"));
  gtk_box_pack_start (GTK_BOX (bbo), dialog->forward, FALSE, TRUE, 5);

  dialog->backward = gtk_radio_button_new_with_label (NULL, _("Search backward"));
  gtk_box_pack_start (GTK_BOX (bbo), dialog->backward, FALSE, TRUE, 5);

  gtk_radio_button_join_group (GTK_RADIO_BUTTON (dialog->backward),
                               GTK_RADIO_BUTTON (dialog->forward));

  gtk_box_pack_start (GTK_BOX (box), bbo, FALSE, TRUE, 5);

  GtkWidget *bb = psppire_button_box_new ();
  g_object_set (bb,
                "buttons", PSPPIRE_BUTTON_FIND_MASK | PSPPIRE_BUTTON_CLOSE_MASK,
                "layout-style", GTK_BUTTONBOX_SPREAD,
                NULL);

  gtk_box_pack_start (GTK_BOX (box), bb, FALSE, TRUE, 5);

  g_signal_connect_swapped (PSPPIRE_BUTTON_BOX (bb)->button[PSPPIRE_BUTTON_FIND],
                            "clicked", G_CALLBACK (on_find), dialog);

  gtk_widget_show_all (box);
  gtk_container_add (GTK_CONTAINER (dialog), box);
}

GtkWidget*
psppire_search_dialog_new (void)
{
  PsppireSearchDialog *dialog
    = g_object_new (psppire_search_dialog_get_type (),
                    "title", _("PSPPIRE Search Syntax"),
                    NULL);

  return GTK_WIDGET (dialog) ;
}
