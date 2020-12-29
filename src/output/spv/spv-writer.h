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

#ifndef OUTPUT_SPV_WRITER_H
#define OUTPUT_SPV_WRITER_H 1

struct page_setup;
struct pivot_table;
struct spv_writer;
struct text_item;

#include "libpspp/compiler.h"

char *spv_writer_open (const char *filename, struct spv_writer **)
  WARN_UNUSED_RESULT;
char *spv_writer_close (struct spv_writer *) WARN_UNUSED_RESULT;

void spv_writer_set_page_setup (struct spv_writer *,
                                const struct page_setup *);

void spv_writer_open_heading (struct spv_writer *, const char *command_id,
                              const char *label);
void spv_writer_close_heading (struct spv_writer *);

void spv_writer_put_text (struct spv_writer *, const struct text_item *,
                          const char *command_id);
void spv_writer_put_table (struct spv_writer *, const struct pivot_table *);

void spv_writer_eject_page (struct spv_writer *);

#endif /* output/spv/spv-writer.h */
