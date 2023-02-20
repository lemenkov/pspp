/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2007, 2010, 2011, 2013 Free Software Foundation, Inc.

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

#include <stdlib.h>

#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Performs DELETE VARIABLES command. */
int
cmd_delete_variables (struct lexer *lexer, struct dataset *ds)
{
  if (proc_has_transformations (ds))
    {
      lex_ofs_error (lexer, 0, lex_ofs (lexer) - 1,
                     _("%s may not be used when there are pending "
                       "transformations (use %s to execute transformations)."),
                     "DELETE VARIABLES", "EXECUTE");
      return CMD_FAILURE;
    }
  if (proc_in_temporary_transformations (ds))
    {
      lex_ofs_error (lexer, 0, lex_ofs (lexer) - 1,
                     _("%s may not be used after %s."),
                     "DELETE VARIABLES", "TEMPORARY");
      return CMD_FAILURE;
    }

  struct variable **vars;
  size_t n_vars;
  if (!parse_variables (lexer, dataset_dict (ds), &vars, &n_vars, PV_NONE))
    return CMD_FAILURE;
  if (n_vars == dict_get_n_vars (dataset_dict (ds)))
    {
      lex_ofs_error (lexer, 0, lex_ofs (lexer) - 1,
                     _("%s may not be used to delete all variables "
                       "from the active dataset dictionary.  "
                       "Use %s instead."), "DELETE VARIABLES", "NEW FILE");
      free (vars);
      return CMD_FAILURE;
    }

  dataset_delete_vars (ds, vars, n_vars);
  free (vars);

  return CMD_SUCCESS;
}
