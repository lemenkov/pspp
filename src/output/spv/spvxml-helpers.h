/* PSPP - a program for statistical analysis.
   Copyright (C) 2018 Free Software Foundation, Inc.

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

#ifndef SPVXML_HELPERS_H
#define SPVXML_HELPERS_H 1

#include <stdbool.h>
#include <stddef.h>
#include <libxml/xmlreader.h>
#include "libpspp/compiler.h"
#include "libpspp/hmap.h"

struct spvxml_node;

struct spvxml_context
  {
    struct hmap id_map;

    char *error;
    bool hard_error;
  };

#define SPVXML_CONTEXT_INIT(CONTEXT) \
  { HMAP_INITIALIZER ((CONTEXT).id_map), NULL, false }

char *spvxml_context_finish (struct spvxml_context *, struct spvxml_node *root)
  WARN_UNUSED_RESULT;

struct spvxml_node_context
  {
    struct spvxml_context *up;
    const xmlNode *parent;

    struct spvxml_attribute *attrs;
    size_t n_attrs;
  };

void spvxml_node_context_uninit (struct spvxml_node_context *);

struct spvxml_node_class
  {
    const char *name;
    void (*spvxml_node_free) (struct spvxml_node *);
    void (*spvxml_node_collect_ids) (struct spvxml_context *,
                                     struct spvxml_node *);
    void (*spvxml_node_resolve_refs) (struct spvxml_context *,
                                      struct spvxml_node *);
  };

struct spvxml_node
  {
    struct hmap_node id_node;
    char *id;

    const struct spvxml_node_class *class_;
    const xmlNode *raw;
  };

void spvxml_node_collect_id (struct spvxml_context *, struct spvxml_node *);
struct spvxml_node *spvxml_node_resolve_ref (
  struct spvxml_context *, const xmlNode *, const char *attr_name,
  const struct spvxml_node_class *const *, size_t n);

/* Attribute parsing. */
struct spvxml_attribute
  {
    const char *name;
    bool required;
    char *value;
  };

void spvxml_parse_attributes (struct spvxml_node_context *);
void spvxml_attr_error (struct spvxml_node_context *, const char *format, ...)
  PRINTF_FORMAT (2, 3);

struct spvxml_enum
  {
    const char *name;
    int value;
  };

int spvxml_attr_parse_enum (struct spvxml_node_context *,
                            const struct spvxml_attribute *,
                            const struct spvxml_enum[]);
int spvxml_attr_parse_bool (struct spvxml_node_context *,
                            const struct spvxml_attribute *);
bool spvxml_attr_parse_fixed (struct spvxml_node_context *,
                             const struct spvxml_attribute *,
                             const char *attr_value);
int spvxml_attr_parse_int (struct spvxml_node_context *,
                           const struct spvxml_attribute *);
int spvxml_attr_parse_color (struct spvxml_node_context *,
                             const struct spvxml_attribute *);
double spvxml_attr_parse_real (struct spvxml_node_context *,
                               const struct spvxml_attribute *);
double spvxml_attr_parse_dimension (struct spvxml_node_context *,
                                    const struct spvxml_attribute *);
struct spvxml_node *spvxml_attr_parse_ref (struct spvxml_node_context *,
                                           const struct spvxml_attribute *);

/* Content parsing. */

void spvxml_content_error (struct spvxml_node_context *, const xmlNode *,
                           const char *format, ...)
  PRINTF_FORMAT (3, 4);
bool spvxml_content_parse_element (struct spvxml_node_context *, xmlNode **,
                                   const char *elem_name, xmlNode **);
bool spvxml_content_parse_text (struct spvxml_node_context *, xmlNode **,
                                char **textp);
void spvxml_content_parse_etc (xmlNode **);
bool spvxml_content_parse_end (struct spvxml_node_context *, xmlNode *);

#endif /* output/spv/spvxml-helpers.h */
