/* PSPP - a program for statistical analysis.
   Copyright (C) 2019 Free Software Foundation, Inc.

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

#ifndef OUTPUT_SPV_SELECT_H
#define OUTPUT_SPV_SELECT_H 1

#include "libpspp/string-array.h"

struct spv_item;
struct spv_reader;

/* Matching criteria for commands, subtypes, and labels.

   Each of the members is an array of strings.  A string that ends in '*'
   matches anything that begins with the rest of the string, otherwise a string
   requires an exact (case-insensitive) match. */
struct spv_criteria_match
  {
    struct string_array commands;
    struct string_array subtypes;
    struct string_array labels;
  };

struct spv_criteria
  {
    /* Include objects that are not visible? */
    bool include_hidden;

    /* If false, include all objects.
       If true, include only objects that have an error on loading. */
    bool error;

    /* Bit-mask of SPV_CLASS_* for the classes to include. */
    unsigned int classes;

    /* Include all of the objects that match 'include' and do not match
       'exclude', except that objects are included by default if 'include' is
       empty. */
    struct spv_criteria_match include;
    struct spv_criteria_match exclude;

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

#define SPV_CRITERIA_INITIALIZER { .classes = SPV_ALL_CLASSES }

void spv_select (const struct spv_reader *,
                 const struct spv_criteria[], size_t n_criteria,
                 struct spv_item ***items, size_t *n_items);

#endif /* output/spv/spv-select.h */
