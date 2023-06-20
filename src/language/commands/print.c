/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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
#include <uniwidth.h>

#include "data/case.h"
#include "data/dataset.h"
#include "data/data-out.h"
#include "data/format.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/commands/data-writer.h"
#include "language/commands/file-handle.h"
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
#include "libpspp/u8-line.h"
#include "output/driver.h"
#include "output/pivot-table.h"
#include "output/output-item.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

/* Describes what to do when an output field is encountered. */
enum field_type
  {
    PRT_LITERAL,                /* Literal string. */
    PRT_VAR                        /* Variable. */
  };

/* Describes how to output one field. */
struct prt_out_spec
  {
    /* All fields. */
    enum field_type type;        /* What type of field this is. */
    int record;                 /* 1-based record number. */
    int first_column;                /* 0-based first column. */
    int start_ofs, end_ofs;

    /* PRT_VAR only. */
    const struct variable *var;        /* Associated variable. */
    struct fmt_spec format;        /* Output spec. */
    bool add_space;             /* Add trailing space? */
    bool sysmis_as_spaces;      /* Output SYSMIS as spaces? */

    /* PRT_LITERAL only. */
    struct substring string;    /* String to output. */
    int width;                  /* Width of 'string', in display columns. */
  };

/* PRINT, PRINT EJECT, WRITE private data structure. */
struct print_trns
  {
    struct pool *pool;          /* Stores related data. */
    bool eject;                 /* Eject page before printing? */
    bool include_prefix;        /* Prefix lines with space? */
    const char *encoding;       /* Encoding to use for output. */
    struct dfm_writer *writer;        /* Output file, NULL=listing file. */
    struct prt_out_spec *specs;
    size_t n_specs;
    size_t n_records;           /* Number of records to write. */
  };

enum which_formats
  {
    PRINT,
    WRITE
  };

static const struct trns_class print_binary_trns_class;
static const struct trns_class print_text_trns_class;

static int cmd_print__ (struct lexer *, struct dataset *,
                        enum which_formats, bool eject);
static bool parse_specs (struct lexer *, struct pool *tmp_pool,
                         struct print_trns *, int records_ofs,
                         struct dictionary *, enum which_formats);
static void dump_table (struct print_trns *);

static bool print_trns_free (void *trns_);

static const struct prt_out_spec *find_binary_spec (const struct print_trns *);

/* Basic parsing. */

/* Parses PRINT command. */
int
cmd_print (struct lexer *lexer, struct dataset *ds)
{
  return cmd_print__ (lexer, ds, PRINT, false);
}

/* Parses PRINT EJECT command. */
int
cmd_print_eject (struct lexer *lexer, struct dataset *ds)
{
  return cmd_print__ (lexer, ds, PRINT, true);
}

/* Parses WRITE command. */
int
cmd_write (struct lexer *lexer, struct dataset *ds)
{
  return cmd_print__ (lexer, ds, WRITE, false);
}

/* Parses the output commands. */
static int
cmd_print__ (struct lexer *lexer, struct dataset *ds,
             enum which_formats which_formats, bool eject)
{
  bool print_table = false;
  struct file_handle *fh = NULL;
  char *encoding = NULL;

  /* Fill in prt to facilitate error-handling. */
  struct pool *pool = pool_create ();
  struct print_trns *trns = pool_alloc (pool, sizeof *trns);
  *trns = (struct print_trns) { .pool = pool, .eject = eject };
  struct pool *tmp_pool = pool_create_subpool (trns->pool);

  /* Parse the command options. */
  int records_ofs = 0;
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
    {
      if (lex_match_id (lexer, "OUTFILE"))
        {
          lex_match (lexer, T_EQUALS);

          fh = fh_parse (lexer, FH_REF_FILE, NULL);
          if (fh == NULL)
            goto error;
        }
      else if (lex_match_id (lexer, "ENCODING"))
        {
          lex_match (lexer, T_EQUALS);
          if (!lex_force_string (lexer))
            goto error;

          free (encoding);
          encoding = ss_xstrdup (lex_tokss (lexer));

          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "RECORDS"))
        {
          lex_match (lexer, T_EQUALS);
          lex_match (lexer, T_LPAREN);
          if (!lex_force_int_range (lexer, "RECORDS", 0, INT_MAX))
            goto error;
          trns->n_records = lex_integer (lexer);
          records_ofs = lex_ofs (lexer);
          lex_get (lexer);
          lex_match (lexer, T_RPAREN);
        }
      else if (lex_match_id (lexer, "TABLE"))
        print_table = true;
      else if (lex_match_id (lexer, "NOTABLE"))
        print_table = false;
      else
        {
          lex_error_expecting (lexer, "OUTFILE", "ENCODING", "RECORDS",
                               "TABLE", "NOTABLE");
          goto error;
        }
    }

  /* When PRINT or PRINT EJECT writes to an external file, we
     prefix each line with a space for compatibility. */
  trns->include_prefix = which_formats == PRINT && fh != NULL;

  /* Parse variables and strings. */
  if (!parse_specs (lexer, tmp_pool, trns, records_ofs,
                    dataset_dict (ds), which_formats))
    goto error;

  /* Are there any binary formats?

     There are real difficulties figuring out what to do when both binary
     formats and nontrivial encodings enter the picture.  So when binary
     formats are present we fall back to much simpler handling. */
  const struct prt_out_spec *binary_spec = find_binary_spec (trns);
  if (binary_spec && !fh)
    {
      lex_ofs_error (lexer, binary_spec->start_ofs, binary_spec->end_ofs,
                     _("%s is required when binary formats are specified."),
                     "OUTFILE");
      goto error;
    }

  if (lex_end_of_command (lexer) != CMD_SUCCESS)
    goto error;

  if (fh != NULL)
    {
      trns->writer = dfm_open_writer (fh, encoding);
      if (trns->writer == NULL)
        goto error;
      trns->encoding = dfm_writer_get_encoding (trns->writer);
    }
  else
    trns->encoding = UTF8;

  /* Output the variable table if requested. */
  if (print_table)
    dump_table (trns);

  /* Put the transformation in the queue. */
  add_transformation (ds, (binary_spec
                           ? &print_binary_trns_class
                           : &print_text_trns_class), trns);

  pool_destroy (tmp_pool);
  fh_unref (fh);

  return CMD_SUCCESS;

 error:
  print_trns_free (trns);
  fh_unref (fh);
  return CMD_FAILURE;
}

static bool parse_string_argument (struct lexer *, struct print_trns *,
                                   size_t *allocated_specs,
                                   int record, int *column);
static bool parse_variable_argument (struct lexer *, const struct dictionary *,
                                     struct print_trns *,
                                     size_t *allocated_specs,
                                     struct pool *tmp_pool,
                                     int *record, int *column,
                                     enum which_formats);

/* Parses all the variable and string specifications on a single
   PRINT, PRINT EJECT, or WRITE command into the prt structure.
   Returns success. */
static bool
parse_specs (struct lexer *lexer, struct pool *tmp_pool,
             struct print_trns *trns, int records_ofs, struct dictionary *dict,
             enum which_formats which_formats)
{
  int record = 0;
  int column = 1;

  if (lex_token (lexer) == T_ENDCMD)
    {
      trns->n_records = 1;
      return true;
    }

  size_t allocated_specs = 0;
  while (lex_token (lexer) != T_ENDCMD)
    {
      if (!parse_record_placement (lexer, &record, &column))
        return false;

      bool ok = (lex_is_string (lexer)
                 ? parse_string_argument (lexer, trns, &allocated_specs,
                                          record, &column)
                 : parse_variable_argument (lexer, dict, trns, &allocated_specs,
                                            tmp_pool, &record, &column,
                                            which_formats));
      if (!ok)
        return 0;

      lex_match (lexer, T_COMMA);
    }

  if (trns->n_records != 0 && trns->n_records != record)
    lex_ofs_error (lexer, records_ofs, records_ofs,
                   _("Output calls for %d records but %zu specified on RECORDS "
                     "subcommand."),
                   record, trns->n_records);
  trns->n_records = record;

  return true;
}

static struct prt_out_spec *
add_spec (struct print_trns *trns, size_t *allocated_specs)
{
  if (trns->n_specs >= *allocated_specs)
    trns->specs = pool_2nrealloc (trns->pool, trns->specs, allocated_specs,
                                  sizeof *trns->specs);
  return &trns->specs[trns->n_specs++];
}

/* Parses a string argument to the PRINT commands.  Returns success. */
static bool
parse_string_argument (struct lexer *lexer, struct print_trns *trns,
                       size_t *allocated_specs, int record, int *column)
{
  struct prt_out_spec *spec = add_spec (trns, allocated_specs);
  *spec = (struct prt_out_spec) {
    .type = PRT_LITERAL,
    .record = record,
    .first_column = *column,
    .string = ss_clone_pool (lex_tokss (lexer), trns->pool),
    .start_ofs = lex_ofs (lexer),
  };
  lex_get (lexer);

  /* Parse the included column range. */
  if (lex_is_number (lexer))
    {
      int first_column, last_column;
      bool range_specified;

      if (!parse_column_range (lexer, 1,
                               &first_column, &last_column, &range_specified))
        return false;

      spec->first_column = first_column;
      if (range_specified)
        {
          struct string s;
          ds_init_substring (&s, spec->string);
          ds_set_length (&s, last_column - first_column + 1, ' ');
          spec->string = ss_clone_pool (s.ss, trns->pool);
          ds_destroy (&s);
        }
    }
  spec->end_ofs = lex_ofs (lexer) - 1;

  spec->width = u8_width (CHAR_CAST (const uint8_t *, spec->string.string),
                          spec->string.length, UTF8);
  *column = spec->first_column + spec->width;

  return true;
}

/* Parses a variable argument to the PRINT commands by passing it off
   to fixed_parse_compatible() or fixed_parse_fortran() as appropriate.
   Returns success. */
static bool
parse_variable_argument (struct lexer *lexer, const struct dictionary *dict,
                         struct print_trns *trns, size_t *allocated_specs,
                         struct pool *tmp_pool, int *record, int *column,
                         enum which_formats which_formats)
{
  const struct variable **vars;
  size_t n_vars;
  if (!parse_variables_const_pool (lexer, tmp_pool, dict,
                                   &vars, &n_vars, PV_DUPLICATE))
    return false;

  struct fmt_spec *formats, *f;
  size_t n_formats;
  bool add_space;
  int formats_start = lex_ofs (lexer);
  if (lex_is_number (lexer) || lex_token (lexer) == T_LPAREN)
    {
      if (!parse_var_placements (lexer, tmp_pool, n_vars, FMT_FOR_OUTPUT,
                                 &formats, &n_formats))
        return false;
      add_space = false;
    }
  else
    {
      lex_match (lexer, T_ASTERISK);

      formats = pool_nmalloc (tmp_pool, n_vars, sizeof *formats);
      n_formats = n_vars;
      for (size_t i = 0; i < n_vars; i++)
        {
          const struct variable *v = vars[i];
          formats[i] = (which_formats == PRINT
                        ? var_get_print_format (v)
                        : var_get_write_format (v));
        }
      add_space = which_formats == PRINT;
    }
  int formats_end = lex_ofs (lexer) - 1;

  size_t var_idx = 0;
  for (f = formats; f < &formats[n_formats]; f++)
    if (!execute_placement_format (*f, record, column))
      {
        const struct variable *var = vars[var_idx++];
        char *error = fmt_check_width_compat__ (*f, var_get_name (var),
                                                var_get_width (var));
        if (error)
          {
            lex_ofs_error (lexer, formats_start, formats_end, "%s", error);
            free (error);
            return false;
          }

        struct prt_out_spec *spec = add_spec (trns, allocated_specs);
        *spec = (struct prt_out_spec) {
          .type = PRT_VAR,
          .record = *record,
          .first_column = *column,
          .var = var,
          .format = *f,
          .add_space = add_space,

          /* This is a completely bizarre twist for compatibility: WRITE
             outputs the system-missing value as a field filled with spaces,
             instead of using the normal format that usually contains a
             period. */
          .sysmis_as_spaces = (which_formats == WRITE
                               && var_is_numeric (var)
                               && (fmt_get_category (f->type)
                                   != FMT_CAT_BINARY)),
        };

        *column += f->w + add_space;
      }
  assert (var_idx == n_vars);

  return true;
}

/* Prints the table produced by the TABLE subcommand to the listing
   file. */
static void
dump_table (struct print_trns *trns)
{
  struct pivot_table *table = pivot_table_create (N_("Print Summary"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Attributes"),
                          N_("Record"), N_("Columns"), N_("Format"));

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable"));

  for (size_t i = 0; i < trns->n_specs; i++)
    {
      const struct prt_out_spec *spec = &trns->specs[i];
      if (spec->type != PRT_VAR)
        continue;

      int row = pivot_category_create_leaf (
        variables->root, pivot_value_new_variable (spec->var));

      pivot_table_put2 (table, 0, row,
                        pivot_value_new_integer (spec->record));
      int last_column = spec->first_column + spec->format.w - 1;
      pivot_table_put2 (table, 1, row, pivot_value_new_user_text_nocopy (
                          xasprintf ("%d-%d",
                                     spec->first_column, last_column)));

      char fmt_string[FMT_STRING_LEN_MAX + 1];
      pivot_table_put2 (table, 2, row, pivot_value_new_user_text (
                          fmt_to_string (spec->format, fmt_string), -1));
    }

  int row = pivot_category_create_leaf (
    variables->root, pivot_value_new_text (N_("N of Records")));
  pivot_table_put2 (table, 0, row,
                    pivot_value_new_integer (trns->n_records));

  pivot_table_submit (table);
}

static const struct prt_out_spec *
find_binary_spec (const struct print_trns *trns)
{
  for (size_t i = 0; i < trns->n_specs; i++)
    {
      const struct prt_out_spec *spec = &trns->specs[i];
      if (spec->type == PRT_VAR
          && fmt_get_category (spec->format.type) == FMT_CAT_BINARY)
        return spec;
    }
  return NULL;
}

/* Transformation, for all-text output. */

static void print_text_flush_records (struct print_trns *, struct u8_line *,
                                      int target_record,
                                      bool *eject, int *record);

/* Performs the transformation inside print_trns T on case C. */
static enum trns_result
print_text_trns_proc (void *trns_, struct ccase **c,
                      casenumber case_num UNUSED)
{
  struct print_trns *trns = trns_;
  struct u8_line line;

  bool eject = trns->eject;
  int record = 1;

  u8_line_init (&line);
  for (size_t i = 0; i < trns->n_specs; i++)
    {
      const struct prt_out_spec *spec = &trns->specs[i];
      int x0 = spec->first_column;

      print_text_flush_records (trns, &line, spec->record, &eject, &record);

      u8_line_set_length (&line, spec->first_column);
      if (spec->type == PRT_VAR)
        {
          const union value *input = case_data (*c, spec->var);
          int x1;

          if (!spec->sysmis_as_spaces || input->f != SYSMIS)
            {
              size_t len;
              int width;
              char *s;

              s = data_out (input, var_get_encoding (spec->var),
                            spec->format, settings_get_fmt_settings ());
              len = strlen (s);
              width = u8_width (CHAR_CAST (const uint8_t *, s), len, UTF8);
              x1 = x0 + width;
              u8_line_put (&line, x0, x1, s, len);
              free (s);
            }
          else
            {
              int n = spec->format.w;

              x1 = x0 + n;
              memset (u8_line_reserve (&line, x0, x1, n), ' ', n);
            }

          if (spec->add_space)
            *u8_line_reserve (&line, x1, x1 + 1, 1) = ' ';
        }
      else
        {
          const struct substring *s = &spec->string;

          u8_line_put (&line, x0, x0 + spec->width, s->string, s->length);
        }
    }
  print_text_flush_records (trns, &line, trns->n_records + 1,
                            &eject, &record);
  u8_line_destroy (&line);

  if (trns->writer != NULL && dfm_write_error (trns->writer))
    return TRNS_ERROR;
  return TRNS_CONTINUE;
}

/* Advance from *RECORD to TARGET_RECORD, outputting records
   along the way.  If *EJECT is true, then the first record
   output is preceded by ejecting the page (and *EJECT is set
   false). */
static void
print_text_flush_records (struct print_trns *trns, struct u8_line *line,
                          int target_record, bool *eject, int *record)
{
  for (; target_record > *record; (*record)++)
    {
      char leader = ' ';

      if (*eject)
        {
          *eject = false;
          if (trns->writer == NULL)
            output_item_submit (page_break_item_create ());
          else
            leader = '1';
        }
      *u8_line_reserve (line, 0, 1, 1) = leader;

      if (trns->writer == NULL)
        output_log ("%s", ds_cstr (&line->s) + 1);
      else
        {
          size_t len = ds_length (&line->s);
          char *s = ds_cstr (&line->s);

          if (!trns->include_prefix)
            {
              s++;
              len--;
            }

          dfm_put_record_utf8 (trns->writer, s, len);
        }
    }
}

/* Transformation, for output involving binary. */

static void print_binary_flush_records (struct print_trns *,
                                        struct string *line, int target_record,
                                        bool *eject, int *record);

/* Performs the transformation inside print_trns T on case C. */
static enum trns_result
print_binary_trns_proc (void *trns_, struct ccase **c,
                        casenumber case_num UNUSED)
{
  struct print_trns *trns = trns_;
  bool eject = trns->eject;
  char encoded_space = recode_byte (trns->encoding, C_ENCODING, ' ');
  int record = 1;
  struct string line = DS_EMPTY_INITIALIZER;

  ds_put_byte (&line, ' ');
  for (size_t i = 0; i < trns->n_specs; i++)
    {
      const struct prt_out_spec *spec = &trns->specs[i];
      print_binary_flush_records (trns, &line, spec->record, &eject, &record);

      ds_set_length (&line, spec->first_column, encoded_space);
      if (spec->type == PRT_VAR)
        {
          const union value *input = case_data (*c, spec->var);
          if (!spec->sysmis_as_spaces || input->f != SYSMIS)
            data_out_recode (input, var_get_encoding (spec->var),
                             spec->format, settings_get_fmt_settings (),
                             &line, trns->encoding);
          else
            ds_put_byte_multiple (&line, encoded_space, spec->format.w);
          if (spec->add_space)
            ds_put_byte (&line, encoded_space);
        }
      else
        {
          ds_put_substring (&line, spec->string);
          if (0 != strcmp (trns->encoding, UTF8))
            {
              size_t length = spec->string.length;
              char *data = ss_data (ds_tail (&line, length));
              char *s = recode_string (trns->encoding, UTF8, data, length);
              memcpy (data, s, length);
              free (s);
            }
        }
    }
  print_binary_flush_records (trns, &line, trns->n_records + 1,
                              &eject, &record);
  ds_destroy (&line);

  if (trns->writer != NULL && dfm_write_error (trns->writer))
    return TRNS_ERROR;
  return TRNS_CONTINUE;
}

/* Advance from *RECORD to TARGET_RECORD, outputting records
   along the way.  If *EJECT is true, then the first record
   output is preceded by ejecting the page (and *EJECT is set
   false). */
static void
print_binary_flush_records (struct print_trns *trns, struct string *line,
                            int target_record, bool *eject, int *record)
{
  for (; target_record > *record; (*record)++)
    {
      char *s = ds_cstr (line);
      size_t length = ds_length (line);
      char leader = ' ';

      if (*eject)
        {
          *eject = false;
          leader = '1';
        }
      s[0] = recode_byte (trns->encoding, C_ENCODING, leader);

      if (!trns->include_prefix)
        {
          s++;
          length--;
        }
      dfm_put_record (trns->writer, s, length);

      ds_truncate (line, 1);
    }
}

/* Frees TRNS. */
static bool
print_trns_free (void *trns_)
{
  struct print_trns *trns = trns_;
  bool ok = true;

  if (trns->writer != NULL)
    ok = dfm_close_writer (trns->writer);
  pool_destroy (trns->pool);

  return ok;
}

static const struct trns_class print_binary_trns_class = {
  .name = "PRINT",
  .execute = print_binary_trns_proc,
  .destroy = print_trns_free,
};

static const struct trns_class print_text_trns_class = {
  .name = "PRINT",
  .execute = print_text_trns_proc,
  .destroy = print_trns_free,
};

