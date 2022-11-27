/* PSPP - a program for statistical analysis.
   Copyright (C) 2005, 2009, 2010 Free Software Foundation, Inc.

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

#include "language/command.h"
#include "language/lexer/lexer.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "output/output-item.h"
#include "output/driver.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Echos a string to the output stream */
int
cmd_echo (struct lexer *lexer, struct dataset *ds UNUSED)
{
  if (!lex_force_string (lexer))
    return CMD_FAILURE;

  output_log ("%s", lex_tokcstr (lexer));
  lex_get (lexer);

  return CMD_SUCCESS;
}
