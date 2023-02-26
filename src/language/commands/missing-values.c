/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2013, 2016 Free Software Foundation, Inc.

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

#include "data/data-in.h"
#include "data/dictionary.h"
#include "data/dataset.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "data/value.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/token.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_missing_values (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);

  while (lex_token (lexer) != T_ENDCMD)
    {
      struct missing_values mv = MV_INIT_EMPTY_NUMERIC;
      struct variable **v = NULL;
      size_t nv;

      if (!parse_variables (lexer, dict, &v, &nv, PV_NONE))
        goto error;

      if (!lex_force_match (lexer, T_LPAREN))
        goto error;

      int values_start = lex_ofs (lexer);
      int values_end;
      for (values_end = values_start; ; values_end++)
        {
          enum token_type next = lex_ofs_token (lexer, values_end + 1)->type;
          if (next == T_RPAREN || next == T_ENDCMD || next == T_STOP)
            break;
        }

      if (!lex_match (lexer, T_RPAREN))
        {
          if (var_is_numeric (v[0]))
            {
              while (!lex_match (lexer, T_RPAREN))
                {
                  enum fmt_type type = var_get_print_format (v[0]).type;
                  double x, y;

                  if (!parse_num_range (lexer, &x, &y, &type))
                    goto error;

                  if (!(x == y
                        ? mv_add_num (&mv, x)
                        : mv_add_range (&mv, x, y)))
                    {
                      lex_ofs_error (lexer, values_start, values_end,
                                     _("Too many numeric missing values.  At "
                                       "most three individual values or one "
                                       "value and one range are allowed."));
                      goto error;
                    }

                  lex_match (lexer, T_COMMA);
                }
            }
          else
            {
              const char *encoding = dict_get_encoding (dict);

              mv_init (&mv, MV_MAX_STRING);
              while (!lex_match (lexer, T_RPAREN))
                {
                  if (!lex_force_string (lexer))
                    goto error;

                  /* Truncate the string to fit in 8 bytes in the dictionary
                     encoding. */
                  const char *utf8_s = lex_tokcstr (lexer);
                  size_t utf8_len = ss_length (lex_tokss (lexer));
                  size_t utf8_trunc_len = utf8_encoding_trunc_len (
                    utf8_s, encoding, MV_MAX_STRING);
                  if (utf8_trunc_len < utf8_len)
                    lex_error (lexer, _("Truncating missing value to maximum "
                                        "acceptable length (%d bytes)."),
                               MV_MAX_STRING);

                  /* Recode to dictionary encoding and add. */
                  char *raw_s = recode_string (encoding, "UTF-8",
                                               utf8_s, utf8_trunc_len);
                  bool ok = mv_add_str (&mv, CHAR_CAST (const uint8_t *, raw_s),
                                        strlen (raw_s));
                  free (raw_s);
                  if (!ok)
                    {
                      lex_ofs_error (lexer, values_start, values_end,
                                     _("Too many string missing values.  "
                                       "At most three individual values "
                                       "are allowed."));
                      goto error;
                    }

                  lex_get (lexer);
                  lex_match (lexer, T_COMMA);
                }
            }
        }
      lex_match (lexer, T_SLASH);

      bool ok = true;
      for (size_t i = 0; i < nv; i++)
        {
          int var_width = var_get_width (v[i]);

          if (mv_is_resizable (&mv, var_width))
            var_set_missing_values (v[i], &mv);
          else
            {
              ok = false;
              if (!var_width)
                lex_ofs_error (lexer, values_start, values_end,
                               _("Cannot assign string missing values to "
                                 "numeric variable %s."), var_get_name (v[i]));
              else
                lex_ofs_error (lexer, values_start, values_end,
                               _("Missing values are too long to assign "
                                 "to variable %s with width %d."),
                               var_get_name (v[i]), var_get_width (v[i]));
            }
        }
      mv_destroy (&mv);
      free (v);
      if (!ok)
        return CMD_FAILURE;
      continue;

    error:
      mv_destroy (&mv);
      free (v);
      return CMD_FAILURE;
    }

  return CMD_SUCCESS;
}

