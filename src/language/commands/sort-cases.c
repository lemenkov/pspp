/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011 Free Software Foundation, Inc.

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

#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>

#include "data/dataset.h"
#include "data/settings.h"
#include "data/subcase.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/commands/sort-criteria.h"
#include "libpspp/message.h"
#include "math/sort.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


/* Performs the SORT CASES procedures. */
int
cmd_sort_cases (struct lexer *lexer, struct dataset *ds)
{
  struct subcase ordering = SUBCASE_EMPTY_INITIALIZER;
  bool ok = false;

  lex_match (lexer, T_BY);

  proc_cancel_temporary_transformations (ds);
  if (!parse_sort_criteria (lexer, dataset_dict (ds), &ordering, NULL, NULL))
    return CMD_CASCADING_FAILURE;

  if (settings_get_testing_mode () && lex_match (lexer, T_SLASH))
    {
      if (!lex_force_match_id (lexer, "BUFFERS"))
        goto done;
      lex_match (lexer, T_EQUALS);
      if (!lex_force_int_range (lexer, "BUFFERS", 2, INT_MAX))
        goto done;
      min_buffers = max_buffers = lex_integer (lexer);
      lex_get (lexer);
    }

  proc_discard_output (ds);
  struct casereader *output = sort_execute (proc_open_filtering (ds, false),
                                            &ordering);
  ok = proc_commit (ds);
  ok = dataset_set_source (ds, output) && ok;

 done:
  min_buffers = 64;
  max_buffers = INT_MAX;

  subcase_uninit (&ordering);
  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}

