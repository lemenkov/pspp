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
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Performs DELETE VARIABLES command. */
int
cmd_delete_variables (struct lexer *lexer, struct dataset *ds)
{
  struct variable **vars;
  size_t var_cnt;
  bool ok;

  if (proc_make_temporary_transformations_permanent (ds))
    msg (SE, _("%s may not be used after %s.  "
               "Temporary transformations will be made permanent."),
	 "DELETE VARIABLES", "TEMPORARY");

  if (!parse_variables (lexer, dataset_dict (ds), &vars, &var_cnt, PV_NONE))
    goto error;
  if (var_cnt == dict_get_var_cnt (dataset_dict (ds)))
    {
      msg (SE, _("%s may not be used to delete all variables "
                 "from the active dataset dictionary.  "
                 "Use %s instead."), "DELETE VARIABLES", "NEW FILE");
      goto error;
    }

  ok = casereader_destroy (proc_open_filtering (ds, false));
  ok = proc_commit (ds) && ok;
  if (!ok)
    goto error;

  dict_delete_vars (dataset_dict (ds), vars, var_cnt);

  /* XXX A bunch of bugs conspire to make executing transformations again here
     necessary, even though it shouldn't be.

     Consider the following (which is included in delete-variables.at):

        DATA LIST NOTABLE /s1 TO s2 1-2(A).
        BEGIN DATA
        12
        END DATA.
        DELETE VARIABLES s1.
        NUMERIC n1.
        LIST.

     The DATA LIST gives us a caseproto with widths 1,1.  DELETE VARIABLES
     deletes the first variable so we now have -1,1.  This already is
     technically a problem because proc_casereader_read() calls
     case_unshare_and_resize() from the former to the latter caseproto, and
     these caseprotos are not conformable (which is a requirement for
     case_resize()).  It doesn't cause an assert by default because
     case_resize() uses expensive_assert() to check for it though.  However, in
     practice we don't see a problem yet because case_resize() only does work
     if the number of widths in the source and dest caseproto are different.

     Executing NUMERIC adds a third variable, though, so we have -1,1,0.  This
     makes caseproto_resize() notice that there are fewer strings in the new
     caseproto.  Therefore it destroys the second one (s2).  It should destroy
     the first one (s1), but if the caseprotos were really conformable then it
     would have destroyed the right one.  This mistake eventually causes a bad
     memory reference.

     Executing transformations a second time after DELETE VARIABLES, like we do
     below, works around the problem because we can never run into a situation
     where we've got both new variables (triggering a resize) and deleted
     variables (triggering the bad free).

     We should fix this in a better way.  Doing it cleanly seems hard.  This
     seems to work for now. */
  ok = casereader_destroy (proc_open_filtering (ds, false));
  ok = proc_commit (ds) && ok;
  if (!ok)
    goto error;

  free (vars);

  return CMD_SUCCESS;

 error:
  free (vars);
  return CMD_CASCADING_FAILURE;
}
