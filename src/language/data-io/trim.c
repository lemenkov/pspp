/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2008, 2010, 2011,
   2019, 2020 Free Software Foundation, Inc.

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

#include "language/data-io/trim.h"

#include <stdlib.h>

#include "data/dictionary.h"
#include "data/variable.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Commands that read and write system files share a great deal
   of common syntactic structure for rearranging and dropping
   variables.  This function parses this syntax and modifies DICT
   appropriately.  If RELAX is true, then the modified dictionary
   need not conform to the usual variable name rules.  Returns
   true on success, false on failure. */
bool
parse_dict_trim (struct lexer *lexer, struct dictionary *dict, bool relax)
{
  if (lex_match_id (lexer, "MAP"))
    {
      /* FIXME. */
      return true;
    }
  else if (lex_match_id (lexer, "DROP"))
    return parse_dict_drop (lexer, dict);
  else if (lex_match_id (lexer, "KEEP"))
    return parse_dict_keep (lexer, dict);
  else if (lex_match_id (lexer, "RENAME"))
    return parse_dict_rename (lexer, dict, relax);
  else
    {
      lex_error (lexer, _("expecting a valid subcommand"));
      return false;
    }
}

/* Check that OLD_NAME can be renamed to NEW_NAME in DICT.  */
static bool
check_rename (const struct dictionary *dict, const char *old_name, const char *new_name)
{
  if (dict_lookup_var (dict, new_name) != NULL)
    {
      msg (SE, _("Cannot rename %s as %s because there already exists "
                 "a variable named %s.  To rename variables with "
                 "overlapping names, use a single RENAME subcommand "
                 "such as `/RENAME (A=B)(B=C)(C=A)', or equivalently, "
                 "`/RENAME (A B C=B C A)'."),
           old_name, new_name, new_name);
      return false;
    }
  return true;
}

/* Parse a  "VarX TO VarY" sequence where X and Y are integers
   such that X >= Y.
   If successfull, returns a string to the prefix Var and sets FIRST
   to X and LAST to Y.  Returns NULL on failure.
   The caller must free the return value.  */
static char *
try_to_sequence (struct lexer *lexer, const struct dictionary *dict,
                 int *first, int *last)
{
  /* Check that the next 3 tokens are of the correct type.  */
  if (lex_token (lexer) != T_ID
      || lex_next_token (lexer, 1) != T_TO
      || lex_next_token (lexer, 2) != T_ID)
    return NULL;

  /* Check that the first and last tokens are suitable as
     variable names.  */
  const char *s0 = lex_tokcstr (lexer);
  if (!id_is_valid (s0, dict_get_encoding (dict), true))
    return NULL;

  const char *s1 = lex_next_tokcstr (lexer, 2);
  if (!id_is_valid (s1, dict_get_encoding (dict), true))
    return NULL;

  int x0 = strcspn (s0, "0123456789");
  int x1 = strcspn (s1, "0123456789");

  /* The non-digit parts of s0 and s1 must be the same length.  */
  if (x0 != x1)
    return NULL;

  /* Both s0 and s1 must have some digits.  */
  if (strlen (s0) <= x0)
    return NULL;

  if (strlen (s1) <= x1)
    return NULL;

  /* The non-digit parts of s0 and s1 must be identical.  */
  if (0 != strncmp (s0, s1, x0))
    return NULL;

  /* Both names must end with digits.  */
  int len_s0_pfx = strspn (s0 + x0, "0123456789");
  if (len_s0_pfx + x0 != strlen (s0))
    return NULL;

  int len_s1_pfx = strspn (s1 + x1, "0123456789");
  if (len_s1_pfx + x1 != strlen (s1))
    return NULL;

  const char *n_start = s0 + x0;
  const char *n_stop = s1 + x1;

  /* The first may not be greater than the last.  */
  if (atoi (n_start) > atoi (n_stop))
    return NULL;

  char *prefix = xstrndup (s0, x1);

  *first = atoi (n_start);
  *last = atoi (n_stop);

  return prefix;
}


/* Parses and performs the RENAME subcommand of GET, SAVE, and
   related commands.  If RELAX is true, then the new variable
   names need  not conform to the normal dictionary rules.
*/
bool
parse_dict_rename (struct lexer *lexer, struct dictionary *dict,
		   bool relax)
{
  struct variable **oldvars = NULL;
  size_t n_newvars = 0;
  int group = 0;
  char **newnames = NULL;
  lex_match (lexer, T_EQUALS);

  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
    {
      size_t n_oldvars = 0;
      oldvars = NULL;
      n_newvars = 0;
      n_oldvars = 0;
      oldvars = NULL;

      bool paren = lex_match (lexer, T_LPAREN);
      group++;
      if (!parse_variables (lexer, dict, &oldvars, &n_oldvars, PV_NO_DUPLICATE))
	goto fail;

      if (!lex_force_match (lexer, T_EQUALS))
	goto fail;

      newnames = xmalloc (sizeof *newnames * n_oldvars);

      char *prefix = NULL;
      int first, last;
      /* First attempt to parse v1 TO v10 format.  */
      if ((prefix = try_to_sequence (lexer, dict, &first, &last)))
        {
          /* These 3 tokens have already been checked in the
             try_to_sequence function.  */
          lex_get (lexer);
          lex_get (lexer);
          lex_get (lexer);

          /* Make sure the new names are suitable.  */
          for (int i = first; i <= last; ++i)
            {
              int sz = strlen (prefix) + intlog10 (last) + 1;
              char *vn = malloc (sz);
              snprintf (vn, sz, "%s%d", prefix, i);

              if (!check_rename (dict, var_get_name (oldvars[n_newvars]), vn))
                {
                  free (prefix);
                  goto fail;
                }

              newnames[i - first] = vn;
              n_newvars++;
            }
        }
      else
      while (lex_token (lexer) == T_ID || lex_token (lexer) == T_STRING)
        {
          if (n_newvars >= n_oldvars)
            break;
          const char *new_name = lex_tokcstr (lexer);
          if (!relax && ! id_is_plausible (new_name, true))
            goto fail;

          if (!check_rename (dict, var_get_name (oldvars[n_newvars]), new_name))
            goto fail;
          newnames[n_newvars] = strdup (new_name);
          lex_get (lexer);
          n_newvars++;
        }
      free (prefix);

      if (n_newvars != n_oldvars)
	{
	  msg (SE, _("Number of variables on left side of `=' (%zu) does not "
                     "match number of variables on right side (%zu), in "
                     "parenthesized group %d of RENAME subcommand."),
	       n_oldvars, n_newvars, group);
	  goto fail;
	}

      if (paren)
	if (!lex_force_match (lexer, T_RPAREN))
	  goto fail;

      char *errname = 0;
      if (!dict_rename_vars (dict, oldvars, newnames, n_newvars, &errname))
	{
	  msg (SE,
	       _("Requested renaming duplicates variable name %s."),
	       errname);
	  goto fail;
	}
      free (oldvars);
      for (int i = 0; i < n_newvars; ++i)
	free (newnames[i]);
      free (newnames);
      newnames = NULL;
    }

  return true;

 fail:
  free (oldvars);
  for (int i = 0; i < n_newvars; ++i)
    free (newnames[i]);
  free (newnames);
  newnames = NULL;
  return false;
}

/* Parses and performs the DROP subcommand of GET, SAVE, and
   related commands.
   Returns true if successful, false on failure.*/
bool
parse_dict_drop (struct lexer *lexer, struct dictionary *dict)
{
  struct variable **v;
  size_t nv;

  lex_match (lexer, T_EQUALS);
  if (!parse_variables (lexer, dict, &v, &nv, PV_NONE))
    return false;
  dict_delete_vars (dict, v, nv);
  free (v);

  if (dict_get_var_cnt (dict) == 0)
    {
      msg (SE, _("Cannot DROP all variables from dictionary."));
      return false;
    }
  return true;
}

/* Parses and performs the KEEP subcommand of GET, SAVE, and
   related commands.
   Returns true if successful, false on failure.*/
bool
parse_dict_keep (struct lexer *lexer, struct dictionary *dict)
{
  struct variable **v;
  size_t nv;
  size_t i;

  lex_match (lexer, T_EQUALS);
  if (!parse_variables (lexer, dict, &v, &nv, PV_NONE))
    return false;

  /* Move the specified variables to the beginning. */
  dict_reorder_vars (dict, v, nv);

  /* Delete the remaining variables. */
  if (dict_get_var_cnt (dict) == nv)
    {
      free (v);
      return true;
    }

  v = xnrealloc (v, dict_get_var_cnt (dict) - nv, sizeof *v);
  for (i = nv; i < dict_get_var_cnt (dict); i++)
    v[i - nv] = dict_get_var (dict, i);
  dict_delete_vars (dict, v, dict_get_var_cnt (dict) - nv);
  free (v);

  return true;
}
