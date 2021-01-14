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

#include "output/spv/spv-legacy-data.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/cast.h"
#include "libpspp/float-format.h"
#include "data/val-type.h"
#include "output/spv/old-binary-parser.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"
#include "gl/xsize.h"
#include "gl/xvasprintf.h"

void
spv_data_uninit (struct spv_data *data)
{
  if (!data)
    return;

  for (size_t i = 0; i < data->n_sources; i++)
    spv_data_source_uninit (&data->sources[i]);
  free (data->sources);
}

void
spv_data_dump (const struct spv_data *data, FILE *stream)
{
  for (size_t i = 0; i < data->n_sources; i++)
    {
      if (i > 0)
        putc ('\n', stream);
      spv_data_source_dump (&data->sources[i], stream);
    }
}

struct spv_data_source *
spv_data_find_source (const struct spv_data *data, const char *source_name)
{
  for (size_t i = 0; i < data->n_sources; i++)
    if (!strcmp (data->sources[i].source_name, source_name))
      return &data->sources[i];

  return NULL;
}

struct spv_data_variable *
spv_data_find_variable (const struct spv_data *data,
                        const char *source_name,
                        const char *variable_name)
{
  struct spv_data_source *source = spv_data_find_source (data, source_name);
  return source ? spv_data_source_find_variable (source, variable_name) : NULL;
}

void
spv_data_source_uninit (struct spv_data_source *source)
{
  if (!source)
    return;

  for (size_t i = 0; i < source->n_vars; i++)
    spv_data_variable_uninit (&source->vars[i]);
  free (source->vars);
  free (source->source_name);
}

void
spv_data_source_dump (const struct spv_data_source *source, FILE *stream)
{
  fprintf (stream, "source \"%s\" (%zu values):\n",
           source->source_name, source->n_values);
  for (size_t i = 0; i < source->n_vars; i++)
    spv_data_variable_dump (&source->vars[i], stream);
}

struct spv_data_variable *
spv_data_source_find_variable (const struct spv_data_source *source,
                               const char *variable_name)
{
  for (size_t i = 0; i < source->n_vars; i++)
    if (!strcmp (source->vars[i].var_name, variable_name))
      return &source->vars[i];
  return NULL;
}

void
spv_data_variable_uninit (struct spv_data_variable *var)
{
  if (!var)
    return;

  free (var->var_name);
  for (size_t i = 0; i < var->n_values; i++)
    spv_data_value_uninit (&var->values[i]);
  free (var->values);
}

void
spv_data_variable_dump (const struct spv_data_variable *var, FILE *stream)
{
  fprintf (stream, "variable \"%s\":", var->var_name);
  for (size_t i = 0; i < var->n_values; i++)
    {
      if (i)
        putc (',', stream);
      putc (' ', stream);
      spv_data_value_dump (&var->values[i], stream);
    }
  putc ('\n', stream);
}

void
spv_data_value_uninit (struct spv_data_value *value)
{
  if (value && value->width >= 0)
    free (value->s);
}

bool
spv_data_value_equal (const struct spv_data_value *a,
                      const struct spv_data_value *b)
{
  return (a->width == b->width
          && a->index == b->index
          && (a->width < 0
              ? a->d == b->d
              : !strcmp (a->s, b->s)));
}

struct spv_data_value *
spv_data_values_clone (const struct spv_data_value *src, size_t n)
{
  struct spv_data_value *dst = xmemdup (src, n * sizeof *src);
  for (size_t i = 0; i < n; i++)
    if (dst[i].width >= 0)
      dst[i].s = xstrdup (dst[i].s);
  return dst;
}

void
spv_data_value_dump (const struct spv_data_value *value, FILE *stream)
{
  if (value->index != SYSMIS)
    fprintf (stream, "%.*ge-", DBL_DIG + 1, value->index);
  if (value->width >= 0)
    fprintf (stream, "\"%s\"", value->s);
  else if (value->d == SYSMIS)
    putc ('.', stream);
  else
    fprintf (stream, "%.*g", DBL_DIG + 1, value->d);
}

static char *
decode_fixed_string (const uint8_t *buf_, size_t size)
{
  const char *buf = CHAR_CAST (char *, buf_);
  return xmemdup0 (buf, strnlen (buf, size));
}

static char *
decode_var_name (const struct spvob_metadata *md)
{
  int n0 = strnlen ((char *) md->source_name, sizeof md->source_name);
  int n1 = (n0 < sizeof md->source_name ? 0
            : strnlen ((char *) md->ext_source_name,
                       sizeof md->ext_source_name));
  return xasprintf ("%.*s%.*s",
                    n0, (char *) md->source_name,
                    n1, (char *) md->ext_source_name);
}

static char * WARN_UNUSED_RESULT
decode_data (const uint8_t *in, size_t size, size_t data_offset,
             struct spv_data_source *source, size_t *end_offsetp)
{
  size_t var_size = xsum (288, xtimes (source->n_values, 8));
  size_t source_size = xtimes (source->n_vars, var_size);
  size_t end_offset = xsum (data_offset, source_size);
  if (size_overflow_p (end_offset))
    return xasprintf ("Data source \"%s\" exceeds supported %zu-byte size.",
                      source->source_name, SIZE_MAX - 1);
  if (end_offset > size)
    return xasprintf ("%zu-byte data source \"%s\" starting at offset %#zx "
                      "runs past end of %zu-byte ZIP member.",
                      source_size, source->source_name, data_offset,
                      size);

  in += data_offset;
  for (size_t i = 0; i < source->n_vars; i++)
    {
      struct spv_data_variable *var = &source->vars[i];
      var->var_name = decode_fixed_string (in, 288);
      in += 288;

      var->values = xnmalloc (source->n_values, sizeof *var->values);
      var->n_values = source->n_values;
      for (size_t j = 0; j < source->n_values; j++)
        {
          var->values[j].index = SYSMIS;
          var->values[j].width = -1;
          var->values[j].d = float_get_double (FLOAT_IEEE_DOUBLE_LE, in);
          in += 8;
        }
    }

  *end_offsetp = end_offset;
  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_variable_map (const char *source_name,
                     const struct spvob_variable_map *in,
                     const struct spvob_labels *labels,
                     struct spv_data_variable *out)
{
  if (strcmp (in->variable_name, out->var_name))
    return xasprintf ("Source \"%s\" variable \"%s\" mapping is associated "
                      "with wrong variable \"%s\".",
                      source_name, out->var_name, in->variable_name);

  for (size_t i = 0; i < in->n_data; i++)
    {
      const struct spvob_datum_map *map = in->data[i];

      if (map->value_idx >= out->n_values)
        return xasprintf ("Source \"%s\" variable \"%s\" mapping %zu "
                          "attempts to set 0-based value %"PRIu32" "
                          "but source has only %zu values.",
                          source_name, out->var_name, i,
                          map->value_idx, out->n_values);
      struct spv_data_value *value = &out->values[map->value_idx];

      if (map->label_idx >= labels->n_labels)
        return xasprintf ("Source \"%s\" variable \"%s\" mapping %zu "
                          "attempts to set value %"PRIu32" to 0-based label "
                          "%"PRIu32" but only %"PRIu32" labels are present.",
                          source_name, out->var_name, i,
                          map->value_idx, map->label_idx, labels->n_labels);
      const struct spvob_label *label = labels->labels[map->label_idx];

      if (value->width >= 0)
        return xasprintf ("Source \"%s\" variable \"%s\" mapping %zu "
                          "attempts to change string value %"PRIu32".",
                          source_name, out->var_name, i,
                          map->value_idx);
#if 0
      else if (value->d != SYSMIS && !isnan (value->d))
        {
#if 1
          return NULL;
#else
          return xasprintf ("Source \"%s\" variable \"%s\" mapping %zu "
                          "attempts to change non-missing value %"PRIu32" "
                          "into \"%s\".",
                          source_name, out->var_name, i,
                          map->value_idx,
                          label->label);
#endif
        }
#endif

      value->width = strlen (label->label);
      value->s = xmemdup0 (label->label, value->width);
    }

  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_source_map (const struct spvob_source_map *in,
                   const struct spvob_labels *labels,
                   struct spv_data_source *out)
{
  if (in->n_variables > out->n_vars)
    return xasprintf ("source map for \"%s\" has %"PRIu32" variables but "
                      "source has only %zu",
                      out->source_name, in->n_variables, out->n_vars);

  for (size_t i = 0; i < in->n_variables; i++)
    {
      char *error = decode_variable_map (out->source_name, in->variables[i],
                                         labels, &out->vars[i]);
      if (error)
        return error;
    }

  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_strings (const struct spvob_strings *in, struct spv_data *out)
{
  for (size_t i = 0; i < in->maps->n_maps; i++)
    {
      const struct spvob_source_map *sm = in->maps->maps[i];
      const char *name = sm->source_name;
      struct spv_data_source *source = spv_data_find_source (out, name);
      if (!source)
        return xasprintf ("cannot decode source map for unknown source \"%s\"",
                          name);

      char *error = decode_source_map (sm, in->labels, source);
      if (error)
        return error;
    }

  return NULL;
}

char * WARN_UNUSED_RESULT
spv_legacy_data_decode (const uint8_t *in, size_t size, struct spv_data *out)
{
  char *error = NULL;
  *out = (struct spv_data) SPV_DATA_INITIALIZER;

  struct spvbin_input input;
  spvbin_input_init (&input, in, size);

  struct spvob_legacy_binary *lb;
  bool ok = spvob_parse_legacy_binary (&input, &lb);
  if (!ok)
    {
      error = spvbin_input_to_error (&input, NULL);
      goto error;
    }

  out->sources = XCALLOC (lb->n_sources, struct spv_data_source);
  out->n_sources = lb->n_sources;

  for (size_t i = 0; i < lb->n_sources; i++)
    {
      const struct spvob_metadata *md = lb->metadata[i];
      struct spv_data_source *source = &out->sources[i];

      source->source_name = decode_var_name (md);
      source->n_vars = md->n_variables;
      source->n_values = md->n_values;
      source->vars = xcalloc (md->n_variables, sizeof *source->vars);

      size_t end = -1;
      error = decode_data (in, size, md->data_offset, source, &end);
      if (error)
        goto error;

      input.ofs = MAX (input.ofs, end);
    }

  if (input.ofs < input.size)
    {
      struct spvob_strings *strings;
      bool ok = spvob_parse_strings (&input, &strings);
      if (!ok)
        error = spvbin_input_to_error (&input, NULL);
      else
        {
          if (input.ofs != input.size)
            error = xasprintf ("expected end of file at offset #%zx",
                               input.ofs);
          else
            error = decode_strings (strings, out);
          spvob_free_strings (strings);
        }

      if (error)
        goto error;
    }

  spvob_free_legacy_binary (lb);

  return NULL;

error:
  spv_data_uninit (out);
  *out = (struct spv_data) SPV_DATA_INITIALIZER;
  spvob_free_legacy_binary (lb);
  return error;
}
