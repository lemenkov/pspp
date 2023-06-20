/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2010, 2011, 2020 Free Software Foundation, Inc.

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
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "data/dataset.h"
#include "data/session.h"
#include "language/command.h"
#include "language/lexer/include-path.h"
#include "language/lexer/lexer.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/dirname.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

enum variant
  {
    INSERT,
    INCLUDE
  };

static int
do_insert (struct lexer *lexer, struct dataset *ds, enum variant variant)
{
  /* Skip optional FILE=. */
  if (lex_match_id (lexer, "FILE"))
    lex_match (lexer, T_EQUALS);

  if (!lex_force_string_or_id (lexer))
    return CMD_FAILURE;

  char *relative_name = utf8_to_filename (lex_tokcstr (lexer));
  char *filename = include_path_search (relative_name);
  free (relative_name);

  if (!filename)
    {
      msg (SE, _("Can't find `%s' in include file search path."),
           lex_tokcstr (lexer));
      return CMD_FAILURE;
    }
  lex_get (lexer);

  enum segmenter_mode syntax_mode = SEG_MODE_INTERACTIVE;
  enum lex_error_mode error_mode = LEX_ERROR_CONTINUE;
  bool cd = false;
  int status = CMD_FAILURE;
  char *encoding = xstrdup (session_get_default_syntax_encoding (
                              dataset_session (ds)));
  while (T_ENDCMD != lex_token (lexer))
    {
      if (lex_match_id (lexer, "ENCODING"))
        {
          lex_match (lexer, T_EQUALS);
          if (!lex_force_string (lexer))
            goto exit;

          free (encoding);
          encoding = xstrdup (lex_tokcstr (lexer));
          lex_get (lexer);
        }
      else if (variant == INSERT && lex_match_id (lexer, "SYNTAX"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "INTERACTIVE"))
            syntax_mode = SEG_MODE_INTERACTIVE;
          else if (lex_match_id (lexer, "BATCH"))
            syntax_mode = SEG_MODE_BATCH;
          else if (lex_match_id (lexer, "AUTO"))
            syntax_mode = SEG_MODE_AUTO;
          else
            {
              lex_error_expecting (lexer, "BATCH", "INTERACTIVE", "AUTO");
              goto exit;
            }
        }
      else if (variant == INSERT && lex_match_id (lexer, "CD"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "YES"))
            cd = true;
          else if (lex_match_id (lexer, "NO"))
            cd = false;
          else
            {
              lex_error_expecting (lexer, "YES", "NO");
              goto exit;
            }
        }
      else if (variant == INSERT && lex_match_id (lexer, "ERROR"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "CONTINUE"))
            error_mode = LEX_ERROR_CONTINUE;
          else if (lex_match_id (lexer, "STOP"))
            error_mode = LEX_ERROR_STOP;
          else if (settings_get_testing_mode ()
                   && lex_match_id (lexer, "IGNORE"))
            error_mode = LEX_ERROR_IGNORE;
          else
            {
              lex_error_expecting (lexer, "CONTINUE", "STOP");
              goto exit;
            }
        }
      else
        {
          if (variant == INSERT)
            lex_error_expecting (lexer, "ENCODING", "SYNTAX", "CD", "ERROR");
          else
            lex_error_expecting (lexer, "ENCODING");
          goto exit;
        }
    }
  status = lex_end_of_command (lexer);

  if (status == CMD_SUCCESS)
    {
      struct lex_reader *reader = lex_reader_for_file (filename, encoding,
                                                       syntax_mode, error_mode);
      if (reader != NULL)
        {
          lex_discard_rest_of_command (lexer);
          lex_include (lexer, reader);

          if (cd)
            {
              char *directory = dir_name (filename);
              if (chdir (directory))
                {
                  int err = errno;
                  msg (SE, _("Cannot change directory to %s: %s"), directory,
                       strerror (err));
                  status = CMD_FAILURE;
                }

              free (directory);
            }
        }
    }

exit:
  free (encoding);
  free (filename);
  return status;
}

int
cmd_include (struct lexer *lexer, struct dataset *ds)
{
  return do_insert (lexer, ds, INCLUDE);
}

int
cmd_insert (struct lexer *lexer, struct dataset *ds)
{
  return do_insert (lexer, ds, INSERT);
}

