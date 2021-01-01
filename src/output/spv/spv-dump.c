/* PSPP - a program for statistical analysis.
   Copyright (C) 2017, 2018 Free Software Foundation, Inc.

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

#include "output/spv/spv.h"

#include <inttypes.h>
#include <stdlib.h>

#include "data/settings.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"

static void
indent (int indentation)
{
  for (int i = 0; i < indentation * 2; i++)
    putchar (' ');
}

void
spv_item_dump (const struct spv_item *item, int indentation)
{
  indent (indentation);
  if (item->label)
    printf ("\"%s\" ", item->label);
  if (!item->visible)
    printf ("(hidden) ");

  switch (item->type)
    {
    case SPV_ITEM_HEADING:
      printf ("heading\n");
      for (size_t i = 0; i < item->n_children; i++)
        spv_item_dump (item->children[i], indentation + 1);
      break;

    case SPV_ITEM_TEXT:
      printf ("text \"%s\"\n", pivot_value_to_string_defaults (item->text));
      break;

    case SPV_ITEM_TABLE:
      if (item->table)
        pivot_table_dump (item->table, indentation + 1);
      else
        {
          printf ("unloaded table in %s", item->bin_member);
          if (item->xml_member)
            printf (" and %s", item->xml_member);
          putchar ('\n');
        }
      break;

    case SPV_ITEM_GRAPH:
      printf ("graph\n");
      break;

    case SPV_ITEM_MODEL:
      printf ("model\n");
      break;

    case SPV_ITEM_OBJECT:
      printf ("object type=\"%s\" uri=\"%s\"\n", item->object_type, item->uri);
      break;

    case SPV_ITEM_TREE:
      printf ("tree\n");
      break;
    }
}
