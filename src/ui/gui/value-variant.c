/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2016  Free Software Foundation

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
#include <gtk/gtk.h>

#include <string.h>
#include "value-variant.h"
#include "data/value.h"


enum
  {
    IDX_WIDTH,
    IDX_DATA
  };


GVariant *
value_variant_new (const union value *in, int width)
{
  GVariant *vv[2] = {NULL, NULL};
  vv[IDX_WIDTH] = g_variant_new_int32 (width);

  if (width == 0)
    vv[IDX_DATA] = g_variant_new_double (in->f);
  else if (width <= MAX_SHORT_STRING)
    {
      char xx[MAX_SHORT_STRING + 1];
      memset (xx, '\0', MAX_SHORT_STRING + 1);
      memcpy (xx, in->short_string, width);
      vv[IDX_DATA] = g_variant_new_bytestring (xx);
    }
  else
    {
      gchar *q = xmalloc (width + 1);
      memcpy (q, in->long_string, width);
      q[width] = '\0';
      vv[IDX_DATA] = g_variant_new_from_data (G_VARIANT_TYPE_BYTESTRING, q,
					      width + 1, FALSE, NULL, NULL);
    }
  
  return g_variant_new_tuple (vv, 2);
}

void
value_destroy_from_variant (union value *val, GVariant *v)
{
  GVariant *vwidth = g_variant_get_child_value (v, IDX_WIDTH);
  gint32 width = g_variant_get_int32 (vwidth);
  g_variant_unref (vwidth);
  value_destroy (val, width);
}


void
value_variant_get (union value *val, GVariant *v)
{
  GVariant *vwidth = g_variant_get_child_value (v, IDX_WIDTH);
  gint32 width = g_variant_get_int32 (vwidth);
  g_variant_unref (vwidth);

  GVariant *vdata = g_variant_get_child_value (v, IDX_DATA);

  if (0 == width)
    val->f = g_variant_get_double (vdata);
  else
    {
      const gchar *data = g_variant_get_bytestring (vdata);
      if (width <= MAX_SHORT_STRING)
	memcpy (val->short_string, data, MAX_SHORT_STRING);
      else
	{
	  val->long_string = xmemdup (data, width);
	}
    }

  g_variant_unref (vdata);
}
