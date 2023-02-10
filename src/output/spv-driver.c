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
#include "output/output-item.h"
#include "output/page-setup.h"
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
            struct driver_options *o)
{
  struct spv_writer *writer;
  char *error = spv_writer_open (fh_get_file_name (fh), &writer);
  if (!writer)
    {
      msg (ME, "%s", error);
      free (error);
      return NULL;
    }

  struct page_setup *ps = page_setup_parse (o);
  spv_writer_set_page_setup (writer, ps);
  page_setup_destroy (ps);

  struct spv_driver *spv = xmalloc (sizeof *spv);
  *spv = (struct spv_driver) {
    .driver = {
      .class = &spv_driver_class,
      .name = xstrdup (fh_get_file_name (fh)),
      .device_type = device_type,
    },
    .handle = fh,
    .writer = writer,
  };
  return &spv->driver;
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

static void
spv_setup (struct output_driver *driver,
           const struct page_setup *ps)
{
  struct spv_driver *spv = spv_driver_cast (driver);

  spv_writer_set_page_setup (spv->writer, ps);
}

struct output_driver_factory spv_driver_factory =
  { "spv", "pspp.spv", spv_create };

static const struct output_driver_class spv_driver_class =
  {
    .name = "spv",
    .destroy = spv_destroy,
    .submit = spv_submit,
    .setup = spv_setup,
    .handles_show = true,
    .handles_groups = true,
  };
