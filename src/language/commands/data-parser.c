/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011, 2012, 2013, 2016 Free Software Foundation, Inc.

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

#include "language/commands/data-parser.h"

#include <stdint.h>
#include <stdlib.h>

#include "data/casereader-provider.h"
#include "data/data-in.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/file-handle-def.h"
#include "data/settings.h"
#include "language/commands/data-reader.h"
#include "libpspp/intern.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

/* Data parser for textual data like that read by DATA LIST. */
struct data_parser
  {
    enum data_parser_type type; /* Type of data to parse. */
    int skip_records;           /* Records to skip before first real data. */

    struct field *fields;       /* Fields to parse. */
    size_t n_fields;            /* Number of fields. */
    size_t field_allocated;     /* Number of fields spaced allocated for. */

    /* DP_DELIMITED parsers only. */
    bool span;                  /* May cases span multiple records? */
    bool empty_line_has_field;  /* Does an empty line have an (empty) field? */
    bool warn_missing_fields;   /* Should missing fields be considered errors? */
    struct substring quotes;    /* Characters that can quote separators. */
    bool quote_escape;          /* Doubled quote acts as escape? */
    struct substring soft_seps; /* Two soft separators act like just one. */
    struct substring hard_seps; /* Two hard separators yield empty fields. */
    struct string any_sep;      /* Concatenation of soft_seps and hard_seps. */

    /* DP_FIXED parsers only. */
    int records_per_case;       /* Number of records in each case. */
  };

/* How to parse one variable. */
struct field
  {
    struct fmt_spec format;        /* Input format of this field. */
    int case_idx;               /* First value in case. */
    char *name;                 /* Var name for error messages and tables. */

    /* DP_FIXED only. */
    int record;                        /* Record number (1-based). */
    int first_column;           /* First column in record (1-based). */
  };

static void set_any_sep (struct data_parser *parser);

/* Creates and returns a new data parser. */
struct data_parser *
data_parser_create (void)
{
  struct data_parser *parser = xmalloc (sizeof *parser);
  *parser = (struct data_parser) {
    .type = DP_FIXED,
    .span = true,
    .warn_missing_fields = true,
    .quotes = ss_clone (ss_cstr ("\"'")),
    .soft_seps = ss_clone (ss_cstr (CC_SPACES)),
    .hard_seps = ss_clone (ss_cstr (",")),
  };
  set_any_sep (parser);

  return parser;
}

/* Destroys PARSER. */
void
data_parser_destroy (struct data_parser *parser)
{
  if (parser != NULL)
    {
      size_t i;

      for (i = 0; i < parser->n_fields; i++)
        free (parser->fields[i].name);
      free (parser->fields);
      ss_dealloc (&parser->quotes);
      ss_dealloc (&parser->soft_seps);
      ss_dealloc (&parser->hard_seps);
      ds_destroy (&parser->any_sep);
      free (parser);
    }
}

/* Returns the type of PARSER (either DP_DELIMITED or DP_FIXED). */
enum data_parser_type
data_parser_get_type (const struct data_parser *parser)
{
  return parser->type;
}

/* Sets the type of PARSER to TYPE (either DP_DELIMITED or
   DP_FIXED). */
void
data_parser_set_type (struct data_parser *parser, enum data_parser_type type)
{
  assert (parser->n_fields == 0);
  assert (type == DP_FIXED || type == DP_DELIMITED);
  parser->type = type;
}

/* Configures PARSER to skip the specified number of
   INITIAL_RECORDS_TO_SKIP before parsing any data.  By default,
   no records are skipped. */
void
data_parser_set_skip (struct data_parser *parser, int initial_records_to_skip)
{
  assert (initial_records_to_skip >= 0);
  parser->skip_records = initial_records_to_skip;
}

/* Returns true if PARSER is configured to allow cases to span
   multiple records. */
bool
data_parser_get_span (const struct data_parser *parser)
{
  return parser->span;
}

/* If MAY_CASES_SPAN_RECORDS is true, configures PARSER to allow
   a single case to span multiple records and multiple cases to
   occupy a single record.  If MAY_CASES_SPAN_RECORDS is false,
   configures PARSER to require each record to contain exactly
   one case.

   This setting affects parsing of DP_DELIMITED files only. */
void
data_parser_set_span (struct data_parser *parser, bool may_cases_span_records)
{
  parser->span = may_cases_span_records;
}

/* If EMPTY_LINE_HAS_FIELD is true, configures PARSER to parse an
   empty line as an empty field and to treat a hard delimiter
   followed by end-of-line as an empty field.  If
   EMPTY_LINE_HAS_FIELD is false, PARSER will skip empty lines
   and hard delimiters at the end of lines without emitting empty
   fields.

   This setting affects parsing of DP_DELIMITED files only. */
void
data_parser_set_empty_line_has_field (struct data_parser *parser,
                                      bool empty_line_has_field)
{
  parser->empty_line_has_field = empty_line_has_field;
}


/* If WARN_MISSING_FIELDS is true, configures PARSER to emit a warning
   and cause an error condition when a missing field is encountered.
   If  WARN_MISSING_FIELDS is false, PARSER will silently fill such
   fields with the system missing value.

   This setting affects parsing of DP_DELIMITED files only. */
void
data_parser_set_warn_missing_fields (struct data_parser *parser,
                                     bool warn_missing_fields)
{
  parser->warn_missing_fields = warn_missing_fields;
}


/* Sets the characters that may be used for quoting field
   contents to QUOTES.  If QUOTES is empty, quoting will be
   disabled.

   The caller retains ownership of QUOTES.

   This setting affects parsing of DP_DELIMITED files only. */
void
data_parser_set_quotes (struct data_parser *parser, struct substring quotes)
{
  ss_dealloc (&parser->quotes);
  parser->quotes = ss_clone (quotes);
}

/* If ESCAPE is false (the default setting), a character used for
   quoting cannot itself be embedded within a quoted field.  If
   ESCAPE is true, then a quote character can be embedded within
   a quoted field by doubling it.

   This setting affects parsing of DP_DELIMITED files only, and
   only when at least one quote character has been set (with
   data_parser_set_quotes). */
void
data_parser_set_quote_escape (struct data_parser *parser, bool escape)
{
  parser->quote_escape = escape;
}

/* Sets PARSER's soft delimiters to DELIMITERS.  Soft delimiters
   separate fields, but consecutive soft delimiters do not yield
   empty fields.  (Ordinarily, only white space characters are
   appropriate soft delimiters.)

   The caller retains ownership of DELIMITERS.

   This setting affects parsing of DP_DELIMITED files only. */
void
data_parser_set_soft_delimiters (struct data_parser *parser,
                                 struct substring delimiters)
{
  ss_dealloc (&parser->soft_seps);
  parser->soft_seps = ss_clone (delimiters);
  set_any_sep (parser);
}

/* Sets PARSER's hard delimiters to DELIMITERS.  Hard delimiters
   separate fields.  A consecutive pair of hard delimiters yield
   an empty field.

   The caller retains ownership of DELIMITERS.

   This setting affects parsing of DP_DELIMITED files only. */
void
data_parser_set_hard_delimiters (struct data_parser *parser,
                                 struct substring delimiters)
{
  ss_dealloc (&parser->hard_seps);
  parser->hard_seps = ss_clone (delimiters);
  set_any_sep (parser);
}

/* Returns the number of records per case. */
int
data_parser_get_records (const struct data_parser *parser)
{
  return parser->records_per_case;
}

/* Sets the number of records per case to RECORDS_PER_CASE.

   This setting affects parsing of DP_FIXED files only. */
void
data_parser_set_records (struct data_parser *parser, int records_per_case)
{
  assert (records_per_case >= 0);
  assert (records_per_case >= parser->records_per_case);
  parser->records_per_case = records_per_case;
}

static void
add_field (struct data_parser *p, struct fmt_spec format, int case_idx,
           const char *name, int record, int first_column)
{
  struct field *field;

  if (p->n_fields == p->field_allocated)
    p->fields = x2nrealloc (p->fields, &p->field_allocated, sizeof *p->fields);
  field = &p->fields[p->n_fields++];
  *field = (struct field) {
    .format = format,
    .case_idx = case_idx,
    .name = xstrdup (name),
    .record = record,
    .first_column = first_column,
  };
}

/* Adds a delimited field to the field parsed by PARSER, which
   must be configured as a DP_DELIMITED parser.  The field is
   parsed as input format FORMAT.  Its data will be stored into case
   index CASE_INDEX.  Errors in input data will be reported
   against variable NAME. */
void
data_parser_add_delimited_field (struct data_parser *parser,
                                 struct fmt_spec format, int case_idx,
                                 const char *name)
{
  assert (parser->type == DP_DELIMITED);
  add_field (parser, format, case_idx, name, 0, 0);
}

/* Adds a fixed field to the field parsed by PARSER, which
   must be configured as a DP_FIXED parser.  The field is
   parsed as input format FORMAT.  Its data will be stored into case
   index CASE_INDEX.  Errors in input data will be reported
   against variable NAME.  The field will be drawn from the
   FORMAT->w columns in 1-based RECORD starting at 1-based
   column FIRST_COLUMN.

   RECORD must be at least as great as that of any field already
   added; that is, fields must be added in increasing order of
   record number.  If RECORD is greater than the current number
   of records per case, the number of records per case are
   increased as needed.  */
void
data_parser_add_fixed_field (struct data_parser *parser,
                             struct fmt_spec format, int case_idx,
                             const char *name,
                             int record, int first_column)
{
  assert (parser->type == DP_FIXED);
  assert (parser->n_fields == 0
          || record >= parser->fields[parser->n_fields - 1].record);
  if (record > parser->records_per_case)
    parser->records_per_case = record;
  add_field (parser, format, case_idx, name, record, first_column);
}

/* Returns true if any fields have been added to PARSER, false
   otherwise. */
bool
data_parser_any_fields (const struct data_parser *parser)
{
  return parser->n_fields > 0;
}

static void
set_any_sep (struct data_parser *parser)
{
  ds_assign_substring (&parser->any_sep, parser->soft_seps);
  ds_put_substring (&parser->any_sep, parser->hard_seps);
}

static bool parse_delimited_span (const struct data_parser *,
                                  struct dfm_reader *,
                                  struct dictionary *, struct ccase *);
static bool parse_delimited_no_span (const struct data_parser *,
                                     struct dfm_reader *,
                                     struct dictionary *, struct ccase *);
static bool parse_fixed (const struct data_parser *, struct dfm_reader *,
                         struct dictionary *, struct ccase *);

/* Reads a case from DFM into C, which matches dictionary DICT, parsing it with
   PARSER.  Returns true if successful, false at end of file or on I/O error.

   Case C must not be shared. */
bool
data_parser_parse (struct data_parser *parser, struct dfm_reader *reader,
                   struct dictionary *dict, struct ccase *c)
{
  bool retval;

  assert (!case_is_shared (c));
  assert (data_parser_any_fields (parser));

  /* Skip the requested number of records before reading the
     first case. */
  for (; parser->skip_records > 0; parser->skip_records--)
    {
      if (dfm_eof (reader))
        return false;
      dfm_forward_record (reader);
    }

  /* Limit cases. */
  if (parser->type == DP_DELIMITED)
    {
      if (parser->span)
        retval = parse_delimited_span (parser, reader, dict, c);
      else
        retval = parse_delimited_no_span (parser, reader, dict, c);
    }
  else
    retval = parse_fixed (parser, reader, dict, c);

  return retval;
}

static void
cut_field__ (const struct data_parser *parser, const struct substring *line,
             struct substring *p, size_t *n_columns,
             struct string *tmp, struct substring *field)
{
  bool quoted = ss_find_byte (parser->quotes, ss_first (*p)) != SIZE_MAX;
  if (quoted)
    {
      /* Quoted field. */
      int quote = ss_get_byte (p);
      if (!ss_get_until (p, quote, field))
        msg (DW, _("Quoted string extends beyond end of line."));
      if (parser->quote_escape && ss_first (*p) == quote)
        {
          ds_assign_substring (tmp, *field);
          while (ss_match_byte (p, quote))
            {
              struct substring ss;
              ds_put_byte (tmp, quote);
              if (!ss_get_until (p, quote, &ss))
                msg (DW, _("Quoted string extends beyond end of line."));
              ds_put_substring (tmp, ss);
            }
          *field = ds_ss (tmp);
        }
      *n_columns = ss_length (*line) - ss_length (*p);
    }
  else
    {
      /* Regular field. */
      ss_get_bytes (p, ss_cspan (*p, ds_ss (&parser->any_sep)), field);
      *n_columns = ss_length (*field);
    }

  /* Skip trailing soft separator and a single hard separator if present. */
  size_t length_before_separators = ss_length (*p);
  ss_ltrim (p, parser->soft_seps);
  if (!ss_is_empty (*p)
      && ss_find_byte (parser->hard_seps, ss_first (*p)) != SIZE_MAX)
    {
      ss_advance (p, 1);
      ss_ltrim (p, parser->soft_seps);
    }

  if (!ss_is_empty (*p) && quoted && length_before_separators == ss_length (*p))
    msg (DW, _("Missing delimiter following quoted string."));
}

/* Extracts a delimited field from the current position in the
   current record according to PARSER, reading data from READER.

   *FIELD is set to the field content.  The caller must not or
   destroy this constant string.

   Sets *FIRST_COLUMN to the 1-based column number of the start of
   the extracted field, and *LAST_COLUMN to the end of the extracted
   field.

   Returns true on success, false on failure. */
static bool
cut_field (const struct data_parser *parser, struct dfm_reader *reader,
           int *first_column, int *last_column, struct string *tmp,
           struct substring *field)
{
  struct substring line, p;

  if (dfm_eof (reader))
    return false;
  if (ss_is_empty (parser->hard_seps))
    dfm_expand_tabs (reader);
  line = p = dfm_get_record (reader);

  /* Skip leading soft separators. */
  ss_ltrim (&p, parser->soft_seps);

  /* Handle empty or completely consumed lines. */
  if (ss_is_empty (p))
    {
      if (!parser->empty_line_has_field || dfm_columns_past_end (reader) > 0)
        return false;
      else
        {
          *field = p;
          *first_column = dfm_column_start (reader);
          *last_column = *first_column + 1;
          dfm_forward_columns (reader, 1);
          return true;
        }
    }

  size_t n_columns;
  cut_field__ (parser, &line, &p, &n_columns, tmp, field);
  *first_column = dfm_column_start (reader);
  *last_column = *first_column + n_columns;

  if (ss_is_empty (p))
    dfm_forward_columns (reader, 1);
  dfm_forward_columns (reader, ss_length (line) - ss_length (p));

  return true;
}

static void
parse_error (const struct dfm_reader *reader, const struct field *field,
             int first_column, int last_column, char *error)
{
  int line_number = dfm_get_line_number (reader);
  struct msg_location *location = xmalloc (sizeof *location);
  *location = (struct msg_location) {
    .file_name = intern_new (dfm_get_file_name (reader)),
    .start = { .line = line_number, .column = first_column },
    .end = { .line = line_number, .column = last_column - 1 },
  };
  struct msg *m = xmalloc (sizeof *m);
  *m = (struct msg) {
    .category = MSG_C_DATA,
    .severity = MSG_S_WARNING,
    .location = location,
    .text = xasprintf (_("Data for variable %s is not valid as format %s: %s"),
                       field->name, fmt_name (field->format.type), error),
  };
  msg_emit (m);

  free (error);
}

/* Reads a case from READER into C, which matches DICT, parsing it according to
   fixed-format syntax rules in PARSER.  Returns true if successful, false at
   end of file or on I/O error. */
static bool
parse_fixed (const struct data_parser *parser, struct dfm_reader *reader,
             struct dictionary *dict, struct ccase *c)
{
  const char *input_encoding = dfm_reader_get_encoding (reader);
  const char *output_encoding = dict_get_encoding (dict);
  struct field *f;
  int row;

  if (dfm_eof (reader))
    return false;

  f = parser->fields;
  for (row = 1; row <= parser->records_per_case; row++)
    {
      struct substring line;

      if (dfm_eof (reader))
        {
          msg (DW, _("Partial case of %d of %d records discarded."),
               row - 1, parser->records_per_case);
          return false;
        }
      dfm_expand_tabs (reader);
      line = dfm_get_record (reader);

      for (; f < &parser->fields[parser->n_fields] && f->record == row; f++)
        {
          struct substring s = ss_substr (line, f->first_column - 1,
                                          f->format.w);
          union value *value = case_data_rw_idx (c, f->case_idx);
          char *error = data_in (s, input_encoding, f->format.type,
                                 settings_get_fmt_settings (),
                                 value, fmt_var_width (f->format),
                                 output_encoding);

          if (error == NULL)
            data_in_imply_decimals (s, input_encoding, f->format.type,
                                    f->format.d, settings_get_fmt_settings (),
                                    value);
          else
            parse_error (reader, f, f->first_column,
                         f->first_column + f->format.w, error);
        }

      dfm_forward_record (reader);
    }

  return true;
}

/* Splits the data line in LINE into individual text fields and returns the
   number of fields.  If SA is nonnull, appends each field to SA; the caller
   retains ownership of SA and its contents.  */
size_t
data_parser_split (const struct data_parser *parser,
                   struct substring line, struct string_array *sa)
{
  size_t n = 0;

  struct string tmp = DS_EMPTY_INITIALIZER;
  for (;;)
    {
      struct substring p = line;
      ss_ltrim (&p, parser->soft_seps);
      if (ss_is_empty (p))
        {
          ds_destroy (&tmp);
          return n;
        }

      size_t n_columns;
      struct substring field;

      msg_disable ();
      cut_field__ (parser, &line, &p, &n_columns, &tmp, &field);
      msg_enable ();

      if (sa)
        string_array_append_nocopy (sa, ss_xstrdup (field));
      n++;
      line = p;
    }
}

/* Reads a case from READER into C, which matches dictionary DICT, parsing it
   according to free-format syntax rules in PARSER.  Returns true if
   successful, false at end of file or on I/O error. */
static bool
parse_delimited_span (const struct data_parser *parser,
                      struct dfm_reader *reader,
                      struct dictionary *dict, struct ccase *c)
{
  const char *output_encoding = dict_get_encoding (dict);
  struct string tmp = DS_EMPTY_INITIALIZER;
  struct field *f;

  for (f = parser->fields; f < &parser->fields[parser->n_fields]; f++)
    {
      struct substring s;
      int first_column, last_column;
      char *error;

      /* Cut out a field and read in a new record if necessary. */
      while (!cut_field (parser, reader,
                         &first_column, &last_column, &tmp, &s))
        {
          if (!dfm_eof (reader))
            dfm_forward_record (reader);
          if (dfm_eof (reader))
            {
              if (f > parser->fields)
                msg (DW, _("Partial case discarded.  The first variable "
                           "missing was %s."), f->name);
              ds_destroy (&tmp);
              return false;
            }
        }

      const char *input_encoding = dfm_reader_get_encoding (reader);
      error = data_in (s, input_encoding, f->format.type,
                       settings_get_fmt_settings (),
                       case_data_rw_idx (c, f->case_idx),
                       fmt_var_width (f->format), output_encoding);
      if (error != NULL)
        parse_error (reader, f, first_column, last_column, error);
    }
  ds_destroy (&tmp);
  return true;
}

/* Reads a case from READER into C, which matches dictionary DICT, parsing it
   according to delimited syntax rules with one case per record in PARSER.
   Returns true if successful, false at end of file or on I/O error. */
static bool
parse_delimited_no_span (const struct data_parser *parser,
                         struct dfm_reader *reader,
                         struct dictionary *dict, struct ccase *c)
{
  const char *output_encoding = dict_get_encoding (dict);
  struct string tmp = DS_EMPTY_INITIALIZER;
  struct substring s;
  struct field *f, *end;

  if (dfm_eof (reader))
    return false;

  end = &parser->fields[parser->n_fields];
  for (f = parser->fields; f < end; f++)
    {
      int first_column, last_column;
      char *error;

      if (!cut_field (parser, reader, &first_column, &last_column, &tmp, &s))
        {
          if (f < end - 1 && settings_get_undefined () && parser->warn_missing_fields)
            msg (DW, _("Missing value(s) for all variables from %s onward.  "
                       "These will be filled with the system-missing value "
                       "or blanks, as appropriate."),
                 f->name);
          for (; f < end; f++)
            value_set_missing (case_data_rw_idx (c, f->case_idx),
                               fmt_var_width (f->format));
          goto exit;
        }

      const char *input_encoding = dfm_reader_get_encoding (reader);
      error = data_in (s, input_encoding, f->format.type,
                       settings_get_fmt_settings (),
                       case_data_rw_idx (c, f->case_idx),
                       fmt_var_width (f->format), output_encoding);
      if (error != NULL)
        parse_error (reader, f, first_column, last_column, error);
    }

  s = dfm_get_record (reader);
  ss_ltrim (&s, parser->soft_seps);
  if (!ss_is_empty (s))
    msg (DW, _("Record ends in data not part of any field."));

exit:
  dfm_forward_record (reader);
  ds_destroy (&tmp);
  return true;
}

/* Displays a table giving information on fixed-format variable
   parsing on DATA LIST. */
static void
dump_fixed_table (const struct data_parser *parser,
                  const struct file_handle *fh)
{
  /* XXX This should not be preformatted. */
  char *title = xasprintf (ngettext ("Reading %d record from %s.",
                                     "Reading %d records from %s.",
                                     parser->records_per_case),
                           parser->records_per_case, fh_get_name (fh));
  struct pivot_table *table = pivot_table_create__ (
    pivot_value_new_user_text (title, -1), "Fixed Data Records");
  free (title);

  pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Attributes"),
    N_("Record"), N_("Columns"), N_("Format"));

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable"));
  variables->root->show_label = true;
  for (size_t i = 0; i < parser->n_fields; i++)
    {
      struct field *f = &parser->fields[i];

      /* XXX It would be better to have the actual variable here. */
      int variable_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_user_text (f->name, -1));

      pivot_table_put2 (table, 0, variable_idx,
                        pivot_value_new_integer (f->record));

      int first_column = f->first_column;
      int last_column = f->first_column + f->format.w - 1;
      char *columns = xasprintf ("%d-%d", first_column, last_column);
      pivot_table_put2 (table, 1, variable_idx,
                        pivot_value_new_user_text (columns, -1));
      free (columns);

      char str[FMT_STRING_LEN_MAX + 1];
      pivot_table_put2 (table, 2, variable_idx,
                        pivot_value_new_user_text (
                          fmt_to_string (f->format, str), -1));

    }

  pivot_table_submit (table);
}

/* Displays a table giving information on free-format variable parsing
   on DATA LIST. */
static void
dump_delimited_table (const struct data_parser *parser,
                      const struct file_handle *fh)
{
  struct pivot_table *table = pivot_table_create__ (
    pivot_value_new_text_format (N_("Reading free-form data from %s."),
                                 fh_get_name (fh)),
    "Free-Form Data Records");

  pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Attributes"), N_("Format"));

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable"));
  variables->root->show_label = true;
  for (size_t i = 0; i < parser->n_fields; i++)
    {
      struct field *f = &parser->fields[i];

      /* XXX It would be better to have the actual variable here. */
      int variable_idx = pivot_category_create_leaf (
        variables->root, pivot_value_new_user_text (f->name, -1));

      char str[FMT_STRING_LEN_MAX + 1];
      pivot_table_put2 (table, 0, variable_idx,
                        pivot_value_new_user_text (
                          fmt_to_string (f->format, str), -1));
    }

  pivot_table_submit (table);
}

/* Displays a table giving information on how PARSER will read
   data from FH. */
void
data_parser_output_description (struct data_parser *parser,
                                const struct file_handle *fh)
{
  if (parser->type == DP_FIXED)
    dump_fixed_table (parser, fh);
  else
    dump_delimited_table (parser, fh);
}

/* Data parser input program. */
struct data_parser_casereader
  {
    struct data_parser *parser; /* Parser. */
    struct dictionary *dict;    /* Dictionary. */
    struct dfm_reader *reader;  /* Data file reader. */
    struct caseproto *proto;    /* Format of cases. */
  };

static const struct casereader_class data_parser_casereader_class;

/* Replaces DS's active dataset by an input program that reads data
   from READER according to the rules in PARSER, using DICT as
   the underlying dictionary.  Ownership of PARSER and READER is
   transferred to the input program, and ownership of DICT is
   transferred to the dataset. */
void
data_parser_make_active_file (struct data_parser *parser, struct dataset *ds,
                               struct dfm_reader *reader,
                              struct dictionary *dict,
                               struct casereader* (*func)(struct casereader *,
                                                          const struct dictionary *,
                                                          void *),
                               void *ud)
{
  struct data_parser_casereader *r;
  struct casereader *casereader0;
  struct casereader *casereader1;

  r = xmalloc (sizeof *r);
  r->parser = parser;
  r->dict = dict_ref (dict);
  r->reader = reader;
  r->proto = caseproto_ref (dict_get_proto (dict));
  casereader0 = casereader_create_sequential (NULL, r->proto,
                                             CASENUMBER_MAX,
                                             &data_parser_casereader_class, r);

  if (func)
    casereader1 = func (casereader0, dict, ud);
  else
    casereader1 = casereader0;

  dataset_set_dict (ds, dict);
  dataset_set_source (ds, casereader1);
}


static struct ccase *
data_parser_casereader_read (struct casereader *reader UNUSED, void *r_)
{
  struct data_parser_casereader *r = r_;
  struct ccase *c = case_create (r->proto);
  if (data_parser_parse (r->parser, r->reader, r->dict, c))
    return c;
  else
    {
      case_unref (c);
      return NULL;
    }
}

static void
data_parser_casereader_destroy (struct casereader *reader, void *r_)
{
  struct data_parser_casereader *r = r_;
  if (dfm_reader_error (r->reader))
    casereader_force_error (reader);
  dfm_close_reader (r->reader);
  caseproto_unref (r->proto);
  dict_unref (r->dict);
  data_parser_destroy (r->parser);
  free (r);
}

static const struct casereader_class data_parser_casereader_class =
  {
    data_parser_casereader_read,
    data_parser_casereader_destroy,
    NULL,
    NULL,
  };
