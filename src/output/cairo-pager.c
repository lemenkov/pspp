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

#include <config.h>

#include "output/cairo-pager.h"

#include <math.h>
#include <cairo/cairo-pdf.h>
#include <pango/pango-layout.h>
#include <pango/pangocairo.h>

#include "output/chart-item.h"
#include "output/driver-provider.h"
#include "output/group-item.h"
#include "output/message-item.h"
#include "output/page-eject-item.h"
#include "output/page-setup-item.h"
#include "output/table-item.h"
#include "output/text-item.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* This file uses TABLE_HORZ and TABLE_VERT enough to warrant abbreviating. */
#define H TABLE_HORZ
#define V TABLE_VERT

struct xr_page_style *
xr_page_style_ref (const struct xr_page_style *ps_)
{
  struct xr_page_style *ps = CONST_CAST (struct xr_page_style *, ps_);
  assert (ps->ref_cnt > 0);
  ps->ref_cnt++;
  return ps;
}

struct xr_page_style *
xr_page_style_unshare (struct xr_page_style *old)
{
  assert (old->ref_cnt > 0);
  if (old->ref_cnt == 1)
    return old;

  xr_page_style_unref (old);

  struct xr_page_style *new = xmemdup (old, sizeof *old);
  new->ref_cnt = 1;
  for (int i = 0; i < 2; i++)
    page_heading_copy (&new->headings[i], &old->headings[i]);

  return new;
}

void
xr_page_style_unref (struct xr_page_style *ps)
{
  if (ps)
    {
      assert (ps->ref_cnt > 0);
      if (!--ps->ref_cnt)
        {
          for (int i = 0; i < 2; i++)
            page_heading_uninit (&ps->headings[i]);
          free (ps);
        }
    }
}

bool
xr_page_style_equals (const struct xr_page_style *a,
                      const struct xr_page_style *b)
{
  for (int i = 0; i < TABLE_N_AXES; i++)
    for (int j = 0; j < 2; j++)
      if (a->margins[i][j] != b->margins[i][j])
        return false;

  for (int i = 0; i < 2; i++)
    if (!page_heading_equals (&a->headings[i], &b->headings[i]))
      return false;

  return a->initial_page_number == b->initial_page_number;
}

struct xr_pager
  {
    struct xr_page_style *page_style;
    struct xr_fsm_style *fsm_style;
    int page_index;
    int heading_heights[2];

    /* Current output item. */
    struct xr_fsm *fsm;
    struct output_item *item;
    int slice_idx;

    /* Grouping, for constructing the outline for PDFs.

       The 'group_ids' were returned by cairo_pdf_surface_add_outline() and
       represent the groups within which upcoming output is nested.  The
       'group_opens' will be passed to cairo_pdf_surface_add_outline() when the
       next item is rendered (we defer it so that the location associated with
       the outline item can be the first object actually output in it). */
    int *group_ids;
    size_t n_group_ids, allocated_group_ids;
    struct group_open_item **group_opens;
    size_t n_opens, allocated_opens;

    /* Current output page. */
    cairo_t *cr;
    int y;
  };

static void xr_pager_run (struct xr_pager *);

/* Conversions to and from points. */
static double
xr_to_pt (int x)
{
  return x / (double) XR_POINT;
}

static int
pango_to_xr (int pango)
{
  return (XR_POINT != PANGO_SCALE
          ? ceil (pango * (1. * XR_POINT / PANGO_SCALE))
          : pango);
}

static int
xr_to_pango (int xr)
{
  return (XR_POINT != PANGO_SCALE
          ? ceil (xr * (1. / XR_POINT * PANGO_SCALE))
          : xr);
}

static int
get_layout_height (PangoLayout *layout)
{
  int w, h;
  pango_layout_get_size (layout, &w, &h);
  return h;
}

static int
xr_render_page_heading (cairo_t *cairo, const PangoFontDescription *font,
                        const struct page_heading *ph, int page_number,
                        int width, int base_y, double font_resolution)
{
  PangoContext *context = pango_cairo_create_context (cairo);
  pango_cairo_context_set_resolution (context, font_resolution);
  PangoLayout *layout = pango_layout_new (context);
  g_object_unref (context);

  pango_layout_set_font_description (layout, font);

  int y = 0;
  for (size_t i = 0; i < ph->n; i++)
    {
      const struct page_paragraph *pp = &ph->paragraphs[i];

      char *markup = output_driver_substitute_heading_vars (pp->markup,
                                                            page_number);
      pango_layout_set_markup (layout, markup, -1);
      free (markup);

      pango_layout_set_alignment (
        layout,
        (pp->halign == TABLE_HALIGN_LEFT ? PANGO_ALIGN_LEFT
         : pp->halign == TABLE_HALIGN_CENTER ? PANGO_ALIGN_CENTER
         : pp->halign == TABLE_HALIGN_MIXED ? PANGO_ALIGN_LEFT
         : PANGO_ALIGN_RIGHT));
      pango_layout_set_width (layout, xr_to_pango (width));

      cairo_save (cairo);
      cairo_translate (cairo, 0, xr_to_pt (y + base_y));
      pango_cairo_show_layout (cairo, layout);
      cairo_restore (cairo);

      y += pango_to_xr (get_layout_height (layout));
    }

  g_object_unref (G_OBJECT (layout));

  return y;
}

static void
xr_measure_headings (const struct xr_page_style *ps,
                     const struct xr_fsm_style *fs,
                     int heading_heights[2])
{
  cairo_surface_t *surface = cairo_recording_surface_create (
    CAIRO_CONTENT_COLOR, NULL);
  cairo_t *cairo = cairo_create (surface);
  for (int i = 0; i < 2; i++)
    {
      int *h = &heading_heights[i];
      *h = xr_render_page_heading (cairo, fs->font,
                                   &ps->headings[i], -1, fs->size[H], 0,
                                   fs->font_resolution);
      if (*h)
        *h += fs->object_spacing;
    }
  cairo_destroy (cairo);
  cairo_surface_destroy (surface);
}

struct xr_pager *
xr_pager_create (const struct xr_page_style *ps_,
                 const struct xr_fsm_style *fs_)
{
  struct xr_page_style *ps = xr_page_style_ref (ps_);
  struct xr_fsm_style *fs = xr_fsm_style_ref (fs_);

  int heading_heights[2];
  xr_measure_headings (ps, fs, heading_heights);
  int total = heading_heights[0] + heading_heights[1];
  if (total > 0 && total < fs->size[V])
    {
      fs = xr_fsm_style_unshare (fs);
      ps = xr_page_style_unshare (ps);

      for (int i = 0; i < 2; i++)
        ps->margins[V][i] += heading_heights[i];
      fs->size[V] -= total;
    }

  struct xr_pager *p = xmalloc (sizeof *p);
  *p = (struct xr_pager) { .page_style = ps, .fsm_style = fs };
  return p;
}

void
xr_pager_destroy (struct xr_pager *p)
{
  if (p)
    {
      free (p->group_ids);
      for (size_t i = 0; i < p->n_opens; i++)
        group_open_item_unref (p->group_opens[i]);
      free (p->group_opens);

      xr_page_style_unref (p->page_style);
      xr_fsm_style_unref (p->fsm_style);

      xr_fsm_destroy (p->fsm);
      output_item_unref (p->item);

      if (p->cr)
        {
          cairo_restore (p->cr);
          cairo_destroy (p->cr);
        }
      free (p);
    }
}

bool
xr_pager_has_item (const struct xr_pager *p)
{
  return p->item != NULL;
}

void
xr_pager_add_item (struct xr_pager *p, const struct output_item *item)
{
  assert (!p->item);
  p->item = output_item_ref (item);
  p->slice_idx = 0;
  xr_pager_run (p);
}

bool
xr_pager_has_page (const struct xr_pager *p)
{
  return p->cr;
}

void
xr_pager_add_page (struct xr_pager *p, cairo_t *cr)
{
  assert (!p->cr);
  cairo_save (cr);
  p->cr = cr;
  p->y = 0;

  const struct xr_fsm_style *fs = p->fsm_style;
  const struct xr_page_style *ps = p->page_style;
  cairo_translate (cr,
                   xr_to_pt (ps->margins[H][0]),
                   xr_to_pt (ps->margins[V][0]));

  int page_number = p->page_index++ + ps->initial_page_number;
  if (p->heading_heights[0])
    xr_render_page_heading (cr, fs->font, &ps->headings[0], page_number,
                            fs->size[H], -p->heading_heights[0],
                            fs->font_resolution);

  if (p->heading_heights[1])
    xr_render_page_heading (cr, fs->font, &ps->headings[1], page_number,
                            fs->size[H], fs->size[V] + fs->object_spacing,
                            fs->font_resolution);

  cairo_surface_t *surface = cairo_get_target (cr);
  if (cairo_surface_get_type (surface) == CAIRO_SURFACE_TYPE_PDF)
    {
      char *page_label = xasprintf ("%d", page_number);
      cairo_pdf_surface_set_page_label (surface, page_label);
      free (page_label);
    }

  xr_pager_run (p);
}

void
xr_pager_finish_page (struct xr_pager *p)
{
  if (p->cr)
    {
      cairo_restore (p->cr);
      cairo_destroy (p->cr);
      p->cr = NULL;
    }
}

bool
xr_pager_needs_new_page (struct xr_pager *p)
{
  if (p->item && (!p->cr || p->y >= p->fsm_style->size[V]))
    {
      xr_pager_finish_page (p);
      return true;
    }
  else
    return false;
}

static int
add_outline (cairo_t *cr, int parent_id,
             const char *utf8, const char *link_attribs,
             cairo_pdf_outline_flags_t flags)
{
  cairo_surface_t *surface = cairo_get_target (cr);
  return (cairo_surface_get_type (surface) == CAIRO_SURFACE_TYPE_PDF
          ? cairo_pdf_surface_add_outline (surface, parent_id,
                                           utf8, link_attribs, flags)
          : 0);
}

static void
xr_pager_run (struct xr_pager *p)
{
  if (p->item && p->cr && p->y < p->fsm_style->size[V])
    {
      if (!p->fsm)
        {
          if (is_group_open_item (p->item))
            {
              if (p->n_opens >= p->allocated_opens)
                p->group_opens = x2nrealloc (p->group_opens,
                                             &p->allocated_opens,
                                             sizeof p->group_opens);
              p->group_opens[p->n_opens++] = group_open_item_ref (
                to_group_open_item (p->item));
            }
          else if (is_group_close_item (p->item))
            {
              if (p->n_opens)
                group_open_item_unref (p->group_opens[--p->n_opens]);
              else if (p->n_group_ids)
                p->n_group_ids--;
              else
                {
                  /* Something wrong! */
                }
            }

          p->fsm = xr_fsm_create_for_printing (p->item, p->fsm_style, p->cr);
          if (!p->fsm)
            {
              output_item_unref (p->item);
              p->item = NULL;

              return;
            }
        }

      for (;;)
        {
          char *dest_name = NULL;
          if (p->page_style->include_outline)
            {
              static int counter = 0;
              dest_name = xasprintf ("dest%d", counter++);
              char *attrs = xasprintf ("name='%s'", dest_name);
              cairo_tag_begin (p->cr, CAIRO_TAG_DEST, attrs);
              free (attrs);
            }

          int spacing = p->fsm_style->object_spacing;
          int chunk = xr_fsm_draw_slice (p->fsm, p->cr,
                                         p->fsm_style->size[V] - p->y);
          p->y += chunk + spacing;
          cairo_translate (p->cr, 0, xr_to_pt (chunk + spacing));

          if (p->page_style->include_outline)
            {
              cairo_tag_end (p->cr, CAIRO_TAG_DEST);

              if (chunk && p->slice_idx++ == 0)
                {
                  char *attrs = xasprintf ("dest='%s'", dest_name);

                  int parent_group_id = (p->n_group_ids
                                         ? p->group_ids[p->n_group_ids - 1]
                                         : CAIRO_PDF_OUTLINE_ROOT);
                  for (size_t i = 0; i < p->n_opens; i++)
                    {
                      parent_group_id = add_outline (
                        p->cr, parent_group_id,
                        p->group_opens[i]->command_name, attrs,
                        CAIRO_PDF_OUTLINE_FLAG_OPEN);
                      group_open_item_unref (p->group_opens[i]);

                      if (p->n_group_ids >= p->allocated_group_ids)
                        p->group_ids = x2nrealloc (p->group_ids,
                                                   &p->allocated_group_ids,
                                                   sizeof *p->group_ids);
                      p->group_ids[p->n_group_ids++] = parent_group_id;
                    }
                  p->n_opens = 0;

                  add_outline (p->cr, parent_group_id,
                               output_item_get_label (p->item), attrs, 0);
                  free (attrs);
                }
              free (dest_name);
            }

          if (xr_fsm_is_empty (p->fsm))
            {
              xr_fsm_destroy (p->fsm);
              p->fsm = NULL;
              output_item_unref (p->item);
              p->item = NULL;
              return;
            }
          else if (!chunk)
            {
              assert (p->y > 0);
              p->y = INT_MAX;
              return;
            }
        }
    }
}
