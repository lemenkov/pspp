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

#ifndef OUTPUT_SPV_LEGACY_DATA_H
#define OUTPUT_SPV_LEGACY_DATA_H 1

/* SPSS Viewer (SPV) legacy binary data decoder.

   Used by spv.h, not useful directly. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "libpspp/compiler.h"

struct spv_data
  {
    struct spv_data_source *sources;
    size_t n_sources;
  };

#define SPV_DATA_INITIALIZER { NULL, 0 }

void spv_data_uninit (struct spv_data *);
void spv_data_dump (const struct spv_data *, FILE *);

struct spv_data_source *spv_data_find_source (const struct spv_data *,
                                              const char *source_name);
struct spv_data_variable *spv_data_find_variable (const struct spv_data *,
                                                  const char *source_name,
                                                  const char *variable_name);

struct spv_data_source
  {
    char *source_name;
    struct spv_data_variable *vars;
    size_t n_vars, n_values;
  };

void spv_data_source_uninit (struct spv_data_source *);
void spv_data_source_dump (const struct spv_data_source *, FILE *);

struct spv_data_variable *spv_data_source_find_variable (
  const struct spv_data_source *, const char *variable_name);

struct spv_data_variable
  {
    char *var_name;
    struct spv_data_value *values;
    size_t n_values;
  };

void spv_data_variable_uninit (struct spv_data_variable *);
void spv_data_variable_dump (const struct spv_data_variable *, FILE *);

struct spv_data_value
  {
    double index;
    int width;
    union
      {
        double d;
        char *s;
      };
  };

void spv_data_value_uninit (struct spv_data_value *);
bool spv_data_value_equal (const struct spv_data_value *,
                           const struct spv_data_value *);
struct spv_data_value *spv_data_values_clone (const struct spv_data_value *,
                                              size_t n);

char *spv_legacy_data_decode (const uint8_t *in, size_t size,
                              struct spv_data *out) WARN_UNUSED_RESULT;
void spv_data_value_dump (const struct spv_data_value *, FILE *);

#endif /* output/spv/spv-legacy-data.h */
