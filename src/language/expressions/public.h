/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#if !expr_h
#define expr_h 1

#include <stddef.h>

#include "data/val-type.h"

struct ccase;
struct dataset;
struct dictionary;
struct expression;
struct lexer;
struct msg_location;
struct pool;
union value;

struct expression *expr_parse (struct lexer *, struct dataset *, enum val_type);
struct expression *expr_parse_bool (struct lexer *, struct dataset *);
struct expression *expr_parse_new_variable (
  struct lexer *, struct dataset *,
  const char *new_var_name, const struct msg_location *new_var_location);
void expr_free (struct expression *);

struct dataset;
double expr_evaluate_num (struct expression *, const struct ccase *,
                          int case_idx);
void expr_evaluate_str (struct expression *, const struct ccase *,
                        int case_idx, char *dst, size_t dst_size);

const struct operation *expr_get_function (size_t idx);
size_t expr_get_n_functions (void);
const char *expr_operation_get_name (const struct operation *);
const char *expr_operation_get_prototype (const struct operation *);
int expr_operation_get_n_args (const struct operation *);

#endif /* expr.h */
