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

   An output item is a self-contained chunk of output.
*/

#include <cairo.h>
#include <stdbool.h>
#include "libpspp/cast.h"

enum output_item_type
  {
    OUTPUT_ITEM_CHART,
    OUTPUT_ITEM_GROUP_OPEN,
    OUTPUT_ITEM_GROUP_CLOSE,
    OUTPUT_ITEM_IMAGE,
    OUTPUT_ITEM_MESSAGE,
    OUTPUT_ITEM_PAGE_BREAK,
    OUTPUT_ITEM_PAGE_SETUP,
    OUTPUT_ITEM_TABLE,
    OUTPUT_ITEM_TEXT,
  };

/* A single output item. */
struct output_item
  {
    /* Reference count.  An output item may be shared between multiple owners,
       indicated by a reference count greater than 1.  When this is the case,
       the output item must not be modified. */
    int ref_cnt;

    /* The localized label for the item that appears in the outline pane in the
       PSPPIRE output viewer and in PDF outlines.  This is NULL if no label has
       been explicitly set.

       Use output_item_get_label() to read an item's label. */
    char *label;

    /* A locale-invariant identifier for the command that produced the output,
       which may be NULL if unknown or if a command did not produce this
       output. */
    char *command_name;

    enum output_item_type type;
    union
      {
        struct chart *chart;

        cairo_surface_t *image;

        struct msg *message;

        struct page_setup *page_setup;

        struct pivot_table *table;

        struct
          {
            enum text_item_subtype
              {
                TEXT_ITEM_PAGE_TITLE,       /* TITLE and SUBTITLE commands. */
                TEXT_ITEM_TITLE,            /* Title. */
                TEXT_ITEM_SYNTAX,           /* Syntax printback logging. */
                TEXT_ITEM_LOG,              /* Other logging. */
              }
            subtype;
            struct pivot_value *content;
          }
        text;
      };

    char *cached_label;
  };

struct output_item *output_item_ref (const struct output_item *);
void output_item_unref (struct output_item *);
bool output_item_is_shared (const struct output_item *);
struct output_item *output_item_unshare (struct output_item *);

void output_item_submit (struct output_item *);

const char *output_item_get_label (const struct output_item *);
void output_item_set_label (struct output_item *, const char *);
void output_item_set_label_nocopy (struct output_item *, char *);

/* OUTPUT_ITEM_CHART. */
struct output_item *chart_item_create (struct chart *);

/* OUTPUT_ITEM_GROUP_OPEN. */
struct output_item *group_open_item_create (const char *command_name,
                                            const char *label);
struct output_item *group_open_item_create_nocopy (char *command_name,
                                                   char *label);

/* OUTPUT_ITEM_GROUP_CLOSE. */

struct output_item *group_close_item_create (void);

/* OUTPUT_ITEM_IMAGE. */

struct output_item *image_item_create (cairo_surface_t *);

/* OUTPUT_ITEM_MESSAGE. */

struct output_item *message_item_create (const struct msg *);

const struct msg *message_item_get_msg (const struct output_item *);

struct output_item *message_item_to_text_item (struct output_item *);

/* OUTPUT_ITEM_PAGE_BREAK. */

struct output_item *page_break_item_create (void);

/* OUTPUT_ITEM_PAGE_SETUP. */

struct output_item *page_setup_item_create (const struct page_setup *);

/* OUTPUT_ITEM_TABLE. */

struct output_item *table_item_create (struct pivot_table *);

/* OUTPUT_ITEM_TEXT. */

struct output_item *text_item_create (enum text_item_subtype,
                                      const char *text, const char *label);
struct output_item *text_item_create_nocopy (enum text_item_subtype,
                                             char *text, char *label);
struct output_item *text_item_create_value (enum text_item_subtype,
                                            struct pivot_value *value,
                                            char *label);

enum text_item_subtype text_item_get_subtype (const struct output_item *);
char *text_item_get_plain_text (const struct output_item *);

bool text_item_append (struct output_item *dst, const struct output_item *src);

struct output_item *text_item_to_table_item (struct output_item *);

const char *text_item_subtype_to_string (enum text_item_subtype);

#endif /* output/output-item.h */
