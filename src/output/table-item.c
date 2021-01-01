/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011, 2014 Free Software Foundation, Inc.

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

#include "output/table-provider.h"

#include <assert.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "output/driver.h"
#include "output/output-item-provider.h"
#include "output/pivot-table.h"
#include "output/table-item.h"
#include "output/table-provider.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Initializes ITEM as a table item for rendering PT.  Takes ownership of
   PT. */
struct table_item *
table_item_create (struct pivot_table *pt)
{
  pivot_table_assign_label_depth (pt);

  struct table_item *item = xmalloc (sizeof *item);
  *item = (struct table_item) {
    .output_item = OUTPUT_ITEM_INITIALIZER (&table_item_class),
    .pt = pivot_table_unshare (pt),
  };
  return item;
}

/* Submits TABLE_ITEM to the configured output drivers, and transfers ownership
   to the output subsystem. */
void
table_item_submit (struct table_item *table_item)
{
  output_submit (&table_item->output_item);
}

static const char *
table_item_get_label (const struct output_item *output_item)
{
  struct table_item *item = to_table_item (output_item);

  if (!item->cached_label)
    {
      if (!item->pt->title)
        return _("Table");

      item->cached_label = pivot_value_to_string (item->pt->title, item->pt);
    }
  return item->cached_label;
}

static void
table_item_destroy (struct output_item *output_item)
{
  struct table_item *item = to_table_item (output_item);
  pivot_table_unref (item->pt);
  free (item->cached_label);
  free (item);
}

const struct output_item_class table_item_class =
  {
    table_item_get_label,
    table_item_destroy,
  };
