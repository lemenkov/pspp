/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011, 2013 Free Software Foundation, Inc.

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

#include <stdio.h>
#include <stdlib.h>

#include "data/dataset.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_variable_alignment (struct lexer *lexer, struct dataset *ds)
{
  do
    {
      struct variable **v;
      size_t nv;

      if (!parse_variables (lexer, dataset_dict (ds), &v, &nv, PV_NONE))
        return CMD_FAILURE;

      if (!lex_force_match (lexer, T_LPAREN))
        goto error;

      enum alignment align;
      if (lex_match_id (lexer, "LEFT"))
        align = ALIGN_LEFT;
      else if (lex_match_id (lexer, "RIGHT"))
        align = ALIGN_RIGHT;
      else if (lex_match_id (lexer, "CENTER"))
        align = ALIGN_CENTRE;
      else
        {
          lex_error_expecting (lexer, "LEFT", "RIGHT", "CENTER");
          goto error;
        }

      if (!lex_force_match (lexer, T_RPAREN))
        goto error;

      for (size_t i = 0; i < nv; ++i)
        var_set_alignment (v[i], align);

      while (lex_token (lexer) == T_SLASH)
        lex_get (lexer);
      free (v);
      continue;

    error:
      free (v);
      return CMD_FAILURE;
    }
  while (lex_token (lexer) != T_ENDCMD);
  return CMD_SUCCESS;
}

int
cmd_variable_width (struct lexer *lexer, struct dataset *ds)
{
  do
    {
      struct variable **v;
      size_t nv;
      if (!parse_variables (lexer, dataset_dict (ds), &v, &nv, PV_NONE))
        return CMD_FAILURE;

      if (!lex_force_match (lexer, T_LPAREN)
          || !lex_force_int_range (lexer, NULL, 1, INT_MAX))
        goto error;
      long width = lex_integer (lexer);
      lex_get (lexer);
      if (!lex_force_match (lexer, T_RPAREN))
        goto error;

      width = MIN (width, 2 * MAX_STRING);

      for (size_t i = 0; i < nv; ++i)
        var_set_display_width (v[i], width);

      while (lex_token (lexer) == T_SLASH)
        lex_get (lexer);
      free (v);
      continue;

    error:
      free (v);
      return CMD_FAILURE;
    }
  while (lex_token (lexer) != T_ENDCMD);
  return CMD_SUCCESS;
}

/* Set variables' measurement level */
int
cmd_variable_level (struct lexer *lexer, struct dataset *ds)
{
  do
    {
      struct variable **v;
      size_t nv;

      if (!parse_variables (lexer, dataset_dict (ds), &v, &nv, PV_NONE))
        return CMD_FAILURE;

      if (!lex_force_match (lexer, T_LPAREN))
        goto error;

      enum measure level;
      if (lex_match_id (lexer, "SCALE"))
        level = MEASURE_SCALE;
      else if (lex_match_id (lexer, "ORDINAL"))
        level = MEASURE_ORDINAL;
      else if (lex_match_id (lexer, "NOMINAL"))
        level = MEASURE_NOMINAL;
      else
        {
          lex_error_expecting (lexer, "SCALE", "ORDINAL", "NOMINAL");
          goto error;
        }

      if (!lex_force_match (lexer, T_RPAREN))
        goto error;

      for (size_t i = 0; i < nv; ++i)
        var_set_measure (v[i], level);

      while (lex_token (lexer) == T_SLASH)
        lex_get (lexer);
      free (v);
      continue;

    error:
      free (v);
      return CMD_FAILURE;
    }
  while (lex_token (lexer) != T_ENDCMD);
  return CMD_SUCCESS;
}

int
cmd_variable_role (struct lexer *lexer, struct dataset *ds)
{
  do
    {
      if (!lex_force_match (lexer, T_SLASH))
        return CMD_FAILURE;

      enum var_role role;
      if (lex_match_id (lexer, "INPUT"))
        role = ROLE_INPUT;
      else if (lex_match_id (lexer, "TARGET"))
        role = ROLE_TARGET;
      else if (lex_match_id (lexer, "BOTH"))
        role = ROLE_BOTH;
      else if (lex_match_id (lexer, "NONE"))
        role = ROLE_NONE;
      else if (lex_match_id (lexer, "PARTITION"))
        role = ROLE_PARTITION;
      else if (lex_match_id (lexer, "SPLIT"))
        role = ROLE_SPLIT;
      else
        {
          lex_error_expecting (lexer, "INPUT", "TARGET", "BOTH",
                               "NONE", "PARTITION", "SPLIT");
          return CMD_FAILURE;
        }

      struct variable **v;
      size_t nv;
      if (!parse_variables (lexer, dataset_dict (ds), &v, &nv, PV_NONE))
        return CMD_FAILURE;

      for (size_t i = 0; i < nv; i++)
        var_set_role (v[i], role);

      free (v);
    }
  while (lex_token (lexer) != T_ENDCMD);

  return CMD_SUCCESS;
}
