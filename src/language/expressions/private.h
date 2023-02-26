/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2011 Free Software Foundation, Inc.

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

#ifndef EXPRESSIONS_PRIVATE_H
#define EXPRESSIONS_PRIVATE_H

#include <assert.h>
#include <stddef.h>

#include "data/format.h"
#include "operations.h"
#include "public.h"
#include "libpspp/str.h"

enum operation_flags
  {
    /* Most operations produce a missing output value if any
       input value is missing.  Setting this bit indicates that
       this operation may produce a non-missing result given
       missing input values (although it is not obliged to do
       so).  Unless this bit is set, the operation's evaluation
       function will never be passed a missing argument. */
    OPF_ABSORB_MISS = 1 << 0,

    /* If set, this operation's final operand is an array of one
       or more elements. */
    OPF_ARRAY_OPERAND = 1 << 1,

    /* If set, the user can specify the minimum number of array
       elements that must be non-missing for the function result
       to be non-missing.  The operation must have an array
       operand and the array must contain `double's.  Both
       OPF_ABSORB_MISS and OPF_ARRAY_OPERAND must also be set. */
    OPF_MIN_VALID = 1 << 2,

    /* If set, operation is non-optimizable in general.  Unless
       combined with OPF_ABSORB_MISS, missing input values are
       still assumed to yield missing results. */
    OPF_NONOPTIMIZABLE = 1 << 3,

    /* If set, this operation is not implemented. */
    OPF_UNIMPLEMENTED = 1 << 4,

    /* If set, this operation is a PSPP extension. */
    OPF_EXTENSION = 1 << 5,

    /* If set, this operation may not occur after TEMPORARY.
       (Currently this applies only to LAG.) */
    OPF_PERM_ONLY = 1 << 6,

    /* If set, this operation's name may not be abbreviated. */
    OPF_NO_ABBREV = 1 << 7,

    /* If set, this operation needs the "struct expr_node *", for message
       locations. */
    OPF_EXPR_NODE = 1 << 8,
  };

#define EXPR_ARG_MAX 4
struct operation
  {
    const char *name;
    const char *prototype;
    enum operation_flags flags;
    atom_type returns;       /* Usually OP_number, OP_string, or OP_boolean. */
    int n_args;
    atom_type args[EXPR_ARG_MAX];
    int array_min_elems;
    int array_granularity;
  };

extern const struct operation operations[];

/* Expression parse tree. */
struct expr_node
  {
    operation_type type;
    struct msg_location *location;

    union
      {
        /* OP_number. */
        double number;

        /* OP_string. */
        struct substring string;

        /* OP_variable. */
        const struct variable *variable;

        /* OP_integer. */
        int integer;

        /* OP_vector. */
        const struct vector *vector;

        /* OP_format. */
        struct fmt_spec format;

        /* All the other node types. */
        struct
          {
            size_t n_args;         /* Number of arguments. */
            struct expr_node **args; /* Arguments. */
            size_t min_valid;   /* Min valid array args to get valid result. */
          };

        /* OP_exprnode. */
        const struct expr_node *expr_node;
      };
  };

union operation_data
  {
    operation_type operation;
    double number;
    struct substring string;
    const struct variable *variable;
    const struct vector *vector;
    struct fmt_spec format;
    const struct expr_node *expr_node;
    int integer;
  };

/* An expression. */
struct expression
  {
    struct pool *expr_pool;     /* Pool for expression static data. */
    struct dataset *ds ;        /* The dataset */
    atom_type type;             /* Type of expression result. */

    union operation_data *ops;  /* Expression data. */
    operation_type *op_types;   /* ops[] element types (for debugging). */
    size_t n_ops, allocated_ops; /* Number of ops, amount of allocated space. */

    double *number_stack;       /* Evaluation stack: numerics, Booleans. */
    struct substring *string_stack; /* Evaluation stack: strings. */
    struct pool *eval_pool;     /* Pool for evaluation temporaries. */
  };

struct expression *expr_parse_any (struct lexer *lexer, struct dataset *,  bool optimize);
void expr_debug_print_postfix (const struct expression *);

struct expr_node *expr_optimize (struct expr_node *, struct expression *);
void expr_flatten (struct expr_node *, struct expression *);

atom_type expr_node_returns (const struct expr_node *);

struct expr_node *expr_allocate_nullary (struct expression *e, operation_type);
struct expr_node *expr_allocate_unary (struct expression *e,
                                     operation_type, struct expr_node *);
struct expr_node *expr_allocate_binary (struct expression *e, operation_type,
                                 struct expr_node *, struct expr_node *);
struct expr_node *expr_allocate_composite (struct expression *e, operation_type,
                                         struct expr_node **, size_t);
struct expr_node *expr_allocate_number (struct expression *e, double);
struct expr_node *expr_allocate_boolean (struct expression *e, double);
struct expr_node *expr_allocate_integer (struct expression *e, int);
struct expr_node *expr_allocate_pos_int (struct expression *e, int);
struct expr_node *expr_allocate_string (struct expression *e, struct substring);
struct expr_node *expr_allocate_variable (struct expression *e,
                                        const struct variable *);
struct expr_node *expr_allocate_format (struct expression *, struct fmt_spec);
struct expr_node *expr_allocate_expr_node (struct expression *,
                                           const struct expr_node *);
struct expr_node *expr_allocate_vector (struct expression *e,
                                      const struct vector *);

const struct msg_location *expr_location (const struct expression *,
                                          const struct expr_node *);

#endif /* expressions/private.h */
