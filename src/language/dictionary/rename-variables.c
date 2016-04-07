/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011, 2015, 2016 Free Software Foundation, Inc.

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

#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* The code for this function is very similar to the code for the
   RENAME subcommand of MODIFY VARS. */
int
cmd_rename_variables (struct lexer *lexer, struct dataset *ds)
{
  struct variable **vars_to_be_renamed = NULL;
  size_t n_vars_to_be_renamed = 0;

  char **new_names = NULL;
  size_t n_new_names = 0;

  char *err_name;

  int status = CMD_CASCADING_FAILURE;

  if (proc_make_temporary_transformations_permanent (ds))
    msg (SE, _("%s may not be used after %s.  "
               "Temporary transformations will be made permanent."), "RENAME VARS", "TEMPORARY");

  do
    {
      int opts = PV_APPEND | PV_NO_DUPLICATE;

      if (!lex_match (lexer, T_LPAREN))
        opts |= PV_SINGLE;
      if (!parse_variables (lexer, dataset_dict (ds),
                            &vars_to_be_renamed, &n_vars_to_be_renamed, opts))
	{
	  goto lossage;
	}
      if (!lex_force_match (lexer, T_EQUALS))
	{
	  goto lossage;
	}
      if (!parse_DATA_LIST_vars (lexer, dataset_dict (ds),
                                 &new_names, &n_new_names, opts))
	{
	  goto lossage;
	}
      if (n_new_names != n_vars_to_be_renamed)
        {
          msg (SE, _("Differing number of variables in old name list "
                     "(%zu) and in new name list (%zu)."),
	       n_vars_to_be_renamed, n_new_names);
          goto lossage;
        }
      if (!(opts & PV_SINGLE) && !lex_force_match (lexer, T_RPAREN))
	{
	  goto lossage;
	}
    }
  while (lex_token (lexer) != T_ENDCMD);

  if (!dict_rename_vars (dataset_dict (ds),
                         vars_to_be_renamed, new_names, n_new_names,
                         &err_name))
    {
      msg (SE, _("Renaming would duplicate variable name %s."), err_name);
      goto lossage;
    }

  status = CMD_SUCCESS;

 lossage:
  free (vars_to_be_renamed);
  if (new_names != NULL)
    {
      size_t i;
      for (i = 0; i < n_new_names; ++i)
        free (new_names[i]);
      free (new_names);
    }
  return status;
}
