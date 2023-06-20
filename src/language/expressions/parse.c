/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011, 2012, 2014 Free Software Foundation, Inc.

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

#include "private.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/dictionary.h"
#include "data/settings.h"
#include "data/variable.h"
#include "language/expressions/helpers.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/c-strcase.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"

/* Declarations. */

/* Recursive descent parser in order of increasing precedence. */
typedef struct expr_node *parse_recursively_func (struct lexer *, struct expression *);
static parse_recursively_func parse_or, parse_and, parse_not;
static parse_recursively_func parse_rel, parse_add, parse_mul;
static parse_recursively_func parse_neg, parse_exp;
static parse_recursively_func parse_primary;
static parse_recursively_func parse_vector_element, parse_function;

/* Utility functions. */
static struct expression *expr_create (struct dataset *ds);
atom_type expr_node_returns (const struct expr_node *);

static const char *atom_type_name (atom_type);
static struct expression *finish_expression (struct expr_node *,
                                             struct expression *);
static bool type_check (const struct expression *, const struct expr_node *,
                        enum val_type expected_type);
static struct expr_node *allocate_unary_variable (struct expression *,
                                                const struct variable *);

/* Public functions. */

static struct expr_node *
parse_expr (struct lexer *lexer, struct expression *e)
{
  struct expr_node *n = parse_or (lexer, e);
  if (n && n->type == OP_VEC_ELEM_NUM_RAW)
    n->type = OP_VEC_ELEM_NUM;
  return n;
}

/* Parses an expression of the given TYPE.  If DS is nonnull then variables and
   vectors within it may be referenced within the expression; otherwise, the
   expression must not reference any variables or vectors.  Returns the new
   expression if successful or a null pointer otherwise. */
struct expression *
expr_parse (struct lexer *lexer, struct dataset *ds, enum val_type type)
{
  assert (val_type_is_valid (type));

  struct expression *e = expr_create (ds);
  struct expr_node *n = parse_expr (lexer, e);
  if (!n || !type_check (e, n, type))
    {
      expr_free (e);
      return NULL;
    }

  return finish_expression (expr_optimize (n, e), e);
}

/* Parses a boolean expression, otherwise similar to expr_parse(). */
struct expression *
expr_parse_bool (struct lexer *lexer, struct dataset *ds)
{
  struct expression *e = expr_create (ds);
  struct expr_node *n = parse_expr (lexer, e);
  if (!n)
    {
      expr_free (e);
      return NULL;
    }

  atom_type actual_type = expr_node_returns (n);
  if (actual_type == OP_number)
    n = expr_allocate_unary (e, OP_EXPR_TO_BOOLEAN, n);
  else if (actual_type != OP_boolean)
    {
      msg_at (SE, expr_location (e, n),
              _("Type mismatch: expression has %s type, "
                 "but a boolean value is required here."),
           atom_type_name (actual_type));
      expr_free (e);
      return NULL;
    }

  return finish_expression (expr_optimize (n, e), e);
}

/* Parses a numeric expression that is intended to be assigned to newly created
   variable NEW_VAR_NAME at NEW_VAR_LOCATION.  (This allows for a better error
   message if the expression is not numeric.)  Otherwise similar to
   expr_parse(). */
struct expression *
expr_parse_new_variable (struct lexer *lexer, struct dataset *ds,
                         const char *new_var_name,
                         const struct msg_location *new_var_location)
{
  struct expression *e = expr_create (ds);
  struct expr_node *n = parse_expr (lexer, e);
  if (!n)
    {
      expr_free (e);
      return NULL;
    }

  atom_type actual_type = expr_node_returns (n);
  if (actual_type != OP_number && actual_type != OP_boolean)
    {
      msg_at (SE, new_var_location,
              _("This command tries to create a new variable %s by assigning a "
                "string value to it, but this is not supported.  Use "
                "the STRING command to create the new variable with the "
                "correct width before assigning to it, e.g. STRING %s(A20)."),
           new_var_name, new_var_name);
      expr_free (e);
      return NULL;
    }

  return finish_expression (expr_optimize (n, e), e);
}

/* Free expression E. */
void
expr_free (struct expression *e)
{
  if (e != NULL)
    pool_destroy (e->expr_pool);
}

struct expression *
expr_parse_any (struct lexer *lexer, struct dataset *ds, bool optimize)
{
  struct expr_node *n;
  struct expression *e;

  e = expr_create (ds);
  n = parse_expr (lexer, e);
  if (n == NULL)
    {
      expr_free (e);
      return NULL;
    }

  if (optimize)
    n = expr_optimize (n, e);
  return finish_expression (n, e);
}

/* Finishing up expression building. */

/* Height of an expression's stacks. */
struct stack_heights
  {
    int number_height;  /* Height of number stack. */
    int string_height;  /* Height of string stack. */
  };

/* Stack heights used by different kinds of arguments. */
static const struct stack_heights on_number_stack = {1, 0};
static const struct stack_heights on_string_stack = {0, 1};
static const struct stack_heights not_on_stack = {0, 0};

/* Returns the stack heights used by an atom of the given
   TYPE. */
static const struct stack_heights *
atom_type_stack (atom_type type)
{
  assert (is_atom (type));

  switch (type)
    {
    case OP_number:
    case OP_boolean:
    case OP_num_vec_elem:
      return &on_number_stack;

    case OP_string:
      return &on_string_stack;

    case OP_format:
    case OP_ni_format:
    case OP_no_format:
    case OP_num_var:
    case OP_str_var:
    case OP_integer:
    case OP_pos_int:
    case OP_vector:
    case OP_expr_node:
      return &not_on_stack;

    default:
      NOT_REACHED ();
    }
}

/* Measures the stack height needed for node N, supposing that
   the stack height is initially *HEIGHT and updating *HEIGHT to
   the final stack height.  Updates *MAX, if necessary, to
   reflect the maximum intermediate or final height. */
static void
measure_stack (const struct expr_node *n,
               struct stack_heights *height, struct stack_heights *max)
{
  const struct stack_heights *return_height;

  if (is_composite (n->type))
    {
      struct stack_heights args;
      int i;

      args = *height;
      for (i = 0; i < n->n_args; i++)
        measure_stack (n->args[i], &args, max);

      return_height = atom_type_stack (operations[n->type].returns);
    }
  else
    return_height = atom_type_stack (n->type);

  height->number_height += return_height->number_height;
  height->string_height += return_height->string_height;

  if (height->number_height > max->number_height)
    max->number_height = height->number_height;
  if (height->string_height > max->string_height)
    max->string_height = height->string_height;
}

/* Allocates stacks within E sufficient for evaluating node N. */
static void
allocate_stacks (struct expr_node *n, struct expression *e)
{
  struct stack_heights initial = {0, 0};
  struct stack_heights max = {0, 0};

  measure_stack (n, &initial, &max);
  e->number_stack = pool_alloc (e->expr_pool,
                                sizeof *e->number_stack * max.number_height);
  e->string_stack = pool_alloc (e->expr_pool,
                                sizeof *e->string_stack * max.string_height);
}

/* Finalizes expression E for evaluating node N. */
static struct expression *
finish_expression (struct expr_node *n, struct expression *e)
{
  /* Allocate stacks. */
  allocate_stacks (n, e);

  /* Output postfix representation. */
  expr_flatten (n, e);

  /* The eval_pool might have been used for allocating strings
     during optimization.  We need to keep those strings around
     for all subsequent evaluations, so start a new eval_pool. */
  e->eval_pool = pool_create_subpool (e->expr_pool);

  return e;
}

/* Verifies that expression E, whose root node is *N, can be
   converted to type EXPECTED_TYPE, inserting a conversion at *N
   if necessary.  Returns true if successful, false on failure. */
static bool
type_check (const struct expression *e, const struct expr_node *n,
            enum val_type expected_type)
{
  atom_type actual_type = expr_node_returns (n);

  switch (expected_type)
    {
    case VAL_NUMERIC:
      if (actual_type != OP_number && actual_type != OP_boolean)
        {
          msg_at (SE, expr_location (e, n),
                  _("Type mismatch: expression has type '%s', "
                     "but a numeric value is required."),
               atom_type_name (actual_type));
          return false;
        }
      break;

    case VAL_STRING:
      if (actual_type != OP_string)
        {
          msg_at (SE, expr_location (e, n),
                  _("Type mismatch: expression has type '%s', "
                     "but a string value is required."),
               atom_type_name (actual_type));
          return false;
        }
      break;

    default:
      NOT_REACHED ();
    }

  return true;
}

/* Recursive-descent expression parser. */

static void
free_msg_location (void *loc_)
{
  struct msg_location *loc = loc_;
  msg_location_destroy (loc);
}

static void
expr_location__ (struct expression *e,
                 const struct expr_node *node,
                 const struct msg_location **minp,
                 const struct msg_location **maxp)
{
  struct msg_location *loc = node->location;
  if (loc)
    {
      const struct msg_location *min = *minp;
      if (loc->start.line
          && (!min
              || loc->start.line < min->start.line
              || (loc->start.line == min->start.line
                  && loc->start.column < min->start.column)))
        *minp = loc;

      const struct msg_location *max = *maxp;
      if (loc->end.line
          && (!max
              || loc->end.line > max->end.line
              || (loc->end.line == max->end.line
                  && loc->end.column > max->end.column)))
        *maxp = loc;

      return;
    }

  if (is_composite (node->type))
    for (size_t i = 0; i < node->n_args; i++)
      expr_location__ (e, node->args[i], minp, maxp);
}

/* Returns the source code location corresponding to expression NODE, computing
   it lazily if needed. */
const struct msg_location *
expr_location (const struct expression *e_, const struct expr_node *node_)
{
  struct expr_node *node = CONST_CAST (struct expr_node *, node_);
  if (!node)
    return NULL;

  if (!node->location)
    {
      struct expression *e = CONST_CAST (struct expression *, e_);
      const struct msg_location *min = NULL;
      const struct msg_location *max = NULL;
      expr_location__ (e, node, &min, &max);
      if (min && max)
        {
          node->location = msg_location_dup (min);
          node->location->end = max->end;
          pool_register (e->expr_pool, free_msg_location, node->location);
        }
    }
  return node->location;
}

/* Sets e->location to the tokens in S's lexer from offset START_OFS to the
   token before the current one.  Has no effect if E already has a location or
   if E is null. */
static void
expr_add_location (struct lexer *lexer, struct expression *e,
                   int start_ofs, struct expr_node *node)
{
  if (node && !node->location)
    {
      node->location = lex_ofs_location (lexer, start_ofs, lex_ofs (lexer) - 1);
      pool_register (e->expr_pool, free_msg_location, node->location);
    }
}

static bool
type_coercion__ (struct expression *e, struct expr_node *node, size_t arg_idx,
                 bool do_coercion)
{
  assert (!!do_coercion == (e != NULL));

  if (!node)
    return false;

  struct expr_node **argp = &node->args[arg_idx];
  struct expr_node *arg = *argp;
  if (!arg)
    return false;

  const struct operation *op = &operations[node->type];
  atom_type required_type = op->args[MIN (arg_idx, op->n_args - 1)];
  atom_type actual_type = expr_node_returns (arg);
  if (actual_type == required_type)
    {
      /* Type match. */
      return true;
    }

  switch (required_type)
    {
    case OP_number:
      if (actual_type == OP_boolean)
        {
          /* To enforce strict typing rules, insert Boolean to
             numeric "conversion".  This conversion is a no-op,
             so it will be removed later. */
          if (do_coercion)
            *argp = expr_allocate_unary (e, OP_BOOLEAN_TO_NUM, arg);
          return true;
        }
      else if (actual_type == OP_num_vec_elem)
        {
          if (do_coercion)
            arg->type = OP_VEC_ELEM_NUM;
          return true;
        }
      break;

    case OP_string:
      /* No coercion to string. */
      break;

    case OP_boolean:
      if (actual_type == OP_number)
        {
          /* Convert numeric to boolean. */
          if (do_coercion)
            *argp = expr_allocate_binary (e, OP_OPERAND_TO_BOOLEAN, arg,
                                          expr_allocate_expr_node (e, node));
          return true;
        }
      break;

    case OP_integer:
      if (actual_type == OP_number)
        {
          /* Convert number to integer. */
          if (do_coercion)
            *argp = expr_allocate_unary (e, OP_NUM_TO_INTEGER, arg);
          return true;
        }
      break;

    case OP_format:
      /* We never coerce to OP_format, only to OP_ni_format or OP_no_format. */
      NOT_REACHED ();

    case OP_ni_format:
      if (arg->type == OP_format
          && fmt_check_input (arg->format)
          && fmt_check_type_compat (arg->format, VAL_NUMERIC))
        {
          if (do_coercion)
            arg->type = OP_ni_format;
          return true;
        }
      break;

    case OP_no_format:
      if (arg->type == OP_format
          && fmt_check_output (arg->format)
          && fmt_check_type_compat (arg->format, VAL_NUMERIC))
        {
          if (do_coercion)
            arg->type = OP_no_format;
          return true;
        }
      break;

    case OP_num_var:
      if (arg->type == OP_NUM_VAR)
        {
          if (do_coercion)
            *argp = arg->args[0];
          return true;
        }
      break;

    case OP_str_var:
      if (arg->type == OP_STR_VAR)
        {
          if (do_coercion)
            *argp = arg->args[0];
          return true;
        }
      break;

    case OP_var:
      if (arg->type == OP_NUM_VAR || arg->type == OP_STR_VAR)
        {
          if (do_coercion)
            *argp = arg->args[0];
          return true;
        }
      break;

    case OP_pos_int:
      if (arg->type == OP_number
          && floor (arg->number) == arg->number
          && arg->number > 0 && arg->number < INT_MAX)
        {
          if (do_coercion)
            *argp = expr_allocate_pos_int (e, arg->number);
          return true;
        }
      break;

    default:
      NOT_REACHED ();
    }
  return false;
}

static bool
type_coercion (struct expression *e, struct expr_node *node, size_t arg_idx)
{
  return type_coercion__ (e, node, arg_idx, true);
}

static bool
is_coercible (const struct expr_node *node_, size_t arg_idx)
{
  struct expr_node *node = CONST_CAST (struct expr_node *, node_);
  return type_coercion__ (NULL, node, arg_idx, false);
}

/* How to parse an operator.

   Some operators support both numeric and string operators.  For those,
   'num_op' and 'str_op' are both nonzero.  Otherwise, only one 'num_op' is
   nonzero.  (PSPP doesn't have any string-only operators.) */
struct operator
  {
    enum token_type token;      /* Operator token. */
    operation_type num_op;      /* Operation for numeric operands (or 0). */
    operation_type str_op;      /* Operation for string operands (or 0). */
  };

static operation_type
match_operator (struct lexer *lexer, const struct operator ops[], size_t n_ops,
                const struct expr_node *lhs)
{
  bool lhs_is_numeric = operations[lhs->type].returns != OP_string;
  for (const struct operator *op = ops; op < ops + n_ops; op++)
    if (lex_token (lexer) == op->token)
      {
        if (op->token != T_NEG_NUM)
          lex_get (lexer);

        return op->str_op && !lhs_is_numeric ? op->str_op : op->num_op;
      }
  return 0;
}

static const char *
operator_name (enum token_type token)
{
  return token == T_NEG_NUM ? "-" : token_type_to_string (token);
}

static struct expr_node *
parse_binary_operators__ (struct lexer *lexer, struct expression *e,
                          const struct operator ops[], size_t n_ops,
                          parse_recursively_func *parse_next_level,
                          const char *chain_warning, struct expr_node *lhs)
{
  for (int op_count = 0; ; op_count++)
    {
      enum token_type token = lex_token (lexer);
      operation_type optype = match_operator (lexer, ops, n_ops, lhs);
      if (!optype)
        {
          if (op_count > 1 && chain_warning)
            msg_at (SW, expr_location (e, lhs), "%s", chain_warning);

          return lhs;
        }

      struct expr_node *rhs = parse_next_level (lexer, e);
      if (!rhs)
        return NULL;

      struct expr_node *node = expr_allocate_binary (e, optype, lhs, rhs);
      if (!is_coercible (node, 0) || !is_coercible (node, 1))
        {
          bool both = false;
          for (size_t i = 0; i < n_ops; i++)
            if (ops[i].token == token)
              both = ops[i].num_op && ops[i].str_op;

          const char *name = operator_name (token);
          if (both)
            msg_at (SE, expr_location (e, node),
                    _("Both operands of %s must have the same type."), name);
          else if (operations[node->type].args[0] != OP_string)
            msg_at (SE, expr_location (e, node),
                    _("Both operands of %s must be numeric."), name);
          else
            NOT_REACHED ();

          msg_at (SN, expr_location (e, node->args[0]),
                  _("This operand has type '%s'."),
                  atom_type_name (expr_node_returns (node->args[0])));
          msg_at (SN, expr_location (e, node->args[1]),
                  _("This operand has type '%s'."),
                  atom_type_name (expr_node_returns (node->args[1])));

          return NULL;
        }

      if (!type_coercion (e, node, 0) || !type_coercion (e, node, 1))
        NOT_REACHED ();

      lhs = node;
    }
}

static struct expr_node *
parse_binary_operators (struct lexer *lexer, struct expression *e,
                        const struct operator ops[], size_t n_ops,
                        parse_recursively_func *parse_next_level,
                        const char *chain_warning)
{
  struct expr_node *lhs = parse_next_level (lexer, e);
  if (!lhs)
    return NULL;

  return parse_binary_operators__ (lexer, e, ops, n_ops, parse_next_level,
                                   chain_warning, lhs);
}

static struct expr_node *
parse_inverting_unary_operator (struct lexer *lexer, struct expression *e,
                                const struct operator *op,
                                parse_recursively_func *parse_next_level)
{
  int start_ofs = lex_ofs (lexer);
  unsigned int op_count = 0;
  while (lex_match (lexer, op->token))
    op_count++;

  struct expr_node *inner = parse_next_level (lexer, e);
  if (!inner || !op_count)
    return inner;

  struct expr_node *outer = expr_allocate_unary (e, op->num_op, inner);
  expr_add_location (lexer, e, start_ofs, outer);

  if (!type_coercion (e, outer, 0))
    {
      assert (operations[outer->type].args[0] != OP_string);

      const char *name = operator_name (op->token);
      msg_at (SE, expr_location (e, outer),
              _("The unary %s operator requires a numeric operand."), name);

      msg_at (SN, expr_location (e, outer->args[0]),
              _("The operand of %s has type '%s'."),
              name, atom_type_name (expr_node_returns (outer->args[0])));

      return NULL;
    }

  return op_count % 2 ? outer : outer->args[0];
}

/* Parses the OR level. */
static struct expr_node *
parse_or (struct lexer *lexer, struct expression *e)
{
  static const struct operator op = { .token = T_OR, .num_op = OP_OR };
  return parse_binary_operators (lexer, e, &op, 1, parse_and, NULL);
}

/* Parses the AND level. */
static struct expr_node *
parse_and (struct lexer *lexer, struct expression *e)
{
  static const struct operator op = { .token = T_AND, .num_op = OP_AND };

  return parse_binary_operators (lexer, e, &op, 1, parse_not, NULL);
}

/* Parses the NOT level. */
static struct expr_node *
parse_not (struct lexer *lexer, struct expression *e)
{
  static const struct operator op = { .token = T_NOT, .num_op = OP_NOT };
  return parse_inverting_unary_operator (lexer, e, &op, parse_rel);
}

/* Parse relational operators. */
static struct expr_node *
parse_rel (struct lexer *lexer, struct expression *e)
{
  const char *chain_warning =
    _("Chaining relational operators (e.g. `a < b < c') will "
      "not produce the mathematically expected result.  "
      "Use the AND logical operator to fix the problem "
      "(e.g. `a < b AND b < c').  "
      "To disable this warning, insert parentheses.");

  static const struct operator ops[] =
    {
      { .token = T_EQUALS, .num_op = OP_EQ, .str_op = OP_EQ_STRING },
      { .token = T_EQ, .num_op = OP_EQ, .str_op = OP_EQ_STRING },
      { .token = T_GE, .num_op = OP_GE, .str_op = OP_GE_STRING },
      { .token = T_GT, .num_op = OP_GT, .str_op = OP_GT_STRING },
      { .token = T_LE, .num_op = OP_LE, .str_op = OP_LE_STRING },
      { .token = T_LT, .num_op = OP_LT, .str_op = OP_LT_STRING },
      { .token = T_NE, .num_op = OP_NE, .str_op = OP_NE_STRING },
    };

  return parse_binary_operators (lexer, e, ops, sizeof ops / sizeof *ops,
                                 parse_add, chain_warning);
}

/* Parses the addition and subtraction level. */
static struct expr_node *
parse_add (struct lexer *lexer, struct expression *e)
{
  static const struct operator ops[] =
    {
      { .token = T_PLUS, .num_op = OP_ADD },
      { .token = T_DASH, .num_op = OP_SUB },
      { .token = T_NEG_NUM, .num_op = OP_ADD },
    };

  return parse_binary_operators (lexer, e, ops, sizeof ops / sizeof *ops,
                                 parse_mul, NULL);
}

/* Parses the multiplication and division level. */
static struct expr_node *
parse_mul (struct lexer *lexer, struct expression *e)
{
  static const struct operator ops[] =
    {
      { .token = T_ASTERISK, .num_op = OP_MUL },
      { .token = T_SLASH, .num_op = OP_DIV },
    };

  return parse_binary_operators (lexer, e, ops, sizeof ops / sizeof *ops,
                                 parse_neg, NULL);
}

/* Parses the unary minus level. */
static struct expr_node *
parse_neg (struct lexer *lexer, struct expression *e)
{
  static const struct operator op = { .token = T_DASH, .num_op = OP_NEG };
  return parse_inverting_unary_operator (lexer, e, &op, parse_exp);
}

static struct expr_node *
parse_exp (struct lexer *lexer, struct expression *e)
{
  static const struct operator op = { .token = T_EXP, .num_op = OP_POW };

  const char *chain_warning =
    _("The exponentiation operator (`**') is left-associative: "
      "`a**b**c' equals `(a**b)**c', not `a**(b**c)'.  "
      "To disable this warning, insert parentheses.");

  if (lex_token (lexer) != T_NEG_NUM || lex_next_token (lexer, 1) != T_EXP)
    return parse_binary_operators (lexer, e, &op, 1,
                                   parse_primary, chain_warning);

  /* Special case for situations like "-5**6", which must be parsed as
     -(5**6). */

  int start_ofs = lex_ofs (lexer);
  struct expr_node *lhs = expr_allocate_number (e, -lex_tokval (lexer));
  lex_get (lexer);
  expr_add_location (lexer, e, start_ofs, lhs);

  struct expr_node *node = parse_binary_operators__ (
    lexer, e, &op, 1, parse_primary, chain_warning, lhs);
  if (!node)
    return NULL;

  node = expr_allocate_unary (e, OP_NEG, node);
  expr_add_location (lexer, e, start_ofs, node);
  return node;
}

static double
ymd_to_offset (int y, int m, int d)
{
  char *error;
  double retval = calendar_gregorian_to_offset (
    y, m, d, settings_get_fmt_settings (), &error);
  if (error)
    {
      msg (SE, "%s", error);
      free (error);
    }
  return retval;
}

static struct expr_node *
expr_date (struct expression *e, int year_digits)
{
  static const char *months[12] =
    {
      "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };

  time_t last_proc_time = time_of_last_procedure (e->ds);
  struct tm *time = localtime (&last_proc_time);

  char *tmp = (year_digits == 2
               ? xasprintf ("%02d-%s-%02d", time->tm_mday, months[time->tm_mon],
                            time->tm_year % 100)
               : xasprintf ("%02d-%s-%04d", time->tm_mday, months[time->tm_mon],
                            time->tm_year + 1900));

  struct substring s = ss_clone_pool (ss_cstr (tmp), e->expr_pool);
  free (tmp);

  return expr_allocate_string (e, s);
}

/* Parses system variables. */
static struct expr_node *
parse_sysvar (struct lexer *lexer, struct expression *e)
{
  if (lex_match_id (lexer, "$CASENUM"))
    return expr_allocate_nullary (e, OP_CASENUM);
  else if (lex_match_id (lexer, "$DATE"))
    return expr_date (e, 2);
  else if (lex_match_id (lexer, "$DATE11"))
    return expr_date (e, 4);
  else if (lex_match_id (lexer, "$TRUE"))
    return expr_allocate_boolean (e, 1.0);
  else if (lex_match_id (lexer, "$FALSE"))
    return expr_allocate_boolean (e, 0.0);
  else if (lex_match_id (lexer, "$SYSMIS"))
    return expr_allocate_number (e, SYSMIS);
  else if (lex_match_id (lexer, "$JDATE"))
    {
      time_t time = time_of_last_procedure (e->ds);
      struct tm *tm = localtime (&time);
      return expr_allocate_number (e, ymd_to_offset (tm->tm_year + 1900,
                                                     tm->tm_mon + 1,
                                                     tm->tm_mday));
    }
  else if (lex_match_id (lexer, "$TIME"))
    {
      time_t time = time_of_last_procedure (e->ds);
      struct tm *tm = localtime (&time);
      return expr_allocate_number (e, ymd_to_offset (tm->tm_year + 1900,
                                                     tm->tm_mon + 1,
                                                     tm->tm_mday) * DAY_S
                                   + tm->tm_hour * 60 * 60.
                                   + tm->tm_min * 60.
                                   + tm->tm_sec);
    }
  else if (lex_match_id (lexer, "$LENGTH"))
    return expr_allocate_number (e, settings_get_viewlength ());
  else if (lex_match_id (lexer, "$WIDTH"))
    return expr_allocate_number (e, settings_get_viewwidth ());
  else
    {
      lex_error (lexer, _("Unknown system variable %s."), lex_tokcstr (lexer));
      return NULL;
    }
}

/* Parses numbers, varnames, etc. */
static struct expr_node *
parse_primary__ (struct lexer *lexer, struct expression *e)
{
  switch (lex_token (lexer))
    {
    case T_ID:
      if (lex_next_token (lexer, 1) == T_LPAREN)
        {
          /* An identifier followed by a left parenthesis may be
             a vector element reference.  If not, it's a function
             call. */
          if (e->ds != NULL && dict_lookup_vector (dataset_dict (e->ds), lex_tokcstr (lexer)) != NULL)
            return parse_vector_element (lexer, e);
          else
            return parse_function (lexer, e);
        }
      else if (lex_tokcstr (lexer)[0] == '$')
        {
          /* $ at the beginning indicates a system variable. */
          return parse_sysvar (lexer, e);
        }
      else if (e->ds != NULL && dict_lookup_var (dataset_dict (e->ds), lex_tokcstr (lexer)))
        {
          /* It looks like a user variable.
             (It could be a format specifier, but we'll assume
             it's a variable unless proven otherwise. */
          return allocate_unary_variable (e, parse_variable (lexer, dataset_dict (e->ds)));
        }
      else
        {
          /* Try to parse it as a format specifier. */
          struct fmt_spec fmt;
          bool ok;

          msg_disable ();
          ok = parse_format_specifier (lexer, &fmt);
          msg_enable ();

          if (ok)
            return expr_allocate_format (e, fmt);

          /* All attempts failed. */
          lex_error (lexer, _("Unknown identifier %s."), lex_tokcstr (lexer));
          return NULL;
        }
      break;

    case T_POS_NUM:
    case T_NEG_NUM:
      {
        struct expr_node *node = expr_allocate_number (e, lex_tokval (lexer));
        lex_get (lexer);
        return node;
      }

    case T_STRING:
      {
        const char *dict_encoding;
        struct expr_node *node;
        char *s;

        dict_encoding = (e->ds != NULL
                         ? dict_get_encoding (dataset_dict (e->ds))
                         : "UTF-8");
        s = recode_string_pool (dict_encoding, "UTF-8", lex_tokcstr (lexer),
                           ss_length (lex_tokss (lexer)), e->expr_pool);
        node = expr_allocate_string (e, ss_cstr (s));

        lex_get (lexer);
        return node;
      }

    case T_LPAREN:
      {
        lex_get (lexer);
        struct expr_node *node = parse_or (lexer, e);
        return !node || !lex_force_match (lexer, T_RPAREN) ? NULL : node;
      }

    default:
      lex_error (lexer, _("Syntax error parsing expression."));
      return NULL;
    }
}

static struct expr_node *
parse_primary (struct lexer *lexer, struct expression *e)
{
  int start_ofs = lex_ofs (lexer);
  struct expr_node *node = parse_primary__ (lexer, e);
  expr_add_location (lexer, e, start_ofs, node);
  return node;
}

static struct expr_node *
parse_vector_element (struct lexer *lexer, struct expression *e)
{
  int vector_start_ofs = lex_ofs (lexer);

  /* Find vector, skip token.
     The caller must already have verified that the current token
     is the name of a vector. */
  const struct vector *vector = dict_lookup_vector (dataset_dict (e->ds),
                                                    lex_tokcstr (lexer));
  assert (vector != NULL);
  lex_get (lexer);

  /* Skip left parenthesis token.
     The caller must have verified that the lookahead is a left
     parenthesis. */
  assert (lex_token (lexer) == T_LPAREN);
  lex_get (lexer);

  int element_start_ofs = lex_ofs (lexer);
  struct expr_node *element = parse_or (lexer, e);
  if (!element)
    return NULL;
  expr_add_location (lexer, e, element_start_ofs, element);

  if (!lex_match (lexer, T_RPAREN))
    return NULL;

  operation_type type = (vector_get_type (vector) == VAL_NUMERIC
                         ? OP_VEC_ELEM_NUM_RAW : OP_VEC_ELEM_STR);
  struct expr_node *node = expr_allocate_binary (
    e, type, element, expr_allocate_vector (e, vector));
  expr_add_location (lexer, e, vector_start_ofs, node);

  if (!type_coercion (e, node, 0))
    {
      msg_at (SE, expr_location (e, node),
              _("A vector index must be numeric."));

      msg_at (SN, expr_location (e, node->args[0]),
              _("This vector index has type '%s'."),
              atom_type_name (expr_node_returns (node->args[0])));

      return NULL;
    }

  return node;
}

/* Individual function parsing. */

const struct operation operations[OP_first + n_OP] = {
#include "parse.inc"
};

static bool
word_matches (const char **test, const char **name)
{
  size_t test_len = strcspn (*test, ".");
  size_t name_len = strcspn (*name, ".");
  if (test_len == name_len)
    {
      if (buf_compare_case (*test, *name, test_len))
        return false;
    }
  else if (test_len < 3 || test_len > name_len)
    return false;
  else
    {
      if (buf_compare_case (*test, *name, test_len))
        return false;
    }

  *test += test_len;
  *name += name_len;
  if (**test != **name)
    return false;

  if (**test == '.')
    {
      (*test)++;
      (*name)++;
    }
  return true;
}

/* Returns 0 if TOKEN and FUNC do not match,
   1 if TOKEN is an acceptable abbreviation for FUNC,
   2 if TOKEN equals FUNC. */
static int
compare_function_names (const char *token_, const char *func_)
{
  const char *token = token_;
  const char *func = func_;
  while (*token || *func)
    if (!word_matches (&token, &func))
      return 0;
  return !c_strcasecmp (token_, func_) ? 2 : 1;
}

static bool
lookup_function (const char *token,
                 const struct operation **first,
                 const struct operation **last)
{
  *first = *last = NULL;
  const struct operation *best = NULL;

  for (const struct operation *f = operations + OP_function_first;
       f <= operations + OP_function_last; f++)
    {
      int score = compare_function_names (token, f->name);
      if (score == 2)
        {
          best = f;
          break;
        }
      else if (score == 1 && !(f->flags & OPF_NO_ABBREV) && !best)
        best = f;
    }

  if (!best)
    return false;

  *first = best;

  const struct operation *f = best;
  while (f <= operations + OP_function_last
         && !c_strcasecmp (f->name, best->name))
    f++;
  *last = f;

  return true;
}

static int
extract_min_valid (const char *s)
{
  char *p = strrchr (s, '.');
  if (p == NULL
      || p[1] < '0' || p[1] > '9'
      || strspn (p + 1, "0123456789") != strlen (p + 1))
    return -1;
  *p = '\0';
  return atoi (p + 1);
}

static bool
match_function__ (struct expr_node *node, const struct operation *f)
{
  if (node->n_args < f->n_args
      || (node->n_args > f->n_args && (f->flags & OPF_ARRAY_OPERAND) == 0)
      || node->n_args - (f->n_args - 1) < f->array_min_elems)
    return false;

  node->type = f - operations;
  for (size_t i = 0; i < node->n_args; i++)
    if (!is_coercible (node, i))
      return false;

  return true;
}

static const struct operation *
match_function (struct expr_node *node,
                const struct operation *first, const struct operation *last)
{
  for (const struct operation *f = first; f < last; f++)
    if (match_function__ (node, f))
      return f;
  return NULL;
}

static bool
validate_function_args (const struct expression *e, const struct expr_node *n,
                        const struct operation *f, int n_args, int min_valid)
{
  /* Count the function arguments that go into the trailing array (if any).  We
     know that there must be at least the minimum number because
     match_function() already checked. */
  int array_n_args = n_args - (f->n_args - 1);
  assert (array_n_args >= f->array_min_elems);

  if ((f->flags & OPF_ARRAY_OPERAND)
      && array_n_args % f->array_granularity != 0)
    {
      /* RANGE is the only case we have so far.  It has paired arguments with
         one initial argument, and that's the only special case we deal with
         here. */
      assert (f->array_granularity == 2);
      assert (n_args % 2 == 0);
      msg_at (SE, expr_location (e, n),
              _("%s must have an odd number of arguments."), f->prototype);
      return false;
    }

  if (min_valid != -1)
    {
      if (f->array_min_elems == 0)
        {
          assert ((f->flags & OPF_MIN_VALID) == 0);
          msg_at (SE, expr_location (e, n),
                  _("%s function cannot accept suffix .%d to specify the "
                    "minimum number of valid arguments."),
                  f->prototype, min_valid);
          return false;
        }
      else
        {
          assert (f->flags & OPF_MIN_VALID);
          if (min_valid > array_n_args)
            {
              msg_at (SE, expr_location (e, n),
                      _("For %s with %d arguments, at most %d (not %d) may be "
                        "required to be valid."),
                      f->prototype, n_args, array_n_args, min_valid);
              return false;
            }
        }
    }

  return true;
}

static void
add_arg (struct expr_node ***args, size_t *n_args, size_t *allocated_args,
         struct expr_node *arg,
         struct expression *e, struct lexer *lexer, int arg_start_ofs)
{
  if (*n_args >= *allocated_args)
    *args = x2nrealloc (*args, allocated_args, sizeof **args);

  expr_add_location (lexer, e, arg_start_ofs, arg);
  (*args)[(*n_args)++] = arg;
}

static void
put_invocation (struct string *s,
                const char *func_name, struct expr_node *node)
{
  size_t i;

  ds_put_format (s, "%s(", func_name);
  for (i = 0; i < node->n_args; i++)
    {
      if (i > 0)
        ds_put_cstr (s, ", ");
      ds_put_cstr (s, operations[expr_node_returns (node->args[i])].prototype);
    }
  ds_put_byte (s, ')');
}

static void
no_match (struct expression *e, const char *func_name, struct expr_node *node,
          const struct operation *ops, size_t n)
{
  struct string s;

  ds_init_empty (&s);

  if (n == 1)
    {
      ds_put_format (&s, _("Type mismatch invoking %s as "), ops->prototype);
      put_invocation (&s, func_name, node);
    }
  else
    {
      ds_put_cstr (&s, _("Function invocation "));
      put_invocation (&s, func_name, node);
      ds_put_cstr (&s, _(" does not match any known function.  Candidates are:"));

      for (size_t i = 0; i < n; i++)
        ds_put_format (&s, "\n%s", ops[i].prototype);
    }
  ds_put_byte (&s, '.');

  msg_at (SE, expr_location (e, node), "%s", ds_cstr (&s));

  if (n == 1 && ops->n_args == node->n_args)
    {
      for (size_t i = 0; i < node->n_args; i++)
        if (!is_coercible (node, i))
          {
            atom_type expected = ops->args[i];
            atom_type actual = expr_node_returns (node->args[i]);
            if ((expected == OP_ni_format || expected == OP_no_format)
                && actual == OP_format)
              {
                struct fmt_spec f = node->args[i]->format;
                char *error = fmt_check__ (f, (ops->args[i] == OP_ni_format
                                               ? FMT_FOR_INPUT : FMT_FOR_OUTPUT));
                if (!error)
                  error = fmt_check_type_compat__ (f, NULL, VAL_NUMERIC);
                if (error)
                  {
                    msg_at (SN, expr_location (e, node->args[i]), "%s", error);
                    free (error);
                  }
              }
            else
              msg_at (SN, expr_location (e, node->args[i]),
                      _("This argument has type '%s' but '%s' is required."),
                      atom_type_name (actual), atom_type_name (expected));
          }
      }

  ds_destroy (&s);
}

static struct expr_node *
parse_function (struct lexer *lexer, struct expression *e)
{
  struct string func_name;
  ds_init_substring (&func_name, lex_tokss (lexer));

  int min_valid = extract_min_valid (lex_tokcstr (lexer));

  const struct operation *first, *last;
  if (!lookup_function (lex_tokcstr (lexer), &first, &last))
    {
      lex_error (lexer, _("No function or vector named %s."),
                 lex_tokcstr (lexer));
      ds_destroy (&func_name);
      return NULL;
    }

  int func_start_ofs = lex_ofs (lexer);
  lex_get (lexer);
  if (!lex_force_match (lexer, T_LPAREN))
    {
      ds_destroy (&func_name);
      return NULL;
    }

  struct expr_node **args = NULL;
  size_t n_args = 0;
  size_t allocated_args = 0;
  if (lex_token (lexer) != T_RPAREN)
    for (;;)
      {
        int arg_start_ofs = lex_ofs (lexer);
        if (lex_token (lexer) == T_ID
            && lex_next_token (lexer, 1) == T_TO)
          {
            const struct variable **vars;
            size_t n_vars;

            if (!parse_variables_const (lexer, dataset_dict (e->ds),
                                        &vars, &n_vars, PV_SINGLE))
              goto fail;
            for (size_t i = 0; i < n_vars; i++)
              add_arg (&args, &n_args, &allocated_args,
                       allocate_unary_variable (e, vars[i]),
                       e, lexer, arg_start_ofs);
            free (vars);
          }
        else
          {
            struct expr_node *arg = parse_or (lexer, e);
            if (arg == NULL)
              goto fail;

            add_arg (&args, &n_args, &allocated_args, arg,
                     e, lexer, arg_start_ofs);
          }
        if (lex_match (lexer, T_RPAREN))
          break;
        else if (!lex_match (lexer, T_COMMA))
          {
            lex_error_expecting (lexer, "`,'", "`)'");
            goto fail;
          }
      }

  struct expr_node *n = expr_allocate_composite (e, first - operations,
                                                 args, n_args);
  expr_add_location (lexer, e, func_start_ofs, n);
  const struct operation *f = match_function (n, first, last);
  if (!f)
    {
      no_match (e, ds_cstr (&func_name), n, first, last - first);
      goto fail;
    }
  n->type = f - operations;
  n->min_valid = min_valid != -1 ? min_valid : f->array_min_elems;

  for (size_t i = 0; i < n_args; i++)
    if (!type_coercion (e, n, i))
      {
        /* Unreachable because match_function already checked that the
           arguments were coercible. */
        NOT_REACHED ();
      }
  if (!validate_function_args (e, n, f, n_args, min_valid))
    goto fail;

  if ((f->flags & OPF_EXTENSION) && settings_get_syntax () == COMPATIBLE)
    msg_at (SW, expr_location (e, n),
            _("%s is a PSPP extension."), f->prototype);
  if (f->flags & OPF_UNIMPLEMENTED)
    {
      msg_at (SE, expr_location (e, n),
              _("%s is not available in this version of PSPP."), f->prototype);
      goto fail;
    }
  if ((f->flags & OPF_PERM_ONLY) &&
      proc_in_temporary_transformations (e->ds))
    {
      msg_at (SE, expr_location (e, n),
              _("%s may not appear after %s."), f->prototype, "TEMPORARY");
      goto fail;
    }

  if (n->type == OP_LAG_Vn || n->type == OP_LAG_Vs)
    dataset_need_lag (e->ds, 1);
  else if (n->type == OP_LAG_Vnn || n->type == OP_LAG_Vsn)
    {
      assert (n->n_args == 2);
      assert (n->args[1]->type == OP_pos_int);
      dataset_need_lag (e->ds, n->args[1]->integer);
    }

  free (args);
  ds_destroy (&func_name);
  return n;

fail:
  free (args);
  ds_destroy (&func_name);
  return NULL;
}

/* Utility functions. */

static struct expression *
expr_create (struct dataset *ds)
{
  struct pool *pool = pool_create ();
  struct expression *e = pool_alloc (pool, sizeof *e);
  *e = (struct expression) {
    .expr_pool = pool,
    .ds = ds,
    .eval_pool = pool_create_subpool (pool),
  };
  return e;
}

atom_type
expr_node_returns (const struct expr_node *n)
{
  assert (n != NULL);
  assert (is_operation (n->type));
  if (is_atom (n->type))
    return n->type;
  else if (is_composite (n->type))
    return operations[n->type].returns;
  else
    NOT_REACHED ();
}

static const char *
atom_type_name (atom_type type)
{
  assert (is_atom (type));

  /* The Boolean type is purely an internal concept that the documentation
     doesn't mention, so it might confuse users if we talked about them in
     diagnostics. */
  return type == OP_boolean ? "number" : operations[type].name;
}

struct expr_node *
expr_allocate_nullary (struct expression *e, operation_type op)
{
  return expr_allocate_composite (e, op, NULL, 0);
}

struct expr_node *
expr_allocate_unary (struct expression *e, operation_type op,
                     struct expr_node *arg0)
{
  return expr_allocate_composite (e, op, &arg0, 1);
}

struct expr_node *
expr_allocate_binary (struct expression *e, operation_type op,
                      struct expr_node *arg0, struct expr_node *arg1)
{
  struct expr_node *args[2];
  args[0] = arg0;
  args[1] = arg1;
  return expr_allocate_composite (e, op, args, 2);
}

struct expr_node *
expr_allocate_composite (struct expression *e, operation_type op,
                         struct expr_node **args, size_t n_args)
{
  for (size_t i = 0; i < n_args; i++)
    if (!args[i])
      return NULL;

  struct expr_node *n = pool_alloc (e->expr_pool, sizeof *n);
  *n = (struct expr_node) {
    .type = op,
    .n_args = n_args,
    .args = pool_clone (e->expr_pool, args, sizeof *n->args * n_args),
  };
  return n;
}

struct expr_node *
expr_allocate_number (struct expression *e, double d)
{
  struct expr_node *n = pool_alloc (e->expr_pool, sizeof *n);
  *n = (struct expr_node) { .type = OP_number, .number = d };
  return n;
}

struct expr_node *
expr_allocate_boolean (struct expression *e, double b)
{
  assert (b == 0.0 || b == 1.0 || b == SYSMIS);

  struct expr_node *n = pool_alloc (e->expr_pool, sizeof *n);
  *n = (struct expr_node) { .type = OP_boolean, .number = b };
  return n;
}

struct expr_node *
expr_allocate_integer (struct expression *e, int i)
{
  struct expr_node *n = pool_alloc (e->expr_pool, sizeof *n);
  *n = (struct expr_node) { .type = OP_integer, .integer = i };
  return n;
}

struct expr_node *
expr_allocate_pos_int (struct expression *e, int i)
{
  assert (i > 0);

  struct expr_node *n = pool_alloc (e->expr_pool, sizeof *n);
  *n = (struct expr_node) { .type = OP_pos_int, .integer = i };
  return n;
}

struct expr_node *
expr_allocate_vector (struct expression *e, const struct vector *vector)
{
  struct expr_node *n = pool_alloc (e->expr_pool, sizeof *n);
  *n = (struct expr_node) { .type = OP_vector, .vector = vector };
  return n;
}

struct expr_node *
expr_allocate_string (struct expression *e, struct substring s)
{
  struct expr_node *n = pool_alloc (e->expr_pool, sizeof *n);
  *n = (struct expr_node) { .type = OP_string, .string = s };
  return n;
}

struct expr_node *
expr_allocate_variable (struct expression *e, const struct variable *v)
{
  struct expr_node *n = pool_alloc (e->expr_pool, sizeof *n);
  *n = (struct expr_node) {
    .type = var_is_numeric (v) ? OP_num_var : OP_str_var,
    .variable = v
  };
  return n;
}

struct expr_node *
expr_allocate_format (struct expression *e, struct fmt_spec format)
{
  struct expr_node *n = pool_alloc (e->expr_pool, sizeof *n);
  *n = (struct expr_node) { .type = OP_format, .format = format };
  return n;
}

struct expr_node *
expr_allocate_expr_node (struct expression *e,
                         const struct expr_node *expr_node)
{
  struct expr_node *n = pool_alloc (e->expr_pool, sizeof *n);
  *n = (struct expr_node) { .type = OP_expr_node, .expr_node = expr_node };
  return n;
}

/* Allocates a unary composite node that represents the value of
   variable V in expression E. */
static struct expr_node *
allocate_unary_variable (struct expression *e, const struct variable *v)
{
  assert (v != NULL);
  return expr_allocate_unary (e, var_is_numeric (v) ? OP_NUM_VAR : OP_STR_VAR,
                              expr_allocate_variable (e, v));
}

/* Export function details to other modules. */

/* Returns the operation structure for the function with the
   given IDX. */
const struct operation *
expr_get_function (size_t idx)
{
  assert (idx < n_OP_function);
  return &operations[OP_function_first + idx];
}

/* Returns the number of expression functions. */
size_t
expr_get_n_functions (void)
{
  return n_OP_function;
}

/* Returns the name of operation OP. */
const char *
expr_operation_get_name (const struct operation *op)
{
  return op->name;
}

/* Returns the human-readable prototype for operation OP. */
const char *
expr_operation_get_prototype (const struct operation *op)
{
  return op->prototype;
}

/* Returns the number of arguments for operation OP. */
int
expr_operation_get_n_args (const struct operation *op)
{
  return op->n_args;
}
