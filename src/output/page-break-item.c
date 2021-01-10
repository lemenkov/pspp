/* PSPP - a program for statistical analysis.
   Copyright (C) 2020 Free Software Foundation, Inc.

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

#include "output/page-break-item.h"

#include <stdlib.h>

#include "output/driver-provider.h"
#include "output/output-item-provider.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct page_break_item *
page_break_item_create (void)
{
  struct page_break_item *item = xmalloc (sizeof *item);
  *item = (struct page_break_item) {
    .output_item = OUTPUT_ITEM_INITIALIZER (&page_break_item_class),
  };
  return item;
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
page_break_item_submit (struct page_break_item *item)
{
  output_submit (&item->output_item);
}

static const char *
page_break_item_get_label (const struct output_item *output_item UNUSED)
{
  return _("Page Break");
}

static void
page_break_item_destroy (struct output_item *output_item)
{
  free (to_page_break_item (output_item));
}

const struct output_item_class page_break_item_class =
  {
    page_break_item_get_label,
    page_break_item_destroy,
  };
