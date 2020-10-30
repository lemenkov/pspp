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

#include "output/spv/spv-light-decoder.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "output/pivot-table.h"
#include "output/spv/light-binary-parser.h"
#include "output/spv/spv.h"

#include "gl/xalloc.h"
#include "gl/xsize.h"

static char *
to_utf8 (const char *s, const char *encoding)
{
  return recode_string ("UTF-8", encoding, s, strlen (s));
}

static char *
to_utf8_if_nonempty (const char *s, const char *encoding)
{
  return s && s[0] ? to_utf8 (s, encoding) : NULL;
}

static void
convert_widths (const uint32_t *in, uint32_t n, int **out, size_t *n_out)
{
  if (n)
    {
      *n_out = n;
      *out = xnmalloc (n, sizeof **out);
      for (size_t i = 0; i < n; i++)
        (*out)[i] = in[i];
    }
}

static void
convert_breakpoints (const struct spvlb_breakpoints *in,
                     size_t **out, size_t *n_out)
{
  if (in && in->n_breaks)
    {
      *n_out = in->n_breaks;
      *out = xnmalloc (in->n_breaks, sizeof *out);
      for (size_t i = 0; i < in->n_breaks; i++)
        (*out)[i] = in->breaks[i];
    }
}

static void
convert_keeps (const struct spvlb_keeps *in,
               struct pivot_keep **out, size_t *n_out)
{
  if (in && in->n_keeps)
    {
      *n_out = in->n_keeps;
      *out = xnmalloc (*n_out, sizeof **out);
      for (size_t i = 0; i < *n_out; i++)
        {
          (*out)[i].ofs = in->keeps[i]->offset;
          (*out)[i].n = in->keeps[i]->n;
        }
    }
}

static char * WARN_UNUSED_RESULT
decode_spvlb_color_string (const char *s, uint8_t def,
                           struct cell_color *colorp)
{
  int r, g, b;
  if (!*s)
    r = g = b = def;
  else if (sscanf (s, "#%2x%2x%2x", &r, &g, &b) != 3)
    return xasprintf ("bad color %s", s);

  *colorp = (struct cell_color) CELL_COLOR (r, g, b);
  return NULL;
}

static struct cell_color
decode_spvlb_color_u32 (uint32_t x)
{
  return (struct cell_color) { x >> 24, x >> 16, x >> 8, x };
}

static char * WARN_UNUSED_RESULT
decode_spvlb_font_style (const struct spvlb_font_style *in,
                         const char *encoding, struct font_style **outp)
{
  if (!in)
    {
      *outp = NULL;
      return NULL;
    }

  struct cell_color fg, bg;
  char *error = decode_spvlb_color_string (in->fg_color, 0x00, &fg);
  if (!error)
    error = decode_spvlb_color_string (in->bg_color, 0xff, &bg);
  if (error)
    return error;

  *outp = xmalloc (sizeof **outp);
  **outp = (struct font_style) {
    .bold = in->bold,
    .italic = in->italic,
    .underline = in->underline,
    .fg = { fg, fg },
    .bg = { bg, bg },
    .typeface = to_utf8 (in->typeface, encoding),
    .size = in->size / 1.33,
  };
  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_spvlb_halign (uint32_t in, enum table_halign *halignp)
{
  switch (in)
    {
    case 0:
      *halignp = TABLE_HALIGN_CENTER;
      return NULL;

    case 2:
      *halignp = TABLE_HALIGN_LEFT;
      return NULL;

    case 4:
      *halignp = TABLE_HALIGN_RIGHT;
      return NULL;

    case 6:
    case 61453:
      *halignp = TABLE_HALIGN_DECIMAL;
      return NULL;

    case 0xffffffad:
    case 64173:
      *halignp = TABLE_HALIGN_MIXED;
      return NULL;

    default:
      return xasprintf ("bad cell style halign %"PRIu32, in);
    }
}

static char * WARN_UNUSED_RESULT
decode_spvlb_valign (uint32_t in, enum table_valign *valignp)
{
  switch (in)
    {
    case 0:
      *valignp = TABLE_VALIGN_CENTER;
      return NULL;

    case 1:
      *valignp = TABLE_VALIGN_TOP;
      return NULL;

    case 3:
      *valignp = TABLE_VALIGN_BOTTOM;
      return NULL;

    default:
      *valignp = 0;
      return xasprintf ("bad cell style valign %"PRIu32, in);
    }
}

static char * WARN_UNUSED_RESULT
decode_spvlb_cell_style (const struct spvlb_cell_style *in,
                         struct cell_style **outp)
{
  if (!in)
    {
      *outp = NULL;
      return NULL;
    }

  enum table_halign halign;
  char *error = decode_spvlb_halign (in->halign, &halign);
  if (error)
    return error;

  enum table_valign valign;
  error = decode_spvlb_valign (in->valign, &valign);
  if (error)
    return error;

  *outp = xzalloc (sizeof **outp);
  **outp = (struct cell_style) {
    .halign = halign,
    .valign = valign,
    .decimal_offset = in->decimal_offset,
    .margin = {
      [TABLE_HORZ] = { in->left_margin, in->right_margin },
      [TABLE_VERT] = { in->top_margin, in->bottom_margin },
    },
  };
  return NULL;
}

static char *decode_spvlb_value (
  const struct pivot_table *, const struct spvlb_value *,
  const char *encoding, struct pivot_value **) WARN_UNUSED_RESULT;

static char * WARN_UNUSED_RESULT
decode_spvlb_argument (const struct pivot_table *table,
                       const struct spvlb_argument *in,
                       const char *encoding, struct pivot_argument *out)
{
  if (in->value)
    {
      struct pivot_value *value;
      char *error = decode_spvlb_value (table, in->value, encoding, &value);
      if (error)
        return error;

      out->n = 1;
      out->values = xmalloc (sizeof *out->values);
      out->values[0] = value;
    }
  else
    {
      out->n = 0;
      out->values = xnmalloc (in->n_values, sizeof *out->values);
      for (size_t i = 0; i < in->n_values; i++)
        {
          char *error = decode_spvlb_value (table, in->values[i], encoding,
                                            &out->values[i]);
          if (error)
            {
              pivot_argument_uninit (out);
              return error;
            }
          out->n++;
        }
    }

  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_spvlb_value_show (uint8_t in, enum settings_value_show *out)
{
  switch (in)
    {
    case 0: *out = SETTINGS_VALUE_SHOW_DEFAULT; return NULL;
    case 1: *out = SETTINGS_VALUE_SHOW_VALUE; return NULL;
    case 2: *out = SETTINGS_VALUE_SHOW_LABEL; return NULL;
    case 3: *out = SETTINGS_VALUE_SHOW_BOTH; return NULL;
    default:
      return xasprintf ("bad value show %"PRIu8, in);
    }
}

static char * WARN_UNUSED_RESULT
decode_spvlb_value (const struct pivot_table *table,
                    const struct spvlb_value *in,
                    const char *encoding, struct pivot_value **outp)
{
  *outp = NULL;

  struct pivot_value *out = xzalloc (sizeof *out);
  const struct spvlb_value_mod *vm;

  char *error;
  switch (in->type)
    {
    case 1:
      vm = in->type_01.value_mod;
      out->type = PIVOT_VALUE_NUMERIC;
      out->numeric.x = in->type_01.x;
      error = spv_decode_fmt_spec (in->type_01.format, &out->numeric.format);
      if (error)
        return error;
      break;

    case 2:
      vm = in->type_02.value_mod;
      out->type = PIVOT_VALUE_NUMERIC;
      out->numeric.x = in->type_02.x;
      error = spv_decode_fmt_spec (in->type_02.format, &out->numeric.format);
      if (!error)
        error = decode_spvlb_value_show (in->type_02.show, &out->numeric.show);
      if (error)
        return NULL;
      out->numeric.var_name = to_utf8_if_nonempty (in->type_02.var_name,
                                                   encoding);
      out->numeric.value_label = to_utf8_if_nonempty (in->type_02.value_label,
                                                      encoding);
      break;

    case 3:
      vm = in->type_03.value_mod;
      out->type = PIVOT_VALUE_TEXT;
      out->text.local = to_utf8 (in->type_03.local, encoding);
      out->text.c = to_utf8 (in->type_03.c, encoding);
      out->text.id = to_utf8 (in->type_03.id, encoding);
      out->text.user_provided = !in->type_03.fixed;
      break;

    case 4:
      vm = in->type_04.value_mod;
      out->type = PIVOT_VALUE_STRING;
      error = decode_spvlb_value_show (in->type_04.show, &out->string.show);
      if (error)
        return NULL;
      out->string.s = to_utf8 (in->type_04.s, encoding);
      out->string.var_name = to_utf8 (in->type_04.var_name, encoding);
      out->string.value_label = to_utf8_if_nonempty (in->type_04.value_label,
                                                     encoding);
      break;

    case 5:
      vm = in->type_05.value_mod;
      out->type = PIVOT_VALUE_VARIABLE;
      error = decode_spvlb_value_show (in->type_05.show, &out->variable.show);
      if (error)
        return error;
      out->variable.var_name = to_utf8 (in->type_05.var_name, encoding);
      out->variable.var_label = to_utf8_if_nonempty (in->type_05.var_label,
                                                     encoding);
      break;

    case 6:
      vm = in->type_06.value_mod;
      out->type = PIVOT_VALUE_TEXT;
      out->text.local = to_utf8 (in->type_06.local, encoding);
      out->text.c = to_utf8 (in->type_06.c, encoding);
      out->text.id = to_utf8 (in->type_06.id, encoding);
      out->text.user_provided = false;
      break;

    case -1:
      vm = in->type_else.value_mod;
      out->type = PIVOT_VALUE_TEMPLATE;
      out->template.local = to_utf8 (in->type_else.template, encoding);
      out->template.id = out->template.local;
      out->template.n_args = 0;
      out->template.args = xnmalloc (in->type_else.n_args,
                                     sizeof *out->template.args);
      for (size_t i = 0; i < in->type_else.n_args; i++)
        {
          error = decode_spvlb_argument (table, in->type_else.args[i],
                                         encoding, &out->template.args[i]);
          if (error)
            {
              pivot_value_destroy (out);
              return error;
            }
          out->template.n_args++;
        }
      break;

    default:
      abort ();
    }

  if (vm)
    {
      if (vm->n_subscripts)
        {
          out->n_subscripts = vm->n_subscripts;
          out->subscripts = xnmalloc (vm->n_subscripts,
                                      sizeof *out->subscripts);
          for (size_t i = 0; i < vm->n_subscripts; i++)
            out->subscripts[i] = to_utf8 (vm->subscripts[i], encoding);
        }

      if (vm->n_refs)
        {
          out->footnotes = xnmalloc (vm->n_refs, sizeof *out->footnotes);
          for (size_t i = 0; i < vm->n_refs; i++)
            {
              uint16_t idx = vm->refs[i];
              if (idx >= table->n_footnotes)
                {
                  pivot_value_destroy (out);
                  return xasprintf ("bad footnote index: %"PRIu16" >= %zu",
                                    idx, table->n_footnotes);
                }

              out->footnotes[out->n_footnotes++] = table->footnotes[idx];
            }
        }

      if (vm->style_pair)
        {
          error = decode_spvlb_font_style (vm->style_pair->font_style,
                                           encoding, &out->font_style);
          if (!error)
            error = decode_spvlb_cell_style (vm->style_pair->cell_style,
                                             &out->cell_style);
          if (error)
            {
              pivot_value_destroy (out);
              return error;
            }
        }

      if (vm->template_string
          && vm->template_string->id
          && vm->template_string->id[0]
          && out->type == PIVOT_VALUE_TEMPLATE)
        out->template.id = to_utf8 (vm->template_string->id, encoding);
    }

  *outp = out;
  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_spvlb_area (const struct spvlb_area *in, struct table_area_style *out,
                   const char *encoding)
{
  char *error;

  struct cell_color fg0, fg1, bg0, bg1;
  error = decode_spvlb_color_string (in->fg_color, 0x00, &fg0);
  if (!error)
    error = decode_spvlb_color_string (in->bg_color, 0xff, &bg0);
  if (!error && in->alternate)
    error = decode_spvlb_color_string (in->alt_fg_color, 0x00, &fg1);
  if (!error && in->alternate)
    error = decode_spvlb_color_string (in->alt_bg_color, 0xff, &bg1);

  enum table_halign halign;
  if (!error)
    {
      error = decode_spvlb_halign (in->halign, &halign);

      /* TABLE_HALIGN_DECIMAL doesn't seem to be a real halign for areas, which
         is good because there's no way to indicate the decimal offset.  Just
         in case: */
      if (!error && halign == TABLE_HALIGN_DECIMAL)
        halign = TABLE_HALIGN_MIXED;
    }

  enum table_valign valign;
  if (!error)
    error = decode_spvlb_valign (in->valign, &valign);

  if (error)
    return error;

  *out = (struct table_area_style) {
    .font_style = {
      .bold = (in->style & 1) != 0,
      .italic = (in->style & 2) != 0,
      .underline = in->underline,
      .fg = { fg0, in->alternate ? fg1 : fg0 },
      .bg = { bg0, in->alternate ? bg1 : bg0 },
      .typeface = to_utf8 (in->typeface, encoding),
      .size = in->size / 1.33,
    },
    .cell_style = {
      .halign = halign,
      .valign = valign,
      .margin = {
        [TABLE_HORZ] = { in->left_margin, in->right_margin },
        [TABLE_VERT] = { in->top_margin, in->bottom_margin },
      },
    },
  };
  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_spvlb_group (const struct pivot_table *,
                    struct spvlb_category **,
                    size_t n_categories,
                    bool show_label,
                    struct pivot_category *parent,
                    struct pivot_dimension *,
                    const char *encoding);

static char * WARN_UNUSED_RESULT
decode_spvlb_categories (const struct pivot_table *table,
                         struct spvlb_category **categories,
                         size_t n_categories,
                         struct pivot_category *parent,
                         struct pivot_dimension *dimension,
                         const char *encoding)
{
  for (size_t i = 0; i < n_categories; i++)
    {
      const struct spvlb_category *in = categories[i];
      if (in->group && in->group->merge)
        {
          char *error = decode_spvlb_categories (
            table, in->group->subcategories, in->group->n_subcategories,
            parent, dimension, encoding);
          if (error)
            return error;

          continue;
        }

      struct pivot_value *name;
      char *error = decode_spvlb_value (table, in->name, encoding, &name);
      if (error)
        return error;

      struct pivot_category *out = xzalloc (sizeof *out);
      out->name = name;
      out->parent = parent;
      out->dimension = dimension;
      if (in->group)
        {
          char *error = decode_spvlb_group (table, in->group->subcategories,
                                            in->group->n_subcategories,
                                            true, out, dimension, encoding);
          if (error)
            {
              pivot_category_destroy (out);
              return error;
            }

          out->data_index = SIZE_MAX;
          out->presentation_index = SIZE_MAX;
        }
      else
        {
          out->data_index = in->leaf->leaf_index;
          out->presentation_index = dimension->n_leaves;
          dimension->n_leaves++;
        }

      if (parent->n_subs >= parent->allocated_subs)
        parent->subs = x2nrealloc (parent->subs, &parent->allocated_subs,
                                   sizeof *parent->subs);
      parent->subs[parent->n_subs++] = out;
    }
  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_spvlb_group (const struct pivot_table *table,
                    struct spvlb_category **categories,
                    size_t n_categories, bool show_label,
                    struct pivot_category *category,
                    struct pivot_dimension *dimension,
                    const char *encoding)
{
  category->subs = XCALLOC (n_categories, struct pivot_category *);
  category->n_subs = 0;
  category->allocated_subs = 0;
  category->show_label = show_label;

  return decode_spvlb_categories (table, categories, n_categories, category,
                                  dimension, encoding);
}

static char * WARN_UNUSED_RESULT
fill_leaves (struct pivot_category *category,
             struct pivot_dimension *dimension)
{
  if (pivot_category_is_group (category))
    {
      for (size_t i = 0; i < category->n_subs; i++)
        {
          char *error = fill_leaves (category->subs[i], dimension);
          if (error)
            return error;
        }
    }
  else
    {
      if (category->data_index >= dimension->n_leaves)
        return xasprintf ("leaf_index %zu >= n_leaves %zu",
                          category->data_index, dimension->n_leaves);
      if (dimension->data_leaves[category->data_index])
        return xasprintf ("two leaves with data_index %zu",
                          category->data_index);
      dimension->data_leaves[category->data_index] = category;
      dimension->presentation_leaves[category->presentation_index] = category;
    }
  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_spvlb_dimension (const struct pivot_table *table,
                        const struct spvlb_dimension *in,
                        size_t idx, const char *encoding,
                        struct pivot_dimension **outp)
{
  /* Convert most of the dimension. */
  struct pivot_value *name;
  char *error = decode_spvlb_value (table, in->name, encoding, &name);
  if (error)
    return error;

  struct pivot_dimension *out = xzalloc (sizeof *out);
  out->level = UINT_MAX;
  out->top_index = idx;
  out->hide_all_labels = in->props->hide_all_labels;

  out->root = xzalloc (sizeof *out->root);
  *out->root = (struct pivot_category) {
    .name = name,
    .dimension = out,
    .data_index = SIZE_MAX,
    .presentation_index = SIZE_MAX,
  };
  error = decode_spvlb_group (table, in->categories, in->n_categories,
                              !in->props->hide_dim_label, out->root,
                              out, encoding);
  if (error)
    goto error;

  /* Allocate and fill the array of leaves now that we know how many there
     are. */
  out->data_leaves = XCALLOC (out->n_leaves, struct pivot_category *);
  out->presentation_leaves = XCALLOC (out->n_leaves, struct pivot_category *);
  out->allocated_leaves = out->n_leaves;
  error = fill_leaves (out->root, out);
  if (error)
    goto error;
  for (size_t i = 0; i < out->n_leaves; i++)
    {
      assert (out->data_leaves[i] != NULL);
      assert (out->presentation_leaves[i] != NULL);
    }
  *outp = out;
  return NULL;

error:
  pivot_dimension_destroy (out);
  return error;
}

static char * WARN_UNUSED_RESULT
decode_spvlb_stroke (uint32_t stroke_type, enum table_stroke *strokep)
{
  enum table_stroke strokes[] = {
    TABLE_STROKE_NONE,
    TABLE_STROKE_SOLID,
    TABLE_STROKE_DASHED,
    TABLE_STROKE_THICK,
    TABLE_STROKE_THIN,
    TABLE_STROKE_DOUBLE,
  };

  if (stroke_type >= sizeof strokes / sizeof *strokes)
    return xasprintf ("bad stroke %"PRIu32, stroke_type);

  *strokep = strokes[stroke_type];
  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_spvlb_border (const struct spvlb_border *in, struct pivot_table *table)

{
  if (in->border_type >= PIVOT_N_BORDERS)
    return xasprintf ("bad border type %"PRIu32, in->border_type);

  struct table_border_style *out = &table->look.borders[in->border_type];
  out->color = decode_spvlb_color_u32 (in->color);
  return decode_spvlb_stroke (in->stroke_type, &out->stroke);
}

static char * WARN_UNUSED_RESULT
decode_spvlb_axis (const uint32_t *dimension_indexes, size_t n_dimensions,
                   enum pivot_axis_type axis_type, struct pivot_table *table)
{
  struct pivot_axis *axis = &table->axes[axis_type];
  axis->dimensions = XCALLOC (n_dimensions, struct pivot_dimension *);
  axis->n_dimensions = n_dimensions;
  axis->extent = 1;
  for (size_t i = 0; i < n_dimensions; i++)
    {
      uint32_t idx = dimension_indexes[i];
      if (idx >= table->n_dimensions)
        return xasprintf ("bad dimension index %"PRIu32" >= %zu",
                          idx, table->n_dimensions);

      struct pivot_dimension *d = table->dimensions[idx];
      if (d->level != UINT_MAX)
        return xasprintf ("duplicate dimension %"PRIu32, idx);

      axis->dimensions[i] = d;
      d->axis_type = axis_type;
      d->level = i;

      axis->extent *= d->n_leaves;
    }

  return NULL;
}

static char *
decode_data_index (uint64_t in, const struct pivot_table *table,
                   size_t *out)
{
  uint64_t remainder = in;
  for (size_t i = table->n_dimensions - 1; i > 0; i--)
    {
      const struct pivot_dimension *d = table->dimensions[i];
      if (d->n_leaves)
        {
          out[i] = remainder % d->n_leaves;
          remainder /= d->n_leaves;
        }
      else
        out[i] = 0;
    }
  if (remainder >= table->dimensions[0]->n_leaves)
    return xasprintf ("out of range cell data index %"PRIu64, in);

  out[0] = remainder;
  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_spvlb_cells (struct spvlb_cell **in, size_t n_in,
                    struct pivot_table *table, const char *encoding)
{
  if (!table->n_dimensions)
    return NULL;

  size_t *dindexes = xnmalloc (table->n_dimensions, sizeof *dindexes);
  for (size_t i = 0; i < n_in; i++)
    {
      struct pivot_value *value;
      char *error = decode_data_index (in[i]->index, table, dindexes);
      if (!error)
        error = decode_spvlb_value (table, in[i]->value, encoding, &value);
      if (error)
        {
          free (dindexes);
          return error;
        }
      pivot_table_put (table, dindexes, table->n_dimensions, value);
    }
  free (dindexes);

  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_spvlb_footnote (const struct spvlb_footnote *in, const char *encoding,
                       size_t idx, struct pivot_table *table)
{
  struct pivot_value *content;
  char *error = decode_spvlb_value (table, in->text, encoding, &content);
  if (error)
    return error;

  struct pivot_value *marker = NULL;
  if (in->marker)
    {
      error = decode_spvlb_value (table, in->marker, encoding, &marker);
      if (error)
        {
          pivot_value_destroy (content);
          return error;
        }
      if (marker->type == PIVOT_VALUE_TEXT)
        marker->text.user_provided = false;
    }

  struct pivot_footnote *f = pivot_table_create_footnote__ (
    table, idx, marker, content);
  f->show = (int32_t) in->show > 0;
  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_current_layer (uint64_t current_layer, struct pivot_table *table)
{
  const struct pivot_axis *axis = &table->axes[PIVOT_AXIS_LAYER];
  table->current_layer = xnmalloc (axis->n_dimensions,
                                   sizeof *table->current_layer);

  for (size_t i = 0; i < axis->n_dimensions; i++)
    {
      const struct pivot_dimension *d = axis->dimensions[i];
      if (d->n_leaves)
        {
          table->current_layer[i] = current_layer % d->n_leaves;
          current_layer /= d->n_leaves;
        }
      else
        table->current_layer[i] = 0;
    }
  if (current_layer > 0)
    return xasprintf ("out of range layer data index %"PRIu64, current_layer);
  return NULL;
}

char * WARN_UNUSED_RESULT
decode_spvlb_table (const struct spvlb_table *in, struct pivot_table **outp)
{
  *outp = NULL;
  if (in->header->version != 1 && in->header->version != 3)
    return xasprintf ("unknown version %"PRIu32" (expected 1 or 3)",
                      in->header->version);

  char *error = NULL;
  struct pivot_table *out = xzalloc (sizeof *out);
  out->ref_cnt = 1;
  hmap_init (&out->cells);

  const struct spvlb_y1 *y1 = (in->formats->x0 ? in->formats->x0->y1
                               : in->formats->x3 ? in->formats->x3->y1
                               : NULL);
  const char *encoding;
  if (y1)
    encoding = y1->charset;
  else
    {
      const char *dot = strchr (in->formats->locale, '.');
      encoding = dot ? dot + 1 : "windows-1252";
    }

  /* Display settings. */
  out->look.show_numeric_markers = !in->ts->show_alphabetic_markers;
  out->rotate_inner_column_labels = in->header->rotate_inner_column_labels;
  out->rotate_outer_row_labels = in->header->rotate_outer_row_labels;
  out->look.row_labels_in_corner = in->ts->show_row_labels_in_corner;
  out->show_grid_lines = in->borders->show_grid_lines;
  out->show_caption = true;
  out->look.footnote_marker_superscripts = in->ts->footnote_marker_superscripts;
  out->look.omit_empty = in->ts->omit_empty;

  const struct spvlb_x1 *x1 = in->formats->x1;
  if (x1)
    {
      error = decode_spvlb_value_show (x1->show_values, &out->show_values);
      if (!error)
        error = decode_spvlb_value_show (x1->show_variables,
                                         &out->show_variables);
      if (error)
        goto error;

      out->show_caption = x1->show_caption;
    }

  /* Column and row display settings. */
  out->look.width_ranges[TABLE_VERT][0] = in->header->min_row_height;
  out->look.width_ranges[TABLE_VERT][1] = in->header->max_row_height;
  out->look.width_ranges[TABLE_HORZ][0] = in->header->min_col_width;
  out->look.width_ranges[TABLE_HORZ][1] = in->header->max_col_width;

  convert_widths (in->formats->widths, in->formats->n_widths,
                  &out->sizing[TABLE_HORZ].widths,
                  &out->sizing[TABLE_HORZ].n_widths);

  const struct spvlb_x2 *x2 = in->formats->x2;
  if (x2)
    convert_widths (x2->row_heights, x2->n_row_heights,
                    &out->sizing[TABLE_VERT].widths,
                    &out->sizing[TABLE_VERT].n_widths);

  convert_breakpoints (in->ts->row_breaks,
                       &out->sizing[TABLE_VERT].breaks,
                       &out->sizing[TABLE_VERT].n_breaks);
  convert_breakpoints (in->ts->col_breaks,
                       &out->sizing[TABLE_HORZ].breaks,
                       &out->sizing[TABLE_HORZ].n_breaks);

  convert_keeps (in->ts->row_keeps,
                 &out->sizing[TABLE_VERT].keeps,
                 &out->sizing[TABLE_VERT].n_keeps);
  convert_keeps (in->ts->col_keeps,
                 &out->sizing[TABLE_HORZ].keeps,
                 &out->sizing[TABLE_HORZ].n_keeps);

  out->notes = to_utf8_if_nonempty (in->ts->notes, encoding);
  out->look.name = to_utf8_if_nonempty (in->ts->table_look, encoding);

  /* Print settings. */
  out->look.print_all_layers = in->ps->all_layers;
  out->look.paginate_layers = in->ps->paginate_layers;
  out->look.shrink_to_fit[TABLE_HORZ] = in->ps->fit_width;
  out->look.shrink_to_fit[TABLE_VERT] = in->ps->fit_length;
  out->look.top_continuation = in->ps->top_continuation;
  out->look.bottom_continuation = in->ps->bottom_continuation;
  out->look.continuation = xstrdup (in->ps->continuation_string);
  out->look.n_orphan_lines = in->ps->n_orphan_lines;

  /* Format settings. */
  out->epoch = in->formats->y0->epoch;
  out->decimal = in->formats->y0->decimal;
  out->grouping = in->formats->y0->grouping;
  const struct spvlb_custom_currency *cc = in->formats->custom_currency;
  for (int i = 0; i < 5; i++)
    if (cc && i < cc->n_ccs)
      out->ccs[i] = xstrdup (cc->ccs[i]);
  out->small = in->formats->x3 ? in->formats->x3->small : 0;

  /* Command information. */
  if (y1)
    {
      out->command_local = to_utf8 (y1->command_local, encoding);
      out->command_c = to_utf8 (y1->command, encoding);
      out->language = xstrdup (y1->language);
      /* charset? */
      out->locale = xstrdup (y1->locale);
    }

  /* Source information. */
  const struct spvlb_x3 *x3 = in->formats->x3;
  if (x3)
    {
      if (x3->dataset && x3->dataset[0] && x3->dataset[0] != 4)
        out->dataset = to_utf8 (x3->dataset, encoding);
      out->datafile = to_utf8_if_nonempty (x3->datafile, encoding);
      out->date = x3->date;
    }

  /* Footnotes.

     Any pivot_value might refer to footnotes, so it's important to process the
     footnotes early to ensure that those references can be resolved.  There is
     a possible problem that a footnote might itself reference an
     as-yet-unprocessed footnote, but that's OK because footnote references
     don't actually look at the footnote contents but only resolve a pointer to
     where the footnote will go later.

     Before we really start, create all the footnotes we'll fill in.  This is
     because sometimes footnotes refer to themselves or to each other and we
     don't want to reject those references. */
  const struct spvlb_footnotes *fn = in->footnotes;
  if (fn->n_footnotes > 0)
    {
      pivot_table_create_footnote__ (out, fn->n_footnotes - 1, NULL, NULL);
      for (size_t i = 0; i < fn->n_footnotes; i++)
        {
          error = decode_spvlb_footnote (in->footnotes->footnotes[i],
                                         encoding, i, out);
          if (error)
            goto error;
        }
    }

  /* Title and caption. */
  error = decode_spvlb_value (out, in->titles->user_title, encoding,
                              &out->title);
  if (error)
    goto error;

  error = decode_spvlb_value (out, in->titles->subtype, encoding,
                              &out->subtype);
  if (error)
    goto error;

  if (in->titles->corner_text)
    {
      error = decode_spvlb_value (out, in->titles->corner_text,
                                  encoding, &out->corner_text);
      if (error)
        goto error;
    }

  if (in->titles->caption)
    {
      error = decode_spvlb_value (out, in->titles->caption, encoding,
                                  &out->caption);
      if (error)
        goto error;
    }


  /* Styles. */
  for (size_t i = 0; i < PIVOT_N_AREAS; i++)
    {
      error = decode_spvlb_area (in->areas->areas[i], &out->look.areas[i],
                                 encoding);
      if (error)
        goto error;
    }
  for (size_t i = 0; i < PIVOT_N_BORDERS; i++)
    {
      error = decode_spvlb_border (in->borders->borders[i], out);
      if (error)
        goto error;
    }

  /* Dimensions. */
  out->n_dimensions = in->dimensions->n_dims;
  out->dimensions = XCALLOC (out->n_dimensions, struct pivot_dimension *);
  for (size_t i = 0; i < out->n_dimensions; i++)
    {
      error = decode_spvlb_dimension (out, in->dimensions->dims[i],
                                      i, encoding, &out->dimensions[i]);
      if (error)
        goto error;
    }

  /* Axes. */
  size_t a = in->axes->n_layers;
  size_t b = in->axes->n_rows;
  size_t c = in->axes->n_columns;
  if (size_overflow_p (xsum3 (a, b, c)) || a + b + c != out->n_dimensions)
    {
      error = xasprintf ("dimensions do not sum correctly "
                         "(%zu + %zu + %zu != %zu)",
                         a, b, c, out->n_dimensions);
      goto error;
    }
  error = decode_spvlb_axis (in->axes->layers, in->axes->n_layers,
                             PIVOT_AXIS_LAYER, out);
  if (error)
    goto error;
  error = decode_spvlb_axis (in->axes->rows, in->axes->n_rows,
                             PIVOT_AXIS_ROW, out);
  if (error)
    goto error;
  error = decode_spvlb_axis (in->axes->columns, in->axes->n_columns,
                             PIVOT_AXIS_COLUMN, out);
  if (error)
    goto error;

  pivot_table_assign_label_depth (out);

  error = decode_current_layer (in->ts->current_layer, out);
  if (error)
    goto error;

  /* Data. */
  error = decode_spvlb_cells (in->cells->cells, in->cells->n_cells, out,
                              encoding);

  *outp = out;
  return NULL;

error:
  pivot_table_unref (out);
  return error;
}
