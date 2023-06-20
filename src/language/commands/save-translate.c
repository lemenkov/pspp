/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2013, 2016 Free Software Foundation, Inc.

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

#include "data/case-map.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/csv-file-writer.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/file-name.h"
#include "data/format.h"
#include "data/settings.h"
#include "language/command.h"
#include "language/commands/file-handle.h"
#include "language/commands/trim.h"
#include "language/lexer/lexer.h"
#include "libpspp/message.h"


#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

int
cmd_save_translate (struct lexer *lexer, struct dataset *ds)
{
  enum { CSV_FILE = 1, TAB_FILE } type = 0;

  struct dictionary *dict = dict_clone (dataset_dict (ds));
  dict_set_names_must_be_ids (dict, false);

  struct case_map_stage *stage = case_map_stage_create (dict);
  dict_delete_scratch_vars (dict);

  struct file_handle *handle = NULL;

  bool replace = false;

  bool retain_unselected = true;
  bool recode_user_missing = false;
  bool include_var_names = false;
  bool use_value_labels = false;
  bool use_print_formats = false;
  char decimal = settings_get_fmt_settings ()->decimal;
  char delimiter = 0;
  char qualifier = '"';

  int outfile_start = 0;
  int outfile_end = 0;

  lex_match (lexer, T_SLASH);
  for (;;)
    {
      if (lex_match_id (lexer, "OUTFILE"))
        {
          outfile_start = lex_ofs (lexer) - 1;
          if (handle != NULL)
            {
              lex_sbc_only_once (lexer, "OUTFILE");
              goto error;
            }

          lex_match (lexer, T_EQUALS);

          handle = fh_parse (lexer, FH_REF_FILE, NULL);
          if (handle == NULL)
            goto error;
          outfile_end = lex_ofs (lexer) - 1;
        }
      else if (lex_match_id (lexer, "TYPE"))
        {
          if (type != 0)
            {
              lex_sbc_only_once (lexer, "TYPE");
              goto error;
            }

          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "CSV"))
            type = CSV_FILE;
          else if (lex_match_id (lexer, "TAB"))
            type = TAB_FILE;
          else
            {
              lex_error_expecting (lexer, "CSV", "TAB");
              goto error;
            }
        }
      else if (lex_match_id (lexer, "REPLACE"))
        replace = true;
      else if (lex_match_id (lexer, "FIELDNAMES"))
        include_var_names = true;
      else if (lex_match_id (lexer, "MISSING"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "IGNORE"))
            recode_user_missing = false;
          else if (lex_match_id (lexer, "RECODE"))
            recode_user_missing = true;
          else
            {
              lex_error_expecting (lexer, "IGNORE", "RECODE");
              goto error;
            }
        }
      else if (lex_match_id (lexer, "CELLS"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "VALUES"))
            use_value_labels = false;
          else if (lex_match_id (lexer, "LABELS"))
            use_value_labels = true;
          else
            {
              lex_error_expecting (lexer, "VALUES", "LABELS");
              goto error;
            }
        }
      else if (lex_match_id (lexer, "TEXTOPTIONS"))
        {
          lex_match (lexer, T_EQUALS);
          for (;;)
            {
              if (lex_match_id (lexer, "DELIMITER"))
                {
                  lex_match (lexer, T_EQUALS);
                  if (!lex_force_string (lexer))
                    goto error;
                  /* XXX should support multibyte UTF-8 delimiters */
                  if (ss_length (lex_tokss (lexer)) != 1)
                    {
                      lex_error (lexer, _("The %s string must contain exactly "
                                          "one character."), "DELIMITER");
                      goto error;
                    }
                  delimiter = ss_first (lex_tokss (lexer));
                  lex_get (lexer);
                }
              else if (lex_match_id (lexer, "QUALIFIER"))
                {
                  lex_match (lexer, T_EQUALS);
                  if (!lex_force_string (lexer))
                    goto error;
                  /* XXX should support multibyte UTF-8 qualifiers */
                  if (ss_length (lex_tokss (lexer)) != 1)
                    {
                      lex_error (lexer, _("The %s string must contain exactly "
                                          "one character."), "QUALIFIER");
                      goto error;
                    }
                  qualifier = ss_first (lex_tokss (lexer));
                  lex_get (lexer);
                }
              else if (lex_match_id (lexer, "DECIMAL"))
                {
                  lex_match (lexer, T_EQUALS);
                  if (lex_match_id (lexer, "DOT"))
                    decimal = '.';
                  else if (lex_match_id (lexer, "COMMA"))
                    decimal = ',';
                  else
                    {
                      lex_error_expecting (lexer, "DOT", "COMMA");
                      goto error;
                    }
                }
              else if (lex_match_id (lexer, "FORMAT"))
                {
                  lex_match (lexer, T_EQUALS);
                  if (lex_match_id (lexer, "PLAIN"))
                    use_print_formats = false;
                  else if (lex_match_id (lexer, "VARIABLE"))
                    use_print_formats = true;
                  else
                    {
                      lex_error_expecting (lexer, "PLAIN", "VARIABLE");
                      goto error;
                    }
                }
              else
                break;
            }
        }
      else if (lex_match_id (lexer, "UNSELECTED"))
        {
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "RETAIN"))
            retain_unselected = true;
          else if (lex_match_id (lexer, "DELETE"))
            retain_unselected = false;
          else
            {
              lex_error_expecting (lexer, "RETAIN", "DELETE");
              goto error;
            }
        }
      else if (!parse_dict_trim (lexer, dict))
        goto error;

      if (lex_token (lexer) == T_ENDCMD)
        break;

      if (!lex_force_match (lexer, T_SLASH))
        goto error;
    }

  if (type == 0)
    {
      lex_sbc_missing (lexer, "TYPE");
      goto error;
    }
  else if (handle == NULL)
    {
      lex_sbc_missing (lexer, "OUTFILE");
      goto error;
    }
  else if (!replace && fn_exists (handle))
    {
      lex_ofs_error (lexer, outfile_start, outfile_end,
                     _("Output file `%s' exists but %s was not specified."),
                     fh_get_file_name (handle), "REPLACE");
      goto error;
    }

  dict_delete_scratch_vars (dict);

  struct csv_writer_options csv_opts = {
    .recode_user_missing = recode_user_missing,
    .include_var_names = include_var_names,
    .use_value_labels = use_value_labels,
    .use_print_formats = use_print_formats,
    .decimal = decimal,
    .delimiter = (delimiter ? delimiter
                  : type == TAB_FILE ? '\t'
                  : decimal == '.' ? ','
                  : ';'),
    .qualifier = qualifier,
  };
  struct casewriter *writer = csv_writer_open (handle, dict, &csv_opts);
  if (writer == NULL)
    goto error;
  fh_unref (handle);

  writer = case_map_create_output_translator (
    case_map_stage_to_case_map (stage), writer);
  dict_unref (dict);

  casereader_transfer (proc_open_filtering (ds, !retain_unselected), writer);
  bool ok = casewriter_destroy (writer);
  ok = proc_commit (ds) && ok;

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

error:
  case_map_stage_destroy (stage);
  fh_unref (handle);
  dict_unref (dict);
  return CMD_FAILURE;
}
