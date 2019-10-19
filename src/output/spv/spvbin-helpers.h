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

#ifndef SPVBIN_HELPERS_H
#define SPVBIN_HELPERS_H 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct spvbin_input
  {
    const uint8_t *data;
    size_t ofs;
    size_t size;
    int version;

#define SPVBIN_MAX_ERRORS 16
    struct
      {
        const char *name;
        size_t start;
      }
    errors[SPVBIN_MAX_ERRORS];
    size_t n_errors;
    size_t error_ofs;
  };

void spvbin_input_init (struct spvbin_input *, const void *, size_t);
bool spvbin_input_at_end (const struct spvbin_input *);

char *spvbin_input_to_error (const struct spvbin_input *, const char *name);

bool spvbin_match_bytes (struct spvbin_input *, const void *, size_t);
bool spvbin_match_byte (struct spvbin_input *, uint8_t);

bool spvbin_parse_bool (struct spvbin_input *, bool *);
bool spvbin_parse_byte (struct spvbin_input *, uint8_t *);
bool spvbin_parse_int16 (struct spvbin_input *, uint16_t *);
bool spvbin_parse_int32 (struct spvbin_input *, uint32_t *);
bool spvbin_parse_int64 (struct spvbin_input *, uint64_t *);
bool spvbin_parse_be16 (struct spvbin_input *, uint16_t *);
bool spvbin_parse_be32 (struct spvbin_input *, uint32_t *);
bool spvbin_parse_be64 (struct spvbin_input *, uint64_t *);
bool spvbin_parse_double (struct spvbin_input *, double *);
bool spvbin_parse_float (struct spvbin_input *, double *);
bool spvbin_parse_string (struct spvbin_input *, char **);
bool spvbin_parse_bestring (struct spvbin_input *, char **);

void spvbin_error (struct spvbin_input *, const char *name, size_t start);

void spvbin_print_header (const char *title, size_t start, size_t len,
                          int indent);
void spvbin_print_presence (const char *title, int indent, bool);
void spvbin_print_bool (const char *title, int indent, bool);
void spvbin_print_byte (const char *title, int indent, uint8_t);
void spvbin_print_int16 (const char *title, int indent, uint16_t);
void spvbin_print_int32 (const char *title, int indent, uint32_t);
void spvbin_print_int64 (const char *title, int indent, uint64_t);
void spvbin_print_double (const char *title, int indent, double);
void spvbin_print_string (const char *title, int indent, const char *);
void spvbin_print_case (const char *title, int indent, int);

struct spvbin_position
  {
    size_t ofs;
  };

struct spvbin_position spvbin_position_save (const struct spvbin_input *);
void spvbin_position_restore (struct spvbin_position *, struct spvbin_input *);

struct spvbin_limit
  {
    size_t size;
  };

bool spvbin_limit_parse (struct spvbin_limit *, struct spvbin_input *);
bool spvbin_limit_parse_be (struct spvbin_limit *, struct spvbin_input *);
void spvbin_limit_pop (struct spvbin_limit *, struct spvbin_input *);

#endif /* output/spv/spvbin-helpers.h */
