/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011, 2012, 2020 Free Software Foundation, Inc.

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

#include "language/lexer/variable-parser.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>

#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/variable.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/hash-functions.h"
#include "libpspp/i18n.h"
#include "libpspp/hmapx.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/stringi-set.h"

#include "math/interaction.h"

#include "gl/c-ctype.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static struct variable *var_set_get_var (const struct var_set *, size_t);
static struct variable *var_set_lookup_var (const struct var_set *,
                                            const char *);
static bool var_set_lookup_var_idx (const struct var_set *, const char *,
                                    size_t *);
static bool var_set_get_names_must_be_ids (const struct var_set *);

static bool
is_name_token (const struct lexer *lexer, bool names_must_be_ids)
{
  return (lex_token (lexer) == T_ID
          || (!names_must_be_ids && lex_token (lexer) == T_STRING));
}

static bool
is_vs_name_token (const struct lexer *lexer, const struct var_set *vs)
{
  return is_name_token (lexer, var_set_get_names_must_be_ids (vs));
}

static bool
is_dict_name_token (const struct lexer *lexer, const struct dictionary *d)
{
  return is_name_token (lexer, dict_get_names_must_be_ids (d));
}

/* Parses a name as a variable within VS.  Sets *IDX to the
   variable's index and returns true if successful.  On failure
   emits an error message and returns false. */
static bool
parse_vs_variable_idx (struct lexer *lexer, const struct var_set *vs,
                       size_t *idx)
{
  assert (idx != NULL);

  if (!is_vs_name_token (lexer, vs))
    {
      lex_error (lexer, _("Syntax error expecting variable name."));
      return false;
    }
  else if (var_set_lookup_var_idx (vs, lex_tokcstr (lexer), idx))
    {
      lex_get (lexer);
      return true;
    }
  else
    {
      lex_error (lexer, _("%s is not a variable name."), lex_tokcstr (lexer));
      return false;
    }
}

/* Parses a name as a variable within VS and returns the variable
   if successful.  On failure emits an error message and returns
   a null pointer. */
static struct variable *
parse_vs_variable (struct lexer *lexer, const struct var_set *vs)
{
  size_t idx;
  return parse_vs_variable_idx (lexer, vs, &idx) ? var_set_get_var (vs, idx) : NULL;
}

/* Parses a variable name in dictionary D and returns the
   variable if successful.  On failure emits an error message and
   returns a null pointer. */
struct variable *
parse_variable (struct lexer *lexer, const struct dictionary *d)
{
  struct var_set *vs = var_set_create_from_dict (d);
  struct variable *var = parse_vs_variable (lexer, vs);
  var_set_destroy (vs);
  return var;
}

/* Parses a set of variables from dictionary D given options
   OPTS.  Resulting list of variables stored in *VAR and the
   number of variables into *N.  Returns true only if
   successful.  The dictionary D must contain at least one
   variable.  */
bool
parse_variables (struct lexer *lexer, const struct dictionary *d,
                 struct variable ***var,
                 size_t *n, int opts)
{
  struct var_set *vs;
  int success;

  assert (d != NULL);
  assert (var != NULL);
  assert (n != NULL);

  vs = var_set_create_from_dict (d);
  if (var_set_get_n (vs) == 0)
    {
      *n = 0;
      var_set_destroy (vs);
      return false;
    }
  success = parse_var_set_vars (lexer, vs, var, n, opts);
  var_set_destroy (vs);
  return success;
}

/* Parses a set of variables from dictionary D given options
   OPTS.  Resulting list of variables stored in *VARS and the
   number of variables into *N_VARS.  Returns true only if
   successful.  Same behavior as parse_variables, except that all
   allocations are taken from the given POOL. */
bool
parse_variables_pool (struct lexer *lexer, struct pool *pool,
                const struct dictionary *dict,
                struct variable ***vars, size_t *n_vars, int opts)
{
  int retval;

  /* PV_APPEND is unsafe because parse_variables would free the
     existing names on failure, but those names are presumably
     already in the pool, which would attempt to re-free it
     later. */
  assert (!(opts & PV_APPEND));

  retval = parse_variables (lexer, dict, vars, n_vars, opts);
  if (retval)
    pool_register (pool, free, *vars);
  return retval;
}

/* Parses a variable name from VS.  If successful, sets *IDX to
   the variable's index in VS, *CLASS to the variable's
   dictionary class, and returns true.  Returns false on
   failure. */
static bool
parse_var_idx_class (struct lexer *lexer, const struct var_set *vs,
                     size_t *idx, enum dict_class *class)
{
  if (!parse_vs_variable_idx (lexer, vs, idx))
    return false;

  *class = dict_class_from_id (var_get_name (var_set_get_var (vs, *idx)));
  return true;
}

/* Add the variable from VS with index IDX to the list of
   variables V that has *NV elements and room for *MV.
   Uses and updates INCLUDED to avoid duplicates if indicated by
   PV_OPTS, which also affects what variables are allowed in
   appropriate ways. */
static void
add_variable (struct lexer *lexer,
              struct variable ***v, size_t *nv, size_t *mv,
              char *included, int pv_opts,
              const struct var_set *vs, size_t idx,
              int start_ofs, int end_ofs)
{
  struct variable *add = var_set_get_var (vs, idx);
  const char *add_name = var_get_name (add);

  if ((pv_opts & PV_NUMERIC) && !var_is_numeric (add))
    lex_ofs_msg (lexer, SW, start_ofs, end_ofs,
                 _("%s is not a numeric variable.  It will not be "
                   "included in the variable list."), add_name);
  else if ((pv_opts & PV_STRING) && !var_is_alpha (add))
    lex_ofs_error (lexer, start_ofs, end_ofs,
                   _("%s is not a string variable.  It will not be "
                     "included in the variable list."), add_name);
  else if ((pv_opts & PV_NO_SCRATCH)
           && dict_class_from_id (add_name) == DC_SCRATCH)
    lex_ofs_error (lexer, start_ofs, end_ofs,
                   _("Scratch variables (such as %s) are not allowed "
                     "here."), add_name);
  else if ((pv_opts & (PV_SAME_TYPE | PV_SAME_WIDTH)) && *nv
           && var_get_type (add) != var_get_type ((*v)[0]))
    lex_ofs_error (lexer, start_ofs, end_ofs,
                 _("%s and %s are not the same type.  All variables in "
                   "this variable list must be of the same type.  %s "
                   "will be omitted from the list."),
                 var_get_name ((*v)[0]), add_name, add_name);
  else if ((pv_opts & PV_SAME_WIDTH) && *nv
           && var_get_width (add) != var_get_width ((*v)[0]))
    lex_ofs_error (lexer, start_ofs, end_ofs,
                 _("%s and %s are string variables with different widths.  "
                   "All variables in this variable list must have the "
                   "same width.  %s will be omitted from the list."),
                 var_get_name ((*v)[0]), add_name, add_name);
  else if ((pv_opts & PV_NO_DUPLICATE) && included && included[idx])
    lex_ofs_error (lexer, start_ofs, end_ofs,
                   _("Variable %s appears twice in variable list."), add_name);
  else if ((pv_opts & PV_DUPLICATE) || !included || !included[idx])
    {
      if (*nv >= *mv)
        {
          *mv = 2 * (*nv + 1);
          *v = xnrealloc (*v, *mv, sizeof **v);
        }
      (*v)[(*nv)++] = add;
      if (included != NULL)
        included[idx] = 1;
    }
}

/* Adds the variables in VS with indexes FIRST_IDX through
   LAST_IDX, inclusive, to the list of variables V that has *NV
   elements and room for *MV.  Uses and updates INCLUDED to avoid
   duplicates if indicated by PV_OPTS, which also affects what
   variables are allowed in appropriate ways. */
static void
add_variables (struct lexer *lexer,
               struct variable ***v, size_t *nv, size_t *mv, char *included,
               int pv_opts,
               const struct var_set *vs, int first_idx, int last_idx,
               enum dict_class class,
               int start_ofs, int end_ofs)
{
  size_t i;

  for (i = first_idx; i <= last_idx; i++)
    if (dict_class_from_id (var_get_name (var_set_get_var (vs, i))) == class)
      add_variable (lexer, v, nv, mv, included, pv_opts, vs, i,
                    start_ofs, end_ofs);
}

/* Note that if parse_variables() returns false, *v is free()'d.
   Conversely, if parse_variables() returns true, then *nv is
   nonzero and *v is non-NULL. */
bool
parse_var_set_vars (struct lexer *lexer, const struct var_set *vs,
                    struct variable ***v, size_t *nv,
                    int pv_opts)
{
  size_t mv;
  char *included;

  assert (vs != NULL);
  assert (v != NULL);
  assert (nv != NULL);

  /* At most one of PV_NUMERIC, PV_STRING, PV_SAME_TYPE,
     PV_SAME_WIDTH may be specified. */
  assert (((pv_opts & PV_NUMERIC) != 0)
          + ((pv_opts & PV_STRING) != 0)
          + ((pv_opts & PV_SAME_TYPE) != 0)
          + ((pv_opts & PV_SAME_WIDTH) != 0) <= 1);

  /* PV_DUPLICATE and PV_NO_DUPLICATE are incompatible. */
  assert (!(pv_opts & PV_DUPLICATE) || !(pv_opts & PV_NO_DUPLICATE));

  if (!(pv_opts & PV_APPEND))
    {
      *v = NULL;
      *nv = 0;
      mv = 0;
    }
  else
    mv = *nv;

  if (!(pv_opts & PV_DUPLICATE))
    {
      size_t i;

      included = xcalloc (var_set_get_n (vs), sizeof *included);
      for (i = 0; i < *nv; i++)
        {
          size_t index;
          if (!var_set_lookup_var_idx (vs, var_get_name ((*v)[i]), &index))
            NOT_REACHED ();
          included[index] = 1;
        }
    }
  else
    included = NULL;

  do
    {
      int start_ofs = lex_ofs (lexer);
      if (lex_match (lexer, T_ALL))
        add_variables (lexer, v, nv, &mv, included, pv_opts,
                       vs, 0, var_set_get_n (vs) - 1, DC_ORDINARY,
                       start_ofs, start_ofs);
      else
        {
          enum dict_class class;
          size_t first_idx;

          if (!parse_var_idx_class (lexer, vs, &first_idx, &class))
            goto fail;

          if (!lex_match (lexer, T_TO))
            add_variable (lexer, v, nv, &mv, included, pv_opts, vs, first_idx,
                          start_ofs, start_ofs);
          else
            {
              size_t last_idx;
              enum dict_class last_class;
              struct variable *first_var, *last_var;

              if (!parse_var_idx_class (lexer, vs, &last_idx, &last_class))
                goto fail;

              int end_ofs = lex_ofs (lexer) - 1;

              first_var = var_set_get_var (vs, first_idx);
              last_var = var_set_get_var (vs, last_idx);

              if (last_idx < first_idx)
                {
                  const char *first_name = var_get_name (first_var);
                  const char *last_name = var_get_name (last_var);
                  lex_ofs_error (lexer, start_ofs, end_ofs,
                                 _("%s TO %s is not valid syntax since %s "
                                   "precedes %s in the dictionary."),
                                 first_name, last_name, first_name, last_name);
                  goto fail;
                }

              if (class != last_class)
                {
                  lex_ofs_error (lexer, start_ofs, end_ofs,
                                 _("With the syntax <a> TO <b>, variables <a> "
                                   "and <b> must be both regular variables "
                                   "or both scratch variables."));
                  struct pair
                    {
                      const char *name;
                      enum dict_class class;
                      int ofs;
                    }
                  pairs[2] = {
                    { var_get_name (first_var), class, start_ofs },
                    { var_get_name (last_var), last_class, end_ofs },
                  };
                  for (size_t i = 0; i < 2; i++)
                    switch (pairs[i].class)
                      {
                      case DC_ORDINARY:
                        lex_ofs_msg (lexer, SN, pairs[i].ofs, pairs[i].ofs,
                                     _("%s is a regular variable."),
                                     pairs[i].name);
                        break;

                      case DC_SCRATCH:
                        lex_ofs_msg (lexer, SN, pairs[i].ofs, pairs[i].ofs,
                                     _("%s is a scratch variable."),
                                     pairs[i].name);
                        break;

                      case DC_SYSTEM:
                        lex_ofs_msg (lexer, SN, pairs[i].ofs, pairs[i].ofs,
                                     _("%s is a system variable."),
                                     pairs[i].name);
                        break;
                      }
                  goto fail;
                }

              add_variables (lexer, v, nv, &mv, included, pv_opts,
                             vs, first_idx, last_idx, class,
                             start_ofs, lex_ofs (lexer) - 1);
            }
        }

      if (pv_opts & PV_SINGLE)
        break;
      lex_match (lexer, T_COMMA);
    }
  while (lex_token (lexer) == T_ALL
         || (is_vs_name_token (lexer, vs)
             && var_set_lookup_var (vs, lex_tokcstr (lexer)) != NULL));

  if (*nv == 0)
    goto fail;

  free (included);
  return 1;

fail:
  free (included);
  free (*v);
  *v = NULL;
  *nv = 0;
  return 0;
}

char *
parse_DATA_LIST_var (struct lexer *lexer, const struct dictionary *d,
                     enum dict_class classes)
{
  if (!is_dict_name_token (lexer, d))
    {
      lex_error (lexer, ("Syntax error expecting variable name."));
      return NULL;
    }
  char *error = dict_id_is_valid__ (d, lex_tokcstr (lexer), classes);
  if (error)
    {
      lex_error (lexer, "%s", error);
      free (error);
      return NULL;
    }

  char *name = xstrdup (lex_tokcstr (lexer));
  lex_get (lexer);
  return name;
}

/* Attempts to break UTF-8 encoded NAME into a root (whose contents are
   arbitrary except that it does not end in a digit) followed by an integer
   numeric suffix.  On success, stores the value of the suffix into *NUMBERP,
   the number of digits in the suffix into *N_DIGITSP, and returns the number
   of bytes in the root.  On failure, returns 0. */
static int
extract_numeric_suffix (struct lexer *lexer, int ofs, const char *name,
                        unsigned long int *numberp, int *n_digitsp)
{
  size_t root_len, n_digits;
  size_t i;

  /* Count length of root. */
  root_len = 1;                 /* Valid identifier never starts with digit. */
  for (i = 1; name[i] != '\0'; i++)
    if (!c_isdigit (name[i]))
      root_len = i + 1;
  n_digits = i - root_len;

  if (n_digits == 0)
    {
      lex_ofs_error (lexer, ofs, ofs,
                     _("`%s' cannot be used with TO because it does not end in "
                       "a digit."), name);
      return 0;
    }

  *numberp = strtoull (name + root_len, NULL, 10);
  if (*numberp == ULONG_MAX)
    {
      lex_ofs_error (lexer, ofs, ofs,
                     _("Numeric suffix on `%s' is larger than supported with TO."),
                     name);
      return 0;
    }
  *n_digitsp = n_digits;
  return root_len;
}

static bool
add_var_name (struct lexer *lexer, int start_ofs, int end_ofs, char *name,
              char ***names, size_t *n_vars, size_t *allocated_vars,
              struct stringi_set *set, int pv_opts)
{
  if (pv_opts & PV_NO_DUPLICATE && !stringi_set_insert (set, name))
    {
      lex_ofs_error (lexer, start_ofs, end_ofs,
                     _("Variable %s appears twice in variable list."),
                     name);
      return false;
    }

  if (*n_vars >= *allocated_vars)
    *names = x2nrealloc (*names, allocated_vars, sizeof **names);
  (*names)[(*n_vars)++] = name;
  return true;
}

/* Parses a list of variable names according to the DATA LIST version
   of the TO convention.  */
bool
parse_DATA_LIST_vars (struct lexer *lexer, const struct dictionary *dict,
                      char ***namesp, size_t *n_varsp, int pv_opts)
{
  char **names;
  size_t n_vars;
  size_t allocated_vars;

  struct stringi_set set;

  char *name1 = NULL;
  char *name2 = NULL;

  bool ok = false;

  assert ((pv_opts & ~(PV_APPEND | PV_SINGLE | PV_DUPLICATE
                       | PV_NO_SCRATCH | PV_NO_DUPLICATE)) == 0);
  stringi_set_init (&set);

  if (pv_opts & PV_APPEND)
    {
      n_vars = allocated_vars = *n_varsp;
      names = *namesp;

      if (pv_opts & PV_NO_DUPLICATE)
        {
          size_t i;

          for (i = 0; i < n_vars; i++)
            stringi_set_insert (&set, names[i]);
        }
    }
  else
    {
      n_vars = allocated_vars = 0;
      names = NULL;
    }

  enum dict_class classes = (pv_opts & PV_NO_SCRATCH
                             ? DC_ORDINARY
                             : DC_ORDINARY | DC_SCRATCH);
  do
    {
      int start_ofs = lex_ofs (lexer);
      name1 = parse_DATA_LIST_var (lexer, dict, classes);
      if (!name1)
        goto exit;
      if (lex_match (lexer, T_TO))
        {
          unsigned long int num1, num2;
          int n_digits1, n_digits2;
          int root_len1, root_len2;
          unsigned long int number;

          name2 = parse_DATA_LIST_var (lexer, dict, classes);
          if (!name2)
            goto exit;
          int end_ofs = lex_ofs (lexer) - 1;

          root_len1 = extract_numeric_suffix (lexer, start_ofs,
                                              name1, &num1, &n_digits1);
          if (root_len1 == 0)
            goto exit;

          root_len2 = extract_numeric_suffix (lexer, end_ofs,
                                              name2, &num2, &n_digits2);
          if (root_len2 == 0)
            goto exit;

          if (root_len1 != root_len2 || memcasecmp (name1, name2, root_len1))
            {
              lex_ofs_error (lexer, start_ofs, end_ofs,
                             _("Prefixes don't match in use of TO convention."));
              goto exit;
            }
          if (num1 > num2)
            {
              lex_ofs_error (lexer, start_ofs, end_ofs,
                             _("Bad bounds in use of TO convention."));
              goto exit;
            }

          for (number = num1; number <= num2; number++)
            {
              char *name = xasprintf ("%.*s%0*lu",
                                      root_len1, name1,
                                      n_digits1, number);
              if (!add_var_name (lexer, start_ofs, end_ofs,
                                 name, &names, &n_vars, &allocated_vars,
                                 &set, pv_opts))
                {
                  free (name);
                  goto exit;
                }
            }

          free (name1);
          name1 = NULL;
          free (name2);
          name2 = NULL;
        }
      else
        {
          if (!add_var_name (lexer, start_ofs, start_ofs,
                             name1, &names, &n_vars, &allocated_vars,
                             &set, pv_opts))
            goto exit;
          name1 = NULL;
        }

      lex_match (lexer, T_COMMA);

      if (pv_opts & PV_SINGLE)
        break;
    }
  while (lex_token (lexer) == T_ID);
  ok = true;

exit:
  stringi_set_destroy (&set);
  if (ok)
    {
      *namesp = names;
      *n_varsp = n_vars;
    }
  else
    {
      int i;
      for (i = 0; i < n_vars; i++)
        free (names[i]);
      free (names);
      *namesp = NULL;
      *n_varsp = 0;

      free (name1);
      free (name2);
    }
  return ok;
}

/* Registers each of the NAMES[0...NNAMES - 1] in POOL, as well
   as NAMES itself. */
static void
register_vars_pool (struct pool *pool, char **names, size_t nnames)
{
  size_t i;

  for (i = 0; i < nnames; i++)
    pool_register (pool, free, names[i]);
  pool_register (pool, free, names);
}

/* Parses a list of variable names according to the DATA LIST
   version of the TO convention.  Same args as
   parse_DATA_LIST_vars(), except that all allocations are taken
   from the given POOL. */
bool
parse_DATA_LIST_vars_pool (struct lexer *lexer, const struct dictionary *dict,
                           struct pool *pool,
                           char ***names, size_t *nnames, int pv_opts)
{
  int retval;

  /* PV_APPEND is unsafe because parse_DATA_LIST_vars would free
     the existing names on failure, but those names are
     presumably already in the pool, which would attempt to
     re-free it later. */
  assert (!(pv_opts & PV_APPEND));

  retval = parse_DATA_LIST_vars (lexer, dict, names, nnames, pv_opts);
  if (retval)
    register_vars_pool (pool, *names, *nnames);
  return retval;
}

/* Parses a list of variables where some of the variables may be
   existing and the rest are to be created.  Same args as
   parse_DATA_LIST_vars(). */
bool
parse_mixed_vars (struct lexer *lexer, const struct dictionary *dict,
                  char ***names, size_t *nnames, int pv_opts)
{
  size_t i;

  assert (names != NULL);
  assert (nnames != NULL);

  if (!(pv_opts & PV_APPEND))
    {
      *names = NULL;
      *nnames = 0;
    }
  while (is_dict_name_token (lexer, dict) || lex_token (lexer) == T_ALL)
    {
      if (lex_token (lexer) == T_ALL || dict_lookup_var (dict, lex_tokcstr (lexer)) != NULL)
        {
          struct variable **v;
          size_t nv;

          if (!parse_variables (lexer, dict, &v, &nv, pv_opts))
            goto fail;
          *names = xnrealloc (*names, *nnames + nv, sizeof **names);
          for (i = 0; i < nv; i++)
            (*names)[*nnames + i] = xstrdup (var_get_name (v[i]));
          free (v);
          *nnames += nv;
        }
      else if (!parse_DATA_LIST_vars (lexer, dict, names, nnames, PV_APPEND | pv_opts))
        goto fail;
    }
  if (*nnames == 0)
    goto fail;

  return true;

fail:
  for (i = 0; i < *nnames; i++)
    free ((*names)[i]);
  free (*names);
  *names = NULL;
  *nnames = 0;
  return false;
}

/* Parses a list of variables where some of the variables may be
   existing and the rest are to be created.  Same args as
   parse_mixed_vars(), except that all allocations are taken
   from the given POOL. */
bool
parse_mixed_vars_pool (struct lexer *lexer, const struct dictionary *dict, struct pool *pool,
                       char ***names, size_t *nnames, int pv_opts)
{
  int retval;

  /* PV_APPEND is unsafe because parse_mixed_vars_pool would free
     the existing names on failure, but those names are
     presumably already in the pool, which would attempt to
     re-free it later. */
  assert (!(pv_opts & PV_APPEND));

  retval = parse_mixed_vars (lexer, dict, names, nnames, pv_opts);
  if (retval)
    register_vars_pool (pool, *names, *nnames);
  return retval;
}

/* Frees the N var_syntax structures in VS, as well as VS itself. */
void
var_syntax_destroy (struct var_syntax *vs, size_t n)
{
  for (size_t i = 0; i < n; i++)
    {
      free (vs[i].first);
      free (vs[i].last);
    }
  free (vs);
}

/* Parses syntax for variables and variable ranges from LEXER.  If successful,
   initializes *VS to the beginning of an array of var_syntax structs and *N_VS
   to the number of elements in the array and returns true.  On error, sets *VS
   to NULL and *N_VS to 0 and returns false. */
bool
var_syntax_parse (struct lexer *lexer, struct var_syntax **vs, size_t *n_vs)
{
  *vs = NULL;
  *n_vs = 0;

  if (lex_token (lexer) != T_ID)
    {
      lex_error (lexer, _("Syntax error expecting variable name."));
      goto error;
    }

  size_t allocated_vs = 0;
  do
    {
      if (allocated_vs >= *n_vs)
        *vs = x2nrealloc (*vs, &allocated_vs, sizeof **vs);
      struct var_syntax *new = &(*vs)[(*n_vs)++];
      *new = (struct var_syntax) {
        .first = ss_xstrdup (lex_tokss (lexer)),
        .first_ofs = lex_ofs (lexer)
      };
      lex_get (lexer);

      if (lex_match (lexer, T_TO))
        {
          if (lex_token (lexer) != T_ID)
            {
              lex_error (lexer, _("Syntax error expecting variable name."));
              goto error;
            }

          new->last = ss_xstrdup (lex_tokss (lexer));
          lex_get (lexer);
        }
      new->last_ofs = lex_ofs (lexer) - 1;
    }
  while (lex_token (lexer) == T_ID);
  return true;

error:
  var_syntax_destroy (*vs, *n_vs);
  *vs = NULL;
  *n_vs = 0;
  return false;
}

/* Looks up the N_VS var syntax structs in VS in DICT, translating them to an
   array of variables.  If successful, initializes *VARS to the beginning of an
   array of pointers to variables and *N_VARS to the length of the array and
   returns true.  On error, sets *VARS to NULL and *N_VARS to 0.

   The LEXER is just used for error messages.

   For the moment, only honors PV_NUMERIC in OPTS. */
bool
var_syntax_evaluate (struct lexer *lexer,
                     const struct var_syntax *vs, size_t n_vs,
                     const struct dictionary *dict,
                     struct variable ***vars, size_t *n_vars, int opts)
{
  assert (!(opts & ~PV_NUMERIC));

  *vars = NULL;
  *n_vars = 0;

  size_t allocated_vars = 0;
  for (size_t i = 0; i < n_vs; i++)
    {
      int first_ofs = vs[i].first_ofs;
      struct variable *first = dict_lookup_var (dict, vs[i].first);
      if (!first)
        {
          lex_ofs_error (lexer, first_ofs, first_ofs,
                         _("%s is not a variable name."), vs[i].first);
          goto error;
        }

      int last_ofs = vs[i].last_ofs;
      struct variable *last = (vs[i].last
                               ? dict_lookup_var (dict, vs[i].last)
                               : first);
      if (!last)
        {
          lex_ofs_error (lexer, last_ofs, last_ofs,
                         _("%s is not a variable name."), vs[i].last);
          goto error;
        }

      size_t first_idx = var_get_dict_index (first);
      size_t last_idx = var_get_dict_index (last);
      if (last_idx < first_idx)
        {
          lex_ofs_error (lexer, first_ofs, last_ofs,
                         _("%s TO %s is not valid syntax since %s "
                           "precedes %s in the dictionary."),
                         vs[i].first, vs[i].last,
                         vs[i].first, vs[i].last);
          goto error;
        }

      for (size_t j = first_idx; j <= last_idx; j++)
        {
          struct variable *v = dict_get_var (dict, j);
          if (opts & PV_NUMERIC && !var_is_numeric (v))
            {
              lex_ofs_error (lexer, first_ofs, last_ofs,
                             _("%s is not a numeric variable."),
                             var_get_name (v));
              goto error;
            }

          if (*n_vars >= allocated_vars)
            *vars = x2nrealloc (*vars, &allocated_vars, sizeof **vars);
          (*vars)[(*n_vars)++] = v;
        }
    }

  return true;

error:
  free (*vars);
  *vars = NULL;
  *n_vars = 0;
  return false;
}

/* A set of variables. */
struct var_set
  {
    bool names_must_be_ids;
    size_t (*get_n) (const struct var_set *);
    struct variable *(*get_var) (const struct var_set *, size_t idx);
    bool (*lookup_var_idx) (const struct var_set *, const char *, size_t *);
    void (*destroy) (struct var_set *);
    void *aux;
  };

/* Returns the number of variables in VS. */
size_t
var_set_get_n (const struct var_set *vs)
{
  assert (vs != NULL);

  return vs->get_n (vs);
}

/* Return variable with index IDX in VS.
   IDX must be less than the number of variables in VS. */
static struct variable *
var_set_get_var (const struct var_set *vs, size_t idx)
{
  assert (vs != NULL);
  assert (idx < var_set_get_n (vs));

  return vs->get_var (vs, idx);
}

/* Returns the variable in VS named NAME, or a null pointer if VS
   contains no variable with that name. */
struct variable *
var_set_lookup_var (const struct var_set *vs, const char *name)
{
  size_t idx;
  return (var_set_lookup_var_idx (vs, name, &idx)
          ? var_set_get_var (vs, idx)
          : NULL);
}

/* If VS contains a variable named NAME, sets *IDX to its index
   and returns true.  Otherwise, returns false. */
bool
var_set_lookup_var_idx (const struct var_set *vs, const char *name,
                        size_t *idx)
{
  assert (vs != NULL);
  assert (name != NULL);

  return vs->lookup_var_idx (vs, name, idx);
}

/* Destroys VS. */
void
var_set_destroy (struct var_set *vs)
{
  if (vs != NULL)
    vs->destroy (vs);
}

static bool
var_set_get_names_must_be_ids (const struct var_set *vs)
{
  return vs->names_must_be_ids;
}

/* Returns the number of variables in VS. */
static size_t
dict_var_set_get_n (const struct var_set *vs)
{
  struct dictionary *d = vs->aux;

  return dict_get_n_vars (d);
}

/* Return variable with index IDX in VS.
   IDX must be less than the number of variables in VS. */
static struct variable *
dict_var_set_get_var (const struct var_set *vs, size_t idx)
{
  struct dictionary *d = vs->aux;

  return dict_get_var (d, idx);
}

/* If VS contains a variable named NAME, sets *IDX to its index
   and returns true.  Otherwise, returns false. */
static bool
dict_var_set_lookup_var_idx (const struct var_set *vs, const char *name,
                             size_t *idx)
{
  struct dictionary *d = vs->aux;
  struct variable *v = dict_lookup_var (d, name);
  if (v != NULL)
    {
      *idx = var_get_dict_index (v);
      return true;
    }
  else
    return false;
}

/* Destroys VS. */
static void
dict_var_set_destroy (struct var_set *vs)
{
  free (vs);
}

/* Returns a variable set based on D. */
struct var_set *
var_set_create_from_dict (const struct dictionary *d)
{
  struct var_set *vs = xmalloc (sizeof *vs);
  vs->names_must_be_ids = dict_get_names_must_be_ids (d);
  vs->get_n = dict_var_set_get_n;
  vs->get_var = dict_var_set_get_var;
  vs->lookup_var_idx = dict_var_set_lookup_var_idx;
  vs->destroy = dict_var_set_destroy;
  vs->aux = (void *) d;
  return vs;
}

/* A variable set based on an array. */
struct array_var_set
  {
    struct variable *const *var;/* Array of variables. */
    size_t n_vars;              /* Number of elements in var. */
    struct hmapx vars_by_name;  /* Variables hashed by name. */
  };

/* Returns the number of variables in VS. */
static size_t
array_var_set_get_n (const struct var_set *vs)
{
  struct array_var_set *avs = vs->aux;

  return avs->n_vars;
}

/* Return variable with index IDX in VS.
   IDX must be less than the number of variables in VS. */
static struct variable *
array_var_set_get_var (const struct var_set *vs, size_t idx)
{
  struct array_var_set *avs = vs->aux;

  return CONST_CAST (struct variable *, avs->var[idx]);
}

/* If VS contains a variable named NAME, sets *IDX to its index
   and returns true.  Otherwise, returns false. */
static bool
array_var_set_lookup_var_idx (const struct var_set *vs, const char *name,
                              size_t *idx)
{
  struct array_var_set *avs = vs->aux;
  struct hmapx_node *node;
  struct variable **varp;

  HMAPX_FOR_EACH_WITH_HASH (varp, node, utf8_hash_case_string (name, 0),
                            &avs->vars_by_name)
    if (!utf8_strcasecmp (name, var_get_name (*varp)))
      {
        *idx = varp - avs->var;
        return true;
      }

  return false;
}

/* Destroys VS. */
static void
array_var_set_destroy (struct var_set *vs)
{
  struct array_var_set *avs = vs->aux;

  hmapx_destroy (&avs->vars_by_name);
  free (avs);
  free (vs);
}

/* Returns a variable set based on the N_VARS variables in VAR. */
struct var_set *
var_set_create_from_array (struct variable *const *var, size_t n_vars)
{
  struct var_set *vs;
  struct array_var_set *avs;
  size_t i;

  vs = xmalloc (sizeof *vs);
  vs->names_must_be_ids = true;
  vs->get_n = array_var_set_get_n;
  vs->get_var = array_var_set_get_var;
  vs->lookup_var_idx = array_var_set_lookup_var_idx;
  vs->destroy = array_var_set_destroy;
  vs->aux = avs = xmalloc (sizeof *avs);
  avs->var = var;
  avs->n_vars = n_vars;
  hmapx_init (&avs->vars_by_name);
  for (i = 0; i < n_vars; i++)
    {
      const char *name = var_get_name (var[i]);
      size_t idx;

      if (array_var_set_lookup_var_idx (vs, name, &idx))
        {
          var_set_destroy (vs);
          return NULL;
        }
      hmapx_insert (&avs->vars_by_name, CONST_CAST (void *, &avs->var[i]),
                    utf8_hash_case_string (name, 0));
    }

  return vs;
}


/* Match a variable.
   If the match succeeds, the variable will be placed in VAR.
   Returns true if successful */
bool
lex_match_variable (struct lexer *lexer, const struct dictionary *dict, const struct variable **var)
{
  if (lex_token (lexer) != T_ID)
    return false;

  *var = parse_variable_const (lexer, dict);
  return *var != NULL;
}
