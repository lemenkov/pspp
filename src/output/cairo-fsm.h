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

#ifndef OUTPUT_CAIRO_FSM_H
#define OUTPUT_CAIRO_FSM_H 1

#include <stdbool.h>

#include <cairo/cairo.h>
#include <pango/pango-font.h>
#include "output/table.h"

struct xr_fsm;
struct output_item;

/* The unit used for internal measurements is inch/(72 * XR_POINT).
   (Thus, XR_POINT units represent one point.) */
#define XR_POINT PANGO_SCALE

struct xr_fsm_style
  {
    int ref_cnt;

    int size[TABLE_N_AXES];      /* Page size. */
    int min_break[TABLE_N_AXES]; /* Minimum cell size to allow breaking. */
    PangoFontDescription *font;
    struct cell_color fg;
    bool use_system_colors;

    int object_spacing;

    /* Resolution, in units per inch, used for measuring font "points".  If
       this is 72.0, for example, then 1pt = 1 device unit, which is
       appropriate for rendering to a surface created by
       cairo_ps_surface_create() with its default transformation matrix of 72
       units/inch.  For a screen-based surface, it is traditionally 96.0. */
    double font_resolution;
  };
struct xr_fsm_style *xr_fsm_style_ref (const struct xr_fsm_style *);
struct xr_fsm_style *xr_fsm_style_unshare (struct xr_fsm_style *);
void xr_fsm_style_unref (struct xr_fsm_style *);
bool xr_fsm_style_equals (const struct xr_fsm_style *,
                          const struct xr_fsm_style *);

/* Interface used for rendering output items in a single on-screen region. */
struct xr_fsm *xr_fsm_create_for_scrolling (const struct output_item *,
                                            const struct xr_fsm_style *,
                                            cairo_t *);
void xr_fsm_measure (struct xr_fsm *, cairo_t *, int *w, int *h);
void xr_fsm_draw_all (struct xr_fsm *, cairo_t *);
void xr_fsm_draw_region (struct xr_fsm *, cairo_t *,
                         int x, int y, int w, int h);

/* Interface used for rendering output items to a series of printed pages. */
struct xr_fsm *xr_fsm_create_for_printing (const struct output_item *,
                                           const struct xr_fsm_style *,
                                           cairo_t *);
void xr_fsm_destroy (struct xr_fsm *);
int xr_fsm_draw_slice (struct xr_fsm *, cairo_t *, int space);
bool xr_fsm_is_empty (const struct xr_fsm *);

#endif /* output/cairo-fsm.h */
