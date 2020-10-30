/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "output/text-item.h"

#include <stdarg.h>
#include <stdlib.h>

#include "libpspp/cast.h"
#include "libpspp/pool.h"
#include "output/driver.h"
#include "output/output-item-provider.h"
#include "output/table.h"
#include "output/table-item.h"
#include "output/table-provider.h"

#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

const char *
text_item_type_to_string (enum text_item_type type)
{
  switch (type)
    {
    case TEXT_ITEM_PAGE_TITLE:
      return _("Page Title");

    case TEXT_ITEM_TITLE:
      return _("Title");

    case TEXT_ITEM_SYNTAX:
    case TEXT_ITEM_LOG:
      return _("Log");

    case TEXT_ITEM_EJECT_PAGE:
      return _("Page Break");

    default:
      return _("Text");
    }
}

/* Creates and returns a new text item containing TEXT and the specified TYPE.
   The new text item takes ownership of TEXT. */
struct text_item *
text_item_create_nocopy (enum text_item_type type, char *text)
{
  struct text_item *item = xzalloc (sizeof *item);
  output_item_init (&item->output_item, &text_item_class);
  item->text = text;
  item->type = type;
  return item;
}

/* Creates and returns a new text item containing a copy of TEXT and the
   specified TYPE.  The caller retains ownership of TEXT. */
struct text_item *
text_item_create (enum text_item_type type, const char *text)
{
  return text_item_create_nocopy (type, xstrdup (text));
}

/* Creates and returns a new text item containing a copy of FORMAT, which is
   formatted as if by printf(), and the specified TYPE.  The caller retains
   ownership of FORMAT. */
struct text_item *
text_item_create_format (enum text_item_type type, const char *format, ...)
{
  struct text_item *item;
  va_list args;

  va_start (args, format);
  item = text_item_create_nocopy (type, xvasprintf (format, args));
  va_end (args);

  return item;
}

/* Returns ITEM's type. */
enum text_item_type
text_item_get_type (const struct text_item *item)
{
  return item->type;
}

/* Returns ITEM's text, which the caller may not modify or free. */
const char *
text_item_get_text (const struct text_item *item)
{
  return item->text;
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
text_item_submit (struct text_item *item)
{
  output_submit (&item->output_item);
}

struct table_item *
text_item_to_table_item (struct text_item *text_item)
{
  struct table *tab = table_create (1, 1, 0, 0, 0, 0);

  struct table_area_style *style = pool_alloc (tab->container, sizeof *style);
  *style = (struct table_area_style) { TABLE_AREA_STYLE_INITIALIZER__,
                                       .cell_style.halign = TABLE_HALIGN_LEFT };
  struct font_style *font_style = &style->font_style;
  if (text_item->typeface)
    font_style->typeface = pool_strdup (tab->container, text_item->typeface);
  font_style->size = text_item->size;
  font_style->bold = text_item->bold;
  font_style->italic = text_item->italic;
  font_style->underline = text_item->underline;
  font_style->markup = text_item->markup;
  tab->styles[0] = style;

  int opts = 0;
  if (text_item->markup)
    opts |= TAB_MARKUP;
  if (text_item->type == TEXT_ITEM_SYNTAX || text_item->type == TEXT_ITEM_LOG)
    opts |= TAB_FIX;
  table_text (tab, 0, 0, opts, text_item_get_text (text_item));
  struct table_item *table_item = table_item_create (tab, NULL, NULL);
  text_item_unref (text_item);
  return table_item;
}

static void
text_item_destroy (struct output_item *output_item)
{
  struct text_item *item = to_text_item (output_item);
  free (item->text);
  free (item->typeface);
  free (item);
}

const struct output_item_class text_item_class =
  {
    "text",
    text_item_destroy,
  };
