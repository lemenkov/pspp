/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011, 2012, 2016 Free Software Foundation, Inc.

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

#include "data/dataset.h"
#include "data/format.h"
#include "data/dictionary.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/string-set.h"

#include "gl/intprops.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

int
cmd_vector (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct pool *pool = pool_create ();

  do
    {
      /* Get the name(s) of the new vector(s). */
      if (!lex_force_id (lexer))
        goto error;

      int vectors_start = lex_ofs (lexer);
      char **vectors = NULL;
      size_t n_vectors = 0;
      size_t allocated_vectors = 0;
      while (lex_token (lexer) == T_ID)
        {
          char *error = dict_id_is_valid__ (dict, lex_tokcstr (lexer),
                                            DC_ORDINARY | DC_SCRATCH);
          if (error)
            {
              lex_error (lexer, "%s", error);
              free (error);
              goto error;
            }

          if (dict_lookup_vector (dict, lex_tokcstr (lexer)))
            {
              lex_next_error (lexer, 0, 0,
                              _("A vector named %s already exists."),
                              lex_tokcstr (lexer));
              goto error;
            }

          for (size_t i = 0; i < n_vectors; i++)
            if (!utf8_strcasecmp (vectors[i], lex_tokcstr (lexer)))
              {
                lex_ofs_error (lexer, vectors_start, lex_ofs (lexer),
                               _("Vector name %s is given twice."),
                               lex_tokcstr (lexer));
                goto error;
              }

          if (n_vectors == allocated_vectors)
            vectors = pool_2nrealloc (pool, vectors, &allocated_vectors,
                                      sizeof *vectors);
          vectors[n_vectors++] = pool_strdup (pool, lex_tokcstr (lexer));

          lex_get (lexer);
          lex_match (lexer, T_COMMA);
        }

      /* Now that we have the names it's time to check for the short
         or long forms. */
      if (lex_match (lexer, T_EQUALS))
        {
          if (n_vectors > 1)
            {
              lex_ofs_error (lexer, vectors_start, lex_ofs (lexer) - 1,
                             _("Only a single vector name may be specified "
                               "when a list of variables is given."));
              goto error;
            }

          struct variable **v;
          size_t nv;
          if (!parse_variables_pool (lexer, pool, dict, &v, &nv,
                                     PV_SAME_WIDTH | PV_DUPLICATE))
            goto error;

          dict_create_vector (dict, vectors[0], v, nv);
        }
      else if (lex_match (lexer, T_LPAREN))
        {
          struct fmt_spec format = fmt_for_output (FMT_F, 8, 2);
          bool seen_format = false;
          size_t n_vars = 0;
          int lparen_ofs = lex_ofs (lexer) - 1;
          while (!lex_match (lexer, T_RPAREN))
            {
              if (lex_is_integer (lexer))
                {
                  if (n_vars)
                    {
                      lex_ofs_error (lexer, lparen_ofs, lex_ofs (lexer),
                                     _("Vector length may only be specified "
                                       "once."));
                      goto error;
                    }
                  if (!lex_force_int_range (lexer, NULL, 1, INT_MAX))
                    goto error;
                  n_vars = lex_integer (lexer);
                  lex_get (lexer);
                }
              else if (lex_token (lexer) == T_ID)
                {
                  if (seen_format)
                    {
                      lex_ofs_error (lexer, lparen_ofs, lex_ofs (lexer),
                                     _("Only one format may be specified."));
                      goto error;
                    }
                  seen_format = true;
                  if (!parse_format_specifier (lexer, &format))
                    goto error;
                  char *error = fmt_check_output__ (format);
                  if (error)
                    {
                      lex_next_error (lexer, -1, -1, "%s", error);
                      free (error);
                      goto error;
                    }
                }
              else
                {
                  lex_error (lexer, _("Syntax error expecting vector length "
                                      "or format."));
                  goto error;
                }
              lex_match (lexer, T_COMMA);
            }
          int end_ofs = lex_ofs (lexer) - 1;
          if (n_vars == 0)
            {
              lex_ofs_error (lexer, lparen_ofs, end_ofs,
                             _("Vector length is required."));
              goto error;
            }

          /* Check that none of the variables exist and that their names are
             not excessively long. */
          struct string_set new_names = STRING_SET_INITIALIZER (new_names);
          for (size_t i = 0; i < n_vectors; i++)
            for (size_t j = 0; j < n_vars; j++)
              {
                char *name = xasprintf ("%s%zu", vectors[i], j + 1);
                char *error = dict_id_is_valid__ (dict, name,
                                                  DC_ORDINARY | DC_SCRATCH);
                if (error)
                  {
                    lex_ofs_error (lexer, vectors_start, end_ofs, "%s", error);
                    free (error);
                    free (name);
                    goto error;
                  }
                if (dict_lookup_var (dict, name))
                  {
                    lex_ofs_error (lexer, vectors_start, end_ofs,
                                   _("%s is an existing variable name."),
                                   name);
                    free (name);
                    string_set_destroy (&new_names);
                    goto error;
                  }
                if (!string_set_insert_nocopy (&new_names, name))
                  {
                    /* name was already freed. */
                    lex_ofs_error (
                      lexer, vectors_start, end_ofs,
                      _("Two different vectors add variable %s%zu."),
                      vectors[i], j + 1);
                    string_set_destroy (&new_names);
                    goto error;
                  }
              }
          string_set_destroy (&new_names);

          /* Finally create the variables and vectors. */
          struct variable **vars = pool_nmalloc (pool, n_vars, sizeof *vars);
          for (size_t i = 0; i < n_vectors; i++)
            {
              for (size_t j = 0; j < n_vars; j++)
                {
                  char *name = xasprintf ("%s%zu", vectors[i], j + 1);
                  vars[j] = dict_create_var_assert (dict, name,
                                                    fmt_var_width (format));
                  var_set_both_formats (vars[j], format);
                  free (name);
                }
              dict_create_vector_assert (dict, vectors[i], vars, n_vars);
            }
        }
      else
        {
          lex_error_expecting (lexer, "`='", "`('");
          goto error;
        }
    }
  while (lex_match (lexer, T_SLASH));

  pool_destroy (pool);
  return CMD_SUCCESS;

error:
  pool_destroy (pool);
  return CMD_FAILURE;
}
