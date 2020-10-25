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

#include "spv-select.h"

#include <string.h>

#include "libpspp/assertion.h"
#include "libpspp/bit-vector.h"
#include "output/spv/spv.h"

#include "gl/c-ctype.h"
#include "gl/xalloc.h"

static bool
is_command_item (const struct spv_item *item)
{
  return !item->parent || !item->parent->parent;
}

static struct spv_item *
find_command_item (struct spv_item *item)
{
  while (!is_command_item (item))
    item = item->parent;
  return item;
}

static bool
string_matches (const char *pattern, const char *s)
{
  /* XXX This should be a Unicode case insensitive comparison. */
  while (c_tolower (*pattern) == c_tolower (*s))
    {
      if (*pattern == '\0')
        return true;

      pattern++;
      s++;
    }

  return pattern[0] == '*' && pattern[1] == '\0';
}

static int
string_array_matches (const char *name, const struct string_array *array)
{
  if (!array->n)
    return -1;
  else if (!name)
    return false;

  for (size_t i = 0; i < array->n; i++)
    if (string_matches (array->strings[i], name))
      return true;

  return false;
}

static bool
match (const char *name,
       const struct string_array *white,
       const struct string_array *black)
{
  return (string_array_matches (name, white) != false
          && string_array_matches (name, black) != true);
}

static int
match_instance (const int *instances, size_t n_instances,
                int instance_within_command)
{
  int retval = false;
  for (size_t i = 0; i < n_instances; i++)
    {
      if (instances[i] == instance_within_command)
        return true;
      else if (instances[i] == -1)
        retval = -1;
    }
  return retval;
}

static bool
match_command (size_t nth_command, size_t *commands, size_t n_commands)
{
  for (size_t i = 0; i < n_commands; i++)
    if (nth_command == commands[i])
      return true;
  return false;
}

static void
select_matches (const struct spv_reader *spv, const struct spv_criteria *c,
                unsigned long int *include)
{
  /* Counting instances within a command. */
  struct spv_item *instance_command_item = NULL;
  int instance_within_command = 0;
  int last_instance = -1;

  /* Counting commands. */
  struct spv_item *command_command_item = NULL;
  size_t nth_command = 0;

  struct spv_item *item;
  ssize_t index = -1;
  SPV_ITEM_FOR_EACH_SKIP_ROOT (item, spv_get_root (spv))
    {
      index++;

      struct spv_item *new_command_item = find_command_item (item);
      if (new_command_item != instance_command_item)
        {
          if (last_instance >= 0)
            {
              bitvector_set1 (include, last_instance);
              last_instance = -1;
            }

          instance_command_item = new_command_item;
          instance_within_command = 0;
        }

      if (!((1u << spv_item_get_class (item)) & c->classes))
        continue;

      if (!c->include_hidden && !spv_item_is_visible (item))
        continue;

      if (c->error)
        {
          spv_item_load (item);
          if (!item->error)
            continue;
        }

      if (!match (spv_item_get_command_id (item),
                  &c->include.commands, &c->exclude.commands))
        continue;

      if (c->n_commands)
        {
          if (new_command_item != command_command_item)
            {
              command_command_item = new_command_item;
              nth_command++;
            }

          if (!match_command (nth_command, c->commands, c->n_commands))
            continue;
        }

      if (!match (spv_item_get_subtype (item),
                  &c->include.subtypes, &c->exclude.subtypes))
        continue;

      if (!match (spv_item_get_label (item),
                  &c->include.labels, &c->exclude.labels))
        continue;

      if (c->members.n
          && !((item->xml_member
                && string_array_matches (item->xml_member, &c->members)) ||
               (item->bin_member
                && string_array_matches (item->bin_member, &c->members))))
        continue;

      if (c->n_instances)
        {
          if (is_command_item (item))
            continue;
          instance_within_command++;

          int include_instance = match_instance (c->instances, c->n_instances,
                                                 instance_within_command);
          if (!include_instance)
            continue;
          else if (include_instance < 0)
            {
              last_instance = index;
              continue;
            }
        }

      bitvector_set1 (include, index);
    }

  if (last_instance >= 0)
    bitvector_set1 (include, last_instance);
}

void
spv_select (const struct spv_reader *spv,
            const struct spv_criteria c[], size_t nc,
            struct spv_item ***itemsp, size_t *n_itemsp)
{
  struct spv_item *item;

  struct spv_criteria default_criteria = SPV_CRITERIA_INITIALIZER;
  if (!nc)
    {
      nc = 1;
      c = &default_criteria;
    }

  /* Count items. */
  size_t max_items = 0;
  SPV_ITEM_FOR_EACH_SKIP_ROOT (item, spv_get_root (spv))
    max_items++;

  /* Allocate bitmap for items then fill it in with selected items. */
  unsigned long int *include = bitvector_allocate (max_items);
  for (size_t i = 0; i < nc; i++)
    select_matches (spv, &c[i], include);

  /* Copy selected items into output array. */
  size_t n_items = 0;
  struct spv_item **items = xnmalloc (bitvector_count (include, max_items),
                                      sizeof *items);
  size_t i = 0;
  SPV_ITEM_FOR_EACH_SKIP_ROOT (item, spv_get_root (spv))
    if (bitvector_is_set (include, i++))
      items[n_items++] = item;
  *itemsp = items;
  *n_itemsp = n_items;

  /* Free memory. */
  free (include);
}
