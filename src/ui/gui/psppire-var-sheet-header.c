/*
    A candidate replacement for Pspp's sheet
    Copyright (C) 2016  John Darrington

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

#include "efficient-sheet/src/ssw-axis-model.h"
#include "efficient-sheet/src/ssw-datum.h"

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
  return SSW_TYPE_DATUM;
}

static gpointer
gi (GListModel *list, guint position)
{
  SswDatum *gd = SSW_DATUM (g_object_new (SSW_TYPE_DATUM, NULL));

  switch (position)
    {
    case 0:
      gd->text = g_strdup ("Name");
      break;
    case 1:
      gd->text = g_strdup ("Type");
      break;
    case 2:
      gd->text = g_strdup ("Width");
      break;
    case 3:
      gd->text = g_strdup ("Decimal");
      break;
    case 4:
      gd->text = g_strdup ("Label");
      break;
    case 5:
      gd->text = g_strdup ("Value Labels");
      break;
    case 6:
      gd->text = g_strdup ("Missing Values");
      break;
    case 7:
      gd->text = g_strdup ("Columns");
      break;
    case 8:
      gd->text = g_strdup ("Align");
      break;
    case 9:
      gd->text = g_strdup ("Measure");
      break;
    case 10:
      gd->text = g_strdup ("Role");
      break;
    default:
      //      g_assert_not_reached ();
      g_print ("Bug: Request for item %d\n", position);
      break;
    }

  return gd;
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

