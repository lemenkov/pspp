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

#ifndef OUTPUT_SELECT_H
#define OUTPUT_SELECT_H 1

#include "output/output-item.h"

/* Selecting subsets of a tree of output items based on user-specified
   criteria.  pspp-output uses this; a future OMS or OUTPUT MODIFY command
   would use it too. */

/* Classifications for output items.  These only roughly correspond to the
   output item types; for example, "warnings" are a subset of text items.
   These classifications really  */
#define OUTPUT_CLASSES                                \
    OUTPUT_CLASS(CHARTS, "charts")                    \
    OUTPUT_CLASS(HEADINGS, "headings")                \
    OUTPUT_CLASS(LOGS, "logs")                        \
    OUTPUT_CLASS(MODELS, "models")                    \
    OUTPUT_CLASS(TABLES, "tables")                    \
    OUTPUT_CLASS(TEXTS, "texts")                      \
    OUTPUT_CLASS(TREES, "trees")                      \
    OUTPUT_CLASS(WARNINGS, "warnings")                \
    OUTPUT_CLASS(OUTLINEHEADERS, "outlineheaders")    \
    OUTPUT_CLASS(PAGETITLE, "pagetitle")              \
    OUTPUT_CLASS(NOTES, "notes")                      \
    OUTPUT_CLASS(UNKNOWN, "unknown")                  \
    OUTPUT_CLASS(OTHER, "other")
enum output_item_class
  {
#define OUTPUT_CLASS(ENUM, NAME) OUTPUT_CLASS_##ENUM,
    OUTPUT_CLASSES
#undef OUTPUT_CLASS
  };
enum
  {
#define OUTPUT_CLASS(ENUM, NAME) +1
    OUTPUT_N_CLASSES = OUTPUT_CLASSES
#undef OUTPUT_CLASS
};
#define OUTPUT_ALL_CLASSES ((1u << OUTPUT_N_CLASSES) - 1)

const char *output_item_class_to_string (enum output_item_class);
enum output_item_class output_item_class_from_string (const char *);

enum output_item_class output_item_classify (const struct output_item *);

/* Matching criteria for commands, subtypes, and labels.

   Each of the members is an array of strings.  A string that ends in '*'
   matches anything that begins with the rest of the string, otherwise a string
   requires an exact (case-insensitive) match. */
struct output_criteria_match
  {
    struct string_array commands;
    struct string_array subtypes;
    struct string_array labels;
  };

struct output_criteria
  {
    /* Include objects that are not visible? */
    bool include_hidden;

    /* If false, include all objects.
       If true, include only objects that have an error on loading. */
    bool error;

    /* Bit-mask of OUTPUT_CLASS_* for the classes to include. */
    unsigned int classes;

    /* Include all of the objects that match 'include' and do not match
       'exclude', except that objects are included by default if 'include' is
       empty. */
    struct output_criteria_match include;
    struct output_criteria_match exclude;

    /* Include objects under commands with indexes listed in COMMANDS.  Indexes
       are 1-based.  Everything is included if N_COMMANDS is 0. */
    size_t *commands;
    size_t n_commands;

    /* Include XML and binary member names that match (except that everything
       is included by default if empty). */
    struct string_array members;

    /* Include the objects with indexes listed in INSTANCES within each of the
       commands that are included.  Indexes are 1-based.  Index -1 means the
       last object within a command. */
    int *instances;
    size_t n_instances;
  };

#define OUTPUT_CRITERIA_INITIALIZER { .classes = OUTPUT_ALL_CLASSES }

struct output_item *output_select (struct output_item *,
                                   const struct output_criteria[],
                                   size_t n_criteria);

#endif /* output/select.h */
