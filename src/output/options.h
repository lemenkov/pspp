/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010 Free Software Foundation, Inc.

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

#ifndef OUTPUT_OPTIONS_H
#define OUTPUT_OPTIONS_H 1

/* Helper functions for driver option parsing. */

#include <stdbool.h>
#include "libpspp/compiler.h"
#include "libpspp/string-map.h"
#include "libpspp/string-array.h"

struct output_driver;
struct string_map;

struct driver_options
  {
    const char *driver_name;
    struct string_map map;
    struct string_array garbage;
  };

/* An option being parsed. */
struct driver_option
  {
    const char *driver_name;    /* Driver's name, for use in error messages. */
    const char *name;           /* Option name, for use in error messages.  */
    const char *value;          /* Value supplied by user (NULL if none). */
    const char *default_value;  /* Default value supplied by driver. */
  };

struct driver_option driver_option_get (struct driver_options *,
                                        const char *name,
                                        const char *default_value);

void parse_paper_size (struct driver_option, double *h, double *v);
bool parse_boolean (struct driver_option);

int parse_enum (struct driver_option, ...) SENTINEL(0);
#define parse_enum(...) parse_enum(__VA_ARGS__, NULL_SENTINEL)

int parse_int (struct driver_option, int min_value, int max_value);
double parse_dimension (struct driver_option);
char *parse_string (struct driver_option);
char *parse_chart_file_name (struct driver_option);

struct cell_color parse_color (struct driver_option);
bool parse_color__ (const char *, struct cell_color *);

#endif /* output/options.h */
