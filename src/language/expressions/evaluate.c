/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "language/expressions/private.h"
#include "evaluate.h"

#include <ctype.h>

#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "language/expressions/helpers.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/value-parser.h"
#include "libpspp/pool.h"
#include "output/driver.h"

#include "xalloc.h"

static void
expr_evaluate (struct expression *e, const struct ccase *c, int case_idx,
               void *result)
{
  struct dataset *ds = e->ds;
  union operation_data *op = e->ops;

  double *ns = e->number_stack;
  struct substring *ss = e->string_stack;

  /* Without a dictionary/dataset, the expression can't refer to variables,
     and you don't need to specify a case when you evaluate the
     expression.  With a dictionary/dataset, the expression can refer
     to variables, so you must specify a case when you evaluate the
     expression. */
  assert ((c != NULL) == (e->ds != NULL));

  pool_clear (e->eval_pool);

  for (;;)
    {
      assert (op < e->ops + e->n_ops);
      switch (op++->operation)
        {
        case OP_number:
        case OP_boolean:
          *ns++ = op++->number;
          break;

        case OP_string:
          {
            const struct substring *s = &op++->string;
            *ss++ = copy_string (e, s->string, s->length);
          }
          break;

        case OP_return_number:
          *(double *) result = isfinite (ns[-1]) ? ns[-1] : SYSMIS;
          return;

        case OP_return_string:
          *(struct substring *) result = ss[-1];
          return;

#include "evaluate.inc"

        default:
          NOT_REACHED ();
        }
    }
}

double
expr_evaluate_num (struct expression *e, const struct ccase *c, int case_idx)
{
  double d;

  assert (e->type == OP_number || e->type == OP_boolean);
  expr_evaluate (e, c, case_idx, &d);
  return d;
}

void
expr_evaluate_str (struct expression *e, const struct ccase *c, int case_idx,
                   char *dst, size_t dst_size)
{
  struct substring s;

  assert (e->type == OP_string);
  assert ((dst == NULL) == (dst_size == 0));
  expr_evaluate (e, c, case_idx, &s);

  buf_copy_rpad (dst, dst_size, s.string, s.length, ' ');
}

#include "language/lexer/lexer.h"
#include "language/command.h"

static bool default_optimize = true;

int
cmd_debug_evaluate (struct lexer *lexer, struct dataset *dsother UNUSED)
{
  bool optimize = default_optimize;
  int retval = CMD_FAILURE;
  bool dump_postfix = false;
  bool set_defaults = false;

  struct ccase *c = NULL;

  struct dataset *ds = NULL;

  char *name = NULL;
  char *title = NULL;
  struct fmt_spec format;
  bool has_format = false;

  struct expression *expr;

  struct dictionary *d = NULL;

  for (;;)
    {
      if (lex_match_id (lexer, "NOOPTIMIZE"))
        optimize = false;
      else if (lex_match_id (lexer, "OPTIMIZE"))
        optimize = true;
      else if (lex_match_id (lexer, "POSTFIX"))
        dump_postfix = 1;
      else if (lex_match_id (lexer, "SET"))
        set_defaults = true;
      else if (lex_match (lexer, T_LPAREN))
        {
          struct variable *v;

          if (!lex_force_id (lexer))
            goto done;
          int name_ofs = lex_ofs (lexer);
          name = xstrdup (lex_tokcstr (lexer));

          lex_get (lexer);
          if (!lex_force_match (lexer, T_EQUALS))
            goto done;

          union value value;
          int width;
          if (lex_is_number (lexer))
            {
              width = 0;
              value.f = lex_number (lexer);
              lex_get (lexer);
            }
          else if (lex_match_id (lexer, "SYSMIS"))
            {
              width = 0;
              value.f = SYSMIS;
            }
          else if (lex_is_string (lexer))
            {
              width = ss_length (lex_tokss (lexer));
              value.s = CHAR_CAST (uint8_t *, ss_xstrdup (lex_tokss (lexer)));
              lex_get (lexer);
            }
          else
            {
              lex_error (lexer, _("Syntax error expecting number or string."));
              goto done;
            }

          if  (ds == NULL)
            {
              ds = dataset_create (NULL, "");
              d = dataset_dict (ds);
            }

          v = dict_create_var (d, name, width);
          if (v == NULL)
            {
              lex_ofs_error (lexer, name_ofs, name_ofs,
                             _("Duplicate variable name %s."), name);
              value_destroy (&value, width);
              goto done;
            }
          free (name);
          name = NULL;

          if (lex_match_id (lexer, "MISSING"))
            {
              struct missing_values mv;
              mv_init (&mv, width);
              mv_add_value (&mv, &value);
              var_set_missing_values (v, &mv);
              mv_destroy (&mv);
            }

          if (c == NULL)
            c = case_create (dict_get_proto (d));
          else
            c = case_unshare_and_resize (c, dict_get_proto (d));
          value_swap (case_data_rw (c, v), &value);
          value_destroy (&value, width);

          if (!lex_force_match (lexer, T_RPAREN))
            goto done;
        }
      else if (lex_match_id (lexer, "VECTOR"))
        {
          struct variable **vars;
          size_t n;
          dict_get_vars_mutable (d, &vars, &n, 0);
          dict_create_vector_assert (d, "V", vars, n);
          free (vars);
        }
      else if (lex_match_id (lexer, "FORMAT"))
        {
          lex_match (lexer, T_EQUALS);
          if (!parse_format_specifier (lexer, &format))
            goto done;
          char *error = fmt_check_output__ (format);
          if (!error)
            error = fmt_check_type_compat__ (format, NULL, VAL_NUMERIC);
          if (error)
            {
              lex_next_error (lexer, -1, -1, "%s", error);
              free (error);
              goto done;
            }
          has_format = true;
        }
      else
        break;
    }

  if (set_defaults)
    {
      retval = CMD_SUCCESS;
      default_optimize = optimize;
      goto done;
    }

  if (!lex_force_match (lexer, T_SLASH))
    goto done;

  for (size_t i = 1; ; i++)
    if (lex_next_token (lexer, i) == T_ENDCMD)
      {
        title = lex_next_representation (lexer, 0, i - 1);
        break;
      }

  expr = expr_parse_any (lexer, ds, optimize);
  if (!expr || lex_end_of_command (lexer) != CMD_SUCCESS)
    {
      if (expr != NULL)
        expr_free (expr);
      output_log ("%s => error", title);
      goto done;
    }

  if (dump_postfix)
    expr_debug_print_postfix (expr);
  else
    switch (expr->type)
      {
      case OP_number:
      case OP_num_vec_elem:
        {
          double d = expr_evaluate_num (expr, c, 0);
          if (has_format)
            {
              char *output = data_out (&(const union value) { .f = d },
                                       NULL, format,
                                       settings_get_fmt_settings ());
              output_log ("%s => %s", title, output);
              free (output);
            }
          else if (d == SYSMIS)
            output_log ("%s => sysmis", title);
          else
            output_log ("%s => %.2f", title, d);
        }
        break;

      case OP_boolean:
        {
          double b = expr_evaluate_num (expr, c, 0);
          output_log ("%s => %s", title,
                      b == SYSMIS ? "sysmis" : b == 0.0 ? "false" : "true");
        }
        break;

      case OP_string:
        {
          struct substring out;
          expr_evaluate (expr, c, 0, &out);
          output_log ("%s => \"%.*s\"", title, (int) out.length, out.string);
          break;
        }

      default:
        NOT_REACHED ();
      }

  expr_free (expr);
  retval = CMD_SUCCESS;

 done:
  dataset_destroy (ds);

  case_unref (c);

  free (name);
  free (title);

  return retval;
}

void
expr_debug_print_postfix (const struct expression *e)
{
  struct string s = DS_EMPTY_INITIALIZER;

  for (size_t i = 0; i < e->n_ops; i++)
    {
      union operation_data *op = &e->ops[i];
      if (i > 0)
        ds_put_byte (&s, ' ');
      switch (e->op_types[i])
        {
        case OP_operation:
          if (op->operation == OP_return_number)
            ds_put_cstr (&s, "return_number");
          else if (op->operation == OP_return_string)
            ds_put_cstr (&s, "return_string");
          else if (is_function (op->operation))
            ds_put_format (&s, "%s", operations[op->operation].prototype);
          else if (is_composite (op->operation))
            ds_put_format (&s, "%s", operations[op->operation].name);
          else
            ds_put_format (&s, "%s:", operations[op->operation].name);
          break;
        case OP_number:
          if (op->number != SYSMIS)
            ds_put_format (&s, "n<%g>", op->number);
          else
            ds_put_cstr (&s, "n<SYSMIS>");
          break;
        case OP_string:
          ds_put_cstr (&s, "s<");
          ds_put_substring (&s, op->string);
          ds_put_byte (&s, '>');
          break;
        case OP_format:
          {
            char str[FMT_STRING_LEN_MAX + 1];
            fmt_to_string (op->format, str);
            ds_put_format (&s, "f<%s>", str);
          }
          break;
        case OP_variable:
          ds_put_format (&s, "v<%s>", var_get_name (op->variable));
          break;
        case OP_vector:
          ds_put_format (&s, "vec<%s>", vector_get_name (op->vector));
          break;
        case OP_integer:
          ds_put_format (&s, "i<%d>", op->integer);
          break;
        case OP_expr_node:
          ds_put_cstr (&s, "expr_node");
          break;
        default:
          NOT_REACHED ();
        }
    }
  output_log_nocopy (ds_steal_cstr (&s));
}
