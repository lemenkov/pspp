/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011 Free Software Foundation, Inc.

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
#include "data/dictionary.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/expressions/public.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* SELECT IF transformation. */
struct select_if_trns
  {
    struct expression *e;        /* Test expression. */
  };

static const struct trns_class select_if_trns_class;

/* Parses the SELECT IF transformation. */
int
cmd_select_if (struct lexer *lexer, struct dataset *ds)
{
  struct expression *e;
  struct select_if_trns *t;

  e = expr_parse_bool (lexer, ds);
  if (!e)
    return CMD_CASCADING_FAILURE;

  if (lex_token (lexer) != T_ENDCMD)
    {
      expr_free (e);
      lex_error (lexer, _("Syntax error expecting end of command."));
      return CMD_CASCADING_FAILURE;
    }

  t = xmalloc (sizeof *t);
  t->e = e;
  add_transformation (ds, &select_if_trns_class, t);

  return CMD_SUCCESS;
}

/* Performs the SELECT IF transformation T on case C. */
static enum trns_result
select_if_proc (void *t_, struct ccase **c,
                casenumber case_num)
{
  struct select_if_trns *t = t_;
  return (expr_evaluate_num (t->e, *c, case_num) == 1.0
          ? TRNS_CONTINUE : TRNS_DROP_CASE);
}

/* Frees SELECT IF transformation T. */
static bool
select_if_free (void *t_)
{
  struct select_if_trns *t = t_;
  expr_free (t->e);
  free (t);
  return true;
}

static const struct trns_class select_if_trns_class = {
  .name = "SELECT IF",
  .execute = select_if_proc,
  .destroy = select_if_free,
};

/* Parses the FILTER command. */
int
cmd_filter (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  if (lex_match_id (lexer, "OFF"))
    dict_set_filter (dict, NULL);
  else if (lex_match (lexer, T_BY))
    {
      struct variable *v = parse_variable (lexer, dict);
      if (!v)
        return CMD_FAILURE;

      if (var_is_alpha (v))
        {
          lex_next_error (lexer, -1, -1,
                          _("The filter variable must be numeric."));
          return CMD_FAILURE;
        }

      if (dict_class_from_id (var_get_name (v)) == DC_SCRATCH)
        {
          lex_next_error (lexer, -1, -1,
                          _("The filter variable may not be scratch."));
          return CMD_FAILURE;
        }

      dict_set_filter (dict, v);
    }
  else
    {
      lex_error_expecting (lexer, "OFF", "BY");
      return CMD_FAILURE;
    }

  return CMD_SUCCESS;
}
