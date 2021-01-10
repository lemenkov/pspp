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
#include "output/pivot-table.h"
#include "output/table.h"
#include "output/table-item.h"
#include "output/table-provider.h"

#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

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

    default:
      return _("Text");
    }
}

/* Creates and returns a new text item containing TEXT and the specified TYPE
   and LABEL.  The new text item takes ownership of TEXT and LABEL.  If LABEL
   is NULL, uses the default label for TYPE. */
struct text_item *
text_item_create_nocopy (enum text_item_type type, char *text, char *label)
{
  struct text_item *item = xzalloc (sizeof *item);
  *item = (struct text_item) {
    .output_item = OUTPUT_ITEM_INITIALIZER (&text_item_class),
    .output_item.label = label,
    .text = text,
    .type = type,
    .style = FONT_STYLE_INITIALIZER,
  };

  if (type == TEXT_ITEM_SYNTAX || type == TEXT_ITEM_LOG)
    {
      free (item->style.typeface);
      item->style.typeface = xstrdup ("Monospaced");
    }

  return item;
}

/* Creates and returns a new text item containing a copy of TEXT and the
   specified TYPE and LABEL.  The caller retains ownership of TEXT and LABEL.
   If LABEL is null, uses a default label for TYPE. */
struct text_item *
text_item_create (enum text_item_type type, const char *text,
                  const char *label)
{
  return text_item_create_nocopy (type, xstrdup (text),
                                  label ? xstrdup (label) : NULL);
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

struct text_item *
text_item_unshare (struct text_item *old)
{
  assert (old->output_item.ref_cnt > 0);
  if (!text_item_is_shared (old))
    return old;
  text_item_unref (old);

  struct text_item *new = xmalloc (sizeof *new);
  *new = (struct text_item) {
    .output_item = OUTPUT_ITEM_CLONE_INITIALIZER (&old->output_item),
    .text = xstrdup (old->text),
    .type = old->type,
  };
  font_style_copy (NULL, &new->style, &old->style);
  return new;
}

/* Attempts to append the text in SRC to DST.  If successful, returns true,
   otherwise false.

   Only TEXT_ITEM_SYNTAX and TEXT_ITEM_LOG items can be combined, and not with
   each other.

   DST must not be shared. */
bool
text_item_append (struct text_item *dst, const struct text_item *src)
{
  assert (!text_item_is_shared (dst));
  if (dst->type != src->type
      || (dst->type != TEXT_ITEM_SYNTAX && dst->type != TEXT_ITEM_LOG)
      || strcmp (output_item_get_label (&dst->output_item),
                 output_item_get_label (&src->output_item))
      || !font_style_equal (&dst->style, &src->style)
      || dst->style.markup)
    return false;
  else
    {
      char *new_text = xasprintf ("%s\n%s", dst->text, src->text);
      free (dst->text);
      dst->text = new_text;
      return true;
    }
}

static const struct pivot_table_look *
text_item_table_look (void)
{
  static struct pivot_table_look *look;
  if (!look)
    {
      look = pivot_table_look_new_builtin_default ();

      for (int a = 0; a < PIVOT_N_AREAS; a++)
        memset (look->areas[a].cell_style.margin, 0,
                sizeof look->areas[a].cell_style.margin);
      for (int b = 0; b < PIVOT_N_BORDERS; b++)
        look->borders[b].stroke = TABLE_STROKE_NONE;
    }
  return look;
}

struct table_item *
text_item_to_table_item (struct text_item *text_item)
{
  struct pivot_table *table = pivot_table_create__ (NULL, "Text");
  pivot_table_set_look (table, text_item_table_look ());

  struct pivot_dimension *d = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Text"));
  d->hide_all_labels = true;
  pivot_category_create_leaf (d->root, pivot_value_new_text ("null"));

  struct pivot_value *content = pivot_value_new_user_text (
    text_item->text, SIZE_MAX);
  pivot_value_set_font_style (content, &text_item->style);
  pivot_table_put1 (table, 0, content);

  text_item_unref (text_item);

  return table_item_create (table);
}

static const char *
text_item_get_label (const struct output_item *output_item)
{
  const struct text_item *item = to_text_item (output_item);
  return text_item_type_to_string (item->type);
}

static void
text_item_destroy (struct output_item *output_item)
{
  struct text_item *item = to_text_item (output_item);
  free (item->text);
  font_style_uninit (&item->style);
  free (item);
}

const struct output_item_class text_item_class =
  {
    text_item_get_label,
    text_item_destroy,
  };
