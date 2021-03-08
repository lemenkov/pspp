/* PSPP - a program for statistical analysis.
   Copyright (C) 2014 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <stdlib.h>

#include "data/dataset.h"
#include "data/settings.h"
#include "data/format.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/format-parser.h"
#include "libpspp/message.h"
#include "libpspp/string-set.h"
#include "libpspp/version.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_output (struct lexer *lexer, struct dataset *ds UNUSED)
{
  struct string_set rc_names = STRING_SET_INITIALIZER (rc_names);

  if (!lex_force_match_id (lexer, "MODIFY"))
    {
      lex_error (lexer, NULL);
      goto error;
    }

  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "SELECT"))
	{
	  if (!lex_match_id (lexer, "TABLES"))
	    {
	      lex_error (lexer, NULL);
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "TABLECELLS"))
	{
          string_set_clear (&rc_names);
	  struct fmt_spec fmt = { .type = 0 };

	  while (lex_token (lexer) != T_SLASH &&
		 lex_token (lexer) != T_ENDCMD)
	    {
	      if (lex_match_id (lexer, "SELECT"))
		{
		  if (! lex_force_match (lexer, T_EQUALS))
		    goto error;

		  if (! lex_force_match (lexer, T_LBRACK))
		    goto error;

		  while (lex_token (lexer) == T_ID)
                    {
                      string_set_insert (&rc_names, lex_tokcstr (lexer));
                      lex_get (lexer);
                    }

		  if (! lex_force_match (lexer, T_RBRACK))
		    goto error;
		}
	      else if (lex_match_id (lexer, "FORMAT"))
		{
		  char type[FMT_TYPE_LEN_MAX + 1];
		  uint16_t width;
		  uint8_t decimals;

		  if (! lex_force_match (lexer, T_EQUALS))
		    goto error;
		  if (! parse_abstract_format_specifier (lexer, type, &width, &decimals))
		    {
		      lex_error (lexer, NULL);
		      goto error;
		    }

		  if (width <= 0)
		    {
		      const struct fmt_spec *dflt = settings_get_format ();
		      width = dflt->w;
		    }

                  if (!fmt_from_name (type, &fmt.type))
                    {
                      lex_error (lexer, _("Unknown format type `%s'."), type);
		      goto error;
                    }

		  fmt.w = width;
		  fmt.d = decimals;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }

          if (fmt.w)
            {
              const struct string_set_node *node;
              const char *s;
              STRING_SET_FOR_EACH (s, node, &rc_names)
                if (!pivot_result_class_change (s, &fmt))
                  lex_error (lexer, _("Unknown cell class %s."), s);
            }
	}
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }

  string_set_destroy (&rc_names);
  return CMD_SUCCESS;

 error:
  string_set_destroy (&rc_names);
  return CMD_SUCCESS;
}
