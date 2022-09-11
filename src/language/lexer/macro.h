/* PSPP - a program for statistical analysis.
   Copyright (C) 2021 Free Software Foundation, Inc.

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

#ifndef MACRO_H
#define MACRO_H 1

#include <stdbool.h>
#include <stddef.h>

#include "libpspp/hmap.h"
#include "libpspp/str.h"
#include "language/lexer/segment.h"
#include "language/lexer/token.h"

struct msg_location;

/* A token along with the syntax that was tokenized to produce it.  The syntax
   allows the token to be turned back into syntax accurately. */
struct macro_token
  {
    struct token token;
    struct substring syntax;
  };

void macro_token_copy (struct macro_token *, const struct macro_token *);
void macro_token_uninit (struct macro_token *);

void macro_token_to_syntax (struct macro_token *, struct string *);

bool is_macro_keyword (struct substring);

/* A dynamic array of macro tokens.

   The syntax for the tokens doesn't include white space, etc. between them. */
struct macro_tokens
  {
    struct macro_token *mts;
    size_t n;
    size_t allocated;
  };

void macro_tokens_copy (struct macro_tokens *, const struct macro_tokens *);
void macro_tokens_uninit (struct macro_tokens *);
struct macro_token *macro_tokens_add_uninit (struct macro_tokens *);
void macro_tokens_add (struct macro_tokens *, const struct macro_token *);

void macro_tokens_to_syntax (struct macro_tokens *, struct string *,
                             size_t *ofs, size_t *len);

void macro_tokens_print (const struct macro_tokens *, FILE *);

/* A parameter to a macro. */
struct macro_param
  {
    bool positional;            /* Is this a positional parameter? */
    char *name;                 /* "!1" or "!name". */
    struct macro_tokens def;    /* Default expansion. */
    bool expand_arg;            /* Macro-expand the argument? */

    enum
      {
        ARG_N_TOKENS,
        ARG_CHAREND,
        ARG_ENCLOSE,
        ARG_CMDEND
      }
    arg_type;

    int n_tokens;               /* ARG_N_TOKENS only. */
    struct token start;         /* ARG_ENCLOSE only. */
    struct token end;           /* ARG_ENCLOSE and ARG_CHAREND only. */
  };

/* A macro. */
struct macro
  {
    struct hmap_node hmap_node; /* Indexed by 'name'. */
    char *name;

    /* Source code location of macro definition, for error reporting. */
    struct msg_location *location;

    /* Parameters. */
    struct macro_param *params;
    size_t n_params;

    /* Body. */
    struct macro_tokens body;
  };

void macro_destroy (struct macro *);

/* A collection of macros. */
struct macro_set
  {
    struct hmap macros;
  };

struct macro_set *macro_set_create (void);
void macro_set_destroy (struct macro_set *);
const struct macro *macro_set_find (const struct macro_set *,
                                    const char *);
void macro_set_add (struct macro_set *, struct macro *);

static inline bool
macro_set_is_empty (const struct macro_set *set)
{
  return hmap_is_empty (&set->macros);
}

/* Parsing and expanding macro calls. */

struct macro_call;

int macro_call_create (const struct macro_set *, const struct token *,
                       struct macro_call **);
int macro_call_add (struct macro_call *, const struct macro_token *,
                    const struct msg_location *);

void macro_call_expand (struct macro_call *, enum segmenter_mode segmenter_mode,
                        const struct msg_location *call_loc, struct macro_tokens *);

void macro_call_destroy (struct macro_call *);

#endif /* macro.h */
