/* PSPP - a program for statistical analysis.
   Copyright (C) 2019 Free Software Foundation, Inc.

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

#include "output/driver-provider.h"

#include <stdlib.h>

#include "data/file-handle-def.h"
#include "libpspp/cast.h"
#include "output/cairo-chart.h"
#include "output/chart-item.h"
#include "output/group-item.h"
#include "output/image-item.h"
#include "output/page-eject-item.h"
#include "output/page-setup-item.h"
#include "output/table-item.h"
#include "output/text-item.h"
#include "output/spv/spv-writer.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct spv_driver
  {
    struct output_driver driver;
    struct spv_writer *writer;
    struct file_handle *handle;
  };

static const struct output_driver_class spv_driver_class;

static struct spv_driver *
spv_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &spv_driver_class);
  return UP_CAST (driver, struct spv_driver, driver);
}

static struct output_driver *
spv_create (struct file_handle *fh, enum settings_output_devices device_type,
             struct string_map *o UNUSED)
{
  struct output_driver *d;
  struct spv_driver *spv;

  spv = xzalloc (sizeof *spv);
  d = &spv->driver;
  spv->handle = fh;
  output_driver_init (&spv->driver, &spv_driver_class, fh_get_file_name (fh),
                      device_type);

  char *error = spv_writer_open (fh_get_file_name (fh), &spv->writer);
  if (spv->writer == NULL)
    {
      msg (ME, "%s", error);
      goto error;
    }

  return d;

 error:
  output_driver_destroy (d);
  return NULL;
}

static void
spv_destroy (struct output_driver *driver)
{
  struct spv_driver *spv = spv_driver_cast (driver);

  char *error = spv_writer_close (spv->writer);
  if (error)
    msg (ME, "%s", error);
  fh_unref (spv->handle);
  free (spv);
}

static void
spv_submit (struct output_driver *driver,
             const struct output_item *output_item)
{
  struct spv_driver *spv = spv_driver_cast (driver);

  if (is_group_open_item (output_item))
    spv_writer_open_heading (spv->writer,
                             to_group_open_item (output_item)->command_name,
                             to_group_open_item (output_item)->command_name);
  else if (is_group_close_item (output_item))
    spv_writer_close_heading (spv->writer);
  else if (is_table_item (output_item))
    {
      const struct table_item *table_item = to_table_item (output_item);
      if (table_item->pt)
        spv_writer_put_table (spv->writer, table_item->pt);
    }
  else if (is_chart_item (output_item))
    {
      cairo_surface_t *surface = xr_draw_image_chart (
        to_chart_item (output_item),
        &(struct cell_color) CELL_COLOR_BLACK,
        &(struct cell_color) CELL_COLOR_WHITE);
      if (cairo_surface_status (surface) == CAIRO_STATUS_SUCCESS)
        spv_writer_put_image (spv->writer, surface);
      cairo_surface_destroy (surface);
    }
  else if (is_image_item (output_item))
    spv_writer_put_image (spv->writer, to_image_item (output_item)->image);
  else if (is_text_item (output_item))
    {
      char *command_id = output_get_command_name ();
      spv_writer_put_text (spv->writer, to_text_item (output_item),
                           command_id);
      free (command_id);
    }
  else if (is_page_eject_item (output_item))
    spv_writer_eject_page (spv->writer);
  else if (is_page_setup_item (output_item))
    spv_writer_set_page_setup (spv->writer,
                               to_page_setup_item (output_item)->page_setup);
}

struct output_driver_factory spv_driver_factory =
  { "spv", "pspp.spv", spv_create };

static const struct output_driver_class spv_driver_class =
  {
    "spv",
    spv_destroy,
    spv_submit,
    NULL,
  };
