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
#include "libpspp/string-array.h"

enum output_item_type
  {
    OUTPUT_ITEM_CHART,
    OUTPUT_ITEM_GROUP,
    OUTPUT_ITEM_IMAGE,
    OUTPUT_ITEM_MESSAGE,
    OUTPUT_ITEM_PAGE_BREAK,
    OUTPUT_ITEM_TABLE,
    OUTPUT_ITEM_TEXT,
  };

const char *output_item_type_to_string (enum output_item_type);

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

    /* For OUTPUT_ITEM_GROUP, this is true if the group's subtree should
       be expanded in an outline view, false otherwise.

       For other kinds of output items, this is true to show the item's
       content, false to hide it.  The item's label is always shown in an
       outline view. */
    bool show;

    /* Information about the SPV file this output_item was read from.
       May be NULL. */
    struct spv_info *spv_info;

    enum output_item_type type;
    union
      {
        struct chart *chart;

        cairo_surface_t *image;

        struct
          {
            struct output_item **children;
            size_t n_children;
            size_t allocated_children;
          }
        group;

        struct msg *message;

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
void output_item_submit_children (struct output_item *);

const char *output_item_get_label (const struct output_item *);
void output_item_set_label (struct output_item *, const char *);
void output_item_set_label_nocopy (struct output_item *, char *);

void output_item_set_command_name (struct output_item *, const char *);
void output_item_set_command_name_nocopy (struct output_item *, char *);

char *output_item_get_subtype (const struct output_item *);

void output_item_add_spv_info (struct output_item *);

void output_item_dump (const struct output_item *, int indentation);

/* In-order traversal of a tree of output items. */

struct output_iterator_node
  {
    const struct output_item *group;
    size_t idx;
  };

struct output_iterator
  {
    const struct output_item *cur;
    struct output_iterator_node *nodes;
    size_t n, allocated;
  };
#define OUTPUT_ITERATOR_INIT(ITEM) { .cur = ITEM }

/* Iteration functions. */
void output_iterator_init (struct output_iterator *,
                           const struct output_item *);
void output_iterator_destroy (struct output_iterator *);
void output_iterator_next (struct output_iterator *);

/* Iteration helper macros. */
#define OUTPUT_ITEM_FOR_EACH(ITER, ROOT) \
  for (output_iterator_init (ITER, ROOT); (ITER)->cur; \
       output_iterator_next (ITER))
#define OUTPUT_ITEM_FOR_EACH_SKIP_ROOT(ITER, ROOT) \
  for (output_iterator_init (ITER, ROOT), output_iterator_next (ITER); \
       (ITER)->cur; output_iterator_next (ITER))

/* OUTPUT_ITEM_CHART. */
struct output_item *chart_item_create (struct chart *);

/* OUTPUT_ITEM_GROUP. */
struct output_item *group_item_create (const char *command_name,
                                       const char *label);
struct output_item *group_item_create_nocopy (char *command_name, char *label);

void group_item_add_child (struct output_item *parent,
                           struct output_item *child);

struct output_item *root_item_create (void);

struct output_item *group_item_clone_empty (const struct output_item *);

/* OUTPUT_ITEM_IMAGE. */

struct output_item *image_item_create (cairo_surface_t *);

/* OUTPUT_ITEM_MESSAGE. */

struct output_item *message_item_create (const struct msg *);

const struct msg *message_item_get_msg (const struct output_item *);

struct output_item *message_item_to_text_item (struct output_item *);

/* OUTPUT_ITEM_PAGE_BREAK. */

struct output_item *page_break_item_create (void);

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

/* An informational node for output items that were read from an .spv file.
   This is mostly for debugging and troubleshooting purposes with the
   pspp-output program. */
struct spv_info
  {
    /* The .spv file. */
    struct zip_reader *zip_reader;

    /* True if there was an error reading the output item (e.g. because of
       corruption or because PSPP doesn't understand the format.) */
    bool error;

    /* Zip member names.  All may be NULL. */
    char *structure_member;
    char *xml_member;
    char *bin_member;
    char *png_member;
  };

void spv_info_destroy (struct spv_info *);
struct spv_info *spv_info_clone (const struct spv_info *);
size_t spv_info_get_members (const struct spv_info *, const char **members,
                             size_t allocated_members);

#endif /* output/output-item.h */
