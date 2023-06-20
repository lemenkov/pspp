/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "language/commands/placement-parser.h"

#include <assert.h>

#include "data/format.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "libpspp/message.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/c-strcase.h"
#include "gl/xalloc.h"
#include "gl/xsize.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Extensions to the format specifiers used only for
   placement. */
enum
  {
    PRS_TYPE_T = SCHAR_MAX - 3, /* Tab to absolute column. */
    PRS_TYPE_X,                 /* Skip columns. */
    PRS_TYPE_NEW_REC            /* Next record. */
  };

static bool fixed_parse_columns (struct lexer *, struct pool *, size_t n_vars,
                                 enum fmt_use, struct fmt_spec **, size_t *);
static bool fixed_parse_fortran (struct lexer *l, struct pool *, enum fmt_use,
                                 struct fmt_spec **, size_t *);

/* Parses Fortran-like or column-based specifications for placing
   variable data in fixed positions in columns and rows, that is,
   formats like those parsed by DATA LIST or PRINT.  Returns true
   only if successful.

   The formats parsed are either input or output formats, according
   to USE.

   If USE is FMT_FOR_INPUT, then T, X, and / "formats" are parsed,
   in addition to regular formats.  If USE is FMT_FOR_OUTPUT, then
   T and X "formats" are parsed but not /.

   If successful, formats for N_VARS variables are stored in
   *FORMATS, and the number of formats required is stored in
   *FORMAT_CNT.  *FORMAT_CNT may be greater than N_VARS because
   of T, X, and / "formats", but success guarantees that exactly
   N_VARS variables will be placed by the output formats.  The
   caller should call execute_placement_format to process those
   "formats" in interpreting the output.

   Uses POOL for allocation.  When the caller is finished
   interpreting *FORMATS, POOL may be destroyed. */
bool
parse_var_placements (struct lexer *lexer, struct pool *pool, size_t n_vars,
                      enum fmt_use use,
                      struct fmt_spec **formats, size_t *n_formats)
{
  assert (n_vars > 0);
  if (lex_is_number (lexer))
    return fixed_parse_columns (lexer, pool, n_vars, use,
                                formats, n_formats);
  else if (lex_match (lexer, T_LPAREN))
    {
      int start_ofs = lex_ofs (lexer);
      if (!fixed_parse_fortran (lexer, pool, use, formats, n_formats))
        return false;
      int end_ofs = lex_ofs (lexer) - 1;

      size_t n_assignments = 0;
      for (size_t i = 0; i < *n_formats; i++)
        n_assignments += (*formats)[i].type < FMT_NUMBER_OF_FORMATS;

      if (n_assignments != n_vars)
        {
          lex_ofs_error (lexer, start_ofs, end_ofs,
                         _("Number of variables specified (%zu) "
                           "differs from number of variable formats (%zu)."),
                         n_vars, n_assignments);
          return false;
        }

      return true;
    }
  else
    {
      lex_error (lexer, _("SPSS-like or Fortran-like format "
                          "specification expected after variable names."));
      return false;
    }
}

/* Implements parse_var_placements for column-based formats. */
static bool
fixed_parse_columns (struct lexer *lexer, struct pool *pool, size_t n_vars,
                     enum fmt_use use,
                     struct fmt_spec **formats, size_t *n_formats)
{
  int start_ofs = lex_ofs (lexer);

  int fc, lc;
  if (!parse_column_range (lexer, 1, &fc, &lc, NULL))
    return false;

  /* Divide columns evenly. */
  int w = (lc - fc + 1) / n_vars;
  if ((lc - fc + 1) % n_vars)
    {
      lex_ofs_error (lexer, start_ofs, lex_ofs (lexer) - 1,
                     _("The %d columns %d-%d "
                       "can't be evenly divided into %zu fields."),
                     lc - fc + 1, fc, lc, n_vars);
      return false;
    }

  /* Format specifier. */
  enum fmt_type type;
  int d;
  if (lex_match (lexer, T_LPAREN))
    {
      /* Get format type. */
      if (lex_token (lexer) == T_ID)
        {
          if (!parse_format_specifier_name (lexer, &type))
            return false;
          lex_match (lexer, T_COMMA);
        }
      else
        type = FMT_F;

      /* Get decimal places. */
      if (lex_is_integer (lexer))
        {
          d = lex_integer (lexer);
          lex_get (lexer);
        }
      else
        d = 0;

      if (!lex_force_match (lexer, T_RPAREN))
        return false;
    }
  else
    {
      type = FMT_F;
      d = 0;
    }
  int end_ofs = lex_ofs (lexer) - 1;

  struct fmt_spec format = { .type = type, .w = w, .d = d };
  char *error = fmt_check__ (format, use);
  if (error)
    {
      lex_ofs_error (lexer, start_ofs, end_ofs, "%s", error);
      free (error);
      return false;
    }

  *formats = pool_nalloc (pool, n_vars + 1, sizeof **formats);
  *n_formats = n_vars + 1;
  (*formats)[0].type = (enum fmt_type) PRS_TYPE_T;
  (*formats)[0].w = fc;
  for (size_t i = 1; i <= n_vars; i++)
    (*formats)[i] = format;
  return true;
}

/* Implements parse_var_placements for Fortran-like formats. */
static bool
fixed_parse_fortran (struct lexer *lexer, struct pool *pool, enum fmt_use use,
                     struct fmt_spec **formats, size_t *n_formats)
{
  size_t formats_allocated = 0;
  size_t formats_used = 0;

  *formats = NULL;
  while (!lex_match (lexer, T_RPAREN))
    {
      struct fmt_spec f;
      struct fmt_spec *new_formats;
      size_t n_new_formats;
      size_t count;
      size_t formats_needed;

      /* Parse count. */
      if (lex_is_integer (lexer))
        {
          count = lex_integer (lexer);
          lex_get (lexer);
        }
      else
        count = 1;

      /* Parse format specifier. */
      if (lex_match (lexer, T_LPAREN))
        {
          /* Call ourselves recursively to handle parentheses. */
          if (!fixed_parse_fortran (lexer, pool, use,
                                    &new_formats, &n_new_formats))
            return false;
        }
      else
        {
          new_formats = &f;
          n_new_formats = 1;
          if (use == FMT_FOR_INPUT && lex_match (lexer, T_SLASH))
            f.type = (enum fmt_type) PRS_TYPE_NEW_REC;
          else
            {
              int ofs = lex_ofs (lexer);
              char type[FMT_TYPE_LEN_MAX + 1];
              if (!parse_abstract_format_specifier (lexer, type, &f.w, &f.d))
                return false;

              if (!c_strcasecmp (type, "T"))
                f.type = (enum fmt_type) PRS_TYPE_T;
              else if (!c_strcasecmp (type, "X"))
                {
                  f.type = (enum fmt_type) PRS_TYPE_X;
                  f.w = count;
                  count = 1;
                }
              else
                {
                  if (!fmt_from_name (type, &f.type))
                    {
                      lex_ofs_error (lexer, ofs, ofs,
                                     _("Unknown format type `%s'."), type);
                      return false;
                    }
                  char *error = fmt_check__ (f, use);
                  if (error)
                    {
                      lex_ofs_error (lexer, ofs, ofs, "%s", error);
                      free (error);
                      return false;
                    }
                }
            }
        }

      /* Add COUNT copies of the NEW_FORMAT_CNT formats in
         NEW_FORMATS to FORMATS. */
      if (n_new_formats != 0
          && size_overflow_p (xtimes (xsum (formats_used,
                                            xtimes (count, n_new_formats)),
                                      sizeof *formats)))
        xalloc_die ();
      formats_needed = count * n_new_formats;
      if (formats_used + formats_needed > formats_allocated)
        {
          formats_allocated = formats_used + formats_needed;
          *formats = pool_2nrealloc (pool, *formats, &formats_allocated,
                                     sizeof **formats);
        }
      for (; count > 0; count--)
        {
          memcpy (&(*formats)[formats_used], new_formats,
                  sizeof **formats * n_new_formats);
          formats_used += n_new_formats;
        }

      lex_match (lexer, T_COMMA);
    }

  *n_formats = formats_used;
  return true;
}

/* Checks whether FORMAT represents one of the special "formats"
   for T, X, or /.  If so, updates *RECORD or *COLUMN (or both)
   as appropriate, and returns true.  Otherwise, returns false
   without any side effects. */
bool
execute_placement_format (const struct fmt_spec format,
                          int *record, int *column)
{
  switch ((int) format.type)
    {
    case PRS_TYPE_X:
      *column += format.w;
      return true;

    case PRS_TYPE_T:
      *column = format.w;
      return true;

    case PRS_TYPE_NEW_REC:
      (*record)++;
      *column = 1;
      return true;

    default:
      assert (format.type < FMT_NUMBER_OF_FORMATS);
      return false;
    }
}

static bool
parse_column__ (struct lexer *lexer, bool negative, int base, int *column)
{
  assert (base == 0 || base == 1);

  if (!lex_force_int (lexer))
    return false;
  long int value = lex_integer (lexer);
  if (negative)
    value = -value;
  lex_get (lexer);

  *column = value - base + 1;
  if (*column < 1)
    {
      if (base == 1)
        lex_next_error (lexer, -1, -1,
                        _("Column positions for fields must be positive."));
      else
        lex_next_error (lexer, -1, -1,
                        _("Column positions for fields must not be negative."));
      return false;
    }
  return true;
}

/* Parses a BASE-based column using LEXER.  Returns true and
   stores a 1-based column number into *COLUMN if successful,
   otherwise emits an error message and returns false.

   If BASE is 0, zero-based column numbers are parsed; if BASE is
   1, 1-based column numbers are parsed.  Regardless of BASE, the
   values stored in *FIRST_COLUMN and *LAST_COLUMN are
   1-based. */
bool
parse_column (struct lexer *lexer, int base, int *column)
{
  return parse_column__ (lexer, false, base, column);
}

/* Parse a column or a range of columns, specified as a single
   integer or two integers delimited by a dash.  Stores the range
   in *FIRST_COLUMN and *LAST_COLUMN.  (If only a single integer
   is given, it is stored in both.)  If RANGE_SPECIFIED is
   non-null, then *RANGE_SPECIFIED is set to true if the syntax
   contained a dash, false otherwise.  Returns true if
   successful, false if the syntax was invalid or the values
   specified did not make sense.

   If BASE is 0, zero-based column numbers are parsed; if BASE is
   1, 1-based column numbers are parsed.  Regardless of BASE, the
   values stored in *FIRST_COLUMN and *LAST_COLUMN are
   1-based. */
bool
parse_column_range (struct lexer *lexer, int base,
                    int *first_column, int *last_column,
                    bool *range_specified)
{
  int start_ofs = lex_ofs (lexer);

  /* First column. */
  if (!parse_column__ (lexer, false, base, first_column))
    return false;

  /* Last column. */
  if (lex_is_integer (lexer) && lex_integer (lexer) < 0)
    {
      if (!parse_column__ (lexer, true, base, last_column))
        return false;

      if (*last_column < *first_column)
        {
          lex_ofs_error (lexer, start_ofs, lex_ofs (lexer) - 1,
                         _("The ending column for a field must be "
                           "greater than the starting column."));
          return false;
        }

      if (range_specified)
        *range_specified = true;
    }
  else
    {
      *last_column = *first_column;
      if (range_specified)
        *range_specified = false;
    }

  return true;
}

/* Parses a (possibly empty) sequence of slashes, each of which
   may be followed by an integer.  A slash on its own increases
   *RECORD by 1 and sets *COLUMN to 1.  A slash followed by an
   integer sets *RECORD to the integer, as long as that increases
   *RECORD, and sets *COLUMN to 1.

   Returns true if successful, false on syntax error. */
bool
parse_record_placement (struct lexer *lexer, int *record, int *column)
{
  while (lex_match (lexer, T_SLASH))
    {
      if (lex_is_number (lexer))
        {
          if (!lex_force_int_range (lexer, NULL, *record + 1, INT_MAX))
            return false;
          *record = lex_integer (lexer);
          lex_get (lexer);
        }
      else
        (*record)++;
      *column = 1;
    }
  assert (*record >= 1);

  return true;
}
