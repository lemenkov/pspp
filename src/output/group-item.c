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

#include "libpspp/compiler.h"
#include "output/driver.h"
#include "output/output-item-provider.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct group_open_item *
group_open_item_create (const char *command_name, const char *label)
{
  return group_open_item_create_nocopy (
    command_name ? xstrdup (command_name) : NULL,
    label ? xstrdup (label) : NULL);
}

struct group_open_item *
group_open_item_create_nocopy (char *command_name, char *label)
{
  struct group_open_item *item = xmalloc (sizeof *item);
  *item = (struct group_open_item) {
    .output_item = OUTPUT_ITEM_INITIALIZER (&group_open_item_class),
    .output_item.label = label,
    .command_name = command_name,
  };
  return item;
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
group_open_item_submit (struct group_open_item *item)
{
  output_submit (&item->output_item);
}

static const char *
group_open_item_get_label (const struct output_item *output_item)
{
  struct group_open_item *item = to_group_open_item (output_item);

  return item->command_name ? item->command_name : _("Group");
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
    group_open_item_get_label,
    group_open_item_destroy,
  };

struct group_close_item *
group_close_item_create (void)
{
  struct group_close_item *item = xmalloc (sizeof *item);
  *item = (struct group_close_item) {
    .output_item = OUTPUT_ITEM_INITIALIZER (&group_close_item_class),
  };
  return item;
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
group_close_item_submit (struct group_close_item *item)
{
  output_submit (&item->output_item);
}

static const char *
group_close_item_get_label (const struct output_item *output_item UNUSED)
{
  /* Not marked for translation: user should never see it. */
  return "Group Close";
}

static void
group_close_item_destroy (struct output_item *output_item)
{
  struct group_close_item *item = to_group_close_item (output_item);

  free (item);
}

const struct output_item_class group_close_item_class =
  {
    group_close_item_get_label,
    group_close_item_destroy,
  };
