/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2008, 2009, 2010, 2011, 2012,
                 2013, 2015, 2016 Free Software Foundation, Inc.

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

#include <string.h>

#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/gnumeric-reader.h"
#include "data/ods-reader.h"
#include "data/spreadsheet-reader.h"
#include "data/psql-reader.h"
#include "data/settings.h"
#include "language/command.h"
#include "language/data-io/data-parser.h"
#include "language/data-io/data-reader.h"
#include "language/data-io/file-handle.h"
#include "language/data-io/placement-parser.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "libpspp/cast.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

static bool parse_spreadsheet (struct lexer *lexer, char **filename,
			       struct spreadsheet_read_options *opts);

static void destroy_spreadsheet_read_info (struct spreadsheet_read_options *);

static int parse_get_txt (struct lexer *lexer, struct dataset *);
static int parse_get_psql (struct lexer *lexer, struct dataset *);

int
cmd_get_data (struct lexer *lexer, struct dataset *ds)
{
  char *tok = NULL;
  struct spreadsheet_read_options opts;

  opts.sheet_name = NULL;
  opts.sheet_index = -1;
  opts.cell_range = NULL;
  opts.read_names = false;
  opts.asw = -1;

  if (! lex_force_match (lexer, T_SLASH))
    goto error;

  if (!lex_force_match_id (lexer, "TYPE"))
    goto error;

  if (!lex_force_match (lexer, T_EQUALS))
    goto error;

  const char *s = lex_tokcstr (lexer);

  if (s)
    tok = strdup (s);

  if (lex_match_id (lexer, "TXT"))
    {
      free (tok);
      return parse_get_txt (lexer, ds);
    }
  else if (lex_match_id (lexer, "PSQL"))
    {
      free (tok);
      return parse_get_psql (lexer, ds);
    }
  else if (lex_match_id (lexer, "GNM") ||
      lex_match_id (lexer, "ODS"))
    {
      char *filename = NULL;
      if (!parse_spreadsheet (lexer, &filename, &opts))
	goto error;

      struct spreadsheet *spreadsheet = NULL;
      if (0 == strncasecmp (tok, "GNM", 3))
        spreadsheet = gnumeric_probe (filename, true);
      else if (0 == strncasecmp (tok, "ODS", 3))
        spreadsheet = ods_probe (filename, true);

      if (spreadsheet == NULL)
        {
          msg (SE, _("error reading file `%s'"), filename);
          free (filename);
          goto error;
        }
      free (filename);

      struct casereader *reader = spreadsheet_make_reader (spreadsheet, &opts);
      if (reader)
	{
	  dataset_set_dict (ds, dict_clone (spreadsheet->dict));
	  dataset_set_source (ds, reader);
	  free (tok);
	  destroy_spreadsheet_read_info (&opts);
	  spreadsheet_unref (spreadsheet);
	  return CMD_SUCCESS;
	}
      spreadsheet_unref (spreadsheet);
    }
  else
    lex_error_expecting (lexer, "TXT", "PSQL", "GNM", "ODS");

 error:
  destroy_spreadsheet_read_info (&opts);
  free (tok);
  return CMD_FAILURE;
}

static int
parse_get_psql (struct lexer *lexer, struct dataset *ds)
{
  struct psql_read_info psql;
  psql.allow_clear = false;
  psql.conninfo = NULL;
  psql.str_width = -1;
  psql.bsize = -1;
  ds_init_empty (&psql.sql);

  if (! lex_force_match (lexer, T_SLASH))
    goto error;

  if (!lex_force_match_id (lexer, "CONNECT"))
    goto error;

  if (! lex_force_match (lexer, T_EQUALS))
    goto error;

  if (!lex_force_string (lexer))
    goto error;

  psql.conninfo = ss_xstrdup (lex_tokss (lexer));

  lex_get (lexer);

  while (lex_match (lexer, T_SLASH))
    {
      if (lex_match_id (lexer, "ASSUMEDSTRWIDTH"))
	{
	  lex_match (lexer, T_EQUALS);
          if (lex_force_int_range (lexer, "ASSUMEDSTRWIDTH", 1, 32767))
            {
              psql.str_width = lex_integer (lexer);
              lex_get (lexer);
            }
	}
      else if (lex_match_id (lexer, "BSIZE"))
	{
	  lex_match (lexer, T_EQUALS);
          if (lex_force_int_range (lexer, "BSIZE", 1, INT_MAX))
            {
              psql.bsize = lex_integer (lexer);
              lex_get (lexer);
            }
	}
      else if (lex_match_id (lexer, "UNENCRYPTED"))
	{
	  psql.allow_clear = true;
	}
      else if (lex_match_id (lexer, "SQL"))
	{
	  lex_match (lexer, T_EQUALS);
	  if (! lex_force_string (lexer))
	    goto error;

	  ds_put_substring (&psql.sql, lex_tokss (lexer));
	  lex_get (lexer);
	}
     }
  {
    struct dictionary *dict = NULL;
    struct casereader *reader = psql_open_reader (&psql, &dict);

    if (reader)
      {
        dataset_set_dict (ds, dict);
        dataset_set_source (ds, reader);
      }
  }

  ds_destroy (&psql.sql);
  free (psql.conninfo);

  return CMD_SUCCESS;

 error:

  ds_destroy (&psql.sql);
  free (psql.conninfo);

  return CMD_FAILURE;
}

static bool
parse_spreadsheet (struct lexer *lexer, char **filename,
		   struct spreadsheet_read_options *opts)
{
  opts->sheet_index = 1;
  opts->sheet_name = NULL;
  opts->cell_range = NULL;
  opts->read_names = true;
  opts->asw = -1;

  if (! lex_force_match (lexer, T_SLASH))
    goto error;

  if (!lex_force_match_id (lexer, "FILE"))
    goto error;

  if (! lex_force_match (lexer, T_EQUALS))
    goto error;

  if (!lex_force_string (lexer))
    goto error;

  *filename  = utf8_to_filename (lex_tokcstr (lexer));

  lex_get (lexer);

  while (lex_match (lexer, T_SLASH))
    {
      if (lex_match_id (lexer, "ASSUMEDSTRWIDTH"))
	{
	  lex_match (lexer, T_EQUALS);
          if (lex_force_int_range (lexer, "ASSUMEDSTRWIDTH", 1, 32767))
            {
              opts->asw = lex_integer (lexer);
              lex_get (lexer);
            }
	}
      else if (lex_match_id (lexer, "SHEET"))
	{
	  lex_match (lexer, T_EQUALS);
	  if (lex_match_id (lexer, "NAME"))
	    {
	      if (! lex_force_string (lexer))
		goto error;

	      opts->sheet_name = ss_xstrdup (lex_tokss (lexer));
	      opts->sheet_index = -1;

	      lex_get (lexer);
	    }
	  else if (lex_match_id (lexer, "INDEX"))
	    {
              if (!lex_force_int_range (lexer, "INDEX", 1, INT_MAX))
                goto error;
	      opts->sheet_index = lex_integer (lexer);
	      lex_get (lexer);
	    }
	  else
	    {
              lex_error_expecting (lexer, "NAME", "INDEX");
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "CELLRANGE"))
	{
	  lex_match (lexer, T_EQUALS);

	  if (lex_match_id (lexer, "FULL"))
	    {
	      opts->cell_range = NULL;
	    }
	  else if (lex_match_id (lexer, "RANGE"))
	    {
	      if (! lex_force_string (lexer))
		goto error;

	      opts->cell_range = ss_xstrdup (lex_tokss (lexer));
	      lex_get (lexer);
	    }
	  else
	    {
              lex_error_expecting (lexer, "FULL", "RANGE");
	      goto error;
	    }
	}
      else if (lex_match_id (lexer, "READNAMES"))
	{
	  lex_match (lexer, T_EQUALS);

	  if (lex_match_id (lexer, "ON"))
	    {
	      opts->read_names = true;
	    }
	  else if (lex_match_id (lexer, "OFF"))
	    {
	      opts->read_names = false;
	    }
	  else
	    {
              lex_error_expecting (lexer, "ON", "OFF");
	      goto error;
	    }
	}
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }

  return true;

 error:
  return false;
}


static bool
set_type (struct data_parser *parser, const char *subcommand,
          enum data_parser_type type, bool *has_type)
{
  if (!*has_type)
    {
      data_parser_set_type (parser, type);
      *has_type = true;
    }
  else if (type != data_parser_get_type (parser))
    {
      msg (SE, _("%s is allowed only with %s arrangement, but %s arrangement "
                 "was stated or implied earlier in this command."),
           subcommand,
           type == DP_FIXED ? "FIXED" : "DELIMITED",
           type == DP_FIXED ? "DELIMITED" : "FIXED");
      return false;
    }
  return true;
}

static int
parse_get_txt (struct lexer *lexer, struct dataset *ds)
{
  struct data_parser *parser = NULL;
  struct dictionary *dict = dict_create (get_default_encoding ());
  struct file_handle *fh = NULL;
  struct dfm_reader *reader = NULL;
  char *encoding = NULL;
  char *name = NULL;

  int record;
  enum data_parser_type type;
  bool has_type;

  if (! lex_force_match (lexer, T_SLASH))
    goto error;

  if (!lex_force_match_id (lexer, "FILE"))
    goto error;
  if (! lex_force_match (lexer, T_EQUALS))
    goto error;
  fh = fh_parse (lexer, FH_REF_FILE | FH_REF_INLINE, NULL);
  if (fh == NULL)
    goto error;

  parser = data_parser_create ();
  has_type = false;
  data_parser_set_type (parser, DP_DELIMITED);
  data_parser_set_span (parser, false);
  data_parser_set_quotes (parser, ss_empty ());
  data_parser_set_quote_escape (parser, true);
  data_parser_set_empty_line_has_field (parser, true);

  for (;;)
    {
      if (!lex_force_match (lexer, T_SLASH))
        goto error;

      if (lex_match_id (lexer, "ENCODING"))
	{
	  lex_match (lexer, T_EQUALS);
	  if (!lex_force_string (lexer))
	    goto error;

          free (encoding);
          encoding = ss_xstrdup (lex_tokss (lexer));

	  lex_get (lexer);
	}
      else if (lex_match_id (lexer, "ARRANGEMENT"))
        {
          bool ok;

	  lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "FIXED"))
            ok = set_type (parser, "ARRANGEMENT=FIXED", DP_FIXED, &has_type);
          else if (lex_match_id (lexer, "DELIMITED"))
            ok = set_type (parser, "ARRANGEMENT=DELIMITED",
                           DP_DELIMITED, &has_type);
          else
            {
              lex_error_expecting (lexer, "FIXED", "DELIMITED");
              goto error;
            }
          if (!ok)
            goto error;
        }
      else if (lex_match_id (lexer, "FIRSTCASE"))
        {
	  lex_match (lexer, T_EQUALS);
          if (!lex_force_int_range (lexer, "FIRSTCASE", 1, INT_MAX))
            goto error;
          data_parser_set_skip (parser, lex_integer (lexer) - 1);
          lex_get (lexer);
        }
      else if (lex_match_id_n (lexer, "DELCASE", 4))
        {
          if (!set_type (parser, "DELCASE", DP_DELIMITED, &has_type))
            goto error;
          lex_match (lexer, T_EQUALS);
          if (lex_match_id (lexer, "LINE"))
            data_parser_set_span (parser, false);
          else if (lex_match_id (lexer, "VARIABLES"))
            {
              data_parser_set_span (parser, true);

              /* VARIABLES takes an integer argument, but for no
                 good reason.  We just ignore it. */
              if (!lex_force_int (lexer))
                goto error;
              lex_get (lexer);
            }
          else
            {
              lex_error_expecting (lexer, "LINE", "VARIABLES");
              goto error;
            }
        }
      else if (lex_match_id (lexer, "FIXCASE"))
        {
          if (!set_type (parser, "FIXCASE", DP_FIXED, &has_type))
            goto error;
          lex_match (lexer, T_EQUALS);
          if (!lex_force_int_range (lexer, "FIXCASE", 1, INT_MAX))
            goto error;
          data_parser_set_records (parser, lex_integer (lexer));
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "IMPORTCASES"))
        {
          int start_ofs = lex_ofs (lexer) - 1;
          lex_match (lexer, T_EQUALS);
          if (lex_match (lexer, T_ALL))
            {
              /* Nothing to do. */
            }
          else if (lex_match_id (lexer, "FIRST"))
            {
              if (!lex_force_int (lexer))
                goto error;
              lex_get (lexer);
            }
          else if (lex_match_id (lexer, "PERCENT"))
            {
              if (!lex_force_int (lexer))
                goto error;
              lex_get (lexer);
            }
          lex_ofs_msg (lexer, SW, start_ofs, lex_ofs (lexer) - 1,
                       _("Ignoring obsolete IMPORTCASES subcommand.  (N OF "
                         "CASES or SAMPLE may be used to substitute.)"));
        }
      else if (lex_match_id_n (lexer, "DELIMITERS", 4))
        {
          struct string hard_seps = DS_EMPTY_INITIALIZER;
          const char *soft_seps = "";
          struct substring s;
          int c;

          if (!set_type (parser, "DELIMITERS", DP_DELIMITED, &has_type))
            goto error;
          lex_match (lexer, T_EQUALS);

          if (!lex_force_string (lexer))
            goto error;

          /* XXX should support multibyte UTF-8 characters */
          s = lex_tokss (lexer);
          if (ss_match_string (&s, ss_cstr ("\\t")))
            ds_put_cstr (&hard_seps, "\t");
          if (ss_match_string (&s, ss_cstr ("\\\\")))
            ds_put_cstr (&hard_seps, "\\");
          while ((c = ss_get_byte (&s)) != EOF)
            if (c == ' ')
              soft_seps = " ";
            else
              ds_put_byte (&hard_seps, c);
          data_parser_set_soft_delimiters (parser, ss_cstr (soft_seps));
          data_parser_set_hard_delimiters (parser, ds_ss (&hard_seps));
          ds_destroy (&hard_seps);

          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "QUALIFIERS"))
        {
          if (!set_type (parser, "QUALIFIERS", DP_DELIMITED, &has_type))
            goto error;
          lex_match (lexer, T_EQUALS);

          if (!lex_force_string (lexer))
            goto error;

          /* XXX should support multibyte UTF-8 characters */
          if (settings_get_syntax () == COMPATIBLE
              && ss_length (lex_tokss (lexer)) != 1)
            {
              lex_error (lexer, _("In compatible syntax mode, the QUALIFIER "
                                  "string must contain exactly one character."));
              goto error;
            }

          data_parser_set_quotes (parser, lex_tokss (lexer));
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "VARIABLES"))
        break;
      else
        {
          lex_error_expecting (lexer, "VARIABLES");
          goto error;
        }
    }
  lex_match (lexer, T_EQUALS);

  record = 1;
  type = data_parser_get_type (parser);
  do
    {
      struct fmt_spec input, output;
      struct variable *v;
      int fc, lc;

      while (type == DP_FIXED && lex_match (lexer, T_SLASH))
        {
          if (!lex_force_int_range (lexer, NULL, record,
                                    data_parser_get_records (parser)))
            goto error;
          record = lex_integer (lexer);
          lex_get (lexer);
        }

      int name_ofs = lex_ofs (lexer);
      if (!lex_force_id (lexer))
        goto error;
      name = xstrdup (lex_tokcstr (lexer));
      char *error = dict_id_is_valid__ (dict, name);
      if (error)
        {
          lex_error (lexer, "%s", error);
          free (error);
      	  goto error;
      	}
      lex_get (lexer);
      if (type == DP_DELIMITED)
        {
          if (!parse_format_specifier (lexer, &input)
	      || !fmt_check_input (&input))
	    {
	      goto error;
	    }
          output = fmt_for_output_from_input (&input,
                                              settings_get_fmt_settings ());
        }
      else
        {
          char fmt_type_name[FMT_TYPE_LEN_MAX + 1];
          enum fmt_type fmt_type;
          uint16_t w;
          uint8_t d;

          if (!parse_column_range (lexer, 0, &fc, &lc, NULL))
            goto error;

          /* Accept a format (e.g. F8.2) or just a type name (e.g. DOLLAR).  */
          if (!parse_abstract_format_specifier (lexer, fmt_type_name, &w, &d))
            goto error;
          if (!fmt_from_name (fmt_type_name, &fmt_type))
            {
              lex_next_error (lexer, -1, -1,
                              _("Unknown format type `%s'."), fmt_type_name);
              goto error;
            }
          /* Compose input format. */
          input.type = fmt_type;
          input.w = lc - fc + 1;
          input.d = 0;
          if (!fmt_check_input (&input))
            goto error;
          /* Compose output format. */
          if (w != 0)
            {
              output.type = fmt_type;
              output.w = w;
              output.d = d;
              if (!fmt_check_output (&output))
                goto error;
            }
          else
            output = fmt_for_output_from_input (&input,
                                                settings_get_fmt_settings ());
        }
      v = dict_create_var (dict, name, fmt_var_width (&input));
      if (v == NULL)
        {
          lex_ofs_error (lexer, name_ofs, name_ofs,
                         _("%s is a duplicate variable name."), name);
          goto error;
        }
      var_set_both_formats (v, &output);
      if (type == DP_DELIMITED)
        data_parser_add_delimited_field (parser, &input,
                                         var_get_case_index (v),
                                         name);
      else
        data_parser_add_fixed_field (parser, &input, var_get_case_index (v),
                                     name, record, fc);
      free (name);
      name = NULL;
    }
  while (lex_token (lexer) != T_ENDCMD);

  reader = dfm_open_reader (fh, lexer, encoding);
  if (reader == NULL)
    goto error;

  data_parser_make_active_file (parser, ds, reader, dict, NULL, NULL);
  fh_unref (fh);
  free (encoding);
  return CMD_SUCCESS;

 error:
  data_parser_destroy (parser);
  dict_unref (dict);
  fh_unref (fh);
  free (name);
  free (encoding);
  return CMD_CASCADING_FAILURE;
}


static void
destroy_spreadsheet_read_info (struct spreadsheet_read_options *opts)
{
  free (opts->cell_range);
  free (opts->sheet_name);
}
