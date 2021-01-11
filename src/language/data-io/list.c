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
#include "language/dictionary/split-file.h"
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

enum numbering
  {
    format_unnumbered,
    format_numbered
  };


struct lst_cmd
{
  long first;
  long last;
  long step;
  const struct variable **v_variables;
  size_t n_variables;
  enum numbering numbering;
};


static int
list_execute (const struct lst_cmd *lcmd, struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);

  bool ok;
  int i;
  struct casegrouper *grouper;
  struct casereader *group;
  struct subcase sc;

  subcase_init_empty (&sc);
  for (i = 0; i < lcmd->n_variables; i++)
    subcase_add_var (&sc, lcmd->v_variables[i], SC_ASCEND);


  grouper = casegrouper_create_splits (proc_open (ds), dict);
  while (casegrouper_get_next_group (grouper, &group))
    {
      struct ccase *c = casereader_peek (group, 0);
      if (c != NULL)
        {
          output_split_file_values (ds, c);
          case_unref (c);
        }

      group = casereader_project (group, &sc);
      group = casereader_select (group, lcmd->first - 1,
                                 (lcmd->last != LONG_MAX ? lcmd->last
                                  : CASENUMBER_MAX), lcmd->step);

      struct pivot_table *table = pivot_table_create (N_("Data List"));
      table->show_values = table->show_variables = SETTINGS_VALUE_SHOW_VALUE;

      struct pivot_dimension *variables = pivot_dimension_create (
        table, PIVOT_AXIS_COLUMN, N_("Variables"));
      for (int i = 0; i < lcmd->n_variables; i++)
        pivot_category_create_leaf (
          variables->root, pivot_value_new_variable (lcmd->v_variables[i]));

      struct pivot_dimension *cases = pivot_dimension_create (
        table, PIVOT_AXIS_ROW, N_("Case Number"));
      if (lcmd->numbering == format_numbered)
        cases->root->show_label = true;
      else
        cases->hide_all_labels = true;

      casenumber case_num = lcmd->first;
      for (; (c = casereader_read (group)) != NULL; case_unref (c))
        {
          int case_idx = pivot_category_create_leaf (
            cases->root, pivot_value_new_integer (case_num));
          case_num += lcmd->step;

          for (int i = 0; i < lcmd->n_variables; i++)
            pivot_table_put2 (table, i, case_idx,
                              pivot_value_new_var_value (
                                lcmd->v_variables[i], case_data_idx (c, i)));
        }
      casereader_destroy (group);

      pivot_table_submit (table);
    }
  ok = casegrouper_destroy (grouper);
  ok = proc_commit (ds) && ok;

  subcase_destroy (&sc);
  free (lcmd->v_variables);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;
}


/* Parses and executes the LIST procedure. */
int
cmd_list (struct lexer *lexer, struct dataset *ds)
{
  struct lst_cmd cmd;
  const struct dictionary *dict = dataset_dict (ds);

  /* Fill in defaults. */
  cmd.step = 1;
  cmd.first = 1;
  cmd.last = LONG_MAX;
  cmd.n_variables = 0;
  cmd.v_variables = NULL;
  cmd.numbering = format_unnumbered;


  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);
      if (lex_match_id (lexer, "VARIABLES"))
        {
          lex_match (lexer, T_EQUALS);
          if (! parse_variables_const (lexer, dict, &cmd.v_variables, &cmd.n_variables, 0))
            {
              msg (SE, _("No variables specified."));
              return CMD_FAILURE;
            }
        }
      else if (lex_match_id (lexer, "FORMAT"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "NUMBERED"))
            {
              cmd.numbering = format_numbered;
            }
          else if (lex_match_id (lexer, "UNNUMBERED"))
            {
              cmd.numbering = format_unnumbered;
            }
          else
            {
              lex_error (lexer, NULL);
              goto error;
            }
        }
      /* example: LIST /CASES=FROM 1 TO 25 BY 5. */
      else if (lex_match_id (lexer, "CASES"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "FROM") && lex_force_int (lexer))
            {
              cmd.first = lex_integer (lexer);
              lex_get (lexer);
            }

          if ((lex_match (lexer, T_TO) && lex_force_int (lexer))
              || lex_is_integer (lexer))
            {
              cmd.last = lex_integer (lexer);
              lex_get (lexer);
            }

          if (lex_match (lexer, T_BY) && lex_force_int (lexer))
            {
              cmd.step = lex_integer (lexer);
              lex_get (lexer);
            }
        }
      else if (! parse_variables_const (lexer, dict, &cmd.v_variables, &cmd.n_variables, 0))
        {
          return CMD_FAILURE;
        }
    }


  /* Verify arguments. */
  if (cmd.first > cmd.last)
    {
      int t;
      msg (SW, _("The first case (%ld) specified precedes the last case (%ld) "
                 "specified.  The values will be swapped."), cmd.first, cmd.last);
      t = cmd.first;
      cmd.first = cmd.last;
      cmd.last = t;
    }

  if (cmd.first < 1)
    {
      msg (SW, _("The first case (%ld) to list is numbered less than 1.  "
                 "The value is being reset to 1."), cmd.first);
      cmd.first = 1;
    }

  if (cmd.last < 1)
    {
      msg (SW, _("The last case (%ld) to list is numbered less than 1.  "
                 "The value is being reset to 1."), cmd.last);
      cmd.last = 1;
    }

  if (cmd.step < 1)
    {
      msg (SW, _("The step value %ld is less than 1.  The value is being "
                 "reset to 1."), cmd.step);
      cmd.step = 1;
    }

  /* If no variables were explicitly provided, then default to ALL */
  if (cmd.n_variables == 0)
    dict_get_vars (dict, &cmd.v_variables, &cmd.n_variables,
                   DC_SYSTEM | DC_SCRATCH);

  return list_execute (&cmd, ds);

 error:
  free (cmd.v_variables);
  return CMD_FAILURE;
}

