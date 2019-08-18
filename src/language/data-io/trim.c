/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2008, 2010, 2011,
   2019 Free Software Foundation, Inc.

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
      while (lex_token (lexer) == T_ID || lex_token (lexer) == T_STRING)
	{
	  if (n_newvars >= n_oldvars)
	    break;
	  const char *new_name = lex_tokcstr (lexer);
	  if (!relax && ! id_is_plausible (new_name, true))
	    goto fail;

	  if (dict_lookup_var (dict, new_name) != NULL)
	    {
	      msg (SE, _("Cannot rename %s as %s because there already exists "
			 "a variable named %s.  To rename variables with "
			 "overlapping names, use a single RENAME subcommand "
			 "such as `/RENAME (A=B)(B=C)(C=A)', or equivalently, "
			 "`/RENAME (A B C=B C A)'."),
		   var_get_name (oldvars[n_newvars]), new_name, new_name);
	      goto fail;
	    }
	  newnames[n_newvars] = strdup (new_name);
	  lex_get (lexer);
	  n_newvars++;
	}
      if (n_newvars != n_oldvars)
	{
	  msg (SE, _("Number of variables on left side of `=' (%zu) does not "
                     "match number of variables on right side (%zu), in "
                     "parenthesized group %d of RENAME subcommand."),
	       n_newvars, n_oldvars, group);
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
  v = xnrealloc (v, dict_get_var_cnt (dict) - nv, sizeof *v);
  for (i = nv; i < dict_get_var_cnt (dict); i++)
    v[i - nv] = dict_get_var (dict, i);
  dict_delete_vars (dict, v, dict_get_var_cnt (dict) - nv);
  free (v);

  return true;
}
