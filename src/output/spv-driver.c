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

  spv_writer_write (spv->writer, output_item);
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
