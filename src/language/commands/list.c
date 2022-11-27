/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009-2011, 2013, 2014, 2016 Free Software Foundation, Inc.

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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "data/casegrouper.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/data-out.h"
#include "data/format.h"
#include "data/subcase.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/commands/split-file.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/compiler.h"
#include "libpspp/ll.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "output/pivot-table.h"

#include "gl/intprops.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xmalloca.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

struct lst_cmd
  {
    long first;
    long last;
    long step;
    const struct variable **vars;
    size_t n_vars;
    bool number_cases;
  };

static int
list_execute (const struct lst_cmd *lcmd, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);

  struct subcase sc;
  subcase_init_empty (&sc);
  for (size_t i = 0; i < lcmd->n_vars; i++)
    subcase_add_var (&sc, lcmd->vars[i], SC_ASCEND);

  struct casegrouper *grouper;
  struct casereader *group;
  grouper = casegrouper_create_splits (proc_open (ds), dict);
  while (casegrouper_get_next_group (grouper, &group))
    {
      output_split_file_values_peek (ds, group);
      group = casereader_project (group, &sc);
      group = casereader_select (group, lcmd->first - 1,
                                 (lcmd->last != LONG_MAX ? lcmd->last
                                  : CASENUMBER_MAX), lcmd->step);

      struct pivot_table *table = pivot_table_create (N_("Data List"));
      table->show_values = table->show_variables = SETTINGS_VALUE_SHOW_VALUE;

      struct pivot_dimension *variables = pivot_dimension_create (
        table, PIVOT_AXIS_COLUMN, N_("Variables"));
      for (size_t i = 0; i < lcmd->n_vars; i++)
        pivot_category_create_leaf (
          variables->root, pivot_value_new_variable (lcmd->vars[i]));

      struct pivot_dimension *cases = pivot_dimension_create (
        table, PIVOT_AXIS_ROW, N_("Case Number"));
      if (lcmd->number_cases)
        cases->root->show_label = true;
      else
        cases->hide_all_labels = true;

      casenumber case_num = lcmd->first;
      struct ccase *c;
      for (; (c = casereader_read (group)) != NULL; case_unref (c))
        {
          int case_idx = pivot_category_create_leaf (
            cases->root, pivot_value_new_integer (case_num));
          case_num += lcmd->step;

          for (size_t i = 0; i < lcmd->n_vars; i++)
            pivot_table_put2 (table, i, case_idx,
                              pivot_value_new_var_value (
                                lcmd->vars[i], case_data_idx (c, i)));
        }
      casereader_destroy (group);

      pivot_table_submit (table);
    }

  bool ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  subcase_uninit (&sc);
  free (lcmd->vars);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}


/* Parses and executes the LIST procedure. */
int
cmd_list (struct lexer *lexer, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);

  struct lst_cmd cmd = {
    .step = 1,
    .first = 1,
    .last = LONG_MAX,
  };

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);
      if (lex_match_id (lexer, "VARIABLES"))
        {
          lex_match (lexer, T_EQUALS);
          free (cmd.vars);
          cmd.vars = NULL;
          if (!parse_variables_const (lexer, dict, &cmd.vars, &cmd.n_vars,
                                      PV_DUPLICATE))
            goto error;
        }
      else if (lex_match_id (lexer, "FORMAT"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "NUMBERED"))
            cmd.number_cases = true;
          else if (lex_match_id (lexer, "UNNUMBERED"))
            cmd.number_cases = false;
          else
            {
              lex_error_expecting (lexer, "NUMBERED", "UNNUMBERED");
              goto error;
            }
        }
      else if (lex_match_id (lexer, "CASES"))
        {
          lex_match (lexer, T_EQUALS);

          if (lex_match_id (lexer, "FROM"))
            {
              if (!lex_force_int_range (lexer, "FROM", 1, LONG_MAX))
                goto error;
              cmd.first = lex_integer (lexer);
              lex_get (lexer);
            }
          else
            cmd.first = 1;

          if (lex_match (lexer, T_TO) || lex_is_integer (lexer))
            {
              if (!lex_force_int_range (lexer, "TO", cmd.first, LONG_MAX))
                goto error;
              cmd.last = lex_integer (lexer);
              lex_get (lexer);
            }
          else
            cmd.last = LONG_MAX;

          if (lex_match (lexer, T_BY))
            {
              if (!lex_force_int_range (lexer, "TO", 1, LONG_MAX))
                goto error;
              cmd.step = lex_integer (lexer);
              lex_get (lexer);
            }
          else
            cmd.step = 1;
        }
      else
        {
          free (cmd.vars);
          cmd.vars = NULL;
          if (!parse_variables_const (lexer, dict, &cmd.vars, &cmd.n_vars,
                                       PV_DUPLICATE))
            goto error;
        }
    }

  if (!cmd.n_vars)
    dict_get_vars (dict, &cmd.vars, &cmd.n_vars, DC_SYSTEM | DC_SCRATCH);
  return list_execute (&cmd, ds);

 error:
  free (cmd.vars);
  return CMD_FAILURE;
}
