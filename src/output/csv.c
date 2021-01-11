/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include <errno.h>
#include <stdlib.h>

#include "data/file-name.h"
#include "data/file-handle-def.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/string-map.h"
#include "output/driver-provider.h"
#include "output/options.h"
#include "output/output-item.h"
#include "output/pivot-output.h"
#include "output/pivot-table.h"
#include "output/table-provider.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Comma-separated value output driver. */
struct csv_driver
  {
    struct output_driver driver;

    char *separator;            /* Field separator (usually comma or tab). */
    int quote;                  /* Quote character (usually ' or ") or 0. */
    char *quote_set;            /* Characters that force quoting. */
    bool titles;                /* Print table titles? */
    bool captions;              /* Print table captions? */

    struct file_handle *handle;
    FILE *file;                 /* Output file. */
    int n_items;                /* Number of items output so far. */
  };

static const struct output_driver_class csv_driver_class;

static struct csv_driver *
csv_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &csv_driver_class);
  return UP_CAST (driver, struct csv_driver, driver);
}

static struct driver_option *
opt (struct output_driver *d, struct string_map *options, const char *key,
     const char *default_value)
{
  return driver_option_get (d, options, key, default_value);
}

static struct output_driver *
csv_create (struct file_handle *fh, enum settings_output_devices device_type,
            struct string_map *o)
{
  struct output_driver *d;
  struct csv_driver *csv;
  char *quote;

  csv = xzalloc (sizeof *csv);
  d = &csv->driver;
  output_driver_init (&csv->driver, &csv_driver_class, fh_get_file_name (fh), device_type);

  csv->separator = parse_string (opt (d, o, "separator", ","));
  quote = parse_string (opt (d, o, "quote", "\""));
  csv->quote = quote[0];
  free (quote);
  csv->quote_set = xasprintf ("\n\r\t%s%c", csv->separator, csv->quote);
  csv->titles = parse_boolean (opt (d, o, "titles", "true"));
  csv->captions = parse_boolean (opt (d, o, "captions", "true"));
  csv->handle = fh;
  csv->file = fn_open (fh, "w");
  csv->n_items = 0;

  if (csv->file == NULL)
    {
      msg_error (errno, _("error opening output file `%s'"), fh_get_file_name (fh));
      output_driver_destroy (d);
      return NULL;
    }

  return d;
}

static void
csv_destroy (struct output_driver *driver)
{
  struct csv_driver *csv = csv_driver_cast (driver);

  if (csv->file != NULL)
    fn_close (csv->handle, csv->file);

  free (csv->separator);
  free (csv->quote_set);
  fh_unref (csv->handle);
  free (csv);
}

static void
csv_flush (struct output_driver *driver)
{
  struct csv_driver *csv = csv_driver_cast (driver);
  if (csv->file != NULL)
    fflush (csv->file);
}

static void
csv_output_field__ (struct csv_driver *csv, struct substring field)
{
  ss_ltrim (&field, ss_cstr (" "));

  if (csv->quote && ss_cspan (field, ss_cstr (csv->quote_set)) < field.length)
    {
      putc (csv->quote, csv->file);
      for (size_t i = 0; i < field.length; i++)
        {
          if (field.string[i] == csv->quote)
            putc (csv->quote, csv->file);
          putc (field.string[i], csv->file);
        }
      putc (csv->quote, csv->file);
    }
  else
    fwrite (field.string, field.length, 1, csv->file);
}

static void
csv_output_field (struct csv_driver *csv, const char *field)
{
  csv_output_field__ (csv, ss_cstr (field));
}

static void
csv_put_separator (struct csv_driver *csv)
{
  if (csv->n_items++ > 0)
    putc ('\n', csv->file);
}

static void
csv_output_lines (struct csv_driver *csv, const char *text_)
{
  struct substring text = ss_cstr (text_);
  struct substring line;
  size_t save_idx = 0;
  while (ss_separate (text, ss_cstr ("\n"), &save_idx, &line))
    {
      csv_output_field__ (csv, line);
      putc ('\n', csv->file);
    }
}

static void
csv_output_table_cell (struct csv_driver *csv, const struct pivot_table *pt,
                       const struct table_cell *cell, const char *leader)
{
  struct string s = DS_EMPTY_INITIALIZER;
  if (leader)
    ds_put_format (&s, "%s: ", leader);
  pivot_value_format (cell->value, pt, &s);
  csv_output_field (csv, ds_cstr (&s));
  ds_destroy (&s);
}

static void
csv_output_table__ (struct csv_driver *csv, const struct pivot_table *pt,
                    const struct table *t, const char *leader)
{
  if (!t)
    return;

  for (int y = 0; y < t->n[TABLE_VERT]; y++)
    {
      for (int x = 0; x < t->n[TABLE_HORZ]; x++)
        {
          struct table_cell cell;

          table_get_cell (t, x, y, &cell);

          if (x > 0)
            fputs (csv->separator, csv->file);

          if (x != cell.d[TABLE_HORZ][0] || y != cell.d[TABLE_VERT][0])
            csv_output_field (csv, "");
          else
            csv_output_table_cell (csv, pt, &cell, !x ? leader : NULL);
        }
      putc ('\n', csv->file);
    }
}

static void
csv_output_table_layer (struct csv_driver *csv, const struct pivot_table *pt,
                        const size_t *layer_indexes)
{
  struct table *title, *layers, *body, *caption, *footnotes;
  pivot_output (pt, layer_indexes, true, &title, &layers, &body,
                &caption, &footnotes, NULL, NULL);

  csv_put_separator (csv);
  csv_output_table__ (csv, pt, title, "Table");
  csv_output_table__ (csv, pt, layers, "Layer");
  csv_output_table__ (csv, pt, body, NULL);
  csv_output_table__ (csv, pt, caption, "Caption");
  csv_output_table__ (csv, pt, footnotes, "Footnote");

  table_unref (title);
  table_unref (layers);
  table_unref (body);
  table_unref (caption);
  table_unref (footnotes);
}

static void
csv_submit (struct output_driver *driver,
            const struct output_item *item)
{
  struct csv_driver *csv = csv_driver_cast (driver);

  switch (item->type)
    {
    case OUTPUT_ITEM_CHART:
      break;

    case OUTPUT_ITEM_GROUP_OPEN:
      break;

    case OUTPUT_ITEM_GROUP_CLOSE:
      break;

    case OUTPUT_ITEM_IMAGE:
      break;

    case OUTPUT_ITEM_MESSAGE:
      csv_put_separator (csv);
      char *s = msg_to_string (item->message);
      csv_output_field (csv, s);
      free (s);
      putc ('\n', csv->file);
      break;

    case OUTPUT_ITEM_PAGE_BREAK:
      csv_put_separator (csv);
      csv_output_lines (csv, "");
      break;

    case OUTPUT_ITEM_PAGE_SETUP:
      break;

    case OUTPUT_ITEM_TABLE:
      {
        size_t *layer_indexes;
        PIVOT_OUTPUT_FOR_EACH_LAYER (layer_indexes, item->table, true)
          csv_output_table_layer (csv, item->table, layer_indexes);
      }
      break;

    case OUTPUT_ITEM_TEXT:
      if (item->text.subtype == TEXT_ITEM_SYNTAX
          || item->text.subtype == TEXT_ITEM_PAGE_TITLE)
        return;

      csv_put_separator (csv);

      char *text = text_item_get_plain_text (item);
      csv_output_lines (csv, text);
      free (text);
      break;
    }
}

struct output_driver_factory csv_driver_factory = { "csv", "-", csv_create };

static const struct output_driver_class csv_driver_class =
  {
    "csv",
    csv_destroy,
    csv_submit,
    csv_flush,
  };
