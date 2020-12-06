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

#include "output/page-eject-item.h"

#include <stdlib.h>

#include "output/driver-provider.h"
#include "output/output-item-provider.h"

#include "gl/xalloc.h"

struct page_eject_item *
page_eject_item_create (void)
{
  struct page_eject_item *item = xmalloc (sizeof *item);
  output_item_init (&item->output_item, &page_eject_item_class);
  return item;
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
page_eject_item_submit (struct page_eject_item *item)
{
  output_submit (&item->output_item);
}

static void
page_eject_item_destroy (struct output_item *output_item)
{
  free (to_page_eject_item (output_item));
}

const struct output_item_class page_eject_item_class =
  {
    "page_eject",
    page_eject_item_destroy,
  };
