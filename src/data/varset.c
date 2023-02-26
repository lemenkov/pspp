/* PSPP - a program for statistical analysis.
   Copyright (C) 2023 Free Software Foundation, Inc.

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

#include "data/varset.h"

#include <stdlib.h>

#include "data/dictionary.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Creates and returns a clone of OLD.  The caller is responsible for freeing
   the new variable set (using varset_destroy()). */
struct varset *
varset_clone (const struct varset *old)
{
  struct varset *new = xmalloc (sizeof *new);
  *new = (struct varset) {
    .name = xstrdup (old->name),
    .vars = xmemdup (old->vars, old->n_vars * sizeof *old->vars),
    .n_vars = old->n_vars,
  };
  return new;
}

/* Frees VARSET and the data that it contains. */
void
varset_destroy (struct varset *varset)
{
  if (varset)
    {
      free (varset->name);
      free (varset->vars);
      free (varset);
    }
}
