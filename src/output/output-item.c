/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2011 Free Software Foundation, Inc.

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

#include "output/output-item-provider.h"

#include <assert.h>
#include <stdlib.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"

#include "gl/xalloc.h"

#include "gettext.h"

/* Increases ITEM's reference count, indicating that it has an additional
   owner.  An output item that is shared among multiple owners must not be
   modified. */
struct output_item *
output_item_ref (const struct output_item *item_)
{
  struct output_item *item = CONST_CAST (struct output_item *, item_);
  item->ref_cnt++;
  return item;
}

/* Decreases ITEM's reference count, indicating that it has one fewer owner.
   If ITEM no longer has any owners, it is freed. */
void
output_item_unref (struct output_item *item)
{
  if (item != NULL)
    {
      assert (item->ref_cnt > 0);
      if (--item->ref_cnt == 0)
        {
          char *label = item->label;
          item->class->destroy (item);
          free (label);
        }
    }
}

/* Returns true if ITEM has more than one owner.  An output item that is shared
   among multiple owners must not be modified. */
bool
output_item_is_shared (const struct output_item *item)
{
  return item->ref_cnt > 1;
}

/* Returns the label for ITEM, which the caller must not modify or free. */
const char *
output_item_get_label (const struct output_item *item)
{
  return item->label ? item->label : item->class->get_label (item);
}

/* Sets the label for ITEM to LABEL.  The caller retains ownership of LABEL.
   If LABEL is nonnull, it overrides any previously set label and the default
   label.  If LABEL is null, ITEM will now use its default label.

   ITEM must not be shared. */
void
output_item_set_label (struct output_item *item, const char *label)
{
  output_item_set_label_nocopy (item, label ? xstrdup (label) : NULL);
}

/* Sets the label for ITEM to LABEL, transferring ownership of LABEL to ITEM.
   If LABEL is nonnull, it overrides any previously set label and the default
   label.  If LABEL is null, ITEM will now use its default label.

   ITEM must not be shared. */
void
output_item_set_label_nocopy (struct output_item *item, char *label)
{
  assert (!output_item_is_shared (item));
  free (item->label);
  item->label = label;
}
