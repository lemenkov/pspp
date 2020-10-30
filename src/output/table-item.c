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

#include "gl/xalloc.h"

struct table_item_text *
table_item_text_create (const char *content)
{
  if (!content)
    return NULL;

  struct table_item_text *text = xmalloc (sizeof *text);
  *text = (struct table_item_text) { .content = xstrdup (content) };
  return text;
}

struct table_item_text *
table_item_text_clone (const struct table_item_text *old)
{
  if (!old)
    return NULL;

  struct table_item_text *new = xmalloc (sizeof *new);
  *new = (struct table_item_text) {
    .content = xstrdup (old->content),
    .footnotes = xmemdup (old->footnotes,
                          old->n_footnotes * sizeof *old->footnotes),
    .n_footnotes = old->n_footnotes,
    .style = table_area_style_clone (NULL, old->style),
  };
  return new;
}

void
table_item_text_destroy (struct table_item_text *text)
{
  if (text)
    {
      free (text->content);
      free (text->footnotes);
      table_area_style_free (text->style);
      free (text);
    }
}

void
table_item_layer_copy (struct table_item_layer *dst,
                       const struct table_item_layer *src)
{
  dst->content = xstrdup (src->content);
  dst->footnotes = xmemdup (src->footnotes,
                            src->n_footnotes * sizeof *src->footnotes);
  dst->n_footnotes = src->n_footnotes;
}

void
table_item_layer_uninit (struct table_item_layer *layer)
{
  if (layer)
    {
      free (layer->content);
      free (layer->footnotes);
    }
}

struct table_item_layers *
table_item_layers_clone (const struct table_item_layers *old)
{
  if (!old)
    return NULL;

  struct table_item_layers *new = xmalloc (sizeof *new);
  *new = (struct table_item_layers) {
    .layers = xnmalloc (old->n_layers, sizeof *new->layers),
    .n_layers = old->n_layers,
    .style = table_area_style_clone (NULL, old->style),
  };
  for (size_t i = 0; i < new->n_layers; i++)
    table_item_layer_copy (&new->layers[i], &old->layers[i]);
  return new;
}

void
table_item_layers_destroy (struct table_item_layers *layers)
{
  if (layers)
    {
      for (size_t i = 0; i < layers->n_layers; i++)
        table_item_layer_uninit (&layers->layers[i]);
      free (layers->layers);
      table_area_style_free (layers->style);
      free (layers);
    }
}

/* Initializes ITEM as a table item for rendering TABLE.  The new table item
   initially has the specified TITLE and CAPTION, which may each be NULL.  The
   caller retains ownership of TITLE and CAPTION. */
struct table_item *
table_item_create (struct table *table, const char *title, const char *caption)
{
  struct table_item *item = xmalloc (sizeof *item);
  output_item_init (&item->output_item, &table_item_class);
  item->table = table;
  item->title = table_item_text_create (title);
  item->layers = NULL;
  item->caption = table_item_text_create (caption);
  item->pt = NULL;
  return item;
}

/* Returns the table contained by TABLE_ITEM.  The caller must not modify or
   unref the returned table. */
const struct table *
table_item_get_table (const struct table_item *table_item)
{
  return table_item->table;
}

/* Returns ITEM's title, which is a null pointer if no title has been
   set. */
const struct table_item_text *
table_item_get_title (const struct table_item *item)
{
  return item->title;
}

/* Sets ITEM's title to TITLE, replacing any previous title.  Specify NULL for
   TITLE to clear any title from ITEM.  The caller retains ownership of TITLE.

   This function may only be used on a table_item that is unshared. */
void
table_item_set_title (struct table_item *item,
                      const struct table_item_text *title)
{
  assert (!table_item_is_shared (item));
  table_item_text_destroy (item->title);
  item->title = table_item_text_clone (title);
}

/* Returns ITEM's layers, which will be a null pointer if no layers have been
   set. */
const struct table_item_layers *
table_item_get_layers (const struct table_item *item)
{
  return item->layers;
}

/* Sets ITEM's layers to LAYERS, replacing any previous layers.  Specify NULL
   for LAYERS to clear any layers from ITEM.  The caller retains ownership of
   LAYERS.

   This function may only be used on a table_item that is unshared. */
void
table_item_set_layers (struct table_item *item,
                       const struct table_item_layers *layers)
{
  assert (!table_item_is_shared (item));
  table_item_layers_destroy (item->layers);
  item->layers = table_item_layers_clone (layers);
}

/* Returns ITEM's caption, which is a null pointer if no caption has been
   set. */
const struct table_item_text *
table_item_get_caption (const struct table_item *item)
{
  return item->caption;
}

/* Sets ITEM's caption to CAPTION, replacing any previous caption.  Specify
   NULL for CAPTION to clear any caption from ITEM.  The caller retains
   ownership of CAPTION.

   This function may only be used on a table_item that is unshared. */
void
table_item_set_caption (struct table_item *item,
                        const struct table_item_text *caption)
{
  assert (!table_item_is_shared (item));
  table_item_text_destroy (item->caption);
  item->caption = table_item_text_clone (caption);
}

/* Submits TABLE_ITEM to the configured output drivers, and transfers ownership
   to the output subsystem. */
void
table_item_submit (struct table_item *table_item)
{
  output_submit (&table_item->output_item);
}

static void
table_item_destroy (struct output_item *output_item)
{
  struct table_item *item = to_table_item (output_item);
  table_item_text_destroy (item->title);
  table_item_text_destroy (item->caption);
  table_item_layers_destroy (item->layers);
  pivot_table_unref (item->pt);
  table_unref (item->table);
  free (item);
}

const struct output_item_class table_item_class =
  {
    "table",
    table_item_destroy,
  };
