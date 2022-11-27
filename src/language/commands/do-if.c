/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009-2012 Free Software Foundation, Inc.

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
#include "data/transformations.h"
#include "language/command.h"
#include "language/commands/inpt-pgm.h"
#include "language/expressions/public.h"
#include "language/lexer/lexer.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A conditional clause. */
struct clause
  {
    struct msg_location *location;
    struct expression *condition; /* Test expression; NULL for ELSE clause. */
    struct trns_chain xforms;
  };

/* DO IF transformation. */
struct do_if_trns
  {
    struct clause *clauses;     /* Clauses. */
    size_t n_clauses;           /* Number of clauses. */

    const struct trns_chain *resume;
    size_t ofs;
  };

static const struct trns_class do_if_trns_class;

static void
start_clause (struct lexer *lexer, struct dataset *ds,
              bool condition, struct do_if_trns *do_if,
              size_t *allocated_clauses, bool *ok)
{
  if (*ok && do_if->n_clauses > 0
      && !do_if->clauses[do_if->n_clauses - 1].condition)
    {
      if (condition)
        lex_ofs_error (lexer, 0, 1,
                       _("ELSE IF is not allowed following ELSE "
                         "within DO IF...END IF."));
      else
        lex_ofs_error (lexer, 0, 0,
                       _("Only one ELSE is allowed within DO IF...END IF."));

      msg_at (SN, do_if->clauses[do_if->n_clauses - 1].location,
              _("This is the location of the previous ELSE clause."));

      msg_at (SN, do_if->clauses[0].location,
              _("This is the location of the DO IF command."));

      *ok = false;
    }

  if (do_if->n_clauses >= *allocated_clauses)
    do_if->clauses = x2nrealloc (do_if->clauses, allocated_clauses,
                                 sizeof *do_if->clauses);
  struct clause *clause = &do_if->clauses[do_if->n_clauses++];

  *clause = (struct clause) { .location = NULL };
  if (condition)
    {
      clause->condition = expr_parse_bool (lexer, ds);
      if (!clause->condition)
        lex_discard_rest_of_command (lexer);
    }
  clause->location = lex_ofs_location (lexer, 0, lex_ofs (lexer));

  lex_end_of_command (lexer);
  lex_get (lexer);

  proc_push_transformations (ds);
}

static void
finish_clause (struct dataset *ds, struct do_if_trns *do_if)
{
  struct clause *clause = &do_if->clauses[do_if->n_clauses - 1];
  proc_pop_transformations (ds, &clause->xforms);
}

/* Parse DO IF. */
int
cmd_do_if (struct lexer *lexer, struct dataset *ds)
{
  struct do_if_trns *do_if = xmalloc (sizeof *do_if);
  *do_if = (struct do_if_trns) { .n_clauses = 0 };

  size_t allocated_clauses = 0;
  bool ok = true;

  start_clause (lexer, ds, true, do_if, &allocated_clauses, &ok);
  while (!lex_match_phrase (lexer, "END IF"))
    {
      if (lex_token (lexer) == T_STOP)
        {
          lex_error_expecting (lexer, "END IF");
          break;
        }
      else if (lex_match_phrase (lexer, "ELSE IF"))
        {
          finish_clause (ds, do_if);
          start_clause (lexer, ds, true, do_if, &allocated_clauses, &ok);
        }
      else if (lex_match_id (lexer, "ELSE"))
        {
          finish_clause (ds, do_if);
          start_clause (lexer, ds, false, do_if, &allocated_clauses, &ok);
        }
      else
        cmd_parse_in_state (lexer, ds,
                            (in_input_program ()
                             ? CMD_STATE_NESTED_INPUT_PROGRAM
                             : CMD_STATE_NESTED_DATA));
    }
  finish_clause (ds, do_if);

  add_transformation (ds, &do_if_trns_class, do_if);

  return ok ? CMD_SUCCESS : CMD_FAILURE;
}

int
cmd_inside_do_if (struct lexer *lexer, struct dataset *ds UNUSED)
{
  lex_ofs_error (lexer, 0, lex_ofs (lexer) - 1,
                 _("This command cannot appear outside DO IF...END IF."));
  return CMD_FAILURE;
}

static const struct trns_chain *
do_if_find_clause (const struct do_if_trns *do_if,
                   struct ccase *c, casenumber case_num)
{
  for (size_t i = 0; i < do_if->n_clauses; i++)
    {
      const struct clause *clause = &do_if->clauses[i];
      if (!clause->condition)
        return &clause->xforms;

      double boolean = expr_evaluate_num (clause->condition, c, case_num);
      if (boolean != 0.0)
        return boolean == SYSMIS ? NULL : &clause->xforms;
    }
  return NULL;
}

static enum trns_result
do_if_trns_proc (void *do_if_, struct ccase **c, casenumber case_num)
{
  struct do_if_trns *do_if = do_if_;

  const struct trns_chain *chain;
  size_t start;
  if (do_if->resume)
    {
      chain = do_if->resume;
      start = do_if->ofs;
      do_if->resume = NULL;
      do_if->ofs = 0;
    }
  else
    {
      chain = do_if_find_clause (do_if, *c, case_num);
      if (!chain)
        return TRNS_CONTINUE;
      start = 0;
    }

  for (size_t i = start; i < chain->n; i++)
    {
      const struct transformation *trns = &chain->xforms[i];
      enum trns_result r = trns->class->execute (trns->aux, c, case_num);
      switch (r)
        {
        case TRNS_CONTINUE:
          break;

        case TRNS_BREAK:
        case TRNS_DROP_CASE:
        case TRNS_ERROR:
        case TRNS_END_FILE:
          return r;

        case TRNS_END_CASE:
          do_if->resume = chain;
          do_if->ofs = i;
          return r;
        }
    }
  return TRNS_CONTINUE;
}

static bool
do_if_trns_free (void *do_if_)
{
  struct do_if_trns *do_if = do_if_;

  for (size_t i = 0; i < do_if->n_clauses; i++)
    {
      struct clause *clause = &do_if->clauses[i];

      msg_location_destroy (clause->location);
      expr_free (clause->condition);

      trns_chain_uninit (&clause->xforms);
    }
  free (do_if->clauses);
  free (do_if);
  return true;
}

static const struct trns_class do_if_trns_class = {
  .name = "DO IF",
  .execute = do_if_trns_proc,
  .destroy = do_if_trns_free,
};
