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

/* SPSS Viewer (SPV) file reader. */

#include <stdbool.h>
#include <stdint.h>
#include <libxml/tree.h>

#include "libpspp/compiler.h"

struct fmt_spec;
struct output_item;
struct page_setup;
struct spv_data;
struct spvlb_table;
struct zip_reader;

/* Main functions. */
char *spv_read (const char *filename, struct output_item **,
                struct page_setup **) WARN_UNUSED_RESULT;
char *spv_detect (const char *filename) WARN_UNUSED_RESULT;

/* Debugging functions. */
char *spv_read_light_table (struct zip_reader *, const char *bin_member,
                            struct spvlb_table **) WARN_UNUSED_RESULT;
char *spv_read_legacy_data (struct zip_reader *, const char *bin_member,
                            struct spv_data *) WARN_UNUSED_RESULT;
char *spv_read_xml_member (struct zip_reader *, const char *member_name,
                           bool keep_blanks, const char *root_element_name,
                           xmlDoc **) WARN_UNUSED_RESULT;

/* Helpers. */
char *spv_decode_fmt_spec (uint32_t u32, struct fmt_spec *) WARN_UNUSED_RESULT;

#endif /* output/spv/spv.h */
