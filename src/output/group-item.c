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

#include "output/group-item.h"

#include <stdlib.h>

#include "output/driver.h"
#include "output/output-item-provider.h"

#include "gl/xalloc.h"

struct group_open_item *
group_open_item_create (const char *command_name)
{
  return group_open_item_create_nocopy (
    command_name ? xstrdup (command_name) : NULL);
}

struct group_open_item *
group_open_item_create_nocopy (char *command_name)
{
  struct group_open_item *item;

  item = xmalloc (sizeof *item);
  output_item_init (&item->output_item, &group_open_item_class);
  item->command_name = command_name;

  return item;
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
group_open_item_submit (struct group_open_item *item)
{
  output_submit (&item->output_item);
}

static void
group_open_item_destroy (struct output_item *output_item)
{
  struct group_open_item *item = to_group_open_item (output_item);

  free (item->command_name);
  free (item);
}

const struct output_item_class group_open_item_class =
  {
    "group_open",
    group_open_item_destroy,
  };

struct group_close_item *
group_close_item_create (void)
{
  struct group_close_item *item;

  item = xmalloc (sizeof *item);
  output_item_init (&item->output_item, &group_close_item_class);

  return item;
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
group_close_item_submit (struct group_close_item *item)
{
  output_submit (&item->output_item);
}

static void
group_close_item_destroy (struct output_item *output_item)
{
  struct group_close_item *item = to_group_close_item (output_item);

  free (item);
}

const struct output_item_class group_close_item_class =
  {
    "group_close",
    group_close_item_destroy,
  };
