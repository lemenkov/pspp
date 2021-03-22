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

#include "data/case.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/data-io/data-parser.h"
#include "language/data-io/data-reader.h"
#include "language/data-io/file-handle.h"
#include "language/data-io/inpt-pgm.h"
#include "language/data-io/placement-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"

#include "gl/xsize.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* DATA LIST transformation data. */
struct data_list_trns
  {
    struct data_parser *parser; /* Parser. */
    struct dfm_reader *reader;  /* Data file reader. */
    struct variable *end;	/* Variable specified on END subcommand. */
  };

static trns_free_func data_list_trns_free;
static trns_proc_func data_list_trns_proc;

enum diagonal
  {
    DIAGONAL,
    NO_DIAGONAL
  };

enum triangle
  {
    LOWER,
    UPPER,
    FULL
  };

static const int ROWTYPE_WIDTH = 8;

struct matrix_format
{
  enum triangle triangle;
  enum diagonal diagonal;
  const struct variable *rowtype;
  const struct variable *varname;
  int n_continuous_vars;
  struct variable **split_vars;
  size_t n_split_vars;
  long n;
};

/*
valid rowtype_ values:
  CORR,
  COV,
  MAT,


  MSE,
  DFE,
  MEAN,
  STDDEV (or SD),
  N_VECTOR (or N),
  N_SCALAR,
  N_MATRIX,
  COUNT,
  PROX.
*/

/* Sets the value of OUTCASE which corresponds to VNAME
   to the value STR.  VNAME must be of type string.
 */
static void
set_varname_column (struct ccase *outcase, const struct variable *vname,
     const char *str)
{
  int len = var_get_width (vname);
  uint8_t *s = case_str_rw (outcase, vname);

  strncpy (CHAR_CAST (char *, s), str, len);
}

static void
blank_varname_column (struct ccase *outcase, const struct variable *vname)
{
  int len = var_get_width (vname);
  uint8_t *s = case_str_rw (outcase, vname);

  memset (s, ' ', len);
}

static struct casereader *
preprocess (struct casereader *casereader0, const struct dictionary *dict, void *aux)
{
  struct matrix_format *mformat = aux;
  const struct caseproto *proto = casereader_get_proto (casereader0);
  struct casewriter *writer = autopaging_writer_create (proto);
  struct ccase *prev_case = NULL;
  double **matrices = NULL;
  size_t n_splits = 0;

  const size_t sizeof_matrix =
    sizeof (double) * mformat->n_continuous_vars * mformat->n_continuous_vars;


  /* Make an initial pass to populate our temporary matrix */
  struct casereader *pass0 = casereader_clone (casereader0);
  struct ccase *c;
  union value *prev_values = XCALLOC (mformat->n_split_vars,  union value);
  int row = (mformat->triangle == LOWER && mformat->diagonal == NO_DIAGONAL) ? 1 : 0;
  bool first_case = true;
  for (; (c = casereader_read (pass0)) != NULL; case_unref (c))
    {
      int s;
      bool match = false;
      if (!first_case)
	{
	  match = true;
	  for (s = 0; s < mformat->n_split_vars; ++s)
	    {
	      const struct variable *svar = mformat->split_vars[s];
	      const union value *sv = case_data (c, svar);
	      if (! value_equal (prev_values + s, sv, var_get_width (svar)))
		{
		  match = false;
		  break;
		}
	    }
	}
      first_case = false;

      if (matrices == NULL || ! match)
	{
	  row = (mformat->triangle == LOWER && mformat->diagonal == NO_DIAGONAL) ?
	    1 : 0;

	  n_splits++;
	  matrices = xrealloc (matrices, sizeof (double*)  * n_splits);
	  matrices[n_splits - 1] = xmalloc (sizeof_matrix);
	}

      for (s = 0; s < mformat->n_split_vars; ++s)
	{
	  const struct variable *svar = mformat->split_vars[s];
	  const union value *sv = case_data (c, svar);
	  value_clone (prev_values + s, sv, var_get_width (svar));
	}

      int c_offset = (mformat->triangle == UPPER) ? row : 0;
      if (mformat->triangle == UPPER && mformat->diagonal == NO_DIAGONAL)
	c_offset++;
      const union value *v = case_data (c, mformat->rowtype);
      const char *val = CHAR_CAST (const char *, v->s);
      if (0 == strncasecmp (val, "corr    ", ROWTYPE_WIDTH) ||
	  0 == strncasecmp (val, "cov     ", ROWTYPE_WIDTH))
	{
	  if (row >= mformat->n_continuous_vars)
	    {
	      msg (SE,
		   _("There are %d variable declared but the data has at least %d matrix rows."),
		   mformat->n_continuous_vars, row + 1);
	      case_unref (c);
	      casereader_destroy (pass0);
	      free (prev_values);
	      goto error;
	    }
	  int col;
	  for (col = c_offset; col < mformat->n_continuous_vars; ++col)
	    {
	      const struct variable *var =
		dict_get_var (dict,
			      1 + col - c_offset +
			      var_get_dict_index (mformat->varname));

	      double e = case_data (c, var)->f;
	      if (e == SYSMIS)
	      	continue;

	      /* Fill in the lower triangle */
	      (matrices[n_splits-1])[col + mformat->n_continuous_vars * row] = e;

	      if (mformat->triangle != FULL)
		/* Fill in the upper triangle */
		(matrices[n_splits-1]) [row + mformat->n_continuous_vars * col] = e;
	    }
	  row++;
	}
    }
  casereader_destroy (pass0);
  free (prev_values);

  if (!matrices)
    goto error;

  /* Now make a second pass to fill in the other triangle from our
     temporary matrix */
  const int idx = var_get_dict_index (mformat->varname);
  row = 0;

  if (mformat->n >= 0)
    {
      int col;
      struct ccase *outcase = case_create (proto);
      union value *v = case_data_rw (outcase, mformat->rowtype);
      memcpy (v->s, "N       ", ROWTYPE_WIDTH);
      blank_varname_column (outcase, mformat->varname);
      for (col = 0; col < mformat->n_continuous_vars; ++col)
	{
	  union value *dest_val =
	    case_data_rw_idx (outcase,
			      1 + col + var_get_dict_index (mformat->varname));
	  dest_val->f = mformat->n;
	}
      casewriter_write (writer, outcase);
    }

  n_splits = 0;
  prev_values = xcalloc (mformat->n_split_vars, sizeof *prev_values);
  first_case = true;
  for (; (c = casereader_read (casereader0)) != NULL; prev_case = c)
    {
      int s;
      bool match = false;
      if (!first_case)
	{
	  match = true;
	  for (s = 0; s < mformat->n_split_vars; ++s)
	    {
	      const struct variable *svar = mformat->split_vars[s];
	      const union value *sv = case_data (c, svar);
	      if (! value_equal (prev_values + s, sv, var_get_width (svar)))
		{
		  match = false;
		  break;
		}
	    }
	}
      first_case = false;
      if (! match)
	{
	  n_splits++;
	  row = 0;
	}

      for (s = 0; s < mformat->n_split_vars; ++s)
	{
	  const struct variable *svar = mformat->split_vars[s];
	  const union value *sv = case_data (c, svar);
	  value_clone (prev_values + s, sv, var_get_width (svar));
	}

      case_unref (prev_case);
      const union value *v = case_data (c, mformat->rowtype);
      const char *val = CHAR_CAST (const char *, v->s);
      if (mformat->n >= 0)
	{
	  if (0 == strncasecmp (val, "n       ", ROWTYPE_WIDTH) ||
	      0 == strncasecmp (val, "n_vector", ROWTYPE_WIDTH))
	    {
	      msg (SW,
		   _("The N subcommand was specified, but a N record was also found in the data.  The N record will be ignored."));
	      continue;
	    }
	}

      struct ccase *outcase = case_create (proto);
      case_copy (outcase, 0, c, 0, caseproto_get_n_widths (proto));

      if (0 == strncasecmp (val, "corr    ", ROWTYPE_WIDTH) ||
	  0 == strncasecmp (val, "cov     ", ROWTYPE_WIDTH))
	{
	  int col;
	  const struct variable *var = dict_get_var (dict, idx + 1 + row);
	  set_varname_column (outcase, mformat->varname, var_get_name (var));
	  value_copy (case_data_rw (outcase, mformat->rowtype), v, ROWTYPE_WIDTH);

	  for (col = 0; col < mformat->n_continuous_vars; ++col)
	    {
	      union value *dest_val =
		case_data_rw_idx (outcase,
				  1 + col + var_get_dict_index (mformat->varname));
	      dest_val->f = (matrices[n_splits - 1])[col + mformat->n_continuous_vars * row];
	      if (col == row && mformat->diagonal == NO_DIAGONAL)
		dest_val->f = 1.0;
	    }
	  row++;
	}
      else
	{
	  blank_varname_column (outcase, mformat->varname);
	}

      /* Special case for SD and N_VECTOR: Rewrite as STDDEV and N respectively */
      if (0 == strncasecmp (val, "sd      ", ROWTYPE_WIDTH))
	{
	  value_copy_buf_rpad (case_data_rw (outcase, mformat->rowtype), ROWTYPE_WIDTH,
			       (uint8_t *) "STDDEV", 6, ' ');
	}
      else if (0 == strncasecmp (val, "n_vector", ROWTYPE_WIDTH))
	{
	  value_copy_buf_rpad (case_data_rw (outcase, mformat->rowtype), ROWTYPE_WIDTH,
			       (uint8_t *) "N", 1, ' ');
	}

      casewriter_write (writer, outcase);
    }

  /* If NODIAGONAL is specified, then a final case must be written */
  if (mformat->diagonal == NO_DIAGONAL)
    {
      int col;
      struct ccase *outcase = case_create (proto);

      if (prev_case)
	case_copy (outcase, 0, prev_case, 0, caseproto_get_n_widths (proto));

      const struct variable *var = dict_get_var (dict, idx + 1 + row);
      set_varname_column (outcase, mformat->varname, var_get_name (var));

      for (col = 0; col < mformat->n_continuous_vars; ++col)
	{
	  union value *dest_val =
	    case_data_rw_idx (outcase, 1 + col +
			      var_get_dict_index (mformat->varname));
	  dest_val->f = (matrices[n_splits - 1]) [col + mformat->n_continuous_vars * row];
	  if (col == row && mformat->diagonal == NO_DIAGONAL)
	    dest_val->f = 1.0;
	}

      casewriter_write (writer, outcase);
    }
  free (prev_values);

  if (prev_case)
    case_unref (prev_case);

  int i;
  for (i = 0 ; i < n_splits; ++i)
    free (matrices[i]);
  free (matrices);
  struct casereader *reader1 = casewriter_make_reader (writer);
  casereader_destroy (casereader0);
  return reader1;


error:
  if (prev_case)
    case_unref (prev_case);

  if (matrices)
    for (i = 0 ; i < n_splits; ++i)
      free (matrices[i]);
  free (matrices);
  casereader_destroy (casereader0);
  casewriter_destroy (writer);
  return NULL;
}

int
cmd_matrix (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict;
  struct data_parser *parser;
  struct dfm_reader *reader;
  struct file_handle *fh = NULL;
  char *encoding = NULL;
  struct matrix_format mformat;
  int i;
  size_t n_names;
  char **names = NULL;

  mformat.triangle = LOWER;
  mformat.diagonal = DIAGONAL;
  mformat.n_split_vars = 0;
  mformat.split_vars = NULL;
  mformat.n = -1;

  dict = (in_input_program ()
          ? dataset_dict (ds)
          : dict_create (get_default_encoding ()));
  parser = data_parser_create (dict);
  reader = NULL;

  data_parser_set_type (parser, DP_DELIMITED);
  data_parser_set_warn_missing_fields (parser, false);
  data_parser_set_span (parser, false);

  mformat.rowtype = dict_create_var (dict, "ROWTYPE_", ROWTYPE_WIDTH);

  mformat.n_continuous_vars = 0;
  mformat.n_split_vars = 0;

  if (! lex_force_match_id (lexer, "VARIABLES"))
    goto error;

  lex_match (lexer, T_EQUALS);

  if (! parse_mixed_vars (lexer, dict, &names, &n_names, PV_NO_DUPLICATE))
    {
      int i;
      for (i = 0; i < n_names; ++i)
	free (names[i]);
      free (names);
      goto error;
    }

  int longest_name = 0;
  for (i = 0; i < n_names; ++i)
    {
      maximize_int (&longest_name, strlen (names[i]));
    }

  mformat.varname = dict_create_var (dict, "VARNAME_",
				     8 * DIV_RND_UP (longest_name, 8));

  for (i = 0; i < n_names; ++i)
    {
      if (0 == strcasecmp (names[i], "ROWTYPE_"))
	{
	  const struct fmt_spec fmt = fmt_for_input (FMT_A, 8, 0);
	  data_parser_add_delimited_field (parser,
					   &fmt,
					   var_get_case_index (mformat.rowtype),
					   "ROWTYPE_");
	}
      else
	{
	  const struct fmt_spec fmt = fmt_for_input (FMT_F, 10, 4);
	  struct variable *v = dict_create_var (dict, names[i], 0);
	  var_set_both_formats (v, &fmt);
	  data_parser_add_delimited_field (parser,
					   &fmt,
					   var_get_case_index (mformat.varname) +
					   ++mformat.n_continuous_vars,
					   names[i]);
	}
    }
  for (i = 0; i < n_names; ++i)
    free (names[i]);
  free (names);

  while (lex_token (lexer) != T_ENDCMD)
    {
      if (! lex_force_match (lexer, T_SLASH))
	goto error;

      if (lex_match_id (lexer, "N"))
	{
	  lex_match (lexer, T_EQUALS);

	  if (! lex_force_int_range (lexer, "N", 0, INT_MAX))
	    goto error;

	  mformat.n = lex_integer (lexer);
	  lex_get (lexer);
	}
      else if (lex_match_id (lexer, "FORMAT"))
	{
	  lex_match (lexer, T_EQUALS);

	  while (lex_token (lexer) != T_SLASH && (lex_token (lexer) != T_ENDCMD))
	    {
	      if (lex_match_id (lexer, "LIST"))
		{
		  data_parser_set_span (parser, false);
		}
	      else if (lex_match_id (lexer, "FREE"))
		{
		  data_parser_set_span (parser, true);
		}
	      else if (lex_match_id (lexer, "UPPER"))
		{
		  mformat.triangle = UPPER;
		}
	      else if (lex_match_id (lexer, "LOWER"))
		{
		  mformat.triangle = LOWER;
		}
	      else if (lex_match_id (lexer, "FULL"))
		{
		  mformat.triangle = FULL;
		}
	      else if (lex_match_id (lexer, "DIAGONAL"))
		{
		  mformat.diagonal = DIAGONAL;
		}
	      else if (lex_match_id (lexer, "NODIAGONAL"))
		{
		  mformat.diagonal = NO_DIAGONAL;
		}
	      else
		{
		  lex_error (lexer, NULL);
		  goto error;
		}
	    }
	}
      else if (lex_match_id (lexer, "FILE"))
	{
	  lex_match (lexer, T_EQUALS);
          fh_unref (fh);
	  fh = fh_parse (lexer, FH_REF_FILE | FH_REF_INLINE, NULL);
	  if (fh == NULL)
	    goto error;
	}
      else if (lex_match_id (lexer, "SPLIT"))
	{
	  lex_match (lexer, T_EQUALS);
	  if (! parse_variables (lexer, dict, &mformat.split_vars, &mformat.n_split_vars, 0))
	    {
	      free (mformat.split_vars);
	      goto error;
	    }
	  int i;
	  for (i = 0; i < mformat.n_split_vars; ++i)
	    {
	      const struct fmt_spec fmt = fmt_for_input (FMT_F, 4, 0);
	      var_set_both_formats (mformat.split_vars[i], &fmt);
	    }
	  dict_reorder_vars (dict, mformat.split_vars, mformat.n_split_vars);
	  mformat.n_continuous_vars -= mformat.n_split_vars;
	}
      else
	{
	  lex_error (lexer, NULL);
	  goto error;
	}
    }

  if (mformat.diagonal == NO_DIAGONAL && mformat.triangle == FULL)
    {
      msg (SE, _("FORMAT = FULL and FORMAT = NODIAGONAL are mutually exclusive."));
      goto error;
    }

  if (fh == NULL)
    fh = fh_inline_file ();
  fh_set_default_handle (fh);

  if (!data_parser_any_fields (parser))
    {
      msg (SE, _("At least one variable must be specified."));
      goto error;
    }

  if (lex_end_of_command (lexer) != CMD_SUCCESS)
    goto error;

  reader = dfm_open_reader (fh, lexer, encoding);
  if (reader == NULL)
    goto error;

  if (in_input_program ())
    {
      struct data_list_trns *trns = xmalloc (sizeof *trns);
      trns->parser = parser;
      trns->reader = reader;
      trns->end = NULL;
      add_transformation (ds, data_list_trns_proc, data_list_trns_free, trns);
    }
  else
    {
      data_parser_make_active_file (parser, ds, reader, dict, preprocess,
				    &mformat);
    }

  fh_unref (fh);
  free (encoding);
  free (mformat.split_vars);

  return CMD_DATA_LIST;

 error:
  data_parser_destroy (parser);
  if (!in_input_program ())
    dict_unref (dict);
  fh_unref (fh);
  free (encoding);
  free (mformat.split_vars);
  return CMD_CASCADING_FAILURE;
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
  free (trns);
  return true;
}

/* Handle DATA LIST transformation TRNS, parsing data into *C. */
static int
data_list_trns_proc (void *trns_, struct ccase **c, casenumber case_num UNUSED)
{
  struct data_list_trns *trns = trns_;
  int retval;

  *c = case_unshare (*c);
  if (data_parser_parse (trns->parser, trns->reader, *c))
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
      double *end = &case_data_rw (*c, trns->end)->f;
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
