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

#include <config.h>

#include "output/page-setup-item.h"

#include <stdlib.h>

#include "output/driver-provider.h"
#include "output/output-item-provider.h"

#include "gl/xalloc.h"

bool
page_paragraph_equals (const struct page_paragraph *a,
                       const struct page_paragraph *b)
{
  return (!a || !b ? a == b
          : !a->markup || !b->markup ? a->markup == b->markup
          : !strcmp (a->markup, b->markup) && a->halign == b->halign);
}

void
page_heading_copy (struct page_heading *dst, const struct page_heading *src)
{
  dst->n = src->n;
  dst->paragraphs = xmalloc (dst->n * sizeof *dst->paragraphs);
  for (size_t i = 0; i < dst->n; i++)
    {
      dst->paragraphs[i].markup = xstrdup (src->paragraphs[i].markup);
      dst->paragraphs[i].halign = src->paragraphs[i].halign;
    }
}

void
page_heading_uninit (struct page_heading *ph)
{
  if (!ph)
    return;

  for (size_t i = 0; i < ph->n; i++)
    free (ph->paragraphs[i].markup);
  free (ph->paragraphs);
}

bool
page_heading_equals (const struct page_heading *a,
                     const struct page_heading *b)
{
  if (!a || !b)
    return a == b;

  if (a->n != b->n)
    return false;

  for (size_t i = 0; i < a->n; i++)
    if (!page_paragraph_equals (&a->paragraphs[i], &b->paragraphs[i]))
      return false;

  return true;
}

struct page_setup *
page_setup_clone (const struct page_setup *old)
{
  struct page_setup *new = xmalloc (sizeof *new);
  *new = *old;
  for (int i = 0; i < 2; i++)
    page_heading_copy (&new->headings[i], &old->headings[i]);
  if (new->file_name)
    new->file_name = xstrdup (new->file_name);
  return new;
}

void
page_setup_destroy (struct page_setup *ps)
{
  if (ps)
    {
      for (int i = 0; i < 2; i++)
        page_heading_uninit (&ps->headings[i]);
      free (ps->file_name);
      free (ps);
    }
}

struct page_setup_item *
page_setup_item_create (const struct page_setup *ps)
{
  struct page_setup_item *item = xmalloc (sizeof *item);
  output_item_init (&item->output_item, &page_setup_item_class);
  item->page_setup = page_setup_clone (ps);
  return item;
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
page_setup_item_submit (struct page_setup_item *item)
{
  output_submit (&item->output_item);
}

static void
page_setup_item_destroy (struct output_item *output_item)
{
  struct page_setup_item *item = to_page_setup_item (output_item);
  page_setup_destroy (item->page_setup);
  free (item);
}

const struct output_item_class page_setup_item_class =
  {
    "page_setup",
    page_setup_item_destroy,
  };
