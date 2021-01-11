/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2014, 2020 Free Software Foundation, Inc.

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

#ifndef OUTPUT_CAIRO_PAGER_H
#define OUTPUT_CAIRO_PAGER_H 1

#include <stdbool.h>

/* Cairo output driver paginater. */

#include <cairo/cairo.h>
#include <pango/pango-font.h>
#include "output/cairo-fsm.h"
#include "output/page-setup.h"
#include "output/table.h"

struct xr_page_style
  {
    int ref_cnt;

    int margins[TABLE_N_AXES][2];    /* Margins. */

    struct page_heading headings[2]; /* Top and bottom headings. */

    int initial_page_number;

    /* Whether to include an outline in PDF output.  (The only reason I know to
       omit it is to avoid a Cairo bug that caused crashes in some cases.) */
    bool include_outline;
  };
struct xr_page_style *xr_page_style_ref (const struct xr_page_style *);
struct xr_page_style *xr_page_style_unshare (struct xr_page_style *);
void xr_page_style_unref (struct xr_page_style *);
bool xr_page_style_equals (const struct xr_page_style *,
                           const struct xr_page_style *);
struct xr_page_style *xr_page_style_default (void);

static inline int
xr_page_style_paper_size (const struct xr_page_style *ps,
                          const struct xr_fsm_style *fs, enum table_axis a)
{
  return fs->size[a] + ps->margins[a][0] + ps->margins[a][1];
}

struct xr_pager *xr_pager_create (const struct xr_page_style *,
                                  const struct xr_fsm_style *);
void xr_pager_destroy (struct xr_pager *);

bool xr_pager_has_item (const struct xr_pager *);
void xr_pager_add_item (struct xr_pager *, const struct output_item *);

bool xr_pager_has_page (const struct xr_pager *);
void xr_pager_add_page (struct xr_pager *, cairo_t *);
void xr_pager_finish_page (struct xr_pager *);
bool xr_pager_needs_new_page (struct xr_pager *);

#endif /* output/cairo-pager.h */
