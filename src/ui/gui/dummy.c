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

#include "src/language/stats/chart-category.h"

const GEnumValue align[1];
const GEnumValue measure[1];
const GEnumValue role[1];

const int N_AG_FUNCS = 0;
const struct ag_func ag_func[1];

int F_8_0;

int
var_is_numeric ()
{
  assert (0);
  return -1;
}

int
tt_options_dialog_run ()
{
  assert (0);
  return -1;
}

int
agr_func_tab ()
{
  assert (0);
  return -1;
}
