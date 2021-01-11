/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2018 Free Software Foundation, Inc.

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

#ifndef OUTPUT_PAGE_SETUP_H
#define OUTPUT_PAGE_SETUP_H 1

/* Page setup.

   Configures the paper size, margins, header and footer, and other attributes
   used for printing. */

#include <stdbool.h>
#include "table.h"

enum page_orientation
  {
    PAGE_PORTRAIT,
    PAGE_LANDSCAPE
  };

enum page_chart_size
  {
    PAGE_CHART_AS_IS,
    PAGE_CHART_FULL_HEIGHT,
    PAGE_CHART_HALF_HEIGHT,
    PAGE_CHART_QUARTER_HEIGHT,
  };

struct page_paragraph
  {
    char *markup;
    enum table_halign halign;
  };

bool page_paragraph_equals (const struct page_paragraph *,
                            const struct page_paragraph *);

struct page_heading
  {
    struct page_paragraph *paragraphs;
    size_t n;
  };

void page_heading_copy (struct page_heading *, const struct page_heading *);
void page_heading_uninit (struct page_heading *);
bool page_heading_equals (const struct page_heading *,
                          const struct page_heading *);

struct page_setup
  {
    int initial_page_number;
    double paper[TABLE_N_AXES];         /* Paper size in inches. */
    double margins[TABLE_N_AXES][2];    /* In inches. */
    enum page_orientation orientation;
    double object_spacing;      /* Space between objects, in inches. */
    enum page_chart_size chart_size;
    struct page_heading headings[2]; /* Header and footer. */
    char *file_name;
  };

#define PAGE_SETUP_INITIALIZER                                          \
    {                                                                   \
        .initial_page_number = 1,                                       \
        .paper = { [TABLE_HORZ] = 8.5, [TABLE_VERT] = 11.0 },           \
        .margins = { { 0.5, 0.5 }, { 0.5, 0.5 } },                      \
        .orientation = PAGE_PORTRAIT,                                   \
        .object_spacing = 12.0 / 72.0,                                  \
        .chart_size = PAGE_CHART_AS_IS,                                 \
    }

struct page_setup *page_setup_clone (const struct page_setup *);
void page_setup_destroy (struct page_setup *);

#endif /* output/page-setup.h */
