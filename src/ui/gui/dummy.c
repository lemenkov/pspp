/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2020  Free Software Foundation

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


/* This file exists merely to keep the dynamic linker happy when
   trying to resolve symbols in the libpsppire-glade.so library
   (used to define psppire's custom widgets in glade).  This
   file should not be linked into any binary or library used by
   pspp or psppire themsleves.  */


#include <config.h>
#include <assert.h>

#include <gtk/gtk.h>

#include "libpspp/compiler.h"
#include "data/variable.h"

#include "psppire-data-store.h"
#include "t-test-options.h"
#include "src/language/stats/chart-category.h"
#include "src/language/stats/aggregate.h"

const GEnumValue align[1];
const GEnumValue measure[1];
const GEnumValue role[1];

bool
var_is_numeric (const struct variable *v UNUSED)
{
  assert (0);
  return -1;
}

void
tt_options_dialog_run (struct tt_options_dialog *x UNUSED)
{
  assert (0);
}

const struct agr_func agr_func_tab[] =
  {
  };

const struct ag_func ag_func[] = {};
const int N_AG_FUNCS = 1;


gchar *
psppire_data_store_value_to_string (gpointer unused, PsppireDataStore *store, gint col, gint row, const GValue *v)
{
  assert (0);
  return NULL;
}

gboolean
psppire_data_store_string_to_value (GtkTreeModel *model, gint col, gint row,
				    const gchar *in, GValue *out)
{
  assert (0);
  return FALSE;
}
