/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011, 2014 Free Software Foundation, Inc.

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
#include "data/format.h"
#include "language/command.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Parses the NUMERIC command. */
int
cmd_numeric (struct lexer *lexer, struct dataset *ds)
{
  do
    {
      char **v;
      size_t nv;
      int vars_start = lex_ofs (lexer);
      if (!parse_DATA_LIST_vars (lexer, dataset_dict (ds),
                                 &v, &nv, PV_NO_DUPLICATE))
        return CMD_FAILURE;
      int vars_end = lex_ofs (lexer) - 1;

      bool ok = false;

      /* Get the optional format specification. */
      struct fmt_spec f = var_default_formats (0);
      if (lex_match (lexer, T_LPAREN))
        {
          if (!parse_format_specifier (lexer, &f))
            goto done;

          char *error = fmt_check_output__ (f);
          if (error)
            {
              lex_next_error (lexer, -1, -1, "%s", error);
              free (error);
              goto done;
            }

          if (fmt_is_string (f.type))
            {
              char str[FMT_STRING_LEN_MAX + 1];
              lex_next_error (lexer, -1, -1,
                              _("Format type %s may not be used with a numeric "
                                "variable."), fmt_to_string (f, str));
              goto done;
            }

          if (!lex_match (lexer, T_RPAREN))
            {
              lex_error_expecting (lexer, "`)'");
              goto done;
            }
        }

      /* Create each variable. */
      for (size_t i = 0; i < nv; i++)
        {
          struct variable *new_var = dict_create_var (dataset_dict (ds),
                                                      v[i], 0);
          if (!new_var)
            lex_ofs_error (lexer, vars_start, vars_end,
                           _("There is already a variable named %s."), v[i]);
          else
            var_set_both_formats (new_var, f);
        }
      ok = true;

    done:
      for (size_t i = 0; i < nv; i++)
        free (v[i]);
      free (v);
      if (!ok)
        return CMD_FAILURE;
    }
  while (lex_match (lexer, T_SLASH));

  return CMD_SUCCESS;
}

/* Parses the STRING command. */
int
cmd_string (struct lexer *lexer, struct dataset *ds)
{
  do
    {
      char **v;
      size_t nv;
      int vars_start = lex_ofs (lexer);
      if (!parse_DATA_LIST_vars (lexer, dataset_dict (ds),
                                 &v, &nv, PV_NO_DUPLICATE))
        return CMD_FAILURE;
      int vars_end = lex_ofs (lexer) - 1;

      bool ok = false;

      struct fmt_spec f;
      if (!lex_force_match (lexer, T_LPAREN)
          || !parse_format_specifier (lexer, &f))
        goto done;

      char *error = fmt_check_type_compat__ (f, NULL, VAL_STRING);
      if (!error)
        error = fmt_check_output__ (f);
      if (error)
        {
          lex_next_error (lexer, -1, -1, "%s", error);
          free (error);
          goto done;
        }

      if (!lex_force_match (lexer, T_RPAREN))
        goto done;

      /* Create each variable. */
      int width = fmt_var_width (f);
      for (size_t i = 0; i < nv; i++)
        {
          struct variable *new_var = dict_create_var (dataset_dict (ds), v[i],
                                                      width);
          if (!new_var)
            lex_ofs_error (lexer, vars_start, vars_end,
                           _("There is already a variable named %s."), v[i]);
          else
            var_set_both_formats (new_var, f);
        }
      ok = true;

    done:
      for (size_t i = 0; i < nv; i++)
        free (v[i]);
      free (v);
      if (!ok)
        return CMD_FAILURE;
    }
  while (lex_match (lexer, T_SLASH));

  return CMD_SUCCESS;
}

/* Parses the LEAVE command. */
int
cmd_leave (struct lexer *lexer, struct dataset *ds)
{
  struct variable **v;
  size_t nv;

  if (!parse_variables (lexer, dataset_dict (ds), &v, &nv, PV_NONE))
    return CMD_CASCADING_FAILURE;
  for (size_t i = 0; i < nv; i++)
    var_set_leave (v[i], true);
  free (v);

  return CMD_SUCCESS;
}
