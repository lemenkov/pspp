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

#include "language/commands/trim.h"

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

/* Commands that read and write system files share a great deal of common
   syntactic structure for rearranging and dropping variables.  This function
   parses this syntax and modifies DICT appropriately.  Returns true on
   success, false on failure. */
bool
parse_dict_trim (struct lexer *lexer, struct dictionary *dict)
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
    return parse_dict_rename (lexer, dict);
  else
    {
      lex_error_expecting (lexer, "MAP", "DROP", "KEEP", "RENAME");
      return false;
    }
}

/* Parses and performs the RENAME subcommand of GET, SAVE, and
   related commands. */
bool
parse_dict_rename (struct lexer *lexer, struct dictionary *dict)
{
  lex_match (lexer, T_EQUALS);
  int start_ofs = lex_ofs (lexer);

  struct variable **old_vars = NULL;
  size_t n_old_vars = 0;

  char **new_vars = NULL;
  size_t n_new_vars = 0;

  bool ok = false;
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
    {
      size_t prev_n_old = n_old_vars;
      size_t prev_n_new = n_new_vars;

      bool paren = lex_match (lexer, T_LPAREN);
      int pv_opts = PV_NO_DUPLICATE | PV_APPEND | (paren ? 0 : PV_SINGLE);

      int old_vars_start = lex_ofs (lexer);
      if (!parse_variables (lexer, dict, &old_vars, &n_old_vars, pv_opts))
        goto done;
      int old_vars_end = lex_ofs (lexer) - 1;

      if (!lex_force_match (lexer, T_EQUALS))
        goto done;

      int new_vars_start = lex_ofs (lexer);
      if (!parse_DATA_LIST_vars (lexer, dict, &new_vars, &n_new_vars, pv_opts))
        goto done;
      int new_vars_end = lex_ofs (lexer) - 1;

      if (paren && !lex_force_match (lexer, T_RPAREN))
        goto done;

      if (n_new_vars != n_old_vars)
        {
          size_t added_old = n_old_vars - prev_n_old;
          size_t added_new = n_new_vars - prev_n_new;

          msg (SE, _("Old and new variable counts do not match."));
          lex_ofs_msg (lexer, SN, old_vars_start, old_vars_end,
                       ngettext ("There is %zu old variable.",
                                 "There are %zu old variables.", added_old),
                       added_old);
          lex_ofs_msg (lexer, SN, new_vars_start, new_vars_end,
                       ngettext ("There is %zu new variable name.",
                                 "There are %zu new variable names.",
                                 added_new),
                       added_new);
          goto done;
        }
    }
  int end_ofs = lex_ofs (lexer) - 1;

  char *dup_name = NULL;
  if (!dict_rename_vars (dict, old_vars, new_vars, n_new_vars, &dup_name))
    {
      lex_ofs_error (lexer, start_ofs, end_ofs,
                     _("Requested renaming duplicates variable name %s."),
                     dup_name);
      goto done;
    }
  ok = true;

done:
  free (old_vars);
  for (size_t i = 0; i < n_new_vars; ++i)
    free (new_vars[i]);
  free (new_vars);
  return ok;
}

/* Parses and performs the DROP subcommand of GET, SAVE, and
   related commands.
   Returns true if successful, false on failure.*/
bool
parse_dict_drop (struct lexer *lexer, struct dictionary *dict)
{
  int start_ofs = lex_ofs (lexer) - 1;
  lex_match (lexer, T_EQUALS);

  struct variable **v;
  size_t nv;
  if (!parse_variables (lexer, dict, &v, &nv, PV_NONE))
    return false;
  dict_delete_vars (dict, v, nv);
  free (v);

  if (dict_get_n_vars (dict) == 0)
    {
      lex_ofs_error (lexer, start_ofs, lex_ofs (lexer) - 1,
                     _("Cannot DROP all variables from dictionary."));
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
  if (dict_get_n_vars (dict) == nv)
    {
      free (v);
      return true;
    }

  v = xnrealloc (v, dict_get_n_vars (dict) - nv, sizeof *v);
  for (i = nv; i < dict_get_n_vars (dict); i++)
    v[i - nv] = dict_get_var (dict, i);
  dict_delete_vars (dict, v, dict_get_n_vars (dict) - nv);
  free (v);

  return true;
}
