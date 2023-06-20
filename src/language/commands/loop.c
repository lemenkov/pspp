/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009-2011 Free Software Foundation, Inc.

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

#include <limits.h>

#include "data/case.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/settings.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/commands/inpt-pgm.h"
#include "language/expressions/public.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct loop_trns
  {
    /* a=a TO b [BY c]. */
    struct variable *index_var;    /* Index variable. */
    struct expression *first_expr; /* Starting index. */
    struct expression *by_expr;    /* Index increment (or NULL). */
    struct expression *last_expr;  /* Terminal index. */

    /* IF condition for LOOP or END LOOP. */
    struct expression *loop_condition;
    struct expression *end_loop_condition;

    /* Inner transformations. */
    struct trns_chain xforms;

    /* State. */
    double cur, by, last;       /* Index data. */
    int iteration;              /* For MXLOOPS. */
    size_t resume_idx;          /* For resuming after END CASE. */
  };

static struct trns_class loop_trns_class;

static int in_loop;

static bool parse_if_clause (struct lexer *, struct dataset *,
                             struct expression **);
static bool parse_index_clause (struct dataset *, struct lexer *,
                                struct loop_trns *);

/* LOOP. */

/* Parses LOOP. */
int
cmd_loop (struct lexer *lexer, struct dataset *ds)
{
  struct loop_trns *loop = xmalloc (sizeof *loop);
  *loop = (struct loop_trns) { .resume_idx = SIZE_MAX };

  bool ok = true;
  while (lex_token (lexer) != T_ENDCMD && ok)
    {
      if (lex_match_id (lexer, "IF"))
        ok = parse_if_clause (lexer, ds, &loop->loop_condition);
      else
        ok = parse_index_clause (ds, lexer, loop);
    }
  if (ok)
    lex_end_of_command (lexer);
  lex_discard_rest_of_command (lexer);

  proc_push_transformations (ds);
  in_loop++;
  for (;;)
    {
      if (lex_token (lexer) == T_STOP)
        {
          lex_error_expecting (lexer, "END LOOP");
          ok = false;
          break;
        }
      else if (lex_match_phrase (lexer, "END LOOP"))
        {
          if (lex_match_id (lexer, "IF"))
            ok = parse_if_clause (lexer, ds, &loop->end_loop_condition) && ok;
          break;
        }
      else
        cmd_parse_in_state (lexer, ds,
                            (in_input_program ()
                             ? CMD_STATE_NESTED_INPUT_PROGRAM
                             : CMD_STATE_NESTED_DATA));
    }
  in_loop--;
  proc_pop_transformations (ds, &loop->xforms);

  add_transformation (ds, &loop_trns_class, loop);

  return ok ? CMD_SUCCESS : CMD_FAILURE;
}

int
cmd_inside_loop (struct lexer *lexer, struct dataset *ds UNUSED)
{
  lex_ofs_error (lexer, 0, lex_ofs (lexer) - 1,
                 _("This command cannot appear outside LOOP...END LOOP."));
  return CMD_FAILURE;
}

static enum trns_result
break_trns_proc (void *aux UNUSED, struct ccase **c UNUSED,
                 casenumber case_num UNUSED)
{
  return TRNS_BREAK;
}

/* Parses BREAK. */
int
cmd_break (struct lexer *lexer, struct dataset *ds)
{
  if (!in_loop)
    {
      cmd_inside_loop (lexer, ds);
      return CMD_FAILURE;
    }

  static const struct trns_class trns_class = {
    .name = "BREAK",
    .execute = break_trns_proc
  };
  add_transformation (ds, &trns_class, NULL);

  return CMD_SUCCESS;
}

/* Parses an IF clause for LOOP or END LOOP and stores the
   resulting expression to *CONDITION.
   Returns true if successful, false on failure. */
static bool
parse_if_clause (struct lexer *lexer, struct dataset *ds,
                 struct expression **condition)
{
  if (*condition != NULL)
    {
      lex_sbc_only_once (lexer, "IF");
      return false;
    }

  *condition = expr_parse_bool (lexer, ds);
  return *condition != NULL;
}

/* Parses an indexing clause into LOOP.  Returns true if successful, false on
   failure. */
static bool
parse_index_clause (struct dataset *ds, struct lexer *lexer,
                    struct loop_trns *loop)
{
  if (loop->index_var != NULL)
    {
      lex_error (lexer, _("Only one index clause may be specified."));
      return false;
    }

  if (!lex_force_id (lexer))
    return false;

  loop->index_var = dict_lookup_var (dataset_dict (ds), lex_tokcstr (lexer));
  if (!loop->index_var)
    loop->index_var = dict_create_var_assert (dataset_dict (ds),
                                              lex_tokcstr (lexer), 0);
  lex_get (lexer);

  if (!lex_force_match (lexer, T_EQUALS))
    return false;

  loop->first_expr = expr_parse (lexer, ds, VAL_NUMERIC);
  if (loop->first_expr == NULL)
    return false;

  for (;;)
    {
      struct expression **e;
      if (lex_match (lexer, T_TO))
        e = &loop->last_expr;
      else if (lex_match (lexer, T_BY))
        e = &loop->by_expr;
      else
        break;

      if (*e != NULL)
        {
          lex_sbc_only_once (lexer, e == &loop->last_expr ? "TO" : "BY");
          return false;
        }
      *e = expr_parse (lexer, ds, VAL_NUMERIC);
      if (*e == NULL)
        return false;
    }
  if (loop->last_expr == NULL)
    {
      lex_sbc_missing (lexer, "TO");
      return false;
    }

  return true;
}

/* Sets up LOOP for the first pass. */
static enum trns_result
loop_trns_proc (void *loop_, struct ccase **c, casenumber case_num)
{
  struct loop_trns *loop = loop_;

  size_t start_idx = loop->resume_idx;
  loop->resume_idx = SIZE_MAX;
  if (start_idx != SIZE_MAX)
    goto resume;

  if (loop->index_var)
    {
      /* Evaluate loop index expressions. */
      loop->cur = expr_evaluate_num (loop->first_expr, *c, case_num);
      loop->by = (loop->by_expr
                  ? expr_evaluate_num (loop->by_expr, *c, case_num)
                  : 1.0);
      loop->last = expr_evaluate_num (loop->last_expr, *c, case_num);

      /* Even if the loop is never entered, set the index
         variable to the initial value. */
      *c = case_unshare (*c);
      *case_num_rw (*c, loop->index_var) = loop->cur;

      /* Throw out pathological cases. */
      if (!isfinite (loop->cur)
          || !isfinite (loop->by)
          || !isfinite (loop->last)
          || loop->by == 0.0
          || (loop->by > 0.0 && loop->cur > loop->last)
          || (loop->by < 0.0 && loop->cur < loop->last))
        return TRNS_CONTINUE;
    }

  for (loop->iteration = 0;
       loop->index_var || loop->iteration < settings_get_mxloops ();
       loop->iteration++)
    {
      if (loop->loop_condition
          && expr_evaluate_num (loop->loop_condition, *c, case_num) != 1.0)
        break;

      start_idx = 0;
    resume:
      for (size_t i = start_idx; i < loop->xforms.n; i++)
        {
          const struct transformation *trns = &loop->xforms.xforms[i];
          enum trns_result r = trns->class->execute (trns->aux, c, case_num);
          switch (r)
            {
            case TRNS_CONTINUE:
              break;

            case TRNS_BREAK:
              return TRNS_CONTINUE;

            case TRNS_END_CASE:
              loop->resume_idx = i;
              return TRNS_END_CASE;

            case TRNS_ERROR:
            case TRNS_END_FILE:
              return r;

            case TRNS_DROP_CASE:
              NOT_REACHED ();
            }
        }

      if (loop->end_loop_condition != NULL
          && expr_evaluate_num (loop->end_loop_condition, *c, case_num) != 0.0)
        break;

      if (loop->index_var)
        {
          loop->cur += loop->by;
          if (loop->by > 0.0 ? loop->cur > loop->last : loop->cur < loop->last)
            break;

          *c = case_unshare (*c);
          *case_num_rw (*c, loop->index_var) = loop->cur;
        }
    }
  return TRNS_CONTINUE;
}

/* Frees LOOP. */
static bool
loop_trns_free (void *loop_)
{
  struct loop_trns *loop = loop_;

  expr_free (loop->first_expr);
  expr_free (loop->by_expr);
  expr_free (loop->last_expr);

  expr_free (loop->loop_condition);
  expr_free (loop->end_loop_condition);

  trns_chain_uninit (&loop->xforms);

  free (loop);
  return true;
}

static struct trns_class loop_trns_class = {
  .name = "LOOP",
  .execute = loop_trns_proc,
  .destroy = loop_trns_free,
};
