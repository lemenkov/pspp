/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2014 Free Software Foundation, Inc.

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

#include "output/options.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/str.h"
#include "libpspp/string-map.h"
#include "output/driver-provider.h"
#include "output/measure.h"
#include "output/table.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Creates and returns a new struct driver_option that contains copies of
   all of the supplied arguments.  All of the arguments must be nonnull,
   except that VALUE may be NULL (if the user did not supply a value for this
   option).

   Refer to struct driver_option for the meaning of each argument. */
struct driver_option *
driver_option_create (const char *driver_name, const char *name,
                      const char *value, const char *default_value)
{
  struct driver_option *o = xmalloc (sizeof *o);
  o->driver_name = xstrdup (driver_name);
  o->name = xstrdup (name);
  o->value = value != NULL ? xstrdup (value) : NULL;
  o->default_value = default_value ? xstrdup (default_value) : NULL;
  return o;
}

/* Creates and returns a new struct driver_option for output driver DRIVER
   (which is needed only to the extent that its name will be used in error
   messages).  The option named NAME is extracted from OPTIONS.  DEFAULT_VALUE
   is the default value of the option, used if the given option was not
   supplied or was invalid. */
struct driver_option *
driver_option_get (struct output_driver *driver, struct string_map *options,
                   const char *name, const char *default_value)
{
  struct driver_option *option;
  char *value;

  value = string_map_find_and_delete (options, name);
  option = driver_option_create (output_driver_get_name (driver), name, value,
                                 default_value);
  free (value);
  return option;
}

/* Frees driver option O. */
void
driver_option_destroy (struct driver_option *o)
{
  if (o != NULL)
    {
      free (o->driver_name);
      free (o->name);
      free (o->value);
      free (o->default_value);
      free (o);
    }
}

/* Stores the paper size of the value of option O into *H and *V, in 1/72000"
   units.  Any syntax accepted by measure_paper() may be used.

   Destroys O. */
void
parse_paper_size (struct driver_option *o, int *h, int *v)
{
  if (o->value == NULL || !measure_paper (o->value, h, v))
    measure_paper (o->default_value, h, v);
  driver_option_destroy (o);
}

static int
do_parse_boolean (const char *driver_name, const char *key,
                  const char *value)
{
  if (!strcmp (value, "on") || !strcmp (value, "true")
      || !strcmp (value, "yes") || !strcmp (value, "1"))
    return true;
  else if (!strcmp (value, "off") || !strcmp (value, "false")
           || !strcmp (value, "no") || !strcmp (value, "0"))
    return false;
  else
    {
      msg (MW, _("%s: `%s' is `%s' but a Boolean value is required"),
             driver_name, value, key);
      return -1;
    }
}

/* Parses and return O's value as a Boolean value.  "true" and "false", "yes"
   and "no", "on" and "off", and "1" and "0" are acceptable boolean strings.

   Destroys O. */
bool
parse_boolean (struct driver_option *o)
{
  bool retval;

  retval = do_parse_boolean (o->driver_name, o->name, o->default_value) > 0;
  if (o->value != NULL)
    {
      int value = do_parse_boolean (o->driver_name, o->name, o->value);
      if (value >= 0)
        retval = value;
    }

  driver_option_destroy (o);

  return retval;
}

/* Parses O's value as an enumeration constant.  The arguments to this function
   consist of a series of string/int pairs, terminated by a null pointer value.
   O's value is compared to each string in turn, and parse_enum() returns the
   int associated with the first matching string.  If there is no match, or if
   O has no user-specified value, then O's default value is treated the same
   way.  If the default value still does not match, parse_enum() returns 0.

   Example: parse_enum (o, "a", 1, "b", 2, NULL_SENTINEL) returns 1 if O's
   value if "a", 2 if O's value is "b".

   Destroys O. */
int
parse_enum (struct driver_option *o, ...)
{
  va_list args;
  int retval;

  retval = 0;
  va_start (args, o);
  for (;;)
    {
      const char *s;
      int value;

      s = va_arg (args, const char *);
      if (s == NULL)
        {
          if (o->value != NULL)
            {
              struct string choices;
              int i;

              ds_init_empty (&choices);
              va_end (args);
              va_start (args, o);
              for (i = 0; ; i++)
                {
                  s = va_arg (args, const char *);
                  if (s == NULL)
                    break;
                  value = va_arg (args, int);

                  if (i > 0)
                    ds_put_cstr (&choices, ", ");
                  ds_put_format (&choices, "`%s'", s);
                }

              msg (MW, _("%s: `%s' is `%s' but one of the following "
                             "is required: %s"),
                     o->driver_name, o->name, o->value, ds_cstr (&choices));
              ds_destroy (&choices);
            }
          break;
        }
      value = va_arg (args, int);

      if (o->value != NULL && !strcmp (s, o->value))
        {
          retval = value;
          break;
        }
      else if (!strcmp (s, o->default_value))
        retval = value;
    }
  va_end (args);
  driver_option_destroy (o);
  return retval;
}

/* Parses O's value as an integer in the range MIN_VALUE to MAX_VALUE
   (inclusive) and returns the integer.

   Destroys O. */
int
parse_int (struct driver_option *o, int min_value, int max_value)
{
  int retval = strtol (o->default_value, NULL, 0);

  if (o->value != NULL)
    {
      int value;
      char *tail;

      errno = 0;
      value = strtol (o->value, &tail, 0);
      if (tail != o->value && *tail == '\0' && errno != ERANGE
          && value >= min_value && value <= max_value)
        retval = value;
      else if (max_value == INT_MAX)
        {
          if (min_value == 0)
            msg (MW, _("%s: `%s' is `%s' but a non-negative integer "
                           "is required"),
                   o->driver_name, o->name, o->value);
          else if (min_value == 1)
            msg (MW, _("%s: `%s' is `%s' but a positive integer is "
                           "required"), o->driver_name, o->name, o->value);
          else if (min_value == INT_MIN)
            msg (MW, _("%s: `%s' is `%s' but an integer is required"),
                   o->driver_name, o->name, o->value);
          else
            msg (MW, _("%s: `%s' is `%s' but an integer greater "
                           "than %d is required"),
                   o->driver_name, o->name, o->value, min_value - 1);
        }
      else
        msg (MW, _("%s: `%s' is `%s'  but an integer between %d and "
                       "%d is required"),
               o->driver_name, o->name, o->value, min_value, max_value);
    }

  driver_option_destroy (o);
  return retval;
}

/* Parses O's value as a dimension, as understood by measure_dimension(), and
   returns its length in units of 1/72000".

   Destroys O. */
int
parse_dimension (struct driver_option *o)
{
  int retval;

  retval = (o->value != NULL ? measure_dimension (o->value)
            : o->default_value != NULL ? measure_dimension (o->default_value)
            : -1);

  driver_option_destroy (o);
  return retval;
}

/* Parses O's value as a string and returns it as a malloc'd string that the
   caller is responsible for freeing.

   Destroys O. */
char *
parse_string (struct driver_option *o)
{
  char *retval = xstrdup (o->value != NULL ? o->value : o->default_value);
  driver_option_destroy (o);
  return retval;
}

static char *
default_chart_file_name (const char *file_name)
{
  if (strcmp (file_name, "-"))
    {
      const char *extension = strrchr (file_name, '.');
      int stem_length = extension ? extension - file_name : strlen (file_name);
      return xasprintf ("%.*s-#", stem_length, file_name);
    }
  else
    return NULL;
}

/* Parses and returns a chart file name, or NULL if no charts should be output.
   If a nonnull string is returned, it will contain at least one '#' character,
   which the client will presumably replace by a number as part of writing
   charts to separate files.

   If O->value is "none", then this function returns NULL.

   If O->value is non-NULL but not "none", returns a copy of that string (if it
   contains '#').

   If O->value is NULL, then O's default_value should be the name of the main
   output file.  Returns NULL if default_value is "-", and otherwise returns a
   copy of string string with its extension stripped off and "-#.png" appended.

   Destroys O. */
char *
parse_chart_file_name (struct driver_option *o)
{
  char *chart_file_name;

  if (o->value != NULL)
    {
      if (!strcmp (o->value, "none"))
        chart_file_name = NULL;
      else if (strchr (o->value, '#') != NULL)
        chart_file_name = xstrdup (o->value);
      else
        {
          msg (MW, _("%s: `%s' is `%s' but a file name that contains "
                         "`#' is required."),
               o->driver_name, o->name, o->value);
          chart_file_name = default_chart_file_name (o->default_value);
        }
    }
  else
    chart_file_name = default_chart_file_name (o->default_value);

  driver_option_destroy (o);

  return chart_file_name;
}

static int
lookup_color_name (const char *s)
{
  struct color
    {
      struct hmap_node hmap_node;
      const char *name;
      int code;
    };

  static struct color colors[] =
    {
      { .name = "aliceblue", .code = 0xf0f8ff },
      { .name = "antiquewhite", .code = 0xfaebd7 },
      { .name = "aqua", .code = 0x00ffff },
      { .name = "aquamarine", .code = 0x7fffd4 },
      { .name = "azure", .code = 0xf0ffff },
      { .name = "beige", .code = 0xf5f5dc },
      { .name = "bisque", .code = 0xffe4c4 },
      { .name = "black", .code = 0x000000 },
      { .name = "blanchedalmond", .code = 0xffebcd },
      { .name = "blue", .code = 0x0000ff },
      { .name = "blueviolet", .code = 0x8a2be2 },
      { .name = "brown", .code = 0xa52a2a },
      { .name = "burlywood", .code = 0xdeb887 },
      { .name = "cadetblue", .code = 0x5f9ea0 },
      { .name = "chartreuse", .code = 0x7fff00 },
      { .name = "chocolate", .code = 0xd2691e },
      { .name = "coral", .code = 0xff7f50 },
      { .name = "cornflowerblue", .code = 0x6495ed },
      { .name = "cornsilk", .code = 0xfff8dc },
      { .name = "crimson", .code = 0xdc143c },
      { .name = "cyan", .code = 0x00ffff },
      { .name = "darkblue", .code = 0x00008b },
      { .name = "darkcyan", .code = 0x008b8b },
      { .name = "darkgoldenrod", .code = 0xb8860b },
      { .name = "darkgray", .code = 0xa9a9a9 },
      { .name = "darkgreen", .code = 0x006400 },
      { .name = "darkgrey", .code = 0xa9a9a9 },
      { .name = "darkkhaki", .code = 0xbdb76b },
      { .name = "darkmagenta", .code = 0x8b008b },
      { .name = "darkolivegreen", .code = 0x556b2f },
      { .name = "darkorange", .code = 0xff8c00 },
      { .name = "darkorchid", .code = 0x9932cc },
      { .name = "darkred", .code = 0x8b0000 },
      { .name = "darksalmon", .code = 0xe9967a },
      { .name = "darkseagreen", .code = 0x8fbc8f },
      { .name = "darkslateblue", .code = 0x483d8b },
      { .name = "darkslategray", .code = 0x2f4f4f },
      { .name = "darkslategrey", .code = 0x2f4f4f },
      { .name = "darkturquoise", .code = 0x00ced1 },
      { .name = "darkviolet", .code = 0x9400d3 },
      { .name = "deeppink", .code = 0xff1493 },
      { .name = "deepskyblue", .code = 0x00bfff },
      { .name = "dimgray", .code = 0x696969 },
      { .name = "dimgrey", .code = 0x696969 },
      { .name = "dodgerblue", .code = 0x1e90ff },
      { .name = "firebrick", .code = 0xb22222 },
      { .name = "floralwhite", .code = 0xfffaf0 },
      { .name = "forestgreen", .code = 0x228b22 },
      { .name = "fuchsia", .code = 0xff00ff },
      { .name = "gainsboro", .code = 0xdcdcdc },
      { .name = "ghostwhite", .code = 0xf8f8ff },
      { .name = "gold", .code = 0xffd700 },
      { .name = "goldenrod", .code = 0xdaa520 },
      { .name = "gray", .code = 0x808080 },
      { .name = "green", .code = 0x008000 },
      { .name = "greenyellow", .code = 0xadff2f },
      { .name = "grey", .code = 0x808080 },
      { .name = "honeydew", .code = 0xf0fff0 },
      { .name = "hotpink", .code = 0xff69b4 },
      { .name = "indianred", .code = 0xcd5c5c },
      { .name = "indigo", .code = 0x4b0082 },
      { .name = "ivory", .code = 0xfffff0 },
      { .name = "khaki", .code = 0xf0e68c },
      { .name = "lavender", .code = 0xe6e6fa },
      { .name = "lavenderblush", .code = 0xfff0f5 },
      { .name = "lawngreen", .code = 0x7cfc00 },
      { .name = "lemonchiffon", .code = 0xfffacd },
      { .name = "lightblue", .code = 0xadd8e6 },
      { .name = "lightcoral", .code = 0xf08080 },
      { .name = "lightcyan", .code = 0xe0ffff },
      { .name = "lightgoldenrodyellow", .code = 0xfafad2 },
      { .name = "lightgray", .code = 0xd3d3d3 },
      { .name = "lightgreen", .code = 0x90ee90 },
      { .name = "lightgrey", .code = 0xd3d3d3 },
      { .name = "lightpink", .code = 0xffb6c1 },
      { .name = "lightsalmon", .code = 0xffa07a },
      { .name = "lightseagreen", .code = 0x20b2aa },
      { .name = "lightskyblue", .code = 0x87cefa },
      { .name = "lightslategray", .code = 0x778899 },
      { .name = "lightslategrey", .code = 0x778899 },
      { .name = "lightsteelblue", .code = 0xb0c4de },
      { .name = "lightyellow", .code = 0xffffe0 },
      { .name = "lime", .code = 0x00ff00 },
      { .name = "limegreen", .code = 0x32cd32 },
      { .name = "linen", .code = 0xfaf0e6 },
      { .name = "magenta", .code = 0xff00ff },
      { .name = "maroon", .code = 0x800000 },
      { .name = "mediumaquamarine", .code = 0x66cdaa },
      { .name = "mediumblue", .code = 0x0000cd },
      { .name = "mediumorchid", .code = 0xba55d3 },
      { .name = "mediumpurple", .code = 0x9370db },
      { .name = "mediumseagreen", .code = 0x3cb371 },
      { .name = "mediumslateblue", .code = 0x7b68ee },
      { .name = "mediumspringgreen", .code = 0x00fa9a },
      { .name = "mediumturquoise", .code = 0x48d1cc },
      { .name = "mediumvioletred", .code = 0xc71585 },
      { .name = "midnightblue", .code = 0x191970 },
      { .name = "mintcream", .code = 0xf5fffa },
      { .name = "mistyrose", .code = 0xffe4e1 },
      { .name = "moccasin", .code = 0xffe4b5 },
      { .name = "navajowhite", .code = 0xffdead },
      { .name = "navy", .code = 0x000080 },
      { .name = "oldlace", .code = 0xfdf5e6 },
      { .name = "olive", .code = 0x808000 },
      { .name = "olivedrab", .code = 0x6b8e23 },
      { .name = "orange", .code = 0xffa500 },
      { .name = "orangered", .code = 0xff4500 },
      { .name = "orchid", .code = 0xda70d6 },
      { .name = "palegoldenrod", .code = 0xeee8aa },
      { .name = "palegreen", .code = 0x98fb98 },
      { .name = "paleturquoise", .code = 0xafeeee },
      { .name = "palevioletred", .code = 0xdb7093 },
      { .name = "papayawhip", .code = 0xffefd5 },
      { .name = "peachpuff", .code = 0xffdab9 },
      { .name = "peru", .code = 0xcd853f },
      { .name = "pink", .code = 0xffc0cb },
      { .name = "plum", .code = 0xdda0dd },
      { .name = "powderblue", .code = 0xb0e0e6 },
      { .name = "purple", .code = 0x800080 },
      { .name = "red", .code = 0xff0000 },
      { .name = "rosybrown", .code = 0xbc8f8f },
      { .name = "royalblue", .code = 0x4169e1 },
      { .name = "saddlebrown", .code = 0x8b4513 },
      { .name = "salmon", .code = 0xfa8072 },
      { .name = "sandybrown", .code = 0xf4a460 },
      { .name = "seagreen", .code = 0x2e8b57 },
      { .name = "seashell", .code = 0xfff5ee },
      { .name = "sienna", .code = 0xa0522d },
      { .name = "silver", .code = 0xc0c0c0 },
      { .name = "skyblue", .code = 0x87ceeb },
      { .name = "slateblue", .code = 0x6a5acd },
      { .name = "slategray", .code = 0x708090 },
      { .name = "slategrey", .code = 0x708090 },
      { .name = "snow", .code = 0xfffafa },
      { .name = "springgreen", .code = 0x00ff7f },
      { .name = "steelblue", .code = 0x4682b4 },
      { .name = "tan", .code = 0xd2b48c },
      { .name = "teal", .code = 0x008080 },
      { .name = "thistle", .code = 0xd8bfd8 },
      { .name = "tomato", .code = 0xff6347 },
      { .name = "turquoise", .code = 0x40e0d0 },
      { .name = "violet", .code = 0xee82ee },
      { .name = "wheat", .code = 0xf5deb3 },
      { .name = "white", .code = 0xffffff },
      { .name = "whitesmoke", .code = 0xf5f5f5 },
      { .name = "yellow", .code = 0xffff00 },
      { .name = "yellowgreen", .code = 0x9acd32 },
    };

  static struct hmap color_table = HMAP_INITIALIZER (color_table);

  if (hmap_is_empty (&color_table))
    for (size_t i = 0; i < sizeof colors / sizeof *colors; i++)
      hmap_insert (&color_table, &colors[i].hmap_node,
                   hash_string (colors[i].name, 0));

  const struct color *color;
  HMAP_FOR_EACH_WITH_HASH (color, struct color, hmap_node,
                           hash_string (s, 0), &color_table)
    if (!strcmp (color->name, s))
      return color->code;
  return -1;
}

bool
parse_color__ (const char *s, struct cell_color *color)
{
  /* #rrrrggggbbbb */
  uint16_t r16, g16, b16;
  int len;
  if (sscanf (s, "#%4"SCNx16"%4"SCNx16"%4"SCNx16"%n",
              &r16, &g16, &b16, &len) == 3
      && len == 13
      && !s[len])
    {
      color->r = r16 >> 8;
      color->g = g16 >> 8;
      color->b = b16 >> 8;
      color->alpha = 255;
      return true;
    }

  /* #rrggbb */
  uint8_t r, g, b;
  if (sscanf (s, "#%2"SCNx8"%2"SCNx8"%2"SCNx8"%n", &r, &g, &b, &len) == 3
      && len == 7
      && !s[len])
    {
      color->r = r;
      color->g = g;
      color->b = b;
      color->alpha = 255;
      return true;
    }

  /* rrggbb */
  if (sscanf (s, "%2"SCNx8"%2"SCNx8"%2"SCNx8"%n", &r, &g, &b, &len) == 3
      && len == 6
      && !s[len])
    {
      color->r = r;
      color->g = g;
      color->b = b;
      color->alpha = 255;
      return true;
    }

  /* rgb(r,g,b) */
  if (sscanf (s, "rgb (%"SCNi8" , %"SCNi8" , %"SCNi8") %n",
              &r, &g, &b, &len) == 3
      && !s[len])
    {
      color->r = r;
      color->g = g;
      color->b = b;
      color->alpha = 255;
      return true;
    }

  /* rgba(r,g,b,a), ignoring a. */
  double alpha;
  if (sscanf (s, "rgba (%"SCNi8" , %"SCNi8" , %"SCNi8", %lf) %n",
              &r, &g, &b, &alpha, &len) == 4
      && !s[len])
    {
      color->r = r;
      color->g = g;
      color->b = b;
      color->alpha = alpha <= 0 ? 0 : alpha >= 1 ? 255 : alpha * 255.0;
      return true;
    }

  int code = lookup_color_name (s);
  if (code >= 0)
    {
      color->r = code >> 16;
      color->g = code >> 8;
      color->b = code;
      color->alpha = 255;
      return true;
    }

  if (!strcmp (s, "transparent"))
    {
      *color = (struct cell_color) { .alpha = 0 };
      return true;
    }

  return false;
}

/* Parses and returns color information from O. */
struct cell_color
parse_color (struct driver_option *o)
{
  struct cell_color color = CELL_COLOR_BLACK;
  parse_color__ (o->default_value, &color);
  if (o->value)
    {
      if (!parse_color__ (o->value, &color))
        msg (MW, _("%s: `%s' is `%s', which could not be parsed as a color"),
             o->driver_name, o->name, o->value);
    }
  driver_option_destroy (o);
  return color;
}

