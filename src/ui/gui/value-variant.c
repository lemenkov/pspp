/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2016, 2020  Free Software Foundation

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

#include "value-variant.h"
#include "data/value.h"

enum
  {
    IDX_WIDTH,
    IDX_DATA
  };

/* Returns a GVariant containing the data contained
   in IN and WIDTH.  The returned GVariant has a floating
   reference.
 */
GVariant *
value_variant_new (const union value *in, int width)
{
  GVariant *vv[2] = {NULL, NULL};
  vv[IDX_WIDTH] = g_variant_new_int32 (width);

  if (width == 0)
    vv[IDX_DATA] = g_variant_new_double (in->f);
  else
    {
      vv[IDX_DATA] = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE,
                                                in->s, width,
                                                sizeof (gchar));
    }

  return g_variant_new_tuple (vv, 2);
}

/* Destroy the contents of VAL.  Also unref V */
void
value_destroy_from_variant (union value *val, GVariant *v)
{
  GVariant *vwidth = g_variant_get_child_value (v, IDX_WIDTH);
  gint32 width = g_variant_get_int32 (vwidth); /* v is unreffed here */
  g_variant_unref (vwidth);
  value_destroy (val, width);
}


/* Fills VAL with the value data held in V.
   When VAL is no longer required it must be destroyed using
   value_destroy_from_variant. */
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
      gsize w;
      const gchar *data =
        g_variant_get_fixed_array (vdata, &w, sizeof (gchar));

      if (w != width)
        g_critical ("Value variant's width does not match its array size");
      val->s = xmemdup (data, w);
    }

  g_variant_unref (vdata);
}
