/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011, 2015 Free Software Foundation, Inc.

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

#include "data/case.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/value-parser.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Value or range? */
enum value_type
  {
    CNT_SINGLE,                        /* Single value. */
    CNT_RANGE                        /* a <= x <= b. */
  };

/* Numeric count criteria. */
struct num_value
  {
    enum value_type type;       /* How to interpret a, b. */
    double a, b;                /* Values to count. */
  };

struct criteria
  {
    struct criteria *next;

    /* Variables to count. */
    const struct variable **vars;
    size_t n_vars;

    /* Count special values? */
    bool count_system_missing;  /* Count system missing? */
    bool count_user_missing;    /* Count user missing? */

    /* Criterion values. */
    size_t n_values;
    union
      {
        struct num_value *num;
        char **str;
      }
    values;
  };

struct dst_var
  {
    struct dst_var *next;
    struct variable *var;       /* Destination variable. */
    char *name;                 /* Name of dest var. */
    struct criteria *crit;      /* The criteria specifications. */
  };

struct count_trns
  {
    struct dst_var *dst_vars;
    struct pool *pool;
  };

static const struct trns_class count_trns_class;

static bool parse_numeric_criteria (struct lexer *, struct pool *, struct criteria *);
static bool parse_string_criteria (struct lexer *, struct pool *,
                                   struct criteria *,
                                   const char *dict_encoding);
static bool count_trns_free (void *trns_);

int
cmd_count (struct lexer *lexer, struct dataset *ds)
{
  struct dst_var *dv;           /* Destination var being parsed. */
  struct count_trns *trns;      /* Transformation. */

  /* Parses each slash-delimited specification. */
  trns = pool_create_container (struct count_trns, pool);
  trns->dst_vars = dv = pool_alloc (trns->pool, sizeof *dv);
  for (;;)
    {
      struct criteria *crit;

      /* Initialize this struct dst_var to ensure proper cleanup. */
      dv->next = NULL;
      dv->var = NULL;
      dv->crit = NULL;

      /* Get destination variable, or at least its name. */
      if (!lex_force_id (lexer))
        goto fail;
      dv->var = dict_lookup_var (dataset_dict (ds), lex_tokcstr (lexer));
      if (dv->var != NULL)
        {
          if (var_is_alpha (dv->var))
            {
              lex_error (lexer, _("Destination cannot be a string variable."));
              goto fail;
            }
        }
      else
        dv->name = pool_strdup (trns->pool, lex_tokcstr (lexer));

      lex_get (lexer);
      if (!lex_force_match (lexer, T_EQUALS))
        goto fail;

      crit = dv->crit = pool_alloc (trns->pool, sizeof *crit);
      for (;;)
        {
          struct dictionary *dict = dataset_dict (ds);
          bool ok;

          crit->next = NULL;
          crit->vars = NULL;
          if (!parse_variables_const (lexer, dict, &crit->vars,
                                      &crit->n_vars,
                                      PV_DUPLICATE | PV_SAME_TYPE))
            goto fail;
          pool_register (trns->pool, free, crit->vars);

          if (!lex_force_match (lexer, T_LPAREN))
            goto fail;

          crit->n_values = 0;
          if (var_is_numeric (crit->vars[0]))
            ok = parse_numeric_criteria (lexer, trns->pool, crit);
          else
            ok = parse_string_criteria (lexer, trns->pool, crit,
                                        dict_get_encoding (dict));
          if (!ok)
            goto fail;

          if (lex_token (lexer) == T_SLASH || lex_token (lexer) == T_ENDCMD)
            break;

          crit = crit->next = pool_alloc (trns->pool, sizeof *crit);
        }

      if (lex_token (lexer) == T_ENDCMD)
        break;

      if (!lex_force_match (lexer, T_SLASH))
        goto fail;
      dv = dv->next = pool_alloc (trns->pool, sizeof *dv);
    }

  /* Create all the nonexistent destination variables. */
  for (dv = trns->dst_vars; dv; dv = dv->next)
    if (dv->var == NULL)
      {
        /* It's valid, though motivationally questionable, to count to
           the same dest var more than once. */
        dv->var = dict_lookup_var (dataset_dict (ds), dv->name);

        if (dv->var == NULL)
          dv->var = dict_create_var_assert (dataset_dict (ds), dv->name, 0);
      }

  add_transformation (ds, &count_trns_class, trns);
  return CMD_SUCCESS;

fail:
  count_trns_free (trns);
  return CMD_FAILURE;
}

/* Parses a set of numeric criterion values.  Returns success. */
static bool
parse_numeric_criteria (struct lexer *lexer, struct pool *pool, struct criteria *crit)
{
  size_t allocated = 0;

  crit->values.num = NULL;
  crit->count_system_missing = false;
  crit->count_user_missing = false;
  for (;;)
    {
      double low, high;

      if (lex_match_id (lexer, "SYSMIS"))
        crit->count_system_missing = true;
      else if (lex_match_id (lexer, "MISSING"))
        crit->count_system_missing = crit->count_user_missing = true;
      else if (parse_num_range (lexer, &low, &high, NULL))
        {
          struct num_value *cur;

          if (crit->n_values >= allocated)
            crit->values.num = pool_2nrealloc (pool, crit->values.num,
                                               &allocated,
                                               sizeof *crit->values.num);
          cur = &crit->values.num[crit->n_values++];
          cur->type = low == high ? CNT_SINGLE : CNT_RANGE;
          cur->a = low;
          cur->b = high;
        }
      else
        return false;

      lex_match (lexer, T_COMMA);
      if (lex_match (lexer, T_RPAREN))
        break;
    }
  return true;
}

/* Parses a set of string criteria values.  Returns success. */
static bool
parse_string_criteria (struct lexer *lexer, struct pool *pool,
                       struct criteria *crit, const char *dict_encoding)
{
  int len = 0;
  size_t allocated = 0;
  size_t i;

  for (i = 0; i < crit->n_vars; i++)
    if (var_get_width (crit->vars[i]) > len)
      len = var_get_width (crit->vars[i]);

  crit->values.str = NULL;
  for (;;)
    {
      char **cur;
      char *s;

      if (crit->n_values >= allocated)
        crit->values.str = pool_2nrealloc (pool, crit->values.str,
                                           &allocated,
                                           sizeof *crit->values.str);

      if (!lex_force_string (lexer))
        return false;

      s = recode_string (dict_encoding, "UTF-8", lex_tokcstr (lexer),
                         ss_length (lex_tokss (lexer)));

      cur = &crit->values.str[crit->n_values++];
      *cur = pool_alloc (pool, len + 1);
      str_copy_rpad (*cur, len + 1, s);
      lex_get (lexer);

      free (s);

      lex_match (lexer, T_COMMA);
      if (lex_match (lexer, T_RPAREN))
        break;
    }

  return true;
}

/* Transformation. */

/* Counts the number of values in case C matching CRIT. */
static int
count_numeric (struct criteria *crit, const struct ccase *c)
{
  int counter = 0;
  size_t i;

  for (i = 0; i < crit->n_vars; i++)
    {
      double x = case_num (c, crit->vars[i]);
      struct num_value *v;

      for (v = crit->values.num; v < crit->values.num + crit->n_values;
           v++)
        if (v->type == CNT_SINGLE ? x == v->a : x >= v->a && x <= v->b)
          {
            counter++;
            break;
          }

      if (var_is_num_missing (crit->vars[i], x)
          && (x == SYSMIS
              ? crit->count_system_missing
              : crit->count_user_missing))
        {
          counter++;
          continue;
        }

    }

  return counter;
}

/* Counts the number of values in case C matching CRIT. */
static int
count_string (struct criteria *crit, const struct ccase *c)
{
  int counter = 0;
  size_t i;

  for (i = 0; i < crit->n_vars; i++)
    {
      char **v;
      for (v = crit->values.str; v < crit->values.str + crit->n_values; v++)
        if (!memcmp (case_str (c, crit->vars[i]), *v,
                     var_get_width (crit->vars[i])))
          {
            counter++;
            break;
          }
    }

  return counter;
}

/* Performs the COUNT transformation T on case C. */
static enum trns_result
count_trns_proc (void *trns_, struct ccase **c,
                 casenumber case_num UNUSED)
{
  struct count_trns *trns = trns_;
  struct dst_var *dv;

  *c = case_unshare (*c);
  for (dv = trns->dst_vars; dv; dv = dv->next)
    {
      struct criteria *crit;
      int counter;

      counter = 0;
      for (crit = dv->crit; crit; crit = crit->next)
        if (var_is_numeric (crit->vars[0]))
          counter += count_numeric (crit, *c);
        else
          counter += count_string (crit, *c);
      *case_num_rw (*c, dv->var) = counter;
    }
  return TRNS_CONTINUE;
}

/* Destroys all dynamic data structures associated with TRNS. */
static bool
count_trns_free (void *trns_)
{
  struct count_trns *trns = trns_;
  pool_destroy (trns->pool);
  return true;
}

static const struct trns_class count_trns_class = {
  .name = "COUNT",
  .execute = count_trns_proc,
  .destroy = count_trns_free,
};
