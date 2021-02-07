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

#include "output/spv/spvbin-helpers.h"

#include <inttypes.h>
#include <string.h>

#include "libpspp/float-format.h"
#include "libpspp/integer-format.h"
#include "libpspp/str.h"

#include "gl/xmemdup0.h"

void
spvbin_input_init (struct spvbin_input *input, const void *data, size_t size)
{
  *input = (struct spvbin_input) { .data = data, .size = size };
}

bool
spvbin_input_at_end (const struct spvbin_input *input)
{
  return input->ofs >= input->size;
}

char *
spvbin_input_to_error (const struct spvbin_input *input, const char *name)
{
  struct string s = DS_EMPTY_INITIALIZER;
  if (name)
    ds_put_format (&s, "%s: ", name);
  ds_put_cstr (&s, "parse error decoding ");
  for (size_t i = input->n_errors; i-- > 0;)
    if (i < SPVBIN_MAX_ERRORS)
      ds_put_format (&s, "/%s@%#zx", input->errors[i].name,
                     input->errors[i].start);
  ds_put_format (&s, " near %#zx", input->error_ofs);
  return ds_steal_cstr (&s);
}


bool
spvbin_match_bytes (struct spvbin_input *input, const void *bytes, size_t n)
{
  if (input->size - input->ofs < n
      || memcmp (&input->data[input->ofs], bytes, n))
    return false;

  input->ofs += n;
  return true;
}

bool
spvbin_match_byte (struct spvbin_input *input, uint8_t byte)
{
  return spvbin_match_bytes (input, &byte, 1);
}

bool
spvbin_parse_bool (struct spvbin_input *input, bool *p)
{
  if (input->ofs >= input->size || input->data[input->ofs] > 1)
    return false;
  if (p)
    *p = input->data[input->ofs];
  input->ofs++;
  return true;
}

static const void *
spvbin_parse__ (struct spvbin_input *input, size_t n)
{
  if (input->size - input->ofs < n)
    return NULL;

  const void *src = &input->data[input->ofs];
  input->ofs += n;
  return src;
}

bool
spvbin_parse_byte (struct spvbin_input *input, uint8_t *p)
{
  const void *src = spvbin_parse__ (input, sizeof *p);
  if (src && p)
    *p = *(const uint8_t *) src;
  return src != NULL;
}

bool
spvbin_parse_int16 (struct spvbin_input *input, uint16_t *p)
{
  const void *src = spvbin_parse__ (input, sizeof *p);
  if (src && p)
    *p = le_to_native16 (get_uint16 (src));
  return src != NULL;
}

bool
spvbin_parse_int32 (struct spvbin_input *input, uint32_t *p)
{
  const void *src = spvbin_parse__ (input, sizeof *p);
  if (src && p)
    *p = le_to_native32 (get_uint32 (src));
  return src != NULL;
}

bool
spvbin_parse_int64 (struct spvbin_input *input, uint64_t *p)
{
  const void *src = spvbin_parse__ (input, sizeof *p);
  if (src && p)
    *p = le_to_native64 (get_uint64 (src));
  return src != NULL;
}

bool
spvbin_parse_be16 (struct spvbin_input *input, uint16_t *p)
{
  const void *src = spvbin_parse__ (input, sizeof *p);
  if (src && p)
    *p = be_to_native16 (get_uint16 (src));
  return src != NULL;
}

bool
spvbin_parse_be32 (struct spvbin_input *input, uint32_t *p)
{
  const void *src = spvbin_parse__ (input, sizeof *p);
  if (src && p)
    *p = be_to_native32 (get_uint32 (src));
  return src != NULL;
}

bool
spvbin_parse_be64 (struct spvbin_input *input, uint64_t *p)
{
  const void *src = spvbin_parse__ (input, sizeof *p);
  if (src && p)
    *p = be_to_native64 (get_uint64 (src));
  return src != NULL;
}

bool
spvbin_parse_double (struct spvbin_input *input, double *p)
{
  const void *src = spvbin_parse__ (input, 8);
  if (src && p)
    *p = float_get_double (FLOAT_IEEE_DOUBLE_LE, src);
  return src != NULL;
}

bool
spvbin_parse_float (struct spvbin_input *input, double *p)
{
  const void *src = spvbin_parse__ (input, 4);
  if (src && p)
    *p = float_get_double (FLOAT_IEEE_SINGLE_LE, src);
  return src != NULL;
}

static bool
spvbin_parse_string__ (struct spvbin_input *input,
                       uint32_t (*raw_to_native32) (uint32_t),
                       char **p)
{
  if (p)
    *p = NULL;

  uint32_t length;
  if (input->size - input->ofs < sizeof length)
    return false;

  const uint8_t *src = &input->data[input->ofs];
  length = raw_to_native32 (get_uint32 (src));
  if (input->size - input->ofs - sizeof length < length)
    return false;

  if (p)
    *p = xmemdup0 (src + sizeof length, length);
  input->ofs += sizeof length + length;
  return true;
}

bool
spvbin_parse_string (struct spvbin_input *input, char **p)
{
  return spvbin_parse_string__ (input, le_to_native32, p);
}

bool
spvbin_parse_bestring (struct spvbin_input *input, char **p)
{
  return spvbin_parse_string__ (input, be_to_native32, p);
}

void
spvbin_error (struct spvbin_input *input, const char *name, size_t start)
{
  if (!input->n_errors)
    input->error_ofs = input->ofs;

  /* We keep track of the error depth regardless of whether we can store all of
     them.  The parser needs this to accurately save and restore error
     state. */
  if (input->n_errors < SPVBIN_MAX_ERRORS)
    {
      input->errors[input->n_errors].name = name;
      input->errors[input->n_errors].start = start;
    }
  input->n_errors++;
}

void
spvbin_print_header (const char *title, size_t start UNUSED, size_t len UNUSED, int indent)
{
  for (int i = 0; i < indent * 4; i++)
    putchar (' ');
  fputs (title, stdout);
#if 0
  if (start != SIZE_MAX)
    printf (" (0x%zx, %zu)", start, len);
#endif
  fputs (": ", stdout);
}

void
spvbin_print_presence (const char *title, int indent, bool present)
{
  spvbin_print_header (title, -1, -1, indent);
  puts (present ? "present" : "absent");
}

void
spvbin_print_bool (const char *title, int indent, bool x)
{
  spvbin_print_header (title, -1, -1, indent);
  printf ("%s\n", x ? "true" : "false");
}

void
spvbin_print_byte (const char *title, int indent, uint8_t x)
{
  spvbin_print_header (title, -1, -1, indent);
  printf ("%"PRIu8"\n", x);
}

void
spvbin_print_int16 (const char *title, int indent, uint16_t x)
{
  spvbin_print_header (title, -1, -1, indent);
  printf ("%"PRIu16"\n", x);
}

void
spvbin_print_int32 (const char *title, int indent, uint32_t x)
{
  spvbin_print_header (title, -1, -1, indent);
  printf ("%"PRIu32"\n", x);
}

void
spvbin_print_int64 (const char *title, int indent, uint64_t x)
{
  spvbin_print_header (title, -1, -1, indent);
  printf ("%"PRIu64"\n", x);
}

void
spvbin_print_double (const char *title, int indent, double x)
{
  spvbin_print_header (title, -1, -1, indent);
  printf ("%g\n", x);
}

void
spvbin_print_string (const char *title, int indent, const char *s)
{
  spvbin_print_header (title, -1, -1, indent);
  if (s)
    printf ("\"%s\"\n", s);
  else
    printf ("none\n");
}

void
spvbin_print_case (const char *title, int indent, int x)
{
  spvbin_print_header (title, -1, -1, indent);
  printf ("%d\n", x);
}

struct spvbin_position
spvbin_position_save (const struct spvbin_input *input)
{
  struct spvbin_position pos = { input->ofs };
  return pos;
}

void
spvbin_position_restore (struct spvbin_position *pos,
                         struct spvbin_input *input)
{
  input->ofs = pos->ofs;
}

static bool
spvbin_limit_parse__ (struct spvbin_limit *limit, struct spvbin_input *input,
                      uint32_t (*raw_to_native32) (uint32_t))
{
  limit->size = input->size;

  uint32_t count;
  if (input->size - input->ofs < sizeof count)
    return false;

  const uint8_t *src = &input->data[input->ofs];
  count = raw_to_native32 (get_uint32 (src));
  if (input->size - input->ofs - sizeof count < count)
    return false;

  input->ofs += sizeof count;
  input->size = input->ofs + count;
  return true;
}

bool
spvbin_limit_parse (struct spvbin_limit *limit, struct spvbin_input *input)
{
  return spvbin_limit_parse__ (limit, input, le_to_native32);
}

bool
spvbin_limit_parse_be (struct spvbin_limit *limit, struct spvbin_input *input)
{
  return spvbin_limit_parse__ (limit, input, be_to_native32);
}

void
spvbin_limit_pop (struct spvbin_limit *limit, struct spvbin_input *input)
{
  input->size = limit->size;
}
