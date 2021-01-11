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

#include "output/output-item.h"

#include <assert.h>
#include <stdlib.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "output/chart.h"
#include "output/driver.h"
#include "output/page-setup.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#define OUTPUT_ITEM_INITIALIZER(TYPE) .type = TYPE, .ref_cnt = 1

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
          switch (item->type)
            {
            case OUTPUT_ITEM_CHART:
              chart_unref (item->chart);
              break;

            case OUTPUT_ITEM_GROUP_OPEN:
              break;

            case OUTPUT_ITEM_GROUP_CLOSE:
              break;

            case OUTPUT_ITEM_IMAGE:
              cairo_surface_destroy (item->image);
              break;

            case OUTPUT_ITEM_MESSAGE:
              msg_destroy (item->message);
              break;

            case OUTPUT_ITEM_PAGE_BREAK:
              break;

            case OUTPUT_ITEM_PAGE_SETUP:
              page_setup_destroy (item->page_setup);
              break;

            case OUTPUT_ITEM_TABLE:
              pivot_table_unref (item->table);
              break;

            case OUTPUT_ITEM_TEXT:
              pivot_value_destroy (item->text.content);
              break;
            }

          free (item->label);
          free (item->command_name);
          free (item->cached_label);
          free (item);
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

struct output_item *
output_item_unshare (struct output_item *old)
{
  assert (old->ref_cnt > 0);
  if (!output_item_is_shared (old))
    return old;
  output_item_unref (old);

  struct output_item *new = xmalloc (sizeof *new);
  *new = (struct output_item) {
    .ref_cnt = 1,
    .label = xstrdup_if_nonnull (old->label),
    .command_name = xstrdup_if_nonnull (old->command_name),
    .type = old->type,
  };
  switch (old->type)
    {
    case OUTPUT_ITEM_CHART:
      new->chart = chart_ref (old->chart);
      break;

    case OUTPUT_ITEM_GROUP_OPEN:
      break;

    case OUTPUT_ITEM_GROUP_CLOSE:
      break;

    case OUTPUT_ITEM_IMAGE:
      new->image = cairo_surface_reference (old->image);
      break;

    case OUTPUT_ITEM_MESSAGE:
      new->message = msg_dup (old->message);
      break;

    case OUTPUT_ITEM_PAGE_BREAK:
      break;

    case OUTPUT_ITEM_PAGE_SETUP:
      new->page_setup = page_setup_clone (old->page_setup);
      break;

    case OUTPUT_ITEM_TABLE:
      new->table = pivot_table_ref (old->table);
      break;

    case OUTPUT_ITEM_TEXT:
      new->text.subtype = old->text.subtype;
      new->text.content = pivot_value_clone (old->text.content);
      break;
    }
  return new;
}

void
output_item_submit (struct output_item *item)
{
  output_submit (item);
}

/* Returns the label for ITEM, which the caller must not modify or free. */
const char *
output_item_get_label (const struct output_item *item)
{
  if (item->label)
    return item->label;

  switch (item->type)
    {
    case OUTPUT_ITEM_CHART:
      return item->chart->title ? item->chart->title : _("Chart");

    case OUTPUT_ITEM_GROUP_OPEN:
      return item->command_name ? item->command_name : _("Group");

    case OUTPUT_ITEM_GROUP_CLOSE:
      /* Not marked for translation: user should never see it. */
      return "Group Close";

    case OUTPUT_ITEM_IMAGE:
      return "Image";

    case OUTPUT_ITEM_MESSAGE:
      return (item->message->severity == MSG_S_ERROR ? _("Error")
              : item->message->severity == MSG_S_WARNING ? _("Warning")
              : _("Note"));

    case OUTPUT_ITEM_PAGE_BREAK:
      return _("Page Break");

    case OUTPUT_ITEM_PAGE_SETUP:
      /* Not marked for translation: user should never see it. */
      return "Page Setup";

    case OUTPUT_ITEM_TABLE:
      if (!item->cached_label)
        {
          if (!item->table->title)
            return _("Table");

          struct output_item *item_rw = CONST_CAST (struct output_item *, item);
          item_rw->cached_label = pivot_value_to_string (item->table->title,
                                                         item->table);
        }
      return item->cached_label;

    case OUTPUT_ITEM_TEXT:
      return text_item_subtype_to_string (item->text.subtype);
    }

  NOT_REACHED ();
}

/* Sets the label for ITEM to LABEL.  The caller retains ownership of LABEL.
   If LABEL is nonnull, it overrides any previously set label and the default
   label.  If LABEL is null, ITEM will now use its default label.

   ITEM must not be shared. */
void
output_item_set_label (struct output_item *item, const char *label)
{
  output_item_set_label_nocopy (item, xstrdup_if_nonnull (label));
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

struct output_item *
chart_item_create (struct chart *chart)
{
  struct output_item *item = xmalloc (sizeof *item);
  *item = (struct output_item) {
    OUTPUT_ITEM_INITIALIZER (OUTPUT_ITEM_CHART),
    .chart = chart,
  };
  return item;
}

struct output_item *
group_open_item_create (const char *command_name, const char *label)
{
  return group_open_item_create_nocopy (
    xstrdup_if_nonnull (command_name),
    xstrdup_if_nonnull (label));
}

struct output_item *
group_open_item_create_nocopy (char *command_name, char *label)
{
  struct output_item *item = xmalloc (sizeof *item);
  *item = (struct output_item) {
    OUTPUT_ITEM_INITIALIZER (OUTPUT_ITEM_GROUP_OPEN),
    .label = label,
    .command_name = command_name,
  };
  return item;
}

struct output_item *
group_close_item_create (void)
{
  struct output_item *item = xmalloc (sizeof *item);
  *item = (struct output_item) {
    OUTPUT_ITEM_INITIALIZER (OUTPUT_ITEM_GROUP_CLOSE),
  };
  return item;
}

/* Creates and returns a new output item containing IMAGE.  Takes ownership of
   IMAGE. */
struct output_item *
image_item_create (cairo_surface_t *image)
{
  struct output_item *item = xmalloc (sizeof *item);
  *item = (struct output_item) {
    OUTPUT_ITEM_INITIALIZER (OUTPUT_ITEM_IMAGE),
    .image = image,
  };
  return item;
}

struct output_item *
message_item_create (const struct msg *msg)
{
  struct output_item *item = xmalloc (sizeof *msg);
  *item = (struct output_item) {
    OUTPUT_ITEM_INITIALIZER (OUTPUT_ITEM_MESSAGE),
    .message = msg_dup (msg),
  };
  return item;
}

const struct msg *
message_item_get_msg (const struct output_item *item)
{
  assert (item->type == OUTPUT_ITEM_MESSAGE);
  return item->message;
}

struct output_item *
message_item_to_text_item (struct output_item *message_item)
{
  assert (message_item->type == OUTPUT_ITEM_MESSAGE);
  struct output_item *text_item = text_item_create_nocopy (
    TEXT_ITEM_LOG,
    msg_to_string (message_item->message),
    xstrdup (output_item_get_label (message_item)));
  output_item_unref (message_item);
  return text_item;
}

struct output_item *
page_break_item_create (void)
{
  struct output_item *item = xmalloc (sizeof *item);
  *item = (struct output_item) {
    OUTPUT_ITEM_INITIALIZER (OUTPUT_ITEM_PAGE_BREAK),
  };
  return item;
}

struct output_item *
page_setup_item_create (const struct page_setup *ps)
{
  struct output_item *item = xmalloc (sizeof *item);
  *item = (struct output_item) {
    OUTPUT_ITEM_INITIALIZER (OUTPUT_ITEM_PAGE_SETUP),
    .page_setup = page_setup_clone (ps),
  };
  return item;
}

/* Returns a new output_item for rendering TABLE.  Takes ownership of
   TABLE. */
struct output_item *
table_item_create (struct pivot_table *table)
{
  pivot_table_assign_label_depth (table);

  struct output_item *item = xmalloc (sizeof *item);
  *item = (struct output_item) {
    OUTPUT_ITEM_INITIALIZER (OUTPUT_ITEM_TABLE),
    .command_name = xstrdup_if_nonnull (table->command_c),
    .table = table,
  };
  return item;
}

/* Creates and returns a new text item containing TEXT and the specified
   SUBTYPE and LABEL.  The new text item takes ownership of TEXT and LABEL.  If
   LABEL is NULL, uses the default label for SUBTYPE. */
struct output_item *
text_item_create_nocopy (enum text_item_subtype subtype,
                         char *text, char *label)
{
  return text_item_create_value (subtype,
                                 pivot_value_new_user_text_nocopy (text),
                                 label);
}

/* Creates and returns a new text item containing a copy of TEXT and the
   specified SUBTYPE and LABEL.  The caller retains ownership of TEXT and
   LABEL.  If LABEL is null, uses a default label for SUBTYPE. */
struct output_item *
text_item_create (enum text_item_subtype subtype, const char *text,
                  const char *label)
{
  return text_item_create_nocopy (subtype, xstrdup (text),
                                  xstrdup_if_nonnull (label));
}

/* Creates and returns a new text item containing VALUE, SUBTYPE, and LABEL.
   Takes ownership of VALUE and LABEL.  If LABEL is null, uses a default label
   for SUBTYPE. */
struct output_item *
text_item_create_value (enum text_item_subtype subtype,
                        struct pivot_value *value, char *label)
{
  if (subtype == TEXT_ITEM_SYNTAX || subtype == TEXT_ITEM_LOG)
    {
      if (!value->font_style)
        {
          value->font_style = xmalloc (sizeof *value->font_style);
          *value->font_style = (struct font_style) FONT_STYLE_INITIALIZER;
        }

      free (value->font_style->typeface);
      value->font_style->typeface = xstrdup ("Monospaced");
    }

  struct output_item *item = xzalloc (sizeof *item);
  *item = (struct output_item) {
    OUTPUT_ITEM_INITIALIZER (OUTPUT_ITEM_TEXT),
    .command_name = xstrdup_if_nonnull (output_get_command_name ()),
    .label = label,
    .text = { .subtype = subtype, .content = value },
  };
  return item;
}

/* Returns ITEM's subtype. */
enum text_item_subtype
text_item_get_subtype (const struct output_item *item)
{
  assert (item->type == OUTPUT_ITEM_TEXT);
  return item->text.subtype;
}

/* Returns ITEM's text, which the caller must eventually free. */
char *
text_item_get_plain_text (const struct output_item *item)
{
  assert (item->type == OUTPUT_ITEM_TEXT);
  return pivot_value_to_string_defaults (item->text.content);
}

static bool
nullable_font_style_equal (const struct font_style *a,
                           const struct font_style *b)
{
  return a && b ? font_style_equal (a, b) : !a && !b;
}

/* Attempts to append the text in SRC to DST.  If successful, returns true,
   otherwise false.

   Only TEXT_ITEM_SYNTAX and TEXT_ITEM_LOG items can be combined, and not with
   each other.

   DST must not be shared. */
bool
text_item_append (struct output_item *dst, const struct output_item *src)
{
  assert (dst->type == OUTPUT_ITEM_TEXT);
  assert (src->type == OUTPUT_ITEM_TEXT);
  assert (!output_item_is_shared (dst));

  enum text_item_subtype ds = dst->text.subtype;
  enum text_item_subtype ss = src->text.subtype;

  struct pivot_value *dc = dst->text.content;
  const struct pivot_value *sc = src->text.content;

  if (ds != ss
      || (ds != TEXT_ITEM_SYNTAX && ds != TEXT_ITEM_LOG)
      || strcmp (output_item_get_label (dst), output_item_get_label (src))
      || !nullable_font_style_equal (dc->font_style, sc->font_style)
      || (dc->font_style && dc->font_style->markup)
      || sc->type != PIVOT_VALUE_TEXT
      || dc->type != PIVOT_VALUE_TEXT)
    return false;
  else
    {
      /* Calculate new text. */
      char *new_text = xasprintf ("%s\n%s", dc->text.local, sc->text.local);

      /* Free the old text. */
      free (dc->text.local);
      if (dc->text.c != dc->text.local)
        free (dc->text.c);
      if (dc->text.id != dc->text.local && dc->text.id != dc->text.c)
        free (dc->text.id);

      /* Put in new text. */
      dc->text.local = new_text;
      dc->text.c = new_text;
      dc->text.id = new_text;

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

struct output_item *
text_item_to_table_item (struct output_item *text_item)
{
  assert (text_item->type == OUTPUT_ITEM_TEXT);

  /* Create a new table whose contents come from TEXT_ITEM. */
  struct pivot_table *table = pivot_table_create__ (NULL, "Text");
  pivot_table_set_look (table, text_item_table_look ());

  struct pivot_dimension *d = pivot_dimension_create (
    table, PIVOT_AXIS_ROW, N_("Text"));
  d->hide_all_labels = true;
  pivot_category_create_leaf (d->root, pivot_value_new_text ("null"));

  pivot_table_put1 (table, 0, pivot_value_clone (text_item->text.content));

  /* Free TEXT_ITEM. */
  output_item_unref (text_item);

  /* Return a new output item. */
  return table_item_create (table);
}

const char *
text_item_subtype_to_string (enum text_item_subtype subtype)
{
  switch (subtype)
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

