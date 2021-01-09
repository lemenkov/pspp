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

#include "output/image-item.h"

#include <stdarg.h>
#include <stdlib.h>

#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "output/driver.h"
#include "output/output-item-provider.h"

#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

/* Creates and returns a new image item containing IMAGE.  Takes ownership of
   IMAGE. */
struct image_item *
image_item_create (cairo_surface_t *image)
{
  struct image_item *item = xmalloc (sizeof *item);
  *item = (struct image_item) {
    .output_item = OUTPUT_ITEM_INITIALIZER (&image_item_class),
    .image = image,
  };
  return item;
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
image_item_submit (struct image_item *item)
{
  output_submit (&item->output_item);
}

struct image_item *
image_item_unshare (struct image_item *old)
{
  assert (old->output_item.ref_cnt > 0);
  if (!image_item_is_shared (old))
    return old;
  image_item_unref (old);

  struct image_item *new = xmalloc (sizeof *new);
  *new = (struct image_item) {
    .output_item = OUTPUT_ITEM_CLONE_INITIALIZER (&old->output_item),
    .image = cairo_surface_reference (old->image),
  };
  return new;
}

static const char *
image_item_get_label (const struct output_item *output_item UNUSED)
{
  return "Image";
}

static void
image_item_destroy (struct output_item *output_item)
{
  struct image_item *item = to_image_item (output_item);
  cairo_surface_destroy (item->image);
  free (item);
}

const struct output_item_class image_item_class =
  {
    image_item_get_label,
    image_item_destroy,
  };
