/* PSPP - a program for statistical analysis.
   Copyright (C) 2017 Free Software Foundation, Inc.

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

#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/data-in.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/short-names.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/commands/data-parser.h"
#include "language/commands/data-reader.h"
#include "language/commands/file-handle.h"
#include "language/commands/inpt-pgm.h"
#include "language/commands/placement-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/intern.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/c-ctype.h"
#include "gl/minmax.h"
#include "gl/xsize.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#define ROWTYPES                                \
    /* Matrix row types. */                     \
    RT(CORR,     2)                             \
    RT(COV,      2)                             \
    RT(MAT,      2)                             \
    RT(N_MATRIX, 2)                             \
    RT(PROX,     2)                             \
                                                \
    /* Vector row types. */                     \
    RT(COUNT,    1)                             \
    RT(DFE,      1)                             \
    RT(MEAN,     1)                             \
    RT(MSE,      1)                             \
    RT(STDDEV,   1)                             \
    RT(N, 1)                                    \
                                                \
    /* Scalar row types. */                     \
    RT(N_SCALAR, 0)

enum rowtype
  {
#define RT(NAME, DIMS) C_##NAME,
    ROWTYPES
#undef RT
  };

enum
  {
#define RT(NAME, DIMS) +1
    N_ROWTYPES = ROWTYPES
#undef RT
  };
verify (N_ROWTYPES < 32);

/* Returns the number of dimensions in the indexes for row type RT.  A matrix
   has 2 dimensions, a vector has 1, a scalar has 0. */
static int
rowtype_dimensions (enum rowtype rt)
{
  static const int rowtype_dims[N_ROWTYPES] = {
#define RT(NAME, DIMS) [C_##NAME] = DIMS,
    ROWTYPES
#undef RT
  };
  return rowtype_dims[rt];
}

static struct substring
rowtype_name (enum rowtype rt)
{
  static const struct substring rowtype_names[N_ROWTYPES] = {
#define RT(NAME, DIMS) [C_##NAME] = SS_LITERAL_INITIALIZER (#NAME),
    ROWTYPES
#undef RT
  };

  return rowtype_names[rt];
}

static bool
rowtype_from_string (struct substring token, enum rowtype *rt)
{
  ss_trim (&token, ss_cstr (CC_SPACES));
  for (size_t i = 0; i < N_ROWTYPES; i++)
    if (lex_id_match (rowtype_name (i), token))
      {
        *rt = i;
        return true;
      }

  if (lex_id_match (ss_cstr ("N_VECTOR"), token))
    {
      *rt = C_N;
      return true;
    }
  else if (lex_id_match (ss_cstr ("SD"), token))
    {
      *rt = C_STDDEV;
      return true;
    }

  return false;
}

static bool
rowtype_parse (struct lexer *lexer, enum rowtype *rt)
{
  bool parsed = (lex_token (lexer) == T_ID
                 && rowtype_from_string (lex_tokss (lexer), rt));
  if (parsed)
    lex_get (lexer);
  return parsed;
}

struct matrix_format
  {
    bool span;
    enum triangle
      {
        LOWER,
        UPPER,
        FULL
      }
    triangle;
    enum diagonal
      {
        DIAGONAL,
        NO_DIAGONAL
      }
    diagonal;

    bool input_rowtype;
    struct variable **input_vars;
    size_t n_input_vars;

    /* How to read matrices with each possible number of dimensions (0=scalar,
       1=vector, 2=matrix). */
    struct matrix_sched
      {
        /* Number of rows and columns in the matrix: (1,1) for a scalar, (1,n) for
           a vector, (n,n) for a matrix. */
        int nr, nc;

        /* Rows of data to read and the number of columns in each.  Because we
           often read just a triangle and sometimes omit the diagonal, 'n_rp' can
           be less than 'nr' and 'rp[i]->y' isn't always 'y'. */
        struct row_sched
          {
            /* The y-value of the row inside the matrix. */
            int y;

            /* first and last (exclusive) columns to read in this row. */
            int x0, x1;
          }
          *rp;
        size_t n_rp;
      }
    ms[3];

    struct variable *rowtype;
    struct variable *varname;
    struct variable **cvars;
    int n_cvars;
    struct variable **svars;
    size_t *svar_indexes;
    size_t n_svars;
    struct variable **fvars;
    size_t *fvar_indexes;
    size_t n_fvars;
    int cells;
    int n;

    unsigned int pooled_rowtype_mask;
    unsigned int factor_rowtype_mask;

    struct content
      {
        bool open;
        enum rowtype rowtype;
        bool close;
      }
    *contents;
    size_t n_contents;
  };

static void
matrix_format_uninit (struct matrix_format *mf)
{
  free (mf->input_vars);
  for (int i = 0; i < 3; i++)
    free (mf->ms[i].rp);
  free (mf->cvars);
  free (mf->svars);
  free (mf->svar_indexes);
  free (mf->fvars);
  free (mf->fvar_indexes);
  free (mf->contents);
}

static void
set_string (struct ccase *outcase, const struct variable *var,
            struct substring src)
{
  struct substring dst = case_ss (outcase, var);
  for (size_t i = 0; i < dst.length; i++)
    dst.string[i] = i < src.length ? src.string[i] : ' ';
}

static void
parse_msg (struct dfm_reader *reader, const struct substring *token,
           char *text, enum msg_severity severity)
{
  int first_column = 0;
  if (token)
    {
      struct substring line = dfm_get_record (reader);
      if (token->string >= line.string && token->string < ss_end (line))
        first_column = ss_pointer_to_position (line, token->string) + 1;
    }

  int line_number = dfm_get_line_number (reader);
  struct msg_location *location = xmalloc (sizeof *location);
  int last_column = (first_column && token->length
                     ? first_column + token->length - 1
                     : 0);
  *location = (struct msg_location) {
    .file_name = intern_new (dfm_get_file_name (reader)),
    .start = { .line = line_number, .column = first_column },
    .end = { .line = line_number, .column = last_column },
  };
  struct msg *m = xmalloc (sizeof *m);
  *m = (struct msg) {
    .category = MSG_C_DATA,
    .severity = severity,
    .location = location,
    .text = text,
  };
  msg_emit (m);
}

static void PRINTF_FORMAT (3, 4)
parse_warning (struct dfm_reader *reader, const struct substring *token,
               const char *format, ...)
{
  va_list args;
  va_start (args, format);
  parse_msg (reader, token, xvasprintf (format, args), MSG_S_WARNING);
  va_end (args);
}

static void PRINTF_FORMAT (3, 4)
parse_error (struct dfm_reader *reader, const struct substring *token,
             const char *format, ...)
{
  va_list args;
  va_start (args, format);
  parse_msg (reader, token, xvasprintf (format, args), MSG_S_ERROR);
  va_end (args);
}

/* Advance to beginning of next token. */
static bool
more_tokens (struct substring *p, struct dfm_reader *r)
{
  for (;;)
    {
      ss_ltrim (p, ss_cstr (CC_SPACES ","));
      if (p->length)
        return true;

      dfm_forward_record (r);
      if (dfm_eof (r))
        return false;
      *p = dfm_get_record (r);
    }
}

static bool
next_token (struct substring *p, struct dfm_reader *r, struct substring *token)
{
  if (!more_tokens (p, r))
    return false;

  /* Collect token. */
  int c = ss_first (*p);
  if (c == '\'' || c == '"')
    {
      ss_advance (p, 1);
      ss_get_until (p, c, token);
    }
  else
    {
      size_t n = 1;
      for (;;)
        {
          c = ss_at (*p, n);
          if (c == EOF
              || ss_find_byte (ss_cstr (CC_SPACES ","), c) != SIZE_MAX
              || ((c == '+' || c == '-')
                  && ss_find_byte (ss_cstr ("dDeE"),
                                   ss_at (*p, n - 1)) == SIZE_MAX))
            break;
          n++;
        }
      ss_get_bytes (p, n, token);
    }
  return true;
}

static bool
next_number (struct substring *p, struct dfm_reader *r, double *d)
{
  struct substring token;
  if (!next_token (p, r, &token))
    return false;

  union value v;
  char *error = data_in (token, dfm_reader_get_encoding (r), FMT_F,
                         settings_get_fmt_settings (), &v, 0, NULL);
  if (error)
    {
      parse_error (r, &token, "%s", error);
      free (error);
    }
  *d = v.f;
  return true;
}

static bool
next_rowtype (struct substring *p, struct dfm_reader *r, enum rowtype *rt)
{
  struct substring token;
  if (!next_token (p, r, &token))
    return false;

  if (rowtype_from_string (token, rt))
    return true;

  parse_error (r, &token, _("Unknown row type \"%.*s\"."),
               (int) token.length, token.string);
  return false;
}

struct read_matrix_params
  {
    /* Adjustments to first and last row to read. */
    int dy0, dy1;

    /* Left and right columns to read in first row, inclusive.
       For x1, INT_MAX is the rightmost column. */
    int x0, x1;

    /* Adjustment to x0 and x1 for each subsequent row we read.  Each of these
       is 0 to keep it the same or -1 or +1 to adjust it by that much. */
    int dx0, dx1;
  };

static const struct read_matrix_params *
get_read_matrix_params (const struct matrix_format *mf)
{
  if (mf->triangle == FULL)
    {
      /* 1 2 3 4
         2 1 5 6
         3 5 1 7
         4 6 7 1 */
      static const struct read_matrix_params rmp = { 0, 0, 0, INT_MAX, 0, 0 };
      return &rmp;
    }
  else if (mf->triangle == LOWER)
    {
      if (mf->diagonal == DIAGONAL)
        {
          /* 1 . . .
             2 1 . .
             3 5 1 .
             4 6 7 1 */
          static const struct read_matrix_params rmp = { 0, 0, 0, 0, 0, 1 };
          return &rmp;
        }
      else
        {
          /* . . . .
             2 . . .
             3 5 . .
             4 6 7 . */
          static const struct read_matrix_params rmp = { 1, 0, 0, 0, 0, 1 };
          return &rmp;
        }
    }
  else if (mf->triangle == UPPER)
    {
      if (mf->diagonal == DIAGONAL)
        {
          /* 1 2 3 4
             . 1 5 6
             . . 1 7
             . . . 1 */
          static const struct read_matrix_params rmp = { 0, 0, 0, INT_MAX, 1, 0 };
          return &rmp;
        }
      else
        {
          /* . 2 3 4
             . . 5 6
             . . . 7
             . . . . */
          static const struct read_matrix_params rmp = { 0, -1, 1, INT_MAX, 1, 0 };
          return &rmp;
        }
    }
  else
    NOT_REACHED ();
}

static void
schedule_matrices (struct matrix_format *mf)
{
  struct matrix_sched *ms0 = &mf->ms[0];
  ms0->nr = 1;
  ms0->nc = 1;
  ms0->rp = xmalloc (sizeof *ms0->rp);
  ms0->rp[0] = (struct row_sched) { .y = 0, .x0 = 0, .x1 = 1 };
  ms0->n_rp = 1;

  struct matrix_sched *ms1 = &mf->ms[1];
  ms1->nr = 1;
  ms1->nc = mf->n_cvars;
  ms1->rp = xmalloc (sizeof *ms1->rp);
  ms1->rp[0] = (struct row_sched) { .y = 0, .x0 = 0, .x1 = mf->n_cvars };
  ms1->n_rp = 1;

  struct matrix_sched *ms2 = &mf->ms[2];
  ms2->nr = mf->n_cvars;
  ms2->nc = mf->n_cvars;
  ms2->rp = xmalloc (mf->n_cvars * sizeof *ms2->rp);
  ms2->n_rp = 0;

  const struct read_matrix_params *rmp = get_read_matrix_params (mf);
  int x0 = rmp->x0;
  int x1 = rmp->x1 < mf->n_cvars ? rmp->x1 : mf->n_cvars - 1;
  int y0 = rmp->dy0;
  int y1 = (int) mf->n_cvars + rmp->dy1;
  for (int y = y0; y < y1; y++)
    {
      assert (x0 >= 0 && x0 < mf->n_cvars);
      assert (x1 >= 0 && x1 < mf->n_cvars);
      assert (x1 >= x0);

      ms2->rp[ms2->n_rp++] = (struct row_sched) {
        .y = y, .x0 = x0, .x1 = x1 + 1
      };

      x0 += rmp->dx0;
      x1 += rmp->dx1;
    }
}

static bool
read_id_columns (const struct matrix_format *mf,
                 struct substring *p, struct dfm_reader *r,
                 double *d, enum rowtype *rt)
{
  for (size_t i = 0; mf->input_vars[i] != mf->cvars[0]; i++)
    if (!(mf->input_vars[i] == mf->rowtype
          ? next_rowtype (p, r, rt)
          : next_number (p, r, &d[i])))
      return false;
  return true;
}

static bool
equal_id_columns (const struct matrix_format *mf,
                  const double *a, const double *b)
{
  for (size_t i = 0; mf->input_vars[i] != mf->cvars[0]; i++)
    if (mf->input_vars[i] != mf->rowtype && a[i] != b[i])
      return false;
  return true;
}

static bool
equal_split_columns (const struct matrix_format *mf,
                     const double *a, const double *b)
{
  for (size_t i = 0; i < mf->n_svars; i++)
    {
      size_t idx = mf->svar_indexes[i];
      if (a[idx] != b[idx])
        return false;
    }
  return true;
}

static bool
is_pooled (const struct matrix_format *mf, const double *d)
{
  for (size_t i = 0; i < mf->n_fvars; i++)
    if (d[mf->fvar_indexes[i]] != SYSMIS)
      return false;
  return true;
}

static void
matrix_sched_init (const struct matrix_format *mf, enum rowtype rt,
                   gsl_matrix *m)
{
  int n_dims = rowtype_dimensions (rt);
  const struct matrix_sched *ms = &mf->ms[n_dims];
  double diagonal = n_dims < 2 || rt != C_CORR ? SYSMIS : 1.0;
  for (size_t y = 0; y < ms->nr; y++)
    for (size_t x = 0; x < ms->nc; x++)
      gsl_matrix_set (m, y, x, y == x ? diagonal : SYSMIS);
}

static struct ccase *
matrix_sched_output_create_case (const struct matrix_format *mf,
                                 enum rowtype rt, const struct variable *var,
                                 const double *d, int split_num,
                                 struct casewriter *w)
{
  struct ccase *c = case_create (casewriter_get_proto (w));
  for (size_t i = 0; mf->input_vars[i] != mf->cvars[0]; i++)
    if (mf->input_vars[i] != mf->rowtype)
      *case_num_rw (c, mf->input_vars[i]) = d[i];
  if (mf->n_svars && !mf->svar_indexes)
    *case_num_rw (c, mf->svars[0]) = split_num;
  set_string (c, mf->rowtype, rowtype_name (rt));
  const char *varname = var ? var_get_name (var) : "";
  set_string (c, mf->varname, ss_cstr (varname));
  return c;
}

static void
matrix_sched_output_n (const struct matrix_format *mf, double n,
                       const double *d, int split_num, struct casewriter *w)
{
  struct ccase *c = matrix_sched_output_create_case (mf, C_N, NULL, d,
                                                     split_num, w);
  for (int x = 0; x < mf->n_cvars; x++)
    *case_num_rw (c, mf->cvars[x]) = n;
  casewriter_write (w, c);
}

static void
matrix_sched_output (const struct matrix_format *mf, enum rowtype rt,
                     gsl_matrix *m, const double *d, int split_num,
                     struct casewriter *w)
{
  int n_dims = rowtype_dimensions (rt);
  const struct matrix_sched *ms = &mf->ms[n_dims];

  if (rt == C_N_SCALAR)
    {
      matrix_sched_output_n (mf, gsl_matrix_get (m, 0, 0), d, split_num, w);
      return;
    }

  for (int y = 0; y < ms->nr; y++)
    {
      const struct variable *var = n_dims == 2 ? mf->cvars[y] : NULL;
      struct ccase *c = matrix_sched_output_create_case (mf, rt, var, d,
                                                         split_num, w);
      for (int x = 0; x < mf->n_cvars; x++)
        *case_num_rw (c, mf->cvars[x]) = gsl_matrix_get (m, y, x);
      casewriter_write (w, c);
    }
}

static void
check_eol (const struct matrix_format *mf, struct substring *p,
           struct dfm_reader *r)
{
  if (!mf->span)
    {
      ss_ltrim (p, ss_cstr (CC_SPACES ","));
      if (p->length)
        {
          parse_error (r, p, _("Extraneous data expecting end of line."));
          p->length = 0;
        }
    }
}

static void
parse_data_with_rowtype (const struct matrix_format *mf,
                         struct dfm_reader *r, struct casewriter *w)
{
  if (dfm_eof (r))
    return;
  struct substring p = dfm_get_record (r);

  double *prev = NULL;
  gsl_matrix *m = gsl_matrix_alloc (mf->n_cvars, mf->n_cvars);

  double *d = xnmalloc (mf->n_input_vars, sizeof *d);
  enum rowtype rt;

  double *d_next = xnmalloc (mf->n_input_vars, sizeof *d_next);

  if (!read_id_columns (mf, &p, r, d, &rt))
    goto exit;
  for (;;)
    {
      /* If this has rowtype N but there was an N subcommand, then the
         subcommand takes precedence, so we will suppress outputting this
         record.  We still need to parse it, though, so we can't skip other
         work. */
      bool suppress_output = mf->n >= 0 && (rt == C_N || rt == C_N_SCALAR);
      if (suppress_output)
        parse_error (r, NULL, _("N record is not allowed with N subcommand.  "
                                "Ignoring N record."));

      /* If there's an N subcommand, and this is a new split, then output an N
         record. */
      if (mf->n >= 0 && (!prev || !equal_split_columns (mf, prev, d)))
        {
          matrix_sched_output_n (mf, mf->n, d, 0, w);

          if (!prev)
            prev = xnmalloc (mf->n_input_vars, sizeof *prev);
          memcpy (prev, d, mf->n_input_vars * sizeof *prev);
        }

      /* Usually users don't provide the CONTENTS subcommand with ROWTYPE_, but
         if they did then warn if ROWTYPE_ is an unexpected type. */
      if (mf->factor_rowtype_mask || mf->pooled_rowtype_mask)
        {
          const char *name = rowtype_name (rt).string;
          if (is_pooled (mf, d))
            {
              if (!((1u << rt) & mf->pooled_rowtype_mask))
                parse_warning (r, NULL, _("Data contains pooled row type %s not "
                                          "included in CONTENTS."), name);
            }
          else
            {
              if (!((1u << rt) & mf->factor_rowtype_mask))
                parse_warning (r, NULL, _("Data contains with-factors row type "
                                          "%s not included in CONTENTS."), name);
            }
        }

      /* Initialize the matrix to be filled-in. */
      int n_dims = rowtype_dimensions (rt);
      const struct matrix_sched *ms = &mf->ms[n_dims];
      matrix_sched_init (mf, rt, m);

      enum rowtype rt_next;
      bool eof;

      size_t n_rows;
      for (n_rows = 1; ; n_rows++)
        {
          if (n_rows <= ms->n_rp)
            {
              const struct row_sched *rs = &ms->rp[n_rows - 1];
              size_t y = rs->y;
              for (size_t x = rs->x0; x < rs->x1; x++)
                {
                  double e;
                  if (!next_number (&p, r, &e))
                    goto exit;
                  gsl_matrix_set (m, y, x, e);
                  if (n_dims == 2 && mf->triangle != FULL)
                    gsl_matrix_set (m, x, y, e);
                }
              check_eol (mf, &p, r);
            }
          else
            {
              /* Suppress bad input data.  We'll issue an error later. */
              p.length = 0;
            }

          eof = (!more_tokens (&p, r)
                 || !read_id_columns (mf, &p, r, d_next, &rt_next));
          if (eof)
            break;

          if (!equal_id_columns (mf, d, d_next) || rt_next != rt)
            break;
        }
      if (!suppress_output)
        matrix_sched_output (mf, rt, m, d, 0, w);

      if (n_rows != ms->n_rp)
        parse_error (r, NULL,
                     _("Matrix %s had %zu rows but %zu rows were expected."),
                     rowtype_name (rt).string, n_rows, ms->n_rp);
      if (eof)
        break;

      double *d_tmp = d;
      d = d_next;
      d_next = d_tmp;

      rt = rt_next;
    }

exit:
  free (prev);
  gsl_matrix_free (m);
  free (d);
  free (d_next);
}

static void
parse_matrix_without_rowtype (const struct matrix_format *mf,
                              struct substring *p, struct dfm_reader *r,
                              gsl_matrix *m, enum rowtype rowtype, bool pooled,
                              int split_num, bool *first, struct casewriter *w)
{
  int n_dims = rowtype_dimensions (rowtype);
  const struct matrix_sched *ms = &mf->ms[n_dims];

  double *d = xnmalloc (mf->n_input_vars, sizeof *d);
  matrix_sched_init (mf, rowtype, m);
  for (size_t i = 0; i < ms->n_rp; i++)
    {
      int y = ms->rp[i].y;
      int k = 0;
      int h = 0;
      for (size_t j = 0; j < mf->n_input_vars; j++)
        {
          const struct variable *iv = mf->input_vars[j];
          if (k < mf->n_cvars && iv == mf->cvars[k])
            {
              if (k < ms->rp[i].x1 - ms->rp[i].x0)
                {
                  double e;
                  if (!next_number (p, r, &e))
                    goto exit;

                  int x = k + ms->rp[i].x0;
                  gsl_matrix_set (m, y, x, e);
                  if (n_dims == 2 && mf->triangle != FULL)
                    gsl_matrix_set (m, x, y, e);
                }
              k++;
              continue;
            }
          if (h < mf->n_fvars && iv == mf->fvars[h])
            {
              h++;
              if (pooled)
                {
                  d[j] = SYSMIS;
                  continue;
                }
            }

          double e;
          if (!next_number (p, r, &e))
            goto exit;
          d[j] = e;
        }
      check_eol (mf, p, r);
    }

  /* If there's an N subcommand, and this is a new split, then output an N
     record. */
  if (mf->n >= 0 && *first)
    {
      *first = false;
      matrix_sched_output_n (mf, mf->n, d, 0, w);
    }

  matrix_sched_output (mf, rowtype, m, d, split_num, w);
exit:
  free (d);
}

static void
parse_data_without_rowtype (const struct matrix_format *mf,
                            struct dfm_reader *r, struct casewriter *w)
{
  if (dfm_eof (r))
    return;
  struct substring p = dfm_get_record (r);

  gsl_matrix *m = gsl_matrix_alloc (mf->n_cvars, mf->n_cvars);

  int split_num = 1;
  do
    {
      bool first = true;
      for (size_t i = 0; i < mf->n_contents; )
        {
          size_t j = i;
          if (mf->contents[i].open)
            while (!mf->contents[j].close)
              j++;

          if (mf->contents[i].open)
            {
              for (size_t k = 0; k < mf->cells; k++)
                for (size_t h = i; h <= j; h++)
                  parse_matrix_without_rowtype (mf, &p, r, m,
                                                mf->contents[h].rowtype, false,
                                                split_num, &first, w);
            }
          else
            parse_matrix_without_rowtype (mf, &p, r, m, mf->contents[i].rowtype,
                                          true, split_num, &first, w);
          i = j + 1;
        }

      split_num++;
    }
  while (more_tokens (&p, r));

  gsl_matrix_free (m);
}

/* Parses VARIABLES=varnames for MATRIX DATA and returns a dictionary with the
   named variables in it. */
static struct dictionary *
parse_matrix_data_variables (struct lexer *lexer)
{
  lex_match (lexer, T_SLASH);
  if (!lex_force_match_id (lexer, "VARIABLES"))
    return NULL;
  lex_match (lexer, T_EQUALS);

  struct dictionary *dict = dict_create (get_default_encoding ());

  size_t n_names = 0;
  char **names = NULL;
  int vars_start = lex_ofs (lexer);
  if (!parse_DATA_LIST_vars (lexer, dict, &names, &n_names, PV_NO_DUPLICATE))
    {
      dict_unref (dict);
      return NULL;
    }
  int vars_end = lex_ofs (lexer) - 1;

  for (size_t i = 0; i < n_names; i++)
    if (!strcasecmp (names[i], "ROWTYPE_"))
      dict_create_var_assert (dict, "ROWTYPE_", 8);
    else
      {
        struct variable *var = dict_create_var_assert (dict, names[i], 0);
        var_set_measure (var, MEASURE_SCALE);
      }

  for (size_t i = 0; i < n_names; ++i)
    free (names[i]);
  free (names);

  if (dict_lookup_var (dict, "VARNAME_"))
    {
      lex_ofs_error (lexer, vars_start, vars_end,
                     _("VARIABLES may not include VARNAME_."));
      dict_unref (dict);
      return NULL;
    }
  return dict;
}

static bool
parse_matrix_data_subvars (struct lexer *lexer, struct dictionary *dict,
                           bool *taken_vars,
                           struct variable ***vars, size_t **indexes,
                           size_t *n_vars)
{
  int start_ofs = lex_ofs (lexer);
  if (!parse_variables (lexer, dict, vars, n_vars, 0))
    return false;
  int end_ofs = lex_ofs (lexer) - 1;

  *indexes = xnmalloc (*n_vars, sizeof **indexes);
  for (size_t i = 0; i < *n_vars; i++)
    {
      struct variable *v = (*vars)[i];
      if (!strcasecmp (var_get_name (v), "ROWTYPE_"))
        {
          lex_ofs_error (lexer, start_ofs, end_ofs,
                         _("ROWTYPE_ is not allowed on SPLIT or FACTORS."));
          goto error;
        }
      (*indexes)[i] = var_get_dict_index (v);

      bool *tv = &taken_vars[var_get_dict_index (v)];
      if (*tv)
        {
          lex_ofs_error (lexer, start_ofs, end_ofs,
                         _("%s may not appear on both SPLIT and FACTORS."),
                         var_get_name (v));
          goto error;
        }
      *tv = true;

      var_set_measure (v, MEASURE_NOMINAL);
      var_set_both_formats (v, (struct fmt_spec) { .type = FMT_F, .w = 4 });
    }
  return true;

error:
  free (*vars);
  *vars = NULL;
  *n_vars = 0;
  free (*indexes);
  *indexes = NULL;
  return false;
}

int
cmd_matrix_data (struct lexer *lexer, struct dataset *ds)
{
  int input_vars_start = lex_ofs (lexer);
  struct dictionary *dict = parse_matrix_data_variables (lexer);
  if (!dict)
    return CMD_FAILURE;
  int input_vars_end = lex_ofs (lexer) - 1;

  size_t n_input_vars = dict_get_n_vars (dict);
  struct variable **input_vars = xnmalloc (n_input_vars, sizeof *input_vars);
  for (size_t i = 0; i < n_input_vars; i++)
    input_vars[i] = dict_get_var (dict, i);

  int varname_width = 8;
  for (size_t i = 0; i < n_input_vars; i++)
    {
      int w = strlen (var_get_name (input_vars[i]));
      varname_width = MAX (w, varname_width);
    }

  struct variable *rowtype = dict_lookup_var (dict, "ROWTYPE_");
  bool input_rowtype = rowtype != NULL;
  if (!rowtype)
    rowtype = dict_create_var_assert (dict, "ROWTYPE_", 8);

  struct matrix_format mf = {
    .input_rowtype = input_rowtype,
    .input_vars = input_vars,
    .n_input_vars = n_input_vars,

    .rowtype = rowtype,
    .varname = dict_create_var_assert (dict, "VARNAME_", varname_width),

    .triangle = LOWER,
    .diagonal = DIAGONAL,
    .n = -1,
    .cells = -1,
  };

  bool *taken_vars = XCALLOC (n_input_vars, bool);
  if (input_rowtype)
    taken_vars[var_get_dict_index (rowtype)] = true;

  struct file_handle *fh = NULL;
  int n_start = 0;
  int n_end = 0;
  while (lex_token (lexer) != T_ENDCMD)
    {
      if (!lex_force_match (lexer, T_SLASH))
        goto error;

      if (lex_match_id (lexer, "N"))
        {
          n_start = lex_ofs (lexer) - 1;
          lex_match (lexer, T_EQUALS);

          if (!lex_force_int_range (lexer, "N", 0, INT_MAX))
            goto error;

          mf.n = lex_integer (lexer);
          n_end = lex_ofs (lexer);
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "FORMAT"))
        {
          int start_ofs = lex_ofs (lexer) - 1;
          lex_match (lexer, T_EQUALS);

          while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
            {
              if (lex_match_id (lexer, "LIST"))
                mf.span = false;
              else if (lex_match_id (lexer, "FREE"))
                mf.span = true;
              else if (lex_match_id (lexer, "UPPER"))
                mf.triangle = UPPER;
              else if (lex_match_id (lexer, "LOWER"))
                mf.triangle = LOWER;
              else if (lex_match_id (lexer, "FULL"))
                mf.triangle = FULL;
              else if (lex_match_id (lexer, "DIAGONAL"))
                mf.diagonal = DIAGONAL;
              else if (lex_match_id (lexer, "NODIAGONAL"))
                mf.diagonal = NO_DIAGONAL;
              else
                {
                  lex_error_expecting (lexer, "LIST", "FREE",
                                       "UPPER", "LOWER", "FULL",
                                       "DIAGONAL", "NODIAGONAL");
                  goto error;
                }
            }
          int end_ofs = lex_ofs (lexer) - 1;

          if (mf.diagonal == NO_DIAGONAL && mf.triangle == FULL)
            {
              lex_ofs_error (lexer, start_ofs, end_ofs,
                             _("FORMAT=FULL and FORMAT=NODIAGONAL are "
                               "mutually exclusive."));
              goto error;
            }
        }
      else if (lex_match_id (lexer, "FILE"))
        {
          lex_match (lexer, T_EQUALS);
          fh_unref (fh);
          fh = fh_parse (lexer, FH_REF_FILE | FH_REF_INLINE, NULL);
          if (!fh)
            goto error;
        }
      else if (!mf.n_svars && lex_match_id (lexer, "SPLIT"))
        {
          lex_match (lexer, T_EQUALS);
          if (!mf.input_rowtype
              && lex_token (lexer) == T_ID
              && !dict_lookup_var (dict, lex_tokcstr (lexer)))
            {
              mf.svars = xmalloc (sizeof *mf.svars);
              mf.svars[0] = dict_create_var_assert (dict, lex_tokcstr (lexer),
                                                    0);
              var_set_measure (mf.svars[0], MEASURE_NOMINAL);
              var_set_both_formats (
                mf.svars[0], (struct fmt_spec) { .type = FMT_F, .w = 4 });
              mf.n_svars = 1;
              lex_get (lexer);
            }
          else if (!parse_matrix_data_subvars (lexer, dict, taken_vars,
                                               &mf.svars, &mf.svar_indexes,
                                               &mf.n_svars))
            goto error;
        }
      else if (!mf.n_fvars && lex_match_id (lexer, "FACTORS"))
        {
          lex_match (lexer, T_EQUALS);
          if (!parse_matrix_data_subvars (lexer, dict, taken_vars,
                                          &mf.fvars, &mf.fvar_indexes,
                                          &mf.n_fvars))
            goto error;
        }
      else if (lex_match_id (lexer, "CELLS"))
        {
          if (mf.input_rowtype)
            lex_next_msg (lexer, SW,
                          -1, -1, _("CELLS is ignored when VARIABLES "
                                    "includes ROWTYPE_"));

          lex_match (lexer, T_EQUALS);

          if (!lex_force_int_range (lexer, "CELLS", 0, INT_MAX))
            goto error;

          mf.cells = lex_integer (lexer);
          lex_get (lexer);
        }
      else if (lex_match_id (lexer, "CONTENTS"))
        {
          lex_match (lexer, T_EQUALS);

          size_t allocated_contents = mf.n_contents;
          bool in_parens = false;
          for (;;)
            {
              bool open = !in_parens && lex_match (lexer, T_LPAREN);
              enum rowtype rt;
              if (!rowtype_parse (lexer, &rt))
                {
                  if (open || in_parens || (lex_token (lexer) != T_ENDCMD
                                            && lex_token (lexer) != T_SLASH))
                    {
                      const char *rowtypes[] = {
#define RT(NAME, DIMS) #NAME,
                        ROWTYPES
#undef RT
                        "N_VECTOR", "SD",
                      };
                      lex_error_expecting_array (
                        lexer, rowtypes, sizeof rowtypes / sizeof *rowtypes);
                      goto error;
                    }
                  break;
                }

              if (open)
                in_parens = true;

              if (in_parens)
                mf.factor_rowtype_mask |= 1u << rt;
              else
                mf.pooled_rowtype_mask |= 1u << rt;

              bool close = in_parens && lex_match (lexer, T_RPAREN);
              if (close)
                in_parens = false;

              if (mf.n_contents >= allocated_contents)
                mf.contents = x2nrealloc (mf.contents, &allocated_contents,
                                          sizeof *mf.contents);
              mf.contents[mf.n_contents++] = (struct content) {
                .open = open, .rowtype = rt, .close = close
              };
            }
        }
      else
        {
          lex_error_expecting (lexer, "N", "FORMAT", "FILE", "SPLIT", "FACTORS",
                               "CELLS", "CONTENTS");
          goto error;
        }
    }
  if (!mf.input_rowtype)
    {
      if (mf.cells < 0)
        {
          if (mf.n_fvars)
            {
              msg (SE, _("CELLS is required when factor variables are specified "
                         "and VARIABLES does not include ROWTYPE_."));
              goto error;
            }
          mf.cells = 1;
        }

      if (!mf.n_contents)
        {
          msg (SW, _("CONTENTS was not specified and VARIABLES does not "
                     "include ROWTYPE_.  Assuming CONTENTS=CORR."));

          mf.n_contents = 1;
          mf.contents = xmalloc (sizeof *mf.contents);
          *mf.contents = (struct content) { .rowtype = C_CORR };
        }
    }
  mf.cvars = xmalloc (mf.n_input_vars * sizeof *mf.cvars);
  for (size_t i = 0; i < mf.n_input_vars; i++)
    if (!taken_vars[i])
      {
        struct variable *v = input_vars[i];
        mf.cvars[mf.n_cvars++] = v;
        var_set_both_formats (v, (struct fmt_spec) { .type = FMT_F, .w = 10,
                                                     .d = 4 });
      }
  if (!mf.n_cvars)
    {
      lex_ofs_error (lexer, input_vars_start, input_vars_end,
                     _("At least one continuous variable is required."));
      goto error;
    }
  if (mf.input_rowtype)
    {
      for (size_t i = 0; i < mf.n_cvars; i++)
        if (mf.cvars[i] != input_vars[n_input_vars - mf.n_cvars + i])
          {
            lex_ofs_error (lexer, input_vars_start, input_vars_end,
                           _("VARIABLES includes ROWTYPE_ but the continuous "
                             "variables are not the last ones on VARIABLES."));
            goto error;
          }
    }
  unsigned int rowtype_mask = mf.pooled_rowtype_mask | mf.factor_rowtype_mask;
  if (rowtype_mask & (1u << C_N) && mf.n >= 0)
    {
      lex_ofs_error (lexer, n_start, n_end,
                     _("Cannot specify N on CONTENTS along with the "
                       "N subcommand."));
      goto error;
    }

  struct variable **order = xnmalloc (dict_get_n_vars (dict), sizeof *order);
  size_t n_order = 0;
  for (size_t i = 0; i < mf.n_svars; i++)
    order[n_order++] = mf.svars[i];
  order[n_order++] = mf.rowtype;
  for (size_t i = 0; i < mf.n_fvars; i++)
    order[n_order++] = mf.fvars[i];
  order[n_order++] = mf.varname;
  for (size_t i = 0; i < mf.n_cvars; i++)
    order[n_order++] = mf.cvars[i];
  assert (n_order == dict_get_n_vars (dict));
  dict_reorder_vars (dict, order, n_order);
  free (order);

  dict_set_split_vars (dict, mf.svars, mf.n_svars, SPLIT_LAYERED);

  schedule_matrices (&mf);

  if (fh == NULL)
    fh = fh_inline_file ();

  if (lex_end_of_command (lexer) != CMD_SUCCESS)
    goto error;

  struct dfm_reader *reader = dfm_open_reader (fh, lexer, NULL);
  if (reader == NULL)
    goto error;

  struct casewriter *writer = autopaging_writer_create (dict_get_proto (dict));
  if (mf.input_rowtype)
    parse_data_with_rowtype (&mf, reader, writer);
  else
    parse_data_without_rowtype (&mf, reader, writer);
  dfm_close_reader (reader);

  dataset_set_dict (ds, dict);
  dataset_set_source (ds, casewriter_make_reader (writer));

  matrix_format_uninit (&mf);
  free (taken_vars);
  fh_unref (fh);

  return CMD_SUCCESS;

 error:
  matrix_format_uninit (&mf);
  free (taken_vars);
  dict_unref (dict);
  fh_unref (fh);
  return CMD_FAILURE;
}
