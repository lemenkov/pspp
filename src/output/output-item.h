/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011 Free Software Foundation, Inc.

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

#ifndef OUTPUT_ITEM_H
#define OUTPUT_ITEM_H 1

/* Output items.

   An output item is a self-contained chunk of output.  Several kinds of output
   items exist.  See *-item.h for details.
*/

#include <stdbool.h>
#include "libpspp/cast.h"

/* A single output item. */
struct output_item
  {
    const struct output_item_class *class;

    /* Reference count.  An output item may be shared between multiple owners,
       indicated by a reference count greater than 1.  When this is the case,
       the output item must not be modified. */
    int ref_cnt;

    /* The localized label for the item that appears in the outline pane in the
       PSPPIRE output viewer and in PDF outlines.  This is NULL if no label has
       been explicitly set.

       Use output_item_get_label() to read an item's label. */
    char *label;
  };

struct output_item *output_item_ref (const struct output_item *);
void output_item_unref (struct output_item *);
bool output_item_is_shared (const struct output_item *);

const char *output_item_get_label (const struct output_item *);
void output_item_set_label (struct output_item *, const char *);
void output_item_set_label_nocopy (struct output_item *, char *);

#endif /* output/output-item.h */
