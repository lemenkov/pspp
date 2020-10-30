/* PSPP - a program for statistical analysis.
   Copyright (C) 2017 Free Software Foundation, Inc.

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

#ifndef OUTPUT_SPV_H
#define OUTPUT_SPV_H 1

/* SPSS Viewer (SPV) file reader.

   An SPV file, represented as struct spv_reader, contains a number of
   top-level headings, each of which recursively contains other headings and
   tables.  Here, we model a heading, text, table, or other element as an
   "item", and a an SPV file as a single root item that contains each of the
   top-level headings as a child item.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "libpspp/compiler.h"

struct fmt_spec;
struct pivot_table;
struct spv_data;
struct spv_reader;
struct spvlb_table;
struct string;
struct _xmlDoc;

/* SPV files. */

char *spv_open (const char *filename, struct spv_reader **) WARN_UNUSED_RESULT;
void spv_close (struct spv_reader *);

char *spv_detect (const char *filename) WARN_UNUSED_RESULT;

const char *spv_get_errors (const struct spv_reader *);
void spv_clear_errors (struct spv_reader *);

struct spv_item *spv_get_root (const struct spv_reader *);
void spv_item_dump (const struct spv_item *, int indentation);

const struct page_setup *spv_get_page_setup (const struct spv_reader *);

/* Items.

   An spv_item represents of the elements that can occur in an SPV file.  Items
   form a tree because "heading" items can have an arbitrary number of child
   items, which in turn may also be headings.  The root item, that is, the item
   returned by spv_get_root(), is always a heading. */

enum spv_item_type
  {
    SPV_ITEM_HEADING,
    SPV_ITEM_TEXT,
    SPV_ITEM_TABLE,
    SPV_ITEM_GRAPH,
    SPV_ITEM_MODEL,
    SPV_ITEM_OBJECT,
    SPV_ITEM_TREE,
  };

const char *spv_item_type_to_string (enum spv_item_type);

#define SPV_CLASSES                                \
    SPV_CLASS(CHARTS, "charts")                    \
    SPV_CLASS(HEADINGS, "headings")                \
    SPV_CLASS(LOGS, "logs")                        \
    SPV_CLASS(MODELS, "models")                    \
    SPV_CLASS(TABLES, "tables")                    \
    SPV_CLASS(TEXTS, "texts")                      \
    SPV_CLASS(TREES, "trees")                      \
    SPV_CLASS(WARNINGS, "warnings")                \
    SPV_CLASS(OUTLINEHEADERS, "outlineheaders")    \
    SPV_CLASS(PAGETITLE, "pagetitle")              \
    SPV_CLASS(NOTES, "notes")                      \
    SPV_CLASS(UNKNOWN, "unknown")                  \
    SPV_CLASS(OTHER, "other")
enum spv_item_class
  {
#define SPV_CLASS(ENUM, NAME) SPV_CLASS_##ENUM,
    SPV_CLASSES
#undef SPV_CLASS
  };
enum
  {
#define SPV_CLASS(ENUM, NAME) +1
    SPV_N_CLASSES = SPV_CLASSES
#undef SPV_CLASS
};
#define SPV_ALL_CLASSES ((1u << SPV_N_CLASSES) - 1)

const char *spv_item_class_to_string (enum spv_item_class);
enum spv_item_class spv_item_class_from_string (const char *);

struct spv_item
  {
    struct spv_reader *spv;
    struct spv_item *parent;
    size_t parent_idx;         /* item->parent->children[parent_idx] == item */

    bool error;

    char *structure_member;

    enum spv_item_type type;
    char *label;
    char *command_id;           /* Unique command identifier. */

    /* Whether the item is visible.
       For SPV_ITEM_HEADING, false indicates that the item is collapsed.
       For SPV_ITEM_TABLE, false indicates that the item is not shown. */
    bool visible;

    /* SPV_ITEM_HEADING only. */
    struct spv_item **children;
    size_t n_children, allocated_children;

    /* SPV_ITEM_TABLE only. */
    struct pivot_table *table;    /* NULL if not yet loaded. */
    struct pivot_table_look *table_look;
    char *bin_member;
    char *xml_member;
    char *subtype;

    /* SPV_ITEM_TEXT only.  */
    struct pivot_value *text;

    /* SPV_ITEM_OBJECT only. */
    char *object_type;
    char *uri;
  };

void spv_item_format_path (const struct spv_item *, struct string *);

void spv_item_load (const struct spv_item *);

enum spv_item_type spv_item_get_type (const struct spv_item *);
enum spv_item_class spv_item_get_class (const struct spv_item *);

const char *spv_item_get_label (const struct spv_item *);

bool spv_item_is_heading (const struct spv_item *);
size_t spv_item_get_n_children (const struct spv_item *);
struct spv_item *spv_item_get_child (const struct spv_item *, size_t idx);

bool spv_item_is_table (const struct spv_item *);
struct pivot_table *spv_item_get_table (const struct spv_item *);

bool spv_item_is_text (const struct spv_item *);
const struct pivot_value *spv_item_get_text (const struct spv_item *);

bool spv_item_is_visible (const struct spv_item *);

#define SPV_ITEM_FOR_EACH(ITER, ROOT) \
  for ((ITER) = (ROOT); (ITER) != NULL; (ITER) = spv_item_next(ITER))
#define SPV_ITEM_FOR_EACH_SKIP_ROOT(ITER, ROOT) \
  for ((ITER) = (ROOT); ((ITER) = spv_item_next(ITER)) != NULL;)
struct spv_item *spv_item_next (const struct spv_item *);

const struct spv_item *spv_item_get_parent (const struct spv_item *);
size_t spv_item_get_level (const struct spv_item *);

const char *spv_item_get_member_name (const struct spv_item *);
const char *spv_item_get_command_id (const struct spv_item *);
const char *spv_item_get_subtype (const struct spv_item *);

char *spv_item_get_structure (const struct spv_item *, struct _xmlDoc **)
  WARN_UNUSED_RESULT;

bool spv_item_is_light_table (const struct spv_item *);
char *spv_item_get_light_table (const struct spv_item *,
                                    struct spvlb_table **)
  WARN_UNUSED_RESULT;
char *spv_item_get_raw_light_table (const struct spv_item *,
                                    void **data, size_t *size)
  WARN_UNUSED_RESULT;

bool spv_item_is_legacy_table (const struct spv_item *);
char *spv_item_get_raw_legacy_data (const struct spv_item *item,
                                    void **data, size_t *size)
  WARN_UNUSED_RESULT;
char *spv_item_get_legacy_data (const struct spv_item *, struct spv_data *)
  WARN_UNUSED_RESULT;
char *spv_item_get_legacy_table (const struct spv_item *, struct _xmlDoc **)
  WARN_UNUSED_RESULT;

void spv_item_set_table_look (struct spv_item *,
                              const struct pivot_table_look *);

char *spv_decode_fmt_spec (uint32_t u32, struct fmt_spec *) WARN_UNUSED_RESULT;

#endif /* output/spv/spv.h */
