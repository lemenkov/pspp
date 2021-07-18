/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

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

#ifndef TOKEN_H
#define TOKEN_H 1

#include <stdbool.h>
#include <stdio.h>
#include "libpspp/assertion.h"
#include "libpspp/str.h"
#include "data/identifier.h"

/* A PSPP syntax token. */
struct token
  {
    enum token_type type;
    double number;
    struct substring string;
  };

void token_copy (struct token *, const struct token *);
void token_uninit (struct token *);

bool token_equal (const struct token *, const struct token *);

char *token_to_string (const struct token *);

void token_print (const struct token *, FILE *);

static inline bool token_is_number (const struct token *);
static inline double token_number (const struct token *);
bool token_is_integer (const struct token *);
long token_integer (const struct token *);
static inline bool token_is_string (const struct token *);

static inline bool
token_is_number (const struct token *t)
{
  return t->type == T_POS_NUM || t->type == T_NEG_NUM;
}

static inline double
token_number (const struct token *t)
{
  assert (token_is_number (t));
  return t->number;
}

static inline bool
token_is_string (const struct token *t)
{
  return t->type == T_STRING;
}

#endif /* token.h */
