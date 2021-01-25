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

#include "output/select.h"

#include <string.h>

#include "libpspp/assertion.h"
#include "libpspp/bit-vector.h"
#include "libpspp/message.h"

#include "gl/c-ctype.h"
#include "gl/xalloc.h"

const char *
output_item_class_to_string (enum output_item_class class)
{
  switch (class)
    {
#define OUTPUT_CLASS(ENUM, NAME) case OUTPUT_CLASS_##ENUM: return NAME;
      OUTPUT_CLASSES
#undef OUTPUT_CLASS
    default: return NULL;
    }
}

enum output_item_class
output_item_class_from_string (const char *name)
{
#define OUTPUT_CLASS(ENUM, NAME) \
  if (!strcmp (name, NAME)) return OUTPUT_CLASS_##ENUM;
  OUTPUT_CLASSES
#undef OUTPUT_CLASS

  return (enum output_item_class) OUTPUT_N_CLASSES;
}

enum output_item_class
output_item_classify (const struct output_item *item)
{
  const char *label = output_item_get_label (item);
  if (!label)
    label = "";

  switch (item->type)
    {
    case OUTPUT_ITEM_CHART:
      return OUTPUT_CLASS_CHARTS;

    case OUTPUT_ITEM_GROUP:
      return OUTPUT_CLASS_OUTLINEHEADERS;

    case OUTPUT_ITEM_IMAGE:
      return OUTPUT_CLASS_OTHER;

    case OUTPUT_ITEM_MESSAGE:
      return (item->message->severity == MSG_S_NOTE
              ? OUTPUT_CLASS_NOTES
              : OUTPUT_CLASS_WARNINGS);

    case OUTPUT_ITEM_PAGE_BREAK:
      return OUTPUT_CLASS_OTHER;

    case OUTPUT_ITEM_TABLE:
      return (!strcmp (label, "Warnings") ? OUTPUT_CLASS_WARNINGS
              : !strcmp (label, "Notes") ? OUTPUT_CLASS_NOTES
              : OUTPUT_CLASS_TABLES);

    case OUTPUT_ITEM_TEXT:
      return (!strcmp (label, "Title") ? OUTPUT_CLASS_HEADINGS
              : !strcmp (label, "Log") ? OUTPUT_CLASS_LOGS
              : !strcmp (label, "Page Title") ? OUTPUT_CLASS_PAGETITLE
              : OUTPUT_CLASS_TEXTS);

    default:
      return OUTPUT_CLASS_UNKNOWN;
    }
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
select_matches (const struct output_item **items,
                unsigned int *depths, size_t n_items,
                const struct output_criteria *c, unsigned long int *include)
{
  /* Counting instances within a command. */
  int instance_within_command = 0;
  int last_instance = -1;

  /* Counting commands. */
  const struct output_item *command_item = NULL;
  const struct output_item *command_command_item = NULL;
  size_t nth_command = 0;

  for (size_t i = 0; i < n_items; i++)
    {
      const struct output_item *item = items[i];

      if (depths[i] == 0)
        {
          command_item = item;
          if (last_instance >= 0)
            {
              bitvector_set1 (include, last_instance);
              last_instance = -1;
            }
          instance_within_command = 0;
        }

      if (!((1u << output_item_classify (item)) & c->classes))
        continue;

      if (!c->include_hidden && item->type != OUTPUT_ITEM_GROUP && !item->show)
        continue;

      if (c->error && (!item->spv_info || !item->spv_info->error))
        continue;

      if (!match (item->command_name,
                  &c->include.commands, &c->exclude.commands))
        continue;

      if (c->n_commands)
        {
          if (command_item != command_command_item)
            {
              command_command_item = command_item;
              nth_command++;
            }

          if (!match_command (nth_command, c->commands, c->n_commands))
            continue;
        }

      char *subtype = output_item_get_subtype (item);
      bool match_subtype = match (subtype,
                                  &c->include.subtypes, &c->exclude.subtypes);
      if (!match_subtype)
        continue;

      if (!match (output_item_get_label (item),
                  &c->include.labels, &c->exclude.labels))
        continue;

      if (c->members.n)
        {
          const char *members[4];
          size_t n = spv_info_get_members (item->spv_info, members,
                                           sizeof members / sizeof *members);

          bool found = false;
          for (size_t j = 0; j < n; j++)
            if (string_array_matches (members[j], &c->members) == true)
              {
                found = true;
                break;
              }
          if (!found)
            continue;
        }

      if (c->n_instances)
        {
          if (!depths[i])
            continue;
          instance_within_command++;

          int include_instance = match_instance (c->instances, c->n_instances,
                                                 instance_within_command);
          if (!include_instance)
            continue;
          else if (include_instance < 0)
            {
              last_instance = i;
              continue;
            }
        }

      bitvector_set1 (include, i);
    }

  if (last_instance >= 0)
    bitvector_set1 (include, last_instance);
}

static size_t
count_items (const struct output_item *item)
{
  size_t n = 1;
  if (item->type == OUTPUT_ITEM_GROUP)
    for (size_t i = 0; i < item->group.n_children; i++)
      n += count_items (item->group.children[i]);
  return n;
}

static size_t
flatten_items (const struct output_item *item, size_t index, size_t depth,
               const struct output_item **items, unsigned int *depths)
{
  items[index] = item;
  depths[index] = depth;
  index++;

  if (item->type == OUTPUT_ITEM_GROUP)
    for (size_t i = 0; i < item->group.n_children; i++)
      index = flatten_items (item->group.children[i], index, depth + 1,
                             items, depths);

  return index;
}

static size_t
unflatten_items (const struct output_item *in, size_t index,
                 unsigned long *include, struct output_item *out)
{
  bool include_item = bitvector_is_set (include, index++);
  if (in->type == OUTPUT_ITEM_GROUP)
    {
      /* If we should include the group itself, then clone IN inside OUT, and
         add any children to the clone instead to OUT directly. */
      if (include_item)
        {
          struct output_item *group = group_item_clone_empty (in);
          group_item_add_child (out, group);
          out = group;
        }

      for (size_t i = 0; i < in->group.n_children; i++)
        index = unflatten_items (in->group.children[i], index, include, out);
    }
  else
    {
      if (include_item)
        group_item_add_child (out, output_item_ref (in));
    }
  return index;
}

/* Consumes IN, which must be a group, and returns a new output item whose
   children are all the (direct and indirect) children of IN that meet the NC
   criteria in C[]. */
struct output_item *
output_select (struct output_item *in,
               const struct output_criteria c[], size_t nc)
{
  assert (in->type == OUTPUT_ITEM_GROUP);
  if (!nc)
    return in;

  /* Number of items (not counting the root). */
  size_t n_items = count_items (in) - 1;

  const struct output_item **items = xnmalloc (n_items, sizeof *items);
  unsigned int *depths = xnmalloc (n_items, sizeof *depths);
  size_t n_flattened = 0;
  for (size_t i = 0; i < in->group.n_children; i++)
    n_flattened = flatten_items (in->group.children[i], n_flattened,
                                 0, items, depths);
  assert (n_flattened == n_items);

  unsigned long int *include = bitvector_allocate (n_items);
  for (size_t i = 0; i < nc; i++)
    select_matches (items, depths, n_items, &c[i], include);

  struct output_item *out = root_item_create ();
  size_t n_unflattened = 0;
  for (size_t i = 0; i < in->group.n_children; i++)
    n_unflattened = unflatten_items (in->group.children[i], n_unflattened,
                                     include, out);
  assert (n_unflattened == n_items);

  free (items);
  free (depths);
  free (include);

  output_item_unref (in);
  return out;
}
