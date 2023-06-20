/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011, 2014 Free Software Foundation, Inc.

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

#include <float.h>
#include <stdint.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "data/vector.h"
#include "language/command.h"
#include "language/expressions/public.h"
#include "language/lexer/lexer.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct compute_trns;
struct lvalue;

/* COMPUTE or IF target variable or vector element.
   For a variable, the `variable' member is non-null.
   For a vector element, the `vector' member is non-null. */
struct lvalue
  {
    struct msg_location *location; /* Syntax for variable or vector. */

    struct variable *variable;   /* Destination variable. */
    bool is_new_variable;        /* Did we create the variable? */

    const struct vector *vector; /* Destination vector, if any, or NULL. */
    struct expression *element;  /* Destination vector element, or NULL. */
  };

/* Target of a COMPUTE or IF assignment, either a variable or a
   vector element. */
static struct lvalue *lvalue_parse (struct lexer *lexer, struct dataset *);
static int lvalue_get_type (const struct lvalue *);
static bool lvalue_is_vector (const struct lvalue *);
static void lvalue_finalize (struct lvalue *,
                             struct compute_trns *, struct dictionary *);
static void lvalue_destroy (struct lvalue *, struct dictionary *);

/* COMPUTE and IF transformation. */
struct compute_trns
  {
    /* Test expression (IF only). */
    struct expression *test;         /* Test expression. */

    /* Variable lvalue, if variable != NULL. */
    struct variable *variable;   /* Destination variable, if any. */
    int width;                         /* Lvalue string width; 0=numeric. */

    /* Vector lvalue, if vector != NULL. */
    const struct vector *vector; /* Destination vector, if any. */
    struct expression *element;  /* Destination vector element expr. */

    struct msg_location *lvalue_location;

    /* Rvalue. */
    struct expression *rvalue;         /* Rvalue expression. */
  };

static struct expression *parse_rvalue (struct lexer *lexer,
                                        const struct lvalue *,
                                        struct dataset *);

static struct compute_trns *compute_trns_create (void);
static bool compute_trns_free (void *compute_);
static const struct trns_class *get_trns_class (const struct lvalue *);

/* COMPUTE. */

int
cmd_compute (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct lvalue *lvalue = NULL;
  struct compute_trns *compute = NULL;

  compute = compute_trns_create ();

  lvalue = lvalue_parse (lexer, ds);
  if (lvalue == NULL)
    goto fail;

  if (!lex_force_match (lexer, T_EQUALS))
    goto fail;
  compute->rvalue = parse_rvalue (lexer, lvalue, ds);
  if (compute->rvalue == NULL)
    goto fail;

  add_transformation (ds, get_trns_class (lvalue), compute);

  lvalue_finalize (lvalue, compute, dict);

  return CMD_SUCCESS;

 fail:
  lvalue_destroy (lvalue, dict);
  compute_trns_free (compute);
  return CMD_CASCADING_FAILURE;
}

/* Transformation functions. */

/* Handle COMPUTE or IF with numeric target variable. */
static enum trns_result
compute_num (void *compute_, struct ccase **c, casenumber case_num)
{
  struct compute_trns *compute = compute_;

  if (compute->test == NULL
      || expr_evaluate_num (compute->test, *c, case_num) == 1.0)
    {
      *c = case_unshare (*c);
      *case_num_rw (*c, compute->variable)
        = expr_evaluate_num (compute->rvalue, *c, case_num);
    }

  return TRNS_CONTINUE;
}

/* Handle COMPUTE or IF with numeric vector element target
   variable. */
static enum trns_result
compute_num_vec (void *compute_, struct ccase **c, casenumber case_num)
{
  struct compute_trns *compute = compute_;

  if (compute->test == NULL
      || expr_evaluate_num (compute->test, *c, case_num) == 1.0)
    {
      double index;     /* Index into the vector. */
      int rindx;        /* Rounded index value. */

      index = expr_evaluate_num (compute->element, *c, case_num);
      rindx = floor (index + EPSILON);
      if (index == SYSMIS
          || rindx < 1 || rindx > vector_get_n_vars (compute->vector))
        {
          if (index == SYSMIS)
            msg_at (SW, compute->lvalue_location,
                    _("When executing COMPUTE: SYSMIS is not a valid value "
                      "as an index into vector %s."),
                 vector_get_name (compute->vector));
          else
            msg_at (SW, compute->lvalue_location,
                    _("When executing COMPUTE: %.*g is not a valid value as "
                       "an index into vector %s."),
                 DBL_DIG + 1, index, vector_get_name (compute->vector));
          return TRNS_CONTINUE;
        }

      *c = case_unshare (*c);
      *case_num_rw (*c, vector_get_var (compute->vector, rindx - 1))
        = expr_evaluate_num (compute->rvalue, *c, case_num);
    }

  return TRNS_CONTINUE;
}

/* Handle COMPUTE or IF with string target variable. */
static enum trns_result
compute_str (void *compute_, struct ccase **c, casenumber case_num)
{
  struct compute_trns *compute = compute_;

  if (compute->test == NULL
      || expr_evaluate_num (compute->test, *c, case_num) == 1.0)
    {
      char *s;

      *c = case_unshare (*c);
      s = CHAR_CAST_BUG (char *, case_str_rw (*c, compute->variable));
      expr_evaluate_str (compute->rvalue, *c, case_num, s, compute->width);
    }

  return TRNS_CONTINUE;
}

/* Handle COMPUTE or IF with string vector element target
   variable. */
static enum trns_result
compute_str_vec (void *compute_, struct ccase **c, casenumber case_num)
{
  struct compute_trns *compute = compute_;

  if (compute->test == NULL
      || expr_evaluate_num (compute->test, *c, case_num) == 1.0)
    {
      double index;             /* Index into the vector. */
      int rindx;                /* Rounded index value. */
      struct variable *vr;      /* Variable reference by indexed vector. */

      index = expr_evaluate_num (compute->element, *c, case_num);
      rindx = floor (index + EPSILON);
      if (index == SYSMIS)
        {
          msg_at (SW, compute->lvalue_location,
                  _("When executing COMPUTE: SYSMIS is not a valid "
                    "value as an index into vector %s."),
                  vector_get_name (compute->vector));
          return TRNS_CONTINUE;
        }
      else if (rindx < 1 || rindx > vector_get_n_vars (compute->vector))
        {
          msg_at (SW, compute->lvalue_location,
                  _("When executing COMPUTE: %.*g is not a valid value as "
                    "an index into vector %s."),
                  DBL_DIG + 1, index, vector_get_name (compute->vector));
          return TRNS_CONTINUE;
        }

      vr = vector_get_var (compute->vector, rindx - 1);
      *c = case_unshare (*c);
      expr_evaluate_str (compute->rvalue, *c, case_num,
                         CHAR_CAST_BUG (char *, case_str_rw (*c, vr)),
                         var_get_width (vr));
    }

  return TRNS_CONTINUE;
}

/* IF. */

int
cmd_if (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);
  struct compute_trns *compute = NULL;
  struct lvalue *lvalue = NULL;

  compute = compute_trns_create ();

  /* Test expression. */
  compute->test = expr_parse_bool (lexer, ds);
  if (compute->test == NULL)
    goto fail;

  /* Lvalue variable. */
  lvalue = lvalue_parse (lexer, ds);
  if (lvalue == NULL)
    goto fail;

  /* Rvalue expression. */
  if (!lex_force_match (lexer, T_EQUALS))
    goto fail;
  compute->rvalue = parse_rvalue (lexer, lvalue, ds);
  if (compute->rvalue == NULL)
    goto fail;

  add_transformation (ds, get_trns_class (lvalue), compute);

  lvalue_finalize (lvalue, compute, dict);

  return CMD_SUCCESS;

 fail:
  lvalue_destroy (lvalue, dict);
  compute_trns_free (compute);
  return CMD_CASCADING_FAILURE;
}

/* Code common to COMPUTE and IF. */

static const struct trns_class *
get_trns_class (const struct lvalue *lvalue)
{
  static const struct trns_class classes[2][2] = {
    [false][false] = {
      .name = "COMPUTE",
      .execute = compute_str,
      .destroy = compute_trns_free
    },
    [false][true] = {
      .name = "COMPUTE",
      .execute = compute_str_vec,
      .destroy = compute_trns_free
    },
    [true][false] = {
      .name = "COMPUTE",
      .execute = compute_num,
      .destroy = compute_trns_free
    },
    [true][true] = {
      .name = "COMPUTE",
      .execute = compute_num_vec,
      .destroy = compute_trns_free
    },
  };

  bool is_numeric = lvalue_get_type (lvalue) == VAL_NUMERIC;
  bool is_vector = lvalue_is_vector (lvalue);
  return &classes[is_numeric][is_vector];
}

/* Parses and returns an rvalue expression of the same type as
   LVALUE, or a null pointer on failure. */
static struct expression *
parse_rvalue (struct lexer *lexer,
              const struct lvalue *lvalue, struct dataset *ds)
{
  if (lvalue->is_new_variable)
    return expr_parse_new_variable (lexer, ds, var_get_name (lvalue->variable),
                                    lvalue->location);
  else
    return expr_parse (lexer, ds, lvalue_get_type (lvalue));
}

/* Returns a new struct compute_trns after initializing its fields. */
static struct compute_trns *
compute_trns_create (void)
{
  struct compute_trns *compute = xmalloc (sizeof *compute);
  *compute = (struct compute_trns) { .test = NULL };
  return compute;
}

/* Deletes all the fields in COMPUTE. */
static bool
compute_trns_free (void *compute_)
{
  struct compute_trns *compute = compute_;

  if (compute != NULL)
    {
      msg_location_destroy (compute->lvalue_location);
      expr_free (compute->test);
      expr_free (compute->element);
      expr_free (compute->rvalue);
      free (compute);
    }
  return true;
}

/* Parses the target variable or vector element into a new
   `struct lvalue', which is returned. */
static struct lvalue *
lvalue_parse (struct lexer *lexer, struct dataset *ds)
{
  struct dictionary *dict = dataset_dict (ds);

  struct lvalue *lvalue = xmalloc (sizeof *lvalue);
  *lvalue = (struct lvalue) { .variable = NULL };

  if (!lex_force_id (lexer))
    goto lossage;

  int start_ofs = lex_ofs (lexer);
  if (lex_next_token (lexer, 1) == T_LPAREN)
    {
      /* Vector. */
      lvalue->vector = dict_lookup_vector (dict, lex_tokcstr (lexer));
      if (lvalue->vector == NULL)
        {
          lex_error (lexer, _("There is no vector named %s."),
                     lex_tokcstr (lexer));
          goto lossage;
        }

      /* Vector element. */
      lex_get (lexer);
      if (!lex_force_match (lexer, T_LPAREN))
        goto lossage;
      lvalue->element = expr_parse (lexer, ds, VAL_NUMERIC);
      if (lvalue->element == NULL)
        goto lossage;
      if (!lex_force_match (lexer, T_RPAREN))
        goto lossage;
    }
  else
    {
      /* Variable name. */
      const char *var_name = lex_tokcstr (lexer);
      lvalue->variable = dict_lookup_var (dict, var_name);
      if (lvalue->variable == NULL)
        {
          lvalue->variable = dict_create_var_assert (dict, var_name, 0);
          lvalue->is_new_variable = true;
        }
      lex_get (lexer);
    }
  int end_ofs = lex_ofs (lexer) - 1;
  lvalue->location = lex_ofs_location (lexer, start_ofs, end_ofs);
  return lvalue;

 lossage:
  lvalue_destroy (lvalue, dict);
  return NULL;
}

/* Returns the type (NUMERIC or ALPHA) of the target variable or
   vector in LVALUE. */
static int
lvalue_get_type (const struct lvalue *lvalue)
{
  return (lvalue->variable != NULL
          ? var_get_type (lvalue->variable)
          : vector_get_type (lvalue->vector));
}

/* Returns true if LVALUE has a vector as its target. */
static bool
lvalue_is_vector (const struct lvalue *lvalue)
{
  return lvalue->vector != NULL;
}

/* Finalizes making LVALUE the target of COMPUTE, by creating the
   target variable if necessary and setting fields in COMPUTE. */
static void
lvalue_finalize (struct lvalue *lvalue,
                 struct compute_trns *compute,
                 struct dictionary *dict)
{
  compute->lvalue_location = lvalue->location;
  lvalue->location = NULL;

  if (lvalue->vector == NULL)
    {
      compute->variable = lvalue->variable;
      compute->width = var_get_width (compute->variable);

      /* Goofy behavior, but compatible: Turn off LEAVE. */
      if (!var_must_leave (compute->variable))
        var_set_leave (compute->variable, false);

      /* Prevent lvalue_destroy from deleting variable. */
      lvalue->is_new_variable = false;
    }
  else
    {
      compute->vector = lvalue->vector;
      compute->element = lvalue->element;
      lvalue->element = NULL;
    }

  lvalue_destroy (lvalue, dict);
}

/* Destroys LVALUE. */
static void
lvalue_destroy (struct lvalue *lvalue, struct dictionary *dict)
{
  if (lvalue == NULL)
     return;

  if (lvalue->is_new_variable)
    dict_delete_var (dict, lvalue->variable);
  expr_free (lvalue->element);
  msg_location_destroy (lvalue->location);
  free (lvalue);
}
