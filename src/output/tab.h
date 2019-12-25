/* PSPP - a program for statistical analysis.
   Copyright (C) 1997, 1998, 1999, 2000, 2009, 2011, 2014 Free Software Foundation, Inc.

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

#ifndef OUTPUT_TAB_H
#define OUTPUT_TAB_H

/* Simple table class.

   This is a type of table (see output/table.h) whose content is composed
   manually by the code that generates it, by filling in cells one by one.
*/

#include "libpspp/compiler.h"
#include "output/table.h"
#include "data/format.h"

/* Rule masks. */
#define TAB_RULE_TYPE_MASK   7
#define TAB_RULE_TYPE_SHIFT  0
#define TAB_RULE_STYLE_MASK  (31 << TAB_RULE_STYLE_SHIFT)
#define TAB_RULE_STYLE_SHIFT 3

/* Tables. */
struct table *tab_create (int nc, int nr, int hl, int hr, int ht, int hb);

/* Rules. */
void tab_hline (struct table *, int style, int x1, int x2, int y);
void tab_vline (struct table *, int style, int x, int y1, int y2);
void tab_box (struct table *, int f_h, int f_v, int i_h, int i_v,
	      int x1, int y1, int x2, int y2);

/* Cells. */
void tab_text (struct table *, int c, int r, unsigned opt, const char *);
void tab_text_format (struct table *, int c, int r, unsigned opt,
                      const char *, ...)
  PRINTF_FORMAT (5, 6);

void tab_joint_text (struct table *, int x1, int y1, int x2, int y2,
		     unsigned opt, const char *);

struct footnote *tab_create_footnote (struct table *, size_t idx,
                                      const char *content, const char *marker,
                                      struct area_style *);
void tab_add_footnote (struct table *, int x, int y,
                       const struct footnote *);

void tab_add_style (struct table *, int x, int y,
                    const struct area_style *);

bool tab_cell_is_empty (const struct table *, int c, int r);

/* Simple output. */
void tab_output_text (int options, const char *string);
void tab_output_text_format (int options, const char *, ...)
     PRINTF_FORMAT (2, 3);

#endif /* output/tab.h */

