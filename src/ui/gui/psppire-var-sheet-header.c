/*  PSPPIRE - a graphical user interface for PSPP.
    Copyright (C) 2016  Free Software Foundation

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>
#include <gtk/gtk.h>

#include "psppire-var-sheet-header.h"

#include <gettext.h>

#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

enum  {CHANGED,
       n_SIGNALS};

static guint signals [n_SIGNALS];

static guint
gni (GListModel *list)
{
  return 11;
}

static GType
git (GListModel *list)
{
  return GTK_TYPE_BUTTON;
}


static gpointer
gi (GListModel *list, guint position)
{
  GtkWidget *button = gtk_button_new ();
  const gchar *text = NULL;

  switch (position)
    {
    case 0:
      text = N_("Name");
      break;
    case 1:
      text = N_("Type");
      break;
    case 2:
      text = N_("Width");
      break;
    case 3:
      text = N_("Decimal");
      break;
    case 4:
      text = N_("Label");
      break;
    case 5:
      text = N_("Value Labels");
      break;
    case 6:
      text = N_("Missing Values");
      break;
    case 7:
      text = N_("Columns");
      break;
    case 8:
      text = N_("Align");
      break;
    case 9:
      text = N_("Measure");
      break;
    case 10:
      text = N_("Role");
      break;
    default:
      break;
    }

  if (text)
    gtk_button_set_label (GTK_BUTTON (button), gettext (text));

  return button;
}


static void
psppire_init_iface (GListModelInterface *iface)
{
  iface->get_n_items = gni;
  iface->get_item = gi;
  iface->get_item_type = git;
}


G_DEFINE_TYPE_WITH_CODE (PsppireVarSheetHeader, psppire_var_sheet_header,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, psppire_init_iface));

static void
psppire_var_sheet_header_init (PsppireVarSheetHeader *d)
{
}



static void
psppire_var_sheet_header_class_init (PsppireVarSheetHeaderClass *dc)
{
  GObjectClass *object_class = G_OBJECT_CLASS (dc);

  /* This signal is never emitted.  It is just to satisfy the interface. */
  signals [CHANGED] =
    g_signal_new ("changed",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_FIRST,
		  0,
		  NULL, NULL,
		  g_cclosure_marshal_VOID__VOID,
		  G_TYPE_NONE,
		  0);
}

