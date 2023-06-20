/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2010 Free Software Foundation, Inc.

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

#ifndef VARIABLE_PARSER_H
#define VARIABLE_PARSER_H 1

#include <stdbool.h>
#include <stddef.h>
#include "data/dict-class.h"

struct pool;
struct dictionary;
struct var_set;
struct const_var_set;
struct variable;
struct lexer ;

struct var_set *var_set_create_from_dict (const struct dictionary *d);
struct var_set *var_set_create_from_array (struct variable *const *var,
                                           size_t);

size_t var_set_get_n (const struct var_set *vs);

void var_set_destroy (struct var_set *vs);


/* Variable parsers. */

enum
  {
    PV_NONE = 0,                /* No options. */
    PV_SINGLE = 1 << 0,         /* Restrict to a single name or TO use. */
    PV_DUPLICATE = 1 << 1,      /* Don't merge duplicates. */
    PV_APPEND = 1 << 2,         /* Append to existing list. */
    PV_NO_DUPLICATE = 1 << 3,   /* Error on duplicates. */
    PV_NUMERIC = 1 << 4,        /* Vars must be numeric. */
    PV_STRING = 1 << 5,         /* Vars must be string. */
    PV_SAME_TYPE = 1 << 6,      /* All vars must be the same type. */
    PV_SAME_WIDTH = 1 << 7,     /* All vars must be the same type and width. */
    PV_NO_SCRATCH = 1 << 8,     /* Disallow scratch variables. */
  };

struct variable *parse_variable (struct lexer *, const struct dictionary *);
bool parse_variables (struct lexer *, const struct dictionary *, struct variable ***, size_t *,
                     int opts);
bool parse_variables_pool (struct lexer *, struct pool *, const struct dictionary *,
                          struct variable ***, size_t *, int opts);
bool parse_var_set_vars (struct lexer *, const struct var_set *, struct variable ***, size_t *,
                        int opts);

char *parse_DATA_LIST_var (struct lexer *, const struct dictionary *,
                           enum dict_class);
bool parse_DATA_LIST_vars (struct lexer *, const struct dictionary *,
                           char ***names, size_t *n, int opts);
bool parse_DATA_LIST_vars_pool (struct lexer *, const struct dictionary *,
                                struct pool *,
                                char ***names, size_t *n, int opts);
bool parse_mixed_vars (struct lexer *, const struct dictionary *dict,
                       char ***names, size_t *n, int opts);
bool parse_mixed_vars_pool (struct lexer *, const struct dictionary *dict,
                            struct pool *,
                           char ***names, size_t *n, int opts);

/* This variable parser supports the unusual situation where set of variables
   has to be parsed before the associated dictionary is available.  Thus,
   parsing proceeds in two phases: first, the variables are parsed in a vector
   of "struct var_syntax"; second, when the dictionary becomes available, the
   structs are turned into "struct variable"s. */

struct var_syntax
  {
    char *first;                /* Always nonnull. */
    char *last;                 /* Nonnull for var ranges (e.g. "a TO b"). */

    /* For error reporting.

      This only works if var_syntax_parse() and var_syntax_evaluate() are
      called while we're parsing the same source file.  That matches the
      current use case in MATRIX; if that changes, then this will need to
      switch to use struct msg_location instead. */
    int first_ofs;
    int last_ofs;
  };
void var_syntax_destroy (struct var_syntax *, size_t n);

bool var_syntax_parse (struct lexer *, struct var_syntax **, size_t *);
bool var_syntax_evaluate (struct lexer *, const struct var_syntax *, size_t,
                          const struct dictionary *,
                          struct variable ***, size_t *, int opts);

/* Const wrappers */

static inline const struct variable *
parse_variable_const (struct lexer *l, const struct dictionary *d)
{
  return parse_variable (l, d);
}

static inline bool
parse_variables_const (struct lexer *l, const struct dictionary *d,
                       const struct variable ***v, size_t *s,
                       int opts)
{
  return parse_variables (l, d, (struct variable ***) v, s, opts);
}

static inline bool
parse_variables_const_pool (struct lexer *l, struct pool *p,
                            const struct dictionary *d,
                            const struct variable ***v, size_t *s, int opts)
{
  return parse_variables_pool (l, p, d, (struct variable ***) v, s, opts);
}



static inline struct const_var_set *
const_var_set_create_from_dict (const struct dictionary *d)
{
  return (struct const_var_set *) var_set_create_from_dict (d);
}

static inline struct const_var_set *
const_var_set_create_from_array (const struct variable *const *var,
                                 size_t s)
{
  return (struct const_var_set *) var_set_create_from_array ((struct variable *const *) var, s);
}


static inline bool
parse_const_var_set_vars (struct lexer *l, const struct const_var_set *vs,
                          const struct variable ***v, size_t *s, int opts)
{
  return parse_var_set_vars (l, (const struct var_set *) vs,
                             (struct variable ***) v, s, opts);
}

static inline void
const_var_set_destroy (struct const_var_set *vs)
{
  var_set_destroy ((struct var_set *) vs);
}

/* Match a variable.
   If the match succeeds, the variable will be placed in VAR.
   Returns true if successful */
bool
lex_match_variable (struct lexer *lexer, const struct dictionary *dict, const struct variable **var);

#endif /* variable-parser.h */
