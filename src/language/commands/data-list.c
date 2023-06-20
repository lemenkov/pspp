/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009, 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/data-in.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/settings.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/commands/data-parser.h"
#include "language/commands/data-reader.h"
#include "language/commands/file-handle.h"
#include "language/commands/inpt-pgm.h"
#include "language/commands/placement-parser.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/xsize.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* DATA LIST transformation data. */
struct data_list_trns
  {
    struct data_parser *parser; /* Parser. */
    struct dictionary *dict;    /* Dictionary. */
    struct dfm_reader *reader;  /* Data file reader. */
    struct variable *end;        /* Variable specified on END subcommand. */
  };

static bool parse_fixed (struct lexer *, struct dictionary *,
                         struct pool *, struct data_parser *);
static bool parse_free (struct lexer *, struct dictionary *,
                        struct pool *, struct data_parser *);

static const struct trns_class data_list_trns_class;

int
cmd_data_list (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = (in_input_program ()
                             ? dataset_dict (ds)
                             : dict_create (get_default_encoding ()));
  struct data_parser *parser = data_parser_create ();
  struct dfm_reader *reader = NULL;

  struct variable *end = NULL;
  struct file_handle *fh = NULL;

  char *encoding = NULL;
  int encoding_start = 0, encoding_end = 0;

  int table = -1;               /* Print table if nonzero, -1=undecided. */

  bool has_type = false;

  int end_start = 0, end_end = 0;
  while (lex_token (lexer) != T_SLASH)
    {
      if (lex_match_id (lexer, "FILE"))
        {
          lex_match (lexer, T_EQUALS);
          fh_unref (fh);
          fh = fh_parse (lexer, FH_REF_FILE | FH_REF_INLINE, NULL);
          if (fh == NULL)
            goto error;
        }
      else if (lex_match_id (lexer, "ENCODING"))
        {
          encoding_start = lex_ofs (lexer) - 1;
          lex_match (lexer, T_EQUALS);
          if (!lex_force_string (lexer))
            goto error;

          free (encoding);
          encoding = ss_xstrdup (lex_tokss (lexer));

          encoding_end = lex_ofs (lexer);
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "RECORDS"))
        {
          if (data_parser_get_records (parser) > 0)
            {
              lex_sbc_only_once (lexer, "RECORDS");
              goto error;
            }
          lex_match (lexer, T_EQUALS);
          lex_match (lexer, T_LPAREN);
          if (!lex_force_int_range (lexer, "RECORDS", 0, INT_MAX))
            goto error;
          data_parser_set_records (parser, lex_integer (lexer));
          lex_get (lexer);
          lex_match (lexer, T_RPAREN);
        }
      else if (lex_match_id (lexer, "SKIP"))
        {
          lex_match (lexer, T_EQUALS);
          if (!lex_force_int_range (lexer, "SKIP", 0, INT_MAX))
            goto error;
          data_parser_set_skip (parser, lex_integer (lexer));
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "END"))
        {
          if (!in_input_program ())
            {
              lex_next_error (lexer, -1, -1,
                              _("The %s subcommand may only be used within %s."),
                              "END", "INPUT PROGRAM");
              goto error;
            }
          if (end)
            {
              lex_sbc_only_once (lexer, "END");
              goto error;
            }

          end_start = lex_ofs (lexer) - 1;
          lex_match (lexer, T_EQUALS);
          if (!lex_force_id (lexer))
            goto error;
          end_end = lex_ofs (lexer);

          end = dict_lookup_var (dict, lex_tokcstr (lexer));
          if (!end)
            end = dict_create_var_assert (dict, lex_tokcstr (lexer), 0);
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "NOTABLE"))
        table = 0;
      else if (lex_match_id (lexer, "TABLE"))
        table = 1;
      else if (lex_token (lexer) == T_ID)
        {
          if (lex_match_id (lexer, "FIXED"))
            data_parser_set_type (parser, DP_FIXED);
          else if (lex_match_id (lexer, "FREE"))
            {
              data_parser_set_type (parser, DP_DELIMITED);
              data_parser_set_span (parser, true);
            }
          else if (lex_match_id (lexer, "LIST"))
            {
              data_parser_set_type (parser, DP_DELIMITED);
              data_parser_set_span (parser, false);
            }
          else
            {
              lex_error_expecting (lexer, "FILE", "ENCODING", "RECORDS",
                                   "SKIP", "END", "NOTABLE", "TABLE",
                                   "FIXED", "FREE", "LIST");
              goto error;
            }

          if (has_type)
            {
              lex_next_error (lexer, -1, -1,
                              _("Only one of FIXED, FREE, or LIST may "
                                "be specified."));
              goto error;
            }
          has_type = true;

          if (data_parser_get_type (parser) == DP_DELIMITED)
            {
              if (lex_match (lexer, T_LPAREN))
                {
                  struct string delims = DS_EMPTY_INITIALIZER;

                  do
                    {
                      int delim;

                      if (lex_match_id (lexer, "TAB"))
                        delim = '\t';
                      else if (lex_is_string (lexer)
                               && ss_length (lex_tokss (lexer)) == 1)
                        {
                          delim = ss_first (lex_tokss (lexer));
                          lex_get (lexer);
                        }
                      else
                        {
                          /* XXX should support multibyte UTF-8 characters */
                          lex_error (lexer, _("Syntax error expecting TAB "
                                              "or delimiter string."));
                          ds_destroy (&delims);
                          goto error;
                        }
                      ds_put_byte (&delims, delim);

                      lex_match (lexer, T_COMMA);
                    }
                  while (!lex_match (lexer, T_RPAREN));

                  data_parser_set_empty_line_has_field (parser, true);
                  data_parser_set_quotes (parser, ss_empty ());
                  data_parser_set_soft_delimiters (parser, ss_empty ());
                  data_parser_set_hard_delimiters (parser, ds_ss (&delims));
                  ds_destroy (&delims);
                }
              else
                {
                  data_parser_set_empty_line_has_field (parser, false);
                  data_parser_set_quotes (parser, ss_cstr ("'\""));
                  data_parser_set_soft_delimiters (parser,
                                                   ss_cstr (CC_SPACES));
                  const char decimal = settings_get_fmt_settings ()->decimal;
                  data_parser_set_hard_delimiters (parser,
                                                   ss_buffer (",", (decimal == '.') ? 1 : 0));
                }
            }
        }
      else
        {
          lex_error_expecting (lexer, "FILE", "ENCODING", "RECORDS",
                               "SKIP", "END", "NOTABLE", "TABLE",
                               "FIXED", "FREE", "LIST");
          goto error;
        }
    }

  if (!fh)
    {
      fh = fh_inline_file ();

      if (encoding)
        lex_ofs_msg (lexer, SW, encoding_start, encoding_end,
                     _("Encoding should not be specified for inline data. "
                       "It will be ignored."));
    }
  fh_set_default_handle (fh);

  enum data_parser_type type = data_parser_get_type (parser);
  if (type != DP_FIXED && end != NULL)
    {
      lex_ofs_error (lexer, end_start, end_end,
                     _("The %s subcommand may be used only with %s."),
                     "END", "DATA LIST FIXED");
      goto error;
    }

  struct pool *tmp_pool = pool_create ();
  bool ok = (type == DP_FIXED
             ? parse_fixed (lexer, dict, tmp_pool, parser)
             : parse_free (lexer, dict, tmp_pool, parser));
  pool_destroy (tmp_pool);
  if (!ok)
    goto error;
  assert (data_parser_any_fields (parser));

  if (lex_end_of_command (lexer) != CMD_SUCCESS)
    goto error;

  if (table == -1)
    table = type == DP_FIXED || !data_parser_get_span (parser);
  if (table)
    data_parser_output_description (parser, fh);

  reader = dfm_open_reader (fh, lexer, encoding);
  if (reader == NULL)
    goto error;

  if (in_input_program ())
    {
      struct data_list_trns *trns = xmalloc (sizeof *trns);
      *trns = (struct data_list_trns) {
        .parser = parser,
        .dict = dict_ref (dict),
        .reader = reader,
        .end = end,
      };
      add_transformation (ds, &data_list_trns_class, trns);
    }
  else
    data_parser_make_active_file (parser, ds, reader, dict, NULL, NULL);

  fh_unref (fh);
  free (encoding);

  data_list_seen ();

  return CMD_SUCCESS;

 error:
  data_parser_destroy (parser);
  if (!in_input_program ())
    dict_unref (dict);
  fh_unref (fh);
  free (encoding);
  return CMD_CASCADING_FAILURE;
}

/* Fixed-format parsing. */

/* Parses all the variable specifications for DATA LIST FIXED,
   storing them into DLS.  Uses TMP_POOL for temporary storage;
   the caller may destroy it.  Returns true only if
   successful. */
static bool
parse_fixed (struct lexer *lexer, struct dictionary *dict,
             struct pool *tmp_pool, struct data_parser *parser)
{
  int max_records = data_parser_get_records (parser);
  int record = 0;
  int column = 1;

  int start = lex_ofs (lexer);
  while (lex_token (lexer) != T_ENDCMD)
    {
      if (lex_match (lexer, T_SLASH))
        {
          int records_start = lex_ofs (lexer) - 1;
          if (lex_is_number (lexer))
            {
              if (!lex_force_int_range (lexer, NULL, record + 1, INT_MAX))
                return false;
              record = lex_integer (lexer);
              lex_get (lexer);
            }
          else
            record++;
          column = 1;

          if (max_records && record > max_records)
            {
              lex_ofs_error (lexer, records_start, lex_ofs (lexer) - 1,
                             _("Cannot advance to record %d when "
                               "RECORDS=%d is specified."),
                             record, data_parser_get_records (parser));
              return false;
            }
          if (record > data_parser_get_records (parser))
            data_parser_set_records (parser, record);

          continue;
        }

      int vars_start = lex_ofs (lexer);
      char **names;
      size_t n_names;
      if (!parse_DATA_LIST_vars_pool (lexer, dict, tmp_pool,
                                      &names, &n_names, PV_NONE))
        return false;
      int vars_end = lex_ofs (lexer) - 1;
      struct fmt_spec *formats;
      size_t n_formats;
      if (!parse_var_placements (lexer, tmp_pool, n_names, FMT_FOR_INPUT,
                                 &formats, &n_formats))
        return false;
      int placements_end = lex_ofs (lexer) - 1;

      /* Create variables and var specs. */
      size_t name_idx = 0;
      for (struct fmt_spec *f = formats; f < &formats[n_formats]; f++)
        if (!execute_placement_format (*f, &record, &column))
          {
            /* Create variable. */
            const char *name = names[name_idx++];
            int width = fmt_var_width (*f);
            struct variable *v = dict_create_var (dict, name, width);
            if (v != NULL)
              {
                /* Success. */
                struct fmt_spec output = fmt_for_output_from_input (
                  *f, settings_get_fmt_settings ());
                var_set_both_formats (v, output);
              }
            else
              {
                /* Failure.
                   This can be acceptable if we're in INPUT
                   PROGRAM, but only if the existing variable has
                   the same width as the one we would have
                   created. */
                if (!in_input_program ())
                  {
                    lex_ofs_error (lexer, vars_start, vars_end,
                                   _("%s is a duplicate variable name."), name);
                    return false;
                  }

                v = dict_lookup_var_assert (dict, name);
                if ((width != 0) != (var_get_width (v) != 0))
                  {
                    lex_ofs_error (lexer, vars_start, placements_end,
                                   _("There is already a variable %s of a "
                                     "different type."), name);
                    return false;
                  }
                if (width != 0 && width != var_get_width (v))
                  {
                    lex_ofs_error (lexer, vars_start, placements_end,
                                   _("There is already a string variable %s of "
                                     "a different width."), name);
                    return false;
                  }
              }

            if (max_records && record > max_records)
              {
                lex_ofs_error (lexer, vars_start, placements_end,
                               _("Cannot place variable %s on record %d when "
                                 "RECORDS=%d is specified."),
                               var_get_name (v), record,
                               data_parser_get_records (parser));
                return false;
              }

            data_parser_add_fixed_field (parser, *f,
                                         var_get_dict_index (v),
                                         var_get_name (v), record, column);

            column += f->w;
          }
      assert (name_idx == n_names);
    }

  if (!data_parser_any_fields (parser))
    {
      lex_ofs_error (lexer, start, lex_ofs (lexer) - 1,
                     _("No fields were specified.  "
                       "At least one is required."));
      return false;
    }

  return true;
}

/* Free-format parsing. */

/* Parses variable specifications for DATA LIST FREE and adds
   them to DLS.  Uses TMP_POOL for temporary storage; the caller
   may destroy it.  Returns true only if successful. */
static bool
parse_free (struct lexer *lexer, struct dictionary *dict,
            struct pool *tmp_pool, struct data_parser *parser)
{
  lex_get (lexer);
  do
    {
      char **names;
      size_t n_names;

      int vars_start = lex_ofs (lexer);
      if (!parse_DATA_LIST_vars_pool (lexer, dict, tmp_pool,
                                      &names, &n_names, PV_NONE))
        return false;
      int vars_end = lex_ofs (lexer) - 1;

      struct fmt_spec input, output;
      if (lex_match (lexer, T_LPAREN))
        {
          char type[FMT_TYPE_LEN_MAX + 1];

          if (!parse_abstract_format_specifier (lexer, type, &input.w,
                                                &input.d))
            return NULL;
          if (!fmt_from_name (type, &input.type))
            {
              lex_next_error (lexer, -1, -1,
                              _("Unknown format type `%s'."), type);
              return NULL;
            }

          /* If no width was included, use the minimum width for the type.
             This isn't quite right, because DATETIME by itself seems to become
             DATETIME20 (see bug #30690), whereas this will become
             DATETIME17.  The correct behavior is not documented. */
          if (input.w == 0)
            {
              input.w = fmt_min_input_width (input.type);
              input.d = 0;
            }

          char *error = fmt_check_input__ (input);
          if (error)
            {
              lex_next_error (lexer, -1, -1, "%s", error);
              free (error);
              return NULL;
            }
          if (!lex_force_match (lexer, T_RPAREN))
            return NULL;

          /* As a special case, N format is treated as F format
             for free-field input. */
          if (input.type == FMT_N)
            input.type = FMT_F;

          output = fmt_for_output_from_input (input,
                                              settings_get_fmt_settings ());
        }
      else
        {
          lex_match (lexer, T_ASTERISK);
          input = fmt_for_input (FMT_F, 8, 0);
          output = settings_get_format ();
        }

      for (size_t i = 0; i < n_names; i++)
        {
          struct variable *v = dict_create_var (dict, names[i],
                                                fmt_var_width (input));
          if (!v)
            {
              lex_ofs_error (lexer, vars_start, vars_end,
                             _("%s is a duplicate variable name."), names[i]);
              return false;
            }
          var_set_both_formats (v, output);

          data_parser_add_delimited_field (parser,
                                           input, var_get_dict_index (v),
                                           var_get_name (v));
        }
    }
  while (lex_token (lexer) != T_ENDCMD);

  return true;
}

/* Input procedure. */

/* Destroys DATA LIST transformation TRNS.
   Returns true if successful, false if an I/O error occurred. */
static bool
data_list_trns_free (void *trns_)
{
  struct data_list_trns *trns = trns_;
  data_parser_destroy (trns->parser);
  dfm_close_reader (trns->reader);
  dict_unref (trns->dict);
  free (trns);
  return true;
}

/* Handle DATA LIST transformation TRNS, parsing data into *C. */
static enum trns_result
data_list_trns_proc (void *trns_, struct ccase **c, casenumber case_num UNUSED)
{
  struct data_list_trns *trns = trns_;
  enum trns_result retval;

  *c = case_unshare (*c);
  if (data_parser_parse (trns->parser, trns->reader, trns->dict, *c))
    retval = TRNS_CONTINUE;
  else if (dfm_reader_error (trns->reader) || dfm_eof (trns->reader) > 1)
    {
      /* An I/O error, or encountering end of file for a second
         time, should be escalated into a more serious error. */
      retval = TRNS_ERROR;
    }
  else
    retval = TRNS_END_FILE;

  /* If there was an END subcommand handle it. */
  if (trns->end != NULL)
    {
      double *end = case_num_rw (*c, trns->end);
      if (retval == TRNS_END_FILE)
        {
          *end = 1.0;
          retval = TRNS_CONTINUE;
        }
      else
        *end = 0.0;
    }

  return retval;
}

static const struct trns_class data_list_trns_class = {
  .name = "DATA LIST",
  .execute = data_list_trns_proc,
  .destroy = data_list_trns_free,
};
