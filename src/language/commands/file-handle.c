/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-2000, 2006, 2010-2013, 2016 Free Software Foundation, Inc.

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

#include "data/file-handle-def.h"

#include <limits.h>
#include <errno.h>
#include <stdlib.h>

#include "data/file-name.h"
#include "data/session.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/commands/file-handle.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_file_handle (struct lexer *lexer, struct dataset *ds UNUSED)
{
  enum cmd_result result = CMD_CASCADING_FAILURE;
  char *handle_name = NULL;
  char *file_name = NULL;
  int lrecl = 0;
  int tabwidth = -1;
  enum { MODE_DEFAULT, MODE_CHARACTER, MODE_BINARY, MODE_IMAGE, MODE_360 }
      mode = MODE_DEFAULT;
  int ends = -1;
  enum { RECFORM_FIXED = 1, RECFORM_VARIABLE, RECFORM_SPANNED } recform = 0;
  char *encoding = NULL;

  if (!lex_force_id (lexer))
    goto exit;

  handle_name = xstrdup (lex_tokcstr (lexer));
  struct file_handle *fh = fh_from_id (handle_name);
  if (fh)
    {
      fh_unref (fh);
      lex_error (lexer, _("File handle %s is already defined.  "
                          "Use %s before redefining a file handle."),
                 handle_name, "CLOSE FILE HANDLE");
      goto exit;
    }

  lex_get (lexer);
  if (!lex_force_match (lexer, T_SLASH))
    goto exit;

  int mode_start = 0;
  int mode_end = 0;
  while (lex_token (lexer) != T_ENDCMD)
    {
      if (lex_match_id (lexer, "NAME"))
        {
          if (file_name)
            {
              lex_sbc_only_once (lexer, "NAME");
              goto exit;
            }

          lex_match (lexer, T_EQUALS);
          if (!lex_force_string (lexer))
            goto exit;
          free (file_name);
          file_name = ss_xstrdup (lex_tokss (lexer));
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "LRECL"))
        {
          if (lrecl)
            {
              lex_sbc_only_once (lexer, "LRECL");
              goto exit;
            }

          lex_match (lexer, T_EQUALS);
          if (!lex_force_int_range (lexer, "LRECL", 1, (1UL << 31) - 1))
            goto exit;
          lrecl = lex_integer (lexer);
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "TABWIDTH"))
        {
          if (tabwidth >= 0)
            {
              lex_sbc_only_once (lexer, "TABWIDTH");
              goto exit;
            }
          lex_match (lexer, T_EQUALS);

          if (!lex_force_int_range (lexer, "TABWIDTH", 1, INT_MAX))
            goto exit;
          tabwidth = lex_integer (lexer);
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "MODE"))
        {
          if (mode != MODE_DEFAULT)
            {
              lex_sbc_only_once (lexer, "MODE");
              goto exit;
            }
          mode_start = lex_ofs (lexer) - 1;
          lex_match (lexer, T_EQUALS);

          if (lex_match_id (lexer, "CHARACTER"))
            mode = MODE_CHARACTER;
          else if (lex_match_id (lexer, "BINARY"))
            mode = MODE_BINARY;
          else if (lex_match_id (lexer, "IMAGE"))
            mode = MODE_IMAGE;
          else if (lex_match_int (lexer, 360))
            mode = MODE_360;
          else
            {
              lex_error_expecting (lexer, "CHARACTER", "BINARY",
                                   "IMAGE", "360");
              goto exit;
            }
          mode_end = lex_ofs (lexer) - 1;
        }
      else if (lex_match_id (lexer, "ENDS"))
        {
          if (ends >= 0)
            {
              lex_sbc_only_once (lexer, "ENDS");
              goto exit;
            }
          lex_match (lexer, T_EQUALS);

          if (lex_match_id (lexer, "LF"))
            ends = FH_END_LF;
          else if (lex_match_id (lexer, "CRLF"))
            ends = FH_END_CRLF;
          else
            {
              lex_error_expecting (lexer, "LF", "CRLF");
              goto exit;
            }
        }
      else if (lex_match_id (lexer, "RECFORM"))
        {
          if (recform)
            {
              lex_sbc_only_once (lexer, "RECFORM");
              goto exit;
            }
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "FIXED") || lex_match_id (lexer, "F"))
            recform = RECFORM_FIXED;
          else if (lex_match_id (lexer, "VARIABLE")
                   || lex_match_id (lexer, "V"))
            recform = RECFORM_VARIABLE;
          else if (lex_match_id (lexer, "SPANNED")
                   || lex_match_id (lexer, "VS"))
            recform = RECFORM_SPANNED;
          else
            {
              lex_error_expecting (lexer, "FIXED", "VARIABLE", "SPANNED");
              goto exit;
            }
        }
      else if (lex_match_id (lexer, "ENCODING"))
        {
          if (encoding)
            {
              lex_sbc_only_once (lexer, "ENCODING");
              goto exit;
            }

          lex_match (lexer, T_EQUALS);
          if (!lex_force_string (lexer))
            goto exit;
          free (encoding);
          encoding = ss_xstrdup (lex_tokss (lexer));
          lex_get (lexer);
        }
      if (!lex_match (lexer, T_SLASH))
        break;
    }

  if (lex_end_of_command (lexer) != CMD_SUCCESS)
    goto exit;

  struct fh_properties properties = *fh_default_properties ();
  if (file_name == NULL)
    {
      lex_sbc_missing (lexer, "NAME");
      goto exit;
    }

  switch (mode)
    {
    case MODE_DEFAULT:
    case MODE_CHARACTER:
      properties.mode = FH_MODE_TEXT;
      if (tabwidth >= 0)
        properties.tab_width = tabwidth;
      if (ends)
        properties.line_ends = ends;
      break;
    case MODE_IMAGE:
      properties.mode = FH_MODE_FIXED;
      break;
    case MODE_BINARY:
      properties.mode = FH_MODE_VARIABLE;
      break;
    case MODE_360:
      properties.encoding = CONST_CAST (char *, "EBCDIC-US");
      if (recform == RECFORM_FIXED)
        properties.mode = FH_MODE_FIXED;
      else if (recform == RECFORM_VARIABLE)
        {
          properties.mode = FH_MODE_360_VARIABLE;
          properties.record_width = 8192;
        }
      else if (recform == RECFORM_SPANNED)
        {
          properties.mode = FH_MODE_360_SPANNED;
          properties.record_width = 8192;
        }
      else
        {
          lex_ofs_error (lexer, mode_start, mode_end,
                         _("%s must be specified with %s."),
                         "RECFORM", "MODE=360");
          goto exit;
        }
      break;
    default:
      NOT_REACHED ();
    }

  if (properties.mode == FH_MODE_FIXED || lrecl)
    {
      if (!lrecl)
        msg (SE, _("The specified file mode requires LRECL.  "
                   "Assuming %zu-character records."),
             properties.record_width);
      else
        properties.record_width = lrecl;
    }

  if (encoding)
    properties.encoding = encoding;

  fh_create_file (handle_name, file_name, lex_get_encoding (lexer),
                  &properties);

  result = CMD_SUCCESS;

exit:
  free (handle_name);
  free (file_name);
  free (encoding);
  return result;
}

int
cmd_close_file_handle (struct lexer *lexer, struct dataset *ds UNUSED)
{
  struct file_handle *handle;

  if (!lex_force_id (lexer))
    return CMD_CASCADING_FAILURE;
  handle = fh_from_id (lex_tokcstr (lexer));
  if (handle == NULL)
    {
      lex_next_error (lexer, 0, 0, _("No file handle named %s."),
                      lex_tokcstr (lexer));
      return CMD_CASCADING_FAILURE;
    }
  lex_get (lexer);

  fh_unname (handle);
  fh_unref (handle);
  return CMD_SUCCESS;
}

/* Returns the name for REFERENT. */
static const char *
referent_name (enum fh_referent referent)
{
  switch (referent)
    {
    case FH_REF_FILE:
      return _("file");
    case FH_REF_INLINE:
      return _("inline file");
    case FH_REF_DATASET:
      return _("dataset");
    default:
      NOT_REACHED ();
    }
}

/* Parses a file handle name:

      - If SESSION is nonnull, then the parsed syntax may be the name of a
        dataset within SESSION.  Dataset names take precedence over file handle
        names.

      - If REFERENT_MASK includes FH_REF_FILE, the parsed syntax may be a file
        name as a string or a file handle name as an identifier.

      - If REFERENT_MASK includes FH_REF_INLINE, the parsed syntax may be the
        identifier INLINE to represent inline data.

   Returns the file handle when successful, a null pointer on failure.

   The caller is responsible for fh_unref()'ing the returned file handle when
   it is no longer needed. */
struct file_handle *
fh_parse (struct lexer *lexer, enum fh_referent referent_mask,
          struct session *session)
{
  if (session != NULL && lex_token (lexer) == T_ID)
    {
      struct dataset *ds;

      ds = session_lookup_dataset (session, lex_tokcstr (lexer));
      if (ds != NULL)
        {
          lex_get (lexer);
          return fh_create_dataset (ds);
        }
    }

  int start_ofs = lex_ofs (lexer);
  struct file_handle *handle;
  if (lex_match_id (lexer, "INLINE"))
    handle = fh_inline_file ();
  else
    {
      if (lex_token (lexer) != T_ID && !lex_is_string (lexer))
        {
          lex_error (lexer,
                     _("Syntax error expecting a file name or handle name."));
          return NULL;
        }

      handle = NULL;
      if (lex_token (lexer) == T_ID)
        handle = fh_from_id (lex_tokcstr (lexer));
      if (handle == NULL)
        handle = fh_create_file (NULL, lex_tokcstr (lexer), lex_get_encoding (lexer),
                                     fh_default_properties ());
      lex_get (lexer);
    }

  if (!(fh_get_referent (handle) & referent_mask))
    {
      lex_ofs_error (lexer, start_ofs, lex_ofs (lexer) - 1,
                     _("Handle for %s not allowed here."),
                     referent_name (fh_get_referent (handle)));
      fh_unref (handle);
      return NULL;
    }

  return handle;
}
