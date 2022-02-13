/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2010, 2011 Free Software Foundation, Inc.

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
#include "data/dictionary.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/token.h"
#include "libpspp/message.h"
#include "libpspp/start-date.h"
#include "libpspp/version.h"
#include "output/driver.h"

#include "gl/c-ctype.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static int
parse_title (struct lexer *lexer, void (*set_title) (const char *))
{
  if (lex_token (lexer) == T_STRING)
    {
      set_title (lex_tokcstr (lexer));
      lex_get (lexer);
    }
  else
    {
      int start_ofs = lex_ofs (lexer);
      while (lex_token (lexer) != T_ENDCMD)
        lex_get (lexer);

      /* Get the raw representation of all the tokens, including any space
         between them, and use it as the title. */
      char *title = lex_ofs_representation (lexer, start_ofs,
                                            lex_ofs (lexer) - 1);
      set_title (title);
      free (title);
    }
  return CMD_SUCCESS;
}

int
cmd_title (struct lexer *lexer, struct dataset *ds UNUSED)
{
  return parse_title (lexer, output_set_title);
}

int
cmd_subtitle (struct lexer *lexer, struct dataset *ds UNUSED)
{
  return parse_title (lexer, output_set_subtitle);
}

/* Performs the FILE LABEL command. */
int
cmd_file_label (struct lexer *lexer, struct dataset *ds)
{
  if (!lex_force_string (lexer))
    return CMD_FAILURE;

  dict_set_label (dataset_dict (ds), lex_tokcstr (lexer));
  lex_get (lexer);

  return CMD_SUCCESS;
}

/* Performs the DOCUMENT command. */
int
cmd_document (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  char *trailer;

  if (!lex_force_string (lexer))
    return CMD_FAILURE;

  while (lex_is_string (lexer))
    {
      dict_add_document_line (dict, lex_tokcstr (lexer), true);
      lex_get (lexer);
    }

  trailer = xasprintf (_("   (Entered %s)"), get_start_date ());
  dict_add_document_line (dict, trailer, true);
  free (trailer);

  return CMD_SUCCESS;
}

/* Performs the ADD DOCUMENTS command. */
int
cmd_add_documents (struct lexer *lexer, struct dataset *ds)
{
  return cmd_document (lexer, ds);
}

/* Performs the DROP DOCUMENTS command. */
int
cmd_drop_documents (struct lexer *lexer UNUSED, struct dataset *ds)
{
  dict_clear_documents (dataset_dict (ds));
  return CMD_SUCCESS;
}
