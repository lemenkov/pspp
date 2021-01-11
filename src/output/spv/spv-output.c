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

#include "output/spv/spv-output.h"

#include "output/pivot-table.h"
#include "output/spv/spv.h"
#include "output/output-item.h"

#include "gl/xalloc.h"

void
spv_text_submit (const struct spv_item *in)
{
  enum spv_item_class class = spv_item_get_class (in);
  struct output_item *item = text_item_create_value (
    (class == SPV_CLASS_HEADINGS ? TEXT_ITEM_TITLE
     : class == SPV_CLASS_PAGETITLE ? TEXT_ITEM_PAGE_TITLE
     : TEXT_ITEM_LOG),
    pivot_value_clone (spv_item_get_text (in)),
    xstrdup_if_nonnull (in->label));
  output_item_submit (item);
}
