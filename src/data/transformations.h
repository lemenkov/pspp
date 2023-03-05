/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2011 Free Software Foundation, Inc.

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

#ifndef TRANSFORMATIONS_H
#define TRANSFORMATIONS_H 1

#include <stdbool.h>
#include <stddef.h>
#include "data/case.h"

/* One transformation. */

enum trns_result
  {
    TRNS_CONTINUE,              /* Continue to next transformation. */
    TRNS_BREAK,                 /* Break out of LOOP. */
    TRNS_DROP_CASE,             /* Drop this case. */
    TRNS_ERROR,                 /* A serious error, so stop the procedure. */
    TRNS_END_CASE,              /* Skip to next case.  INPUT PROGRAM only. */
    TRNS_END_FILE               /* End of input.  INPUT PROGRAM only. */
  };

struct ccase;

struct trns_class
  {
    const char *name;           /* For debugging. */
    enum trns_result (*execute) (void *aux, struct ccase **, casenumber);
    bool (*destroy) (void *aux);
  };

struct transformation
  {
    const struct trns_class *class;
    void *aux;
  };

/* A chain of transformations. */

struct trns_chain
  {
    struct transformation *xforms;
    size_t n;
    size_t allocated;
  };

#define TRNS_CHAIN_INIT { .n = 0 }

void trns_chain_init (struct trns_chain *);
bool trns_chain_uninit (struct trns_chain *);

bool trns_chain_clear (struct trns_chain *);

void trns_chain_prepend (struct trns_chain *, const struct transformation *);
void trns_chain_append (struct trns_chain *, const struct transformation *);
void trns_chain_splice (struct trns_chain *, struct trns_chain *);

enum trns_result trns_chain_execute (const struct trns_chain *,
                                     casenumber case_num, struct ccase **);

#endif /* transformations.h */
