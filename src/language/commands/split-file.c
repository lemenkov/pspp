/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "data/case.h"
#include "data/casereader.h"
#include "data/data-out.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/commands/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

int
cmd_split_file (struct lexer *lexer, struct dataset *ds)
{
  if (lex_match_id (lexer, "OFF"))
    dict_clear_split_vars (dataset_dict (ds));
  else
    {
      struct variable **v;
      size_t n;

      enum split_type type = (!lex_match_id (lexer, "LAYERED")
                              && lex_match_id (lexer, "SEPARATE")
                              ? SPLIT_SEPARATE
                              : SPLIT_LAYERED);

      lex_match (lexer, T_BY);
      int vars_start = lex_ofs (lexer);
      if (!parse_variables (lexer, dataset_dict (ds), &v, &n, PV_NO_DUPLICATE))
        return CMD_CASCADING_FAILURE;
      int vars_end = lex_ofs (lexer) - 1;

      if (n > MAX_SPLITS)
        {
          verify (MAX_SPLITS == 8);
          lex_ofs_error (lexer, vars_start, vars_end,
                         _("At most 8 split variables may be specified."));
          free (v);
          return CMD_CASCADING_FAILURE;
        }

      dict_set_split_vars (dataset_dict (ds), v, n, type);
      free (v);
    }

  return CMD_SUCCESS;
}

/* Dumps out the values of all the split variables for the case C. */
void
output_split_file_values (const struct dataset *ds, const struct ccase *c)
{
  const struct dictionary *dict = dataset_dict (ds);
  size_t n_vars = dict_get_n_splits (dict);
  if (n_vars == 0)
    return;

  struct pivot_table *table = pivot_table_create (N_("Split Values"));
  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Value"),
                          N_("Value"));
  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable"));
  variables->root->show_label = true;

  for (size_t i = 0; i < n_vars; i++)
    {
      const struct variable *v = dict_get_split_vars (dict)[i];
      int row = pivot_category_create_leaf (variables->root,
                                            pivot_value_new_variable (v));

      pivot_table_put2 (table, 0, row,
                        pivot_value_new_var_value (v, case_data (c, v)));
    }

  pivot_table_submit (table);
}

/* Dumps out the values of all the split variables for the first case in
   READER. */
void
output_split_file_values_peek (const struct dataset *ds,
                               const struct casereader *reader)
{
  struct ccase *c = casereader_peek (reader, 0);
  if (c)
    {
      output_split_file_values (ds, c);
      case_unref (c);
    }
}
