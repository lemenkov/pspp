/* PSPP - a program for statistical analysis.
   Copyright (C) 2021 Free Software Foundation, Inc.

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

#include <math.h>

#include "data/any-reader.h"
#include "data/any-writer.h"
#include "data/casereader.h"
#include "data/casewriter.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "language/commands/file-handle.h"
#include "language/commands/matrix-reader.h"
#include "language/lexer/lexer.h"
#include "language/command.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_mconvert (struct lexer *lexer, struct dataset *ds)
{
  bool append = false;
  struct file_handle *in = NULL;
  struct file_handle *out = NULL;
  while (lex_token (lexer) != T_ENDCMD)
    {
      lex_match (lexer, T_SLASH);

      if (lex_match_id (lexer, "APPEND"))
        append = true;
      else if (lex_match_id (lexer, "REPLACE"))
        append = false;
      else
        {
          if (lex_match_id (lexer, "MATRIX"))
            lex_match (lexer, T_EQUALS);

          struct file_handle **fhp = (lex_match_id (lexer, "IN") ? &in
                                      : lex_match_id (lexer, "OUT") ? &out
                                      : NULL);
          if (!fhp)
            {
              lex_error_expecting (lexer, "IN", "OUT", "APPEND", "REPLACE");
              goto error;
            }
          if (!lex_force_match (lexer, T_LPAREN))
            goto error;

          fh_unref (*fhp);
          if (lex_match (lexer, T_ASTERISK))
            *fhp = NULL;
          else
            {
              *fhp = fh_parse (lexer, FH_REF_FILE, dataset_session (ds));
              if (!*fhp)
                goto error;
            }

          if (!lex_force_match (lexer, T_RPAREN))
            goto error;
        }
    }

  if (!in && !dataset_has_source (ds))
    {
      msg (SE, _("No active file is defined and no external file is "
                 "specified on MATRIX=IN."));
      goto error;
    }

  struct dictionary *d;
  struct casereader *cr;
  if (in)
    {
      cr = any_reader_open_and_decode (in, NULL, &d, NULL);
      if (!cr)
        goto error;
    }
  else
    {
      d = dict_clone (dataset_dict (ds));
      cr = proc_open (ds);
    }

  struct matrix_reader *mr = matrix_reader_create (d, cr);
  if (!mr)
    {
      casereader_destroy (cr);
      dict_unref (d);
      if (!in)
        proc_commit (ds);
      goto error;
    }

  struct casewriter *cw;
  if (out)
    {
      cw = any_writer_open (out, d);
      if (!cw)
        {
          matrix_reader_destroy (mr);
          casereader_destroy (cr);
          dict_unref (d);
          if (!in)
            proc_commit (ds);
          goto error;
        }
    }
  else
    cw = autopaging_writer_create (dict_get_proto (d));

  for (;;)
    {
      struct matrix_material mm;
      struct casereader *group;
      if (!matrix_reader_next (&mm, mr, &group))
        break;

      bool add_corr = mm.cov && !mm.corr;
      bool add_cov = mm.corr && !mm.cov && mm.var_matrix;
      bool add_stddev = add_corr && !mm.var_matrix;
      bool remove_corr = add_cov && !append;
      bool remove_cov = add_corr && !append;

      struct ccase *model = casereader_peek (group, 0);
      for (size_t i = 0; i < mr->n_fvars; i++)
        *case_num_rw (model, mr->fvars[i]) = SYSMIS;

      for (;;)
        {
          struct ccase *c = casereader_read (group);
          if (!c)
            break;

          struct substring rowtype = matrix_reader_get_string (c, mr->rowtype);
          if ((remove_cov && ss_equals_case (rowtype, ss_cstr ("COV")))
              || (remove_corr && ss_equals_case (rowtype, ss_cstr ("CORR"))))
            case_unref (c);
          else
            casewriter_write (cw, c);
        }
      casereader_destroy (group);

      if (add_corr)
        {
          for (size_t y = 0; y < mr->n_cvars; y++)
            {
              struct ccase *c = case_clone (model);
              for (size_t x = 0; x < mr->n_cvars; x++)
                {
                  double d1 = gsl_matrix_get (mm.cov, x, x);
                  double d2 = gsl_matrix_get (mm.cov, y, y);
                  double cov = gsl_matrix_get (mm.cov, y, x);
                  *case_num_rw (c, mr->cvars[x]) = cov / sqrt (d1 * d2);
                }
              matrix_reader_set_string (c, mr->rowtype, ss_cstr ("CORR"));
              matrix_reader_set_string (c, mr->varname,
                                        ss_cstr (var_get_name (mr->cvars[y])));
              casewriter_write (cw, c);
            }
        }

      if (add_stddev)
        {
          struct ccase *c = case_clone (model);
          for (size_t x = 0; x < mr->n_cvars; x++)
            {
              double variance = gsl_matrix_get (mm.cov, x, x);
              *case_num_rw (c, mr->cvars[x]) = sqrt (variance);
            }
          matrix_reader_set_string (c, mr->rowtype, ss_cstr ("STDDEV"));
          matrix_reader_set_string (c, mr->varname, ss_empty ());
          casewriter_write (cw, c);
        }

      if (add_cov)
        {
          for (size_t y = 0; y < mr->n_cvars; y++)
            {
              struct ccase *c = case_clone (model);
              for (size_t x = 0; x < mr->n_cvars; x++)
                {
                  double d1 = gsl_matrix_get (mm.var_matrix, x, x);
                  double d2 = gsl_matrix_get (mm.var_matrix, y, y);
                  double corr = gsl_matrix_get (mm.corr, y, x);
                  *case_num_rw (c, mr->cvars[x]) = corr * sqrt (d1 * d2);
                }
              matrix_reader_set_string (c, mr->rowtype, ss_cstr ("COV"));
              matrix_reader_set_string (c, mr->varname,
                                        ss_cstr (var_get_name (mr->cvars[y])));
              casewriter_write (cw, c);
            }
        }

      case_unref (model);
      matrix_material_uninit (&mm);
    }

  matrix_reader_destroy (mr);
  if (!in)
    proc_commit (ds);
  if (out)
    casewriter_destroy (cw);
  else
    {
      dataset_set_dict (ds, dict_ref (d));
      dataset_set_source (ds, casewriter_make_reader (cw));
    }

  fh_unref (in);
  fh_unref (out);
  dict_unref (d);
  return CMD_SUCCESS;

error:
  fh_unref (in);
  fh_unref (out);
  return CMD_FAILURE;
}

