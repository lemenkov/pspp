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
#include "output/text-item.h"

#include "gl/xalloc.h"

void
spv_text_submit (const struct spv_item *in)
{
  enum spv_item_class class = spv_item_get_class (in);
  enum text_item_type type
    = (class == SPV_CLASS_HEADINGS ? TEXT_ITEM_TITLE
       : class == SPV_CLASS_PAGETITLE ? TEXT_ITEM_PAGE_TITLE
       : TEXT_ITEM_LOG);
  const struct pivot_value *value = spv_item_get_text (in);
  char *text = pivot_value_to_string_defaults (value);
  char *label = in->label ? xstrdup (in->label) : NULL;
  struct text_item *item = text_item_create_nocopy (type, text, label);

  if (value->font_style)
    {
      font_style_uninit (&item->style);
      font_style_copy (NULL, &item->style, value->font_style);
    }
  text_item_submit (item);
}
