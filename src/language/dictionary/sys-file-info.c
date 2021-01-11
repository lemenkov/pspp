/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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
#include <float.h>
#include <stdlib.h>

#include "data/any-reader.h"
#include "data/attributes.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/file-handle-def.h"
#include "data/format.h"
#include "data/missing-values.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "data/vector.h"
#include "language/command.h"
#include "language/data-io/file-handle.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/hash-functions.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/string-array.h"
#include "output/pivot-table.h"
#include "output/output-item.h"

#include "gl/count-one-bits.h"
#include "gl/localcharset.h"
#include "gl/intprops.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

/* Information to include in displaying a dictionary. */
enum
  {
    /* Variable table. */
    DF_NAME              = 1 << 0,
    DF_POSITION          = 1 << 1,
    DF_LABEL             = 1 << 2,
    DF_MEASUREMENT_LEVEL = 1 << 3,
    DF_ROLE              = 1 << 4,
    DF_WIDTH             = 1 << 5,
    DF_ALIGNMENT         = 1 << 6,
    DF_PRINT_FORMAT      = 1 << 7,
    DF_WRITE_FORMAT      = 1 << 8,
    DF_MISSING_VALUES    = 1 << 9,
#define DF_ALL_VARIABLE ((1 << 10) - 1)

    /* Value labels table. */
    DF_VALUE_LABELS      = 1 << 10,

    /* Attribute table. */
    DF_AT_ATTRIBUTES     = 1 << 11, /* Attributes whose names begin with @. */
    DF_ATTRIBUTES        = 1 << 12, /* All other attributes. */
  };

static void display_variables (const struct variable **, size_t, int flags);
static void display_value_labels (const struct variable **, size_t);
static void display_attributes (const struct attrset *,
                                const struct variable **, size_t, int flags);

static void report_encodings (const struct file_handle *, struct pool *,
                              char **titles, bool *ids,
                              char **strings, size_t n_strings);

static char *get_documents_as_string (const struct dictionary *);

static void
add_row (struct pivot_table *table, const char *attribute,
         struct pivot_value *value)
{
  int row = pivot_category_create_leaf (table->dimensions[0]->root,
                                        pivot_value_new_text (attribute));
  if (value)
    pivot_table_put1 (table, row, value);
}

/* SYSFILE INFO utility. */
int
cmd_sysfile_info (struct lexer *lexer, struct dataset *ds)
{
  struct any_reader *any_reader;
  struct file_handle *h;
  struct dictionary *d;
  struct casereader *reader;
  struct any_read_info info;
  char *encoding;

  h = NULL;
  encoding = NULL;
  for (;;)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "FILE") || lex_is_string (lexer))
	{
	  lex_match (lexer, T_EQUALS);

          fh_unref (h);
	  h = fh_parse (lexer, FH_REF_FILE, NULL);
	  if (h == NULL)
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
      else
        break;
    }

  if (h == NULL)
    {
      lex_sbc_missing ("FILE");
      goto error;
    }

  any_reader = any_reader_open (h);
  if (!any_reader)
    {
      free (encoding);
      return CMD_FAILURE;
    }

  if (encoding && !strcasecmp (encoding, "detect"))
    {
      char **titles, **strings;
      struct pool *pool;
      size_t n_strings;
      bool *ids;

      pool = pool_create ();
      n_strings = any_reader_get_strings (any_reader, pool,
                                          &titles, &ids, &strings);
      any_reader_close (any_reader);

      report_encodings (h, pool, titles, ids, strings, n_strings);
      fh_unref (h);
      pool_destroy (pool);
      free (encoding);

      return CMD_SUCCESS;
    }

  reader = any_reader_decode (any_reader, encoding, &d, &info);
  if (!reader)
    goto error;
  casereader_destroy (reader);

  struct pivot_table *table = pivot_table_create (N_("File Information"));
  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Attribute"));

  add_row (table, N_("File"),
           pivot_value_new_user_text (fh_get_file_name (h), -1));

  const char *label = dict_get_label (d);
  add_row (table, N_("Label"),
           label ? pivot_value_new_user_text (label, -1) : NULL);

  add_row (table, N_("Created"),
           pivot_value_new_user_text_nocopy (
             xasprintf ("%s %s by %s", info.creation_date,
                        info.creation_time, info.product)));

  if (info.product_ext)
    add_row (table, N_("Product"),
             pivot_value_new_user_text (info.product_ext, -1));

  add_row (table, N_("Integer Format"),
           pivot_value_new_text (
             info.integer_format == INTEGER_MSB_FIRST ? N_("Big Endian")
             : info.integer_format == INTEGER_LSB_FIRST ? N_("Little Endian")
             : N_("Unknown")));

  add_row (table, N_("Real Format"),
           pivot_value_new_text (
             info.float_format == FLOAT_IEEE_DOUBLE_LE ? N_("IEEE 754 LE.")
             : info.float_format == FLOAT_IEEE_DOUBLE_BE ? N_("IEEE 754 BE.")
             : info.float_format == FLOAT_VAX_D ? N_("VAX D.")
             : info.float_format == FLOAT_VAX_G ? N_("VAX G.")
             : info.float_format == FLOAT_Z_LONG ? N_("IBM 390 Hex Long.")
             : N_("Unknown")));

  add_row (table, N_("Variables"),
           pivot_value_new_integer (dict_get_var_cnt (d)));

  add_row (table, N_("Cases"),
           (info.case_cnt == -1
            ? pivot_value_new_text (N_("Unknown"))
            : pivot_value_new_integer (info.case_cnt)));

  add_row (table, N_("Type"),
           pivot_value_new_text (info.klass->name));

  struct variable *weight_var = dict_get_weight (d);
  add_row (table, N_("Weight"),
           (weight_var
            ? pivot_value_new_variable (weight_var)
            : pivot_value_new_text (N_("Not weighted"))));

  add_row (table, N_("Compression"),
           (info.compression == ANY_COMP_NONE
            ? pivot_value_new_text (N_("None"))
            : pivot_value_new_user_text (
              info.compression == ANY_COMP_SIMPLE ? "SAV" : "ZSAV", -1)));

  add_row (table, N_("Encoding"),
           pivot_value_new_user_text (dict_get_encoding (d), -1));

  if (dict_get_document_line_cnt (d) > 0)
    add_row (table, N_("Documents"),
             pivot_value_new_user_text_nocopy (get_documents_as_string (d)));

  pivot_table_submit (table);

  size_t n_vars = dict_get_var_cnt (d);
  const struct variable **vars = xnmalloc (n_vars, sizeof *vars);
  for (size_t i = 0; i < dict_get_var_cnt (d); i++)
    vars[i] = dict_get_var (d, i);
  display_variables (vars, n_vars, DF_ALL_VARIABLE);
  display_value_labels (vars, n_vars);
  display_attributes (dict_get_attributes (dataset_dict (ds)),
                      vars, n_vars, DF_ATTRIBUTES);
  free (vars);

  dict_unref (d);

  fh_unref (h);
  free (encoding);
  any_read_info_destroy (&info);
  return CMD_SUCCESS;

error:
  fh_unref (h);
  free (encoding);
  return CMD_FAILURE;
}

/* DISPLAY utility. */

static void display_macros (void);
static void display_documents (const struct dictionary *dict);
static void display_vectors (const struct dictionary *dict, int sorted);

int
cmd_display (struct lexer *lexer, struct dataset *ds)
{
  /* Whether to sort the list of variables alphabetically. */
  int sorted;

  /* Variables to display. */
  size_t n;
  const struct variable **vl;

  if (lex_match_id (lexer, "MACROS"))
    display_macros ();
  else if (lex_match_id (lexer, "DOCUMENTS"))
    display_documents (dataset_dict (ds));
  else if (lex_match_id (lexer, "FILE"))
    {
      if (!lex_force_match_id (lexer, "LABEL"))
	return CMD_FAILURE;

      const char *label = dict_get_label (dataset_dict (ds));

      struct pivot_table *table = pivot_table_create (N_("File Label"));
      pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Label"),
                              N_("Label"));
      pivot_table_put1 (table, 0,
                        (label ? pivot_value_new_user_text (label, -1)
                         : pivot_value_new_text (N_("(none)"))));
      pivot_table_submit (table);
    }
  else
    {
      int flags;

      sorted = lex_match_id (lexer, "SORTED");

      if (lex_match_id (lexer, "VECTORS"))
	{
	  display_vectors (dataset_dict(ds), sorted);
	  return CMD_SUCCESS;
	}
      else if (lex_match_id (lexer, "SCRATCH"))
        {
          dict_get_vars (dataset_dict (ds), &vl, &n, DC_ORDINARY);
          flags = DF_NAME;
        }
      else
        {
          struct subcommand
            {
              const char *name;
              int flags;
            };
          static const struct subcommand subcommands[] =
            {
              {"@ATTRIBUTES", DF_ATTRIBUTES | DF_AT_ATTRIBUTES},
              {"ATTRIBUTES", DF_ATTRIBUTES},
              {"DICTIONARY", (DF_NAME | DF_POSITION | DF_LABEL
                              | DF_MEASUREMENT_LEVEL | DF_ROLE | DF_WIDTH
                              | DF_ALIGNMENT | DF_PRINT_FORMAT
                              | DF_WRITE_FORMAT | DF_MISSING_VALUES
                              | DF_VALUE_LABELS)},
              {"INDEX", DF_NAME | DF_POSITION},
              {"LABELS", DF_NAME | DF_POSITION | DF_LABEL},
              {"NAMES", DF_NAME},
              {"VARIABLES", (DF_NAME | DF_POSITION | DF_PRINT_FORMAT
                             | DF_WRITE_FORMAT | DF_MISSING_VALUES)},
              {NULL, 0},
            };
          const struct subcommand *sbc;
          struct dictionary *dict = dataset_dict (ds);

          flags = 0;
          for (sbc = subcommands; sbc->name != NULL; sbc++)
            if (lex_match_id (lexer, sbc->name))
              {
                flags = sbc->flags;
                break;
              }

          lex_match (lexer, T_SLASH);
          lex_match_id (lexer, "VARIABLES");
          lex_match (lexer, T_EQUALS);

          if (lex_token (lexer) != T_ENDCMD)
            {
              if (!parse_variables_const (lexer, dict, &vl, &n, PV_NONE))
                {
                  free (vl);
                  return CMD_FAILURE;
                }
            }
          else
            dict_get_vars (dict, &vl, &n, 0);
        }

      if (n > 0)
        {
          sort (vl, n, sizeof *vl, (sorted
                                    ? compare_var_ptrs_by_name
                                    : compare_var_ptrs_by_dict_index), NULL);

          int variable_flags = flags & DF_ALL_VARIABLE;
          if (variable_flags)
            display_variables (vl, n, variable_flags);

          if (flags & DF_VALUE_LABELS)
            display_value_labels (vl, n);

          int attribute_flags = flags & (DF_ATTRIBUTES | DF_AT_ATTRIBUTES);
          if (attribute_flags)
            display_attributes (dict_get_attributes (dataset_dict (ds)),
                                vl, n, attribute_flags);
        }
      else
        msg (SW, _("No variables to display."));

      free (vl);
    }

  return CMD_SUCCESS;
}

static void
display_macros (void)
{
  msg (SW, _("Macros not supported."));
}

static char *
get_documents_as_string (const struct dictionary *dict)
{
  const struct string_array *documents = dict_get_documents (dict);
  struct string s = DS_EMPTY_INITIALIZER;
  for (size_t i = 0; i < documents->n; i++)
    {
      if (i)
        ds_put_byte (&s, '\n');
      ds_put_cstr (&s, documents->strings[i]);
    }
  return ds_steal_cstr (&s);
}

static void
display_documents (const struct dictionary *dict)
{
  struct pivot_table *table = pivot_table_create (N_("Documents"));
  struct pivot_dimension *d = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Documents"), N_("Document"));
  d->hide_all_labels = true;

  if (!dict_get_documents (dict)->n)
    pivot_table_put1 (table, 0, pivot_value_new_text (N_("(none)")));
  else
    {
      char *docs = get_documents_as_string (dict);
      pivot_table_put1 (table, 0, pivot_value_new_user_text_nocopy (docs));
    }

  pivot_table_submit (table);
}

static void
display_variables (const struct variable **vl, size_t n, int flags)
{
  struct pivot_table *table = pivot_table_create (N_("Variables"));

  struct pivot_dimension *attributes = pivot_dimension_create (
    table, PIVOT_AXIS_COLUMN, N_("Attributes"));

  struct heading
    {
      int flag;
      const char *title;
    };
  static const struct heading headings[] = {
    { DF_POSITION, N_("Position") },
    { DF_LABEL, N_("Label") },
    { DF_MEASUREMENT_LEVEL, N_("Measurement Level") },
    { DF_ROLE, N_("Role") },
    { DF_WIDTH, N_("Width") },
    { DF_ALIGNMENT, N_("Alignment") },
    { DF_PRINT_FORMAT, N_("Print Format") },
    { DF_WRITE_FORMAT, N_("Write Format") },
    { DF_MISSING_VALUES, N_("Missing Values") },
  };
  for (size_t i = 0; i < sizeof headings / sizeof *headings; i++)
    if (flags & headings[i].flag)
      pivot_category_create_leaf (attributes->root,
                                  pivot_value_new_text (headings[i].title));

  struct pivot_dimension *names = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Name"));
  names->root->show_label = true;

  for (size_t i = 0; i < n; i++)
    {
      const struct variable *v = vl[i];

      struct pivot_value *name = pivot_value_new_variable (v);
      name->variable.show = SETTINGS_VALUE_SHOW_VALUE;
      int row = pivot_category_create_leaf (names->root, name);

      int x = 0;
      if (flags & DF_POSITION)
        pivot_table_put2 (table, x++, row, pivot_value_new_integer (
                            var_get_dict_index (v) + 1));

      if (flags & DF_LABEL)
        {
          const char *label = var_get_label (v);
          if (label)
            pivot_table_put2 (table, x, row,
                              pivot_value_new_user_text (label, -1));
          x++;
        }

      if (flags & DF_MEASUREMENT_LEVEL)
        pivot_table_put2 (
          table, x++, row,
          pivot_value_new_text (measure_to_string (var_get_measure (v))));

      if (flags & DF_ROLE)
        pivot_table_put2 (
          table, x++, row,
          pivot_value_new_text (var_role_to_string (var_get_role (v))));

      if (flags & DF_WIDTH)
        pivot_table_put2 (
          table, x++, row,
          pivot_value_new_integer (var_get_display_width (v)));

      if (flags & DF_ALIGNMENT)
        pivot_table_put2 (
          table, x++, row,
          pivot_value_new_text (alignment_to_string (
                                  var_get_alignment (v))));

      if (flags & DF_PRINT_FORMAT)
        {
          const struct fmt_spec *print = var_get_print_format (v);
          char s[FMT_STRING_LEN_MAX + 1];

          pivot_table_put2 (
            table, x++, row,
            pivot_value_new_user_text (fmt_to_string (print, s), -1));
        }

      if (flags & DF_WRITE_FORMAT)
        {
          const struct fmt_spec *write = var_get_write_format (v);
          char s[FMT_STRING_LEN_MAX + 1];

          pivot_table_put2 (
            table, x++, row,
            pivot_value_new_user_text (fmt_to_string (write, s), -1));
        }

      if (flags & DF_MISSING_VALUES)
        {
          char *s = mv_to_string (var_get_missing_values (v),
                                  var_get_encoding (v));
          if (s)
            pivot_table_put2 (
              table, x, row,
              pivot_value_new_user_text_nocopy (s));

          x++;
        }
    }

  pivot_table_submit (table);
}

static bool
any_value_labels (const struct variable **vars, size_t n_vars)
{
  for (size_t i = 0; i < n_vars; i++)
    if (val_labs_count (var_get_value_labels (vars[i])))
      return true;
  return false;
}

static void
display_value_labels (const struct variable **vars, size_t n_vars)
{
  if (!any_value_labels (vars, n_vars))
    return;

  struct pivot_table *table = pivot_table_create (N_("Value Labels"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN,
                          N_("Label"), N_("Label"));

  struct pivot_dimension *values = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable Value"));
  values->root->show_label = true;

  struct pivot_footnote *missing_footnote = pivot_table_create_footnote (
    table, pivot_value_new_text (N_("User-missing value")));

  for (size_t i = 0; i < n_vars; i++)
    {
      const struct val_labs *val_labs = var_get_value_labels (vars[i]);
      size_t n_labels = val_labs_count (val_labs);
      if (!n_labels)
        continue;

      struct pivot_category *group = pivot_category_create_group__ (
        values->root, pivot_value_new_variable (vars[i]));

      const struct val_lab **labels = val_labs_sorted (val_labs);
      for (size_t j = 0; j < n_labels; j++)
        {
          const struct val_lab *vl = labels[j];

          struct pivot_value *value = pivot_value_new_var_value (
            vars[i], &vl->value);
          if (value->type == PIVOT_VALUE_NUMERIC)
            value->numeric.show = SETTINGS_VALUE_SHOW_VALUE;
          else
            value->string.show = SETTINGS_VALUE_SHOW_VALUE;
          if (var_is_value_missing (vars[i], &vl->value, MV_USER))
            pivot_value_add_footnote (value, missing_footnote);
          int row = pivot_category_create_leaf (group, value);

          struct pivot_value *label = pivot_value_new_var_value (
            vars[i], &vl->value);
          char *escaped_label = xstrdup (val_lab_get_escaped_label (vl));
          if (label->type == PIVOT_VALUE_NUMERIC)
            {
              free (label->numeric.value_label);
              label->numeric.value_label = escaped_label;
              label->numeric.show = SETTINGS_VALUE_SHOW_LABEL;
            }
          else
            {
              free (label->string.value_label);
              label->string.value_label = escaped_label;
              label->string.show = SETTINGS_VALUE_SHOW_LABEL;
            }
          pivot_table_put2 (table, 0, row, label);
        }
      free (labels);
    }
  pivot_table_submit (table);
}

static bool
is_at_name (const char *name)
{
  return name[0] == '@' || (name[0] == '$' && name[1] == '@');
}

static size_t
count_attributes (const struct attrset *set, int flags)
{
  struct attrset_iterator i;
  struct attribute *attr;
  size_t n_attrs;

  n_attrs = 0;
  for (attr = attrset_first (set, &i); attr != NULL;
       attr = attrset_next (set, &i))
    if (flags & DF_AT_ATTRIBUTES || !is_at_name (attribute_get_name (attr)))
      n_attrs += attribute_get_n_values (attr);
  return n_attrs;
}

static void
display_attrset (struct pivot_table *table, struct pivot_value *set_name,
                 const struct attrset *set, int flags)
{
  size_t n_total = count_attributes (set, flags);
  if (!n_total)
    {
      pivot_value_destroy (set_name);
      return;
    }

  struct pivot_category *group = pivot_category_create_group__ (
    table->dimensions[1]->root, set_name);

  size_t n_attrs = attrset_count (set);
  struct attribute **attrs = attrset_sorted (set);
  for (size_t i = 0; i < n_attrs; i++)
    {
      const struct attribute *attr = attrs[i];
      const char *name = attribute_get_name (attr);

      if (!(flags & DF_AT_ATTRIBUTES) && is_at_name (name))
        continue;

      size_t n_values = attribute_get_n_values (attr);
      for (size_t j = 0; j < n_values; j++)
        {
          int row = pivot_category_create_leaf (
            group,
            (n_values > 1
             ? pivot_value_new_user_text_nocopy (xasprintf (
                                                   "%s[%zu]", name, j + 1))
             : pivot_value_new_user_text (name, -1)));
          pivot_table_put2 (table, 0, row,
                            pivot_value_new_user_text (
                              attribute_get_value (attr, j), -1));
        }
    }
  free (attrs);
}

static void
display_attributes (const struct attrset *dict_attrset,
                    const struct variable **vars, size_t n_vars, int flags)
{
  struct pivot_table *table = pivot_table_create (
    N_("Variable and Dataset Attributes"));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN,
                          N_("Value"), N_("Value"));

  struct pivot_dimension *variables = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Variable and Name"));
  variables->root->show_label = true;

  display_attrset (table, pivot_value_new_text (N_("(dataset)")),
                   dict_attrset, flags);
  for (size_t i = 0; i < n_vars; i++)
    display_attrset (table, pivot_value_new_variable (vars[i]),
                     var_get_attributes (vars[i]), flags);

  if (pivot_table_is_empty (table))
    pivot_table_unref (table);
  else
    pivot_table_submit (table);
}

/* Display a list of vectors.  If SORTED is nonzero then they are
   sorted alphabetically. */
static void
display_vectors (const struct dictionary *dict, int sorted)
{
  size_t n_vectors = dict_get_vector_cnt (dict);
  if (n_vectors == 0)
    {
      msg (SW, _("No vectors defined."));
      return;
    }

  const struct vector **vectors = xnmalloc (n_vectors, sizeof *vectors);
  for (size_t i = 0; i < n_vectors; i++)
    vectors[i] = dict_get_vector (dict, i);
  if (sorted)
    qsort (vectors, n_vectors, sizeof *vectors, compare_vector_ptrs_by_name);

  struct pivot_table *table = pivot_table_create (N_("Vectors"));
  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Attributes"),
                          N_("Variable"), N_("Print Format"));
  struct pivot_dimension *vector_dim = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Vector and Position"));
  vector_dim->root->show_label = true;

  for (size_t i = 0; i < n_vectors; i++)
    {
      const struct vector *vec = vectors[i];

      struct pivot_category *group = pivot_category_create_group__ (
        vector_dim->root, pivot_value_new_user_text (
          vector_get_name (vectors[i]), -1));

      for (size_t j = 0; j < vector_get_var_cnt (vec); j++)
        {
          struct variable *var = vector_get_var (vec, j);

          int row = pivot_category_create_leaf (
            group, pivot_value_new_integer (j + 1));

          pivot_table_put2 (table, 0, row, pivot_value_new_variable (var));
          char fmt_string[FMT_STRING_LEN_MAX + 1];
          fmt_to_string (var_get_print_format (var), fmt_string);
          pivot_table_put2 (table, 1, row,
                            pivot_value_new_user_text (fmt_string, -1));
        }
    }

  pivot_table_submit (table);

  free (vectors);
}

/* Encoding analysis. */

static const char *encoding_names[] = {
  /* These encodings are from http://encoding.spec.whatwg.org/, as retrieved
     February 2014.  Encodings not supported by glibc and encodings relevant
     only to HTML have been removed. */
  "utf-8",
  "windows-1252",
  "iso-8859-2",
  "iso-8859-3",
  "iso-8859-4",
  "iso-8859-5",
  "iso-8859-6",
  "iso-8859-7",
  "iso-8859-8",
  "iso-8859-10",
  "iso-8859-13",
  "iso-8859-14",
  "iso-8859-16",
  "macintosh",
  "windows-874",
  "windows-1250",
  "windows-1251",
  "windows-1253",
  "windows-1254",
  "windows-1255",
  "windows-1256",
  "windows-1257",
  "windows-1258",
  "koi8-r",
  "koi8-u",
  "ibm866",
  "gb18030",
  "big5",
  "euc-jp",
  "iso-2022-jp",
  "shift_jis",
  "euc-kr",

  /* Added by user request. */
  "ibm850",
  "din_66003",
};
#define N_ENCODING_NAMES (sizeof encoding_names / sizeof *encoding_names)

struct encoding
  {
    uint64_t encodings;
    char **utf8_strings;
    unsigned int hash;
  };

static char **
recode_strings (struct pool *pool,
                char **strings, bool *ids, size_t n,
                const char *encoding)
{
  char **utf8_strings;
  size_t i;

  utf8_strings = pool_alloc (pool, n * sizeof *utf8_strings);
  for (i = 0; i < n; i++)
    {
      struct substring utf8;
      int error;

      error = recode_pedantically ("UTF-8", encoding, ss_cstr (strings[i]),
                                   pool, &utf8);
      if (!error)
        {
          ss_rtrim (&utf8, ss_cstr (" "));
          utf8.string[utf8.length] = '\0';

          if (ids[i] && !id_is_plausible (utf8.string, false))
            error = EINVAL;
        }

      if (error)
        return NULL;

      utf8_strings[i] = utf8.string;
    }

  return utf8_strings;
}

static struct encoding *
find_duplicate_encoding (struct encoding *encodings, size_t n_encodings,
                         char **utf8_strings, size_t n_strings,
                         unsigned int hash)
{
  struct encoding *e;

  for (e = encodings; e < &encodings[n_encodings]; e++)
    {
      int i;

      if (e->hash != hash)
        goto next_encoding;

      for (i = 0; i < n_strings; i++)
        if (strcmp (utf8_strings[i], e->utf8_strings[i]))
          goto next_encoding;

      return e;
    next_encoding:;
    }

  return NULL;
}

static bool
all_equal (const struct encoding *encodings, size_t n_encodings,
           size_t string_idx)
{
  const char *s0;
  size_t i;

  s0 = encodings[0].utf8_strings[string_idx];
  for (i = 1; i < n_encodings; i++)
    if (strcmp (s0, encodings[i].utf8_strings[string_idx]))
      return false;

  return true;
}

static int
equal_prefix (const struct encoding *encodings, size_t n_encodings,
              size_t string_idx)
{
  const char *s0;
  size_t prefix;
  size_t i;

  s0 = encodings[0].utf8_strings[string_idx];
  prefix = strlen (s0);
  for (i = 1; i < n_encodings; i++)
    {
      const char *si = encodings[i].utf8_strings[string_idx];
      size_t j;

      for (j = 0; j < prefix; j++)
        if (s0[j] != si[j])
          {
            prefix = j;
            if (!prefix)
              return 0;
            break;
          }
    }

  while (prefix > 0 && s0[prefix - 1] != ' ')
    prefix--;
  return prefix;
}

static int
equal_suffix (const struct encoding *encodings, size_t n_encodings,
              size_t string_idx)
{
  const char *s0;
  size_t s0_len;
  size_t suffix;
  size_t i;

  s0 = encodings[0].utf8_strings[string_idx];
  s0_len = strlen (s0);
  suffix = s0_len;
  for (i = 1; i < n_encodings; i++)
    {
      const char *si = encodings[i].utf8_strings[string_idx];
      size_t si_len = strlen (si);
      size_t j;

      if (si_len < suffix)
        suffix = si_len;
      for (j = 0; j < suffix; j++)
        if (s0[s0_len - j - 1] != si[si_len - j - 1])
          {
            suffix = j;
            if (!suffix)
              return 0;
            break;
          }
    }

  while (suffix > 0 && s0[s0_len - suffix] != ' ')
    suffix--;
  return suffix;
}

static void
report_encodings (const struct file_handle *h, struct pool *pool,
                  char **titles, bool *ids, char **strings, size_t n_strings)
{
  struct encoding encodings[N_ENCODING_NAMES];
  size_t n_encodings, n_unique_strings;

  n_encodings = 0;
  for (size_t i = 0; i < N_ENCODING_NAMES; i++)
    {
      char **utf8_strings;
      struct encoding *e;
      unsigned int hash;

      utf8_strings = recode_strings (pool, strings, ids, n_strings,
                                     encoding_names[i]);
      if (!utf8_strings)
        continue;

      /* Hash utf8_strings. */
      hash = 0;
      for (size_t j = 0; j < n_strings; j++)
        hash = hash_string (utf8_strings[j], hash);

      /* If there's a duplicate encoding, just mark it. */
      e = find_duplicate_encoding (encodings, n_encodings,
                                   utf8_strings, n_strings, hash);
      if (e)
        {
          e->encodings |= UINT64_C (1) << i;
          continue;
        }

      e = &encodings[n_encodings++];
      e->encodings = UINT64_C (1) << i;
      e->utf8_strings = utf8_strings;
      e->hash = hash;
    }
  if (!n_encodings)
    {
      msg (SW, _("No valid encodings found."));
      return;
    }

  /* Table of valid encodings. */
  struct pivot_table *table = pivot_table_create__ (
    pivot_value_new_text_format (N_("Usable encodings for %s."),
                                 fh_get_name (h)), "Usable Encodings");
  table->caption = pivot_value_new_text_format (
    N_("Encodings that can successfully read %s (by specifying the encoding "
       "name on the GET command's ENCODING subcommand).  Encodings that "
       "yield identical text are listed together."),
    fh_get_name (h));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Encodings"),
                          N_("Encodings"));
  struct pivot_dimension *number = pivot_dimension_create__ (
    table, PIVOT_AXIS_ROW, pivot_value_new_user_text ("#", -1));
  number->root->show_label = true;

  for (size_t i = 0; i < n_encodings; i++)
    {
      struct string s = DS_EMPTY_INITIALIZER;
      for (size_t j = 0; j < 64; j++)
        if (encodings[i].encodings & (UINT64_C (1) << j))
          ds_put_format (&s, "%s, ", encoding_names[j]);
      ds_chomp (&s, ss_cstr (", "));

      int row = pivot_category_create_leaf (number->root,
                                            pivot_value_new_integer (i + 1));
      pivot_table_put2 (
        table, 0, row, pivot_value_new_user_text_nocopy (ds_steal_cstr (&s)));
    }
  pivot_table_submit (table);

  n_unique_strings = 0;
  for (size_t i = 0; i < n_strings; i++)
    if (!all_equal (encodings, n_encodings, i))
      n_unique_strings++;
  if (!n_unique_strings)
    return;

  /* Table of alternative interpretations. */
  table = pivot_table_create__ (
    pivot_value_new_text_format (N_("%s Encoded Text Strings"),
                                 fh_get_name (h)),
    "Alternate Encoded Text Strings");
  table->caption = pivot_value_new_text (
    N_("Text strings in the file dictionary that the previously listed "
       "encodings interpret differently, along with the interpretations."));

  pivot_dimension_create (table, PIVOT_AXIS_COLUMN, N_("Text"), N_("Text"));

  number = pivot_dimension_create__ (table, PIVOT_AXIS_ROW,
                                     pivot_value_new_user_text ("#", -1));
  number->root->show_label = true;
  for (size_t i = 0; i < n_encodings; i++)
    pivot_category_create_leaf (number->root,
                                pivot_value_new_integer (i + 1));

  struct pivot_dimension *purpose = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Purpose"));
  purpose->root->show_label = true;

  for (size_t i = 0; i < n_strings; i++)
    if (!all_equal (encodings, n_encodings, i))
      {
        int prefix = equal_prefix (encodings, n_encodings, i);
        int suffix = equal_suffix (encodings, n_encodings, i);

        int purpose_idx = pivot_category_create_leaf (
          purpose->root, pivot_value_new_user_text (titles[i], -1));

        for (size_t j = 0; j < n_encodings; j++)
          {
            const char *s = encodings[j].utf8_strings[i] + prefix;

            if (prefix || suffix)
              {
                size_t len = strlen (s) - suffix;
                struct string entry;

                ds_init_empty (&entry);
                if (prefix)
                  ds_put_cstr (&entry, "...");
                ds_put_substring (&entry, ss_buffer (s, len));
                if (suffix)
                  ds_put_cstr (&entry, "...");

                pivot_table_put3 (table, 0, j, purpose_idx,
                                  pivot_value_new_user_text_nocopy (
                                    ds_steal_cstr (&entry)));
              }
            else
              pivot_table_put3 (table, 0, j, purpose_idx,
                                pivot_value_new_user_text (s, -1));
          }
      }

  pivot_table_submit (table);
}
