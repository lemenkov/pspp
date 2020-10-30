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

#include "output/spv/spv-legacy-decoder.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>

#include "data/data-out.h"
#include "data/calendar.h"
#include "data/format.h"
#include "data/value.h"
#include "libpspp/assertion.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/message.h"
#include "output/pivot-table.h"
#include "output/spv/detail-xml-parser.h"
#include "output/spv/spv-legacy-data.h"
#include "output/spv/spv-table-look.h"
#include "output/spv/spv.h"
#include "output/spv/structure-xml-parser.h"

#include "gl/c-strtod.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"

#include <libxml/tree.h>

#include "gettext.h"
#define N_(msgid) msgid
#define _(msgid) gettext (msgid)

struct spv_series
  {
    struct hmap_node hmap_node; /* By name. */
    char *name;
    char *label;
    struct fmt_spec format;

    struct spv_series *label_series;
    bool is_label_series;

    const struct spvxml_node *xml;

    struct spv_data_value *values;
    size_t n_values;
    struct hmap map;            /* Contains "struct spv_mapping". */
    bool remapped;

    struct pivot_dimension *dimension;

    struct pivot_category **index_to_category;
    size_t n_index;

    struct spvdx_affix **affixes;
    size_t n_affixes;
  };

static void spv_map_destroy (struct hmap *);

static struct spv_series *
spv_series_first (struct hmap *series_map)
{
  struct spv_series *series;
  HMAP_FOR_EACH (series, struct spv_series, hmap_node, series_map)
    return series;
  return NULL;
}

static struct spv_series *
spv_series_find (const struct hmap *series_map, const char *name)
{
  struct spv_series *series;
  HMAP_FOR_EACH_WITH_HASH (series, struct spv_series, hmap_node,
                           hash_string (name, 0), series_map)
    if (!strcmp (name, series->name))
      return series;
  return NULL;
}

static struct spv_series *
spv_series_from_ref (const struct hmap *series_map,
                     const struct spvxml_node *ref)
{
  const struct spvxml_node *node
    = (spvdx_is_source_variable (ref)
       ? &spvdx_cast_source_variable (ref)->node_
       : &spvdx_cast_derived_variable (ref)->node_);
  return spv_series_find (series_map, node->id);
}

static void UNUSED
spv_series_dump (const struct spv_series *series)
{
  printf ("series \"%s\"", series->name);
  if (series->label)
    printf (" (label \"%s\")", series->label);
  printf (", %zu values:", series->n_values);
  for (size_t i = 0; i < series->n_values; i++)
    {
      putchar (' ');
      spv_data_value_dump (&series->values[i], stdout);
    }
  putchar ('\n');
}

static void
spv_series_destroy (struct hmap *series_map)
{
  struct spv_series *series, *next_series;
  HMAP_FOR_EACH_SAFE (series, next_series, struct spv_series, hmap_node,
                      series_map)
    {
      free (series->name);
      free (series->label);

      for (size_t i = 0; i < series->n_values; i++)
        spv_data_value_uninit (&series->values[i]);
      free (series->values);

      spv_map_destroy (&series->map);

      free (series->index_to_category);

      hmap_delete (series_map, &series->hmap_node);
      free (series);
    }
  hmap_destroy (series_map);
}

struct spv_mapping
  {
    struct hmap_node hmap_node;
    double from;
    struct spv_data_value to;
  };

static struct spv_mapping *
spv_map_search (const struct hmap *map, double from)
{
  struct spv_mapping *mapping;
  HMAP_FOR_EACH_WITH_HASH (mapping, struct spv_mapping, hmap_node,
                           hash_double (from, 0), map)
    if (mapping->from == from)
      return mapping;
  return NULL;
}

static const struct spv_data_value *
spv_map_lookup (const struct hmap *map, const struct spv_data_value *in)
{
  if (in->width >= 0)
    return in;

  const struct spv_mapping *m = spv_map_search (map, in->d);
  return m ? &m->to : in;
}

static bool
parse_real (const char *s, double *real)
{
  int save_errno = errno;
  errno = 0;
  char *end;
  *real = c_strtod (s, &end);
  bool ok = !errno && end > s && !*end;
  errno = save_errno;

  return ok;
}

static char * WARN_UNUSED_RESULT
spv_map_insert (struct hmap *map, double from, const char *to,
                bool try_strings_as_numbers, const struct fmt_spec *format)
{
  struct spv_mapping *mapping = xmalloc (sizeof *mapping);
  mapping->from = from;

  if ((try_strings_as_numbers || (format && fmt_is_numeric (format->type)))
      && parse_real (to, &mapping->to.d))
    {
      if (try_strings_as_numbers)
        mapping->to.width = -1;
      else
        {
          union value v = { .f = mapping->to.d };
          mapping->to.s = data_out_stretchy (&v, NULL, format, NULL);
          mapping->to.width = strlen (mapping->to.s);
        }
    }
  else
    {
      mapping->to.width = strlen (to);
      mapping->to.s = xstrdup (to);
    }

  struct spv_mapping *old_mapping = spv_map_search (map, from);
  if (old_mapping)
    {
      bool same = spv_data_value_equal (&old_mapping->to, &mapping->to);
      spv_data_value_uninit (&mapping->to);
      free (mapping);
      return (same ? NULL
              : xasprintf ("Duplicate relabeling differs for from=\"%.*g\"",
                           DBL_DIG + 1, from));
    }

  hmap_insert (map, &mapping->hmap_node, hash_double (from, 0));
  return NULL;
}

static void
spv_map_destroy (struct hmap *map)
{
  struct spv_mapping *mapping, *next;
  HMAP_FOR_EACH_SAFE (mapping, next, struct spv_mapping, hmap_node, map)
    {
      spv_data_value_uninit (&mapping->to);
      hmap_delete (map, &mapping->hmap_node);
      free (mapping);
    }
  hmap_destroy (map);
}

static char * WARN_UNUSED_RESULT
spv_series_parse_relabels (struct hmap *map,
                           struct spvdx_relabel **relabels, size_t n_relabels,
                           bool try_strings_as_numbers,
                           const struct fmt_spec *format)
{
  for (size_t i = 0; i < n_relabels; i++)
    {
      const struct spvdx_relabel *relabel = relabels[i];
      char *error = spv_map_insert (map, relabel->from, relabel->to,
                                    try_strings_as_numbers, format);
      if (error)
        return error;
    }
  return NULL;
}

static char * WARN_UNUSED_RESULT
spv_series_parse_value_map_entry (struct hmap *map,
                                  const struct spvdx_value_map_entry *vme)
{
  for (const char *p = vme->from; ; p++)
    {
      int save_errno = errno;
      errno = 0;
      char *end;
      double from = c_strtod (p, &end);
      bool ok = !errno && end > p && strchr (";", *end);
      errno = save_errno;
      if (!ok)
        return xasprintf ("Syntax error in valueMapEntry from=\"%s\".",
                          vme->from);

      char *error = spv_map_insert (map, from, vme->to, true,
                                    &(struct fmt_spec) { FMT_A, 40, 0 });
      if (error)
        return error;

      p = end;
      if (*p == '\0')
        return NULL;
      assert (*p == ';');
    }
}

static struct fmt_spec
decode_date_time_format (const struct spvdx_date_time_format *dtf)
{
  if (dtf->dt_base_format == SPVDX_DT_BASE_FORMAT_DATE)
    {
      enum fmt_type type
        = (dtf->show_quarter > 0 ? FMT_QYR
           : dtf->show_week > 0 ? FMT_WKYR
           : dtf->mdy_order == SPVDX_MDY_ORDER_DAY_MONTH_YEAR
           ? (dtf->month_format == SPVDX_MONTH_FORMAT_NUMBER
              || dtf->month_format == SPVDX_MONTH_FORMAT_PADDED_NUMBER
              ? FMT_EDATE : FMT_DATE)
           : dtf->mdy_order == SPVDX_MDY_ORDER_YEAR_MONTH_DAY ? FMT_SDATE
           : FMT_ADATE);

      int w = fmt_min_output_width (type);
      if (dtf->year_abbreviation <= 0)
        w += 2;
      return (struct fmt_spec) { .type = type, .w = w };
    }
  else
    {
      enum fmt_type type
        = (dtf->dt_base_format == SPVDX_DT_BASE_FORMAT_DATE_TIME
           ? (dtf->mdy_order == SPVDX_MDY_ORDER_YEAR_MONTH_DAY
              ? FMT_YMDHMS
              : FMT_DATETIME)
           : (dtf->show_day > 0 ? FMT_DTIME
              : dtf->show_hour > 0 ? FMT_TIME
              : FMT_MTIME));
      int w = fmt_min_output_width (type);
      int d = 0;
      if (dtf->show_second > 0)
        {
          w += 3;
          if (dtf->show_millis > 0)
            {
              d = 3;
              w += d + 1;
            }
        }
      return (struct fmt_spec) { .type = type, .w = w, .d = d };
    }
}

static struct fmt_spec
decode_elapsed_time_format (const struct spvdx_elapsed_time_format *etf)
{
  enum fmt_type type
    = (etf->dt_base_format != SPVDX_DT_BASE_FORMAT_TIME ? FMT_DTIME
       : etf->show_hour > 0 ? FMT_TIME
       : FMT_MTIME);
  int w = fmt_min_output_width (type);
  int d = 0;
  if (etf->show_second > 0)
    {
      w += 3;
      if (etf->show_millis > 0)
        {
          d = 3;
          w += d + 1;
        }
    }
  return (struct fmt_spec) { .type = type, .w = w, .d = d };
}

static struct fmt_spec
decode_number_format (const struct spvdx_number_format *nf)
{
  enum fmt_type type = (nf->scientific == SPVDX_SCIENTIFIC_TRUE ? FMT_E
                        : nf->prefix && !strcmp (nf->prefix, "$") ? FMT_DOLLAR
                        : nf->suffix && !strcmp (nf->suffix, "%") ? FMT_PCT
                        : nf->use_grouping ? FMT_COMMA
                        : FMT_F);

  int d = nf->maximum_fraction_digits;
  if (d < 0 || d > 15)
    d = 2;

  struct fmt_spec f = (struct fmt_spec) { type, 40, d };
  fmt_fix_output (&f);
  return f;
}

/* Returns an *approximation* of IN as a fmt_spec.

   Not for use with string formats, which don't have any options anyway. */
static struct fmt_spec
decode_format (const struct spvdx_format *in)
{
  if (in->f_base_format == SPVDX_F_BASE_FORMAT_DATE ||
      in->f_base_format == SPVDX_F_BASE_FORMAT_TIME ||
      in->f_base_format == SPVDX_F_BASE_FORMAT_DATE_TIME)
    {
      struct spvdx_date_time_format dtf = {
        .dt_base_format = (in->f_base_format == SPVDX_F_BASE_FORMAT_DATE
                           ? SPVDX_DT_BASE_FORMAT_DATE
                           : in->f_base_format == SPVDX_F_BASE_FORMAT_TIME
                           ? SPVDX_DT_BASE_FORMAT_TIME
                           : SPVDX_DT_BASE_FORMAT_DATE_TIME),
        .separator_chars = in->separator_chars,
        .mdy_order = in->mdy_order,
        .show_year = in->show_year,
        .year_abbreviation = in->year_abbreviation,
        .show_quarter = in->show_quarter,
        .quarter_prefix = in->quarter_prefix,
        .quarter_suffix = in->quarter_suffix,
        .show_month = in->show_month,
        .month_format = in->month_format,
        .show_week = in->show_week,
        .week_padding = in->week_padding,
        .week_suffix = in->week_suffix,
        .show_day_of_week = in->show_day_of_week,
        .day_of_week_abbreviation = in->day_of_week_abbreviation,
        .day_padding = in->day_padding,
        .day_of_month_padding = in->day_of_month_padding,
        .hour_padding = in->hour_padding,
        .minute_padding = in->minute_padding,
        .second_padding = in->second_padding,
        .show_day = in->show_day,
        .show_hour = in->show_hour,
        .show_minute = in->show_minute,
        .show_second = in->show_second,
        .show_millis = in->show_millis,
        .day_type = in->day_type,
        .hour_format = in->hour_format,
      };
      return decode_date_time_format (&dtf);
    }
  else if (in->f_base_format == SPVDX_F_BASE_FORMAT_ELAPSED_TIME)
    {
      struct spvdx_elapsed_time_format etf = {
        .dt_base_format = (in->f_base_format == SPVDX_F_BASE_FORMAT_DATE
                           ? SPVDX_DT_BASE_FORMAT_DATE
                           : in->f_base_format == SPVDX_F_BASE_FORMAT_TIME
                           ? SPVDX_DT_BASE_FORMAT_TIME
                           : SPVDX_DT_BASE_FORMAT_DATE_TIME),
        .day_padding = in->day_padding,
        .minute_padding = in->minute_padding,
        .second_padding = in->second_padding,
        .show_year = in->show_year,
        .show_day = in->show_day,
        .show_hour = in->show_hour,
        .show_minute = in->show_minute,
        .show_second = in->show_second,
        .show_millis = in->show_millis,
      };
      return decode_elapsed_time_format (&etf);
    }
  else
    {
      assert (!in->f_base_format);
      struct spvdx_number_format nf = {
        .minimum_integer_digits = in->minimum_integer_digits,
        .maximum_fraction_digits = in->maximum_fraction_digits,
        .minimum_fraction_digits = in->minimum_fraction_digits,
        .use_grouping = in->use_grouping,
        .scientific = in->scientific,
        .small = in->small,
        .prefix = in->prefix,
        .suffix = in->suffix,
      };
      return decode_number_format (&nf);
    }
}

static void
spv_series_execute_mapping (struct spv_series *series)
{
  if (!hmap_is_empty (&series->map))
    {
      series->remapped = true;
      for (size_t i = 0; i < series->n_values; i++)
        {
          struct spv_data_value *value = &series->values[i];
          if (value->width >= 0)
            continue;

          const struct spv_mapping *mapping = spv_map_search (&series->map,
                                                              value->d);
          if (mapping)
            {
              value->index = value->d;
              assert (value->index == floor (value->index));
              value->width = mapping->to.width;
              if (value->width >= 0)
                value->s = xmemdup0 (mapping->to.s, mapping->to.width);
              else
                value->d = mapping->to.d;
            }
        }
    }
}

static char * WARN_UNUSED_RESULT
spv_series_remap_formats (struct spv_series *series,
                          struct spvxml_node **seq, size_t n_seq)
{
  spv_map_destroy (&series->map);
  hmap_init (&series->map);
  for (size_t i = 0; i < n_seq; i++)
    {
      struct spvxml_node *node = seq[i];
      if (spvdx_is_format (node))
        {
          struct spvdx_format *f = spvdx_cast_format (node);
          series->format = decode_format (f);
          char *error = spv_series_parse_relabels (
            &series->map, f->relabel, f->n_relabel,
            f->try_strings_as_numbers > 0, &series->format);
          if (error)
            return error;

          series->affixes = f->affix;
          series->n_affixes = f->n_affix;
        }
      else if (spvdx_is_string_format (node))
        {
          struct spvdx_string_format *sf = spvdx_cast_string_format (node);
          char *error = spv_series_parse_relabels (&series->map,
                                                   sf->relabel, sf->n_relabel,
                                                   false, NULL);
          if (error)
            return error;

          series->affixes = sf->affix;
          series->n_affixes = sf->n_affix;
        }
      else
        NOT_REACHED ();
    }
  spv_series_execute_mapping (series);
  return NULL;
}

static char * WARN_UNUSED_RESULT
spv_series_remap_vmes (struct spv_series *series,
                       struct spvdx_value_map_entry **vmes,
                       size_t n_vmes)
{
  spv_map_destroy (&series->map);
  hmap_init (&series->map);
  for (size_t i = 0; i < n_vmes; i++)
    {
      char *error = spv_series_parse_value_map_entry (&series->map, vmes[i]);
      if (error)
        return error;
    }
  spv_series_execute_mapping (series);
  return NULL;
}

static void
decode_footnotes (struct pivot_table *table, const struct spvdx_footnotes *f)
{
  if (f->n_footnote_mapping > 0)
    pivot_table_create_footnote__ (table, f->n_footnote_mapping - 1,
                                   NULL, NULL);
  for (size_t i = 0; i < f->n_footnote_mapping; i++)
    {
      const struct spvdx_footnote_mapping *fm = f->footnote_mapping[i];
      pivot_table_create_footnote__ (table, fm->defines_reference - 1,
                                     pivot_value_new_user_text (fm->to, -1),
                                     NULL);
    }
}

static struct cell_color
optional_color (int color, struct cell_color default_color)
{
  return (color >= 0
          ? (struct cell_color) CELL_COLOR (color >> 16, color >> 8, color)
          : default_color);
}

static int
optional_length (const char *s, int default_length)
{
  /* There is usually a "pt" suffix.  We ignore it. */
  int length;
  return s && sscanf (s, "%d", &length) == 1 ? length : default_length;
}

static int
optional_px (double inches, int default_px)
{
  return inches != DBL_MAX ? inches * 96.0 : default_px;
}

static void
decode_spvdx_style_incremental (const struct spvdx_style *in,
                                const struct spvdx_style *bg,
                                struct table_area_style *out)
{
  if (in && in->font_weight)
    out->font_style.bold = in->font_weight == SPVDX_FONT_WEIGHT_BOLD;
  if (in && in->font_style)
    out->font_style.italic = in->font_style == SPVDX_FONT_STYLE_ITALIC;
  if (in && in->font_underline)
    out->font_style.underline = in->font_underline == SPVDX_FONT_UNDERLINE_UNDERLINE;
  if (in && in->color >= 0)
    {
      out->font_style.fg[0] = optional_color (
        in->color, (struct cell_color) CELL_COLOR_BLACK);
      out->font_style.fg[1] = out->font_style.fg[0];
    }
  if (bg && bg->color >= 0)
    {
      out->font_style.bg[0] = optional_color (
        bg->color, (struct cell_color) CELL_COLOR_WHITE);
      out->font_style.bg[1] = out->font_style.bg[0];
    }
  if (in && in->font_family)
    {
      free (out->font_style.typeface);
      out->font_style.typeface = xstrdup (in->font_family);
    }
  if (in && in->font_size)
    {
      int size = optional_length (in->font_size, 0);
      if (size)
        out->font_style.size = size;
    }
  if (in && in->text_alignment)
    out->cell_style.halign
      = (in->text_alignment == SPVDX_TEXT_ALIGNMENT_LEFT
         ? TABLE_HALIGN_LEFT
         : in->text_alignment == SPVDX_TEXT_ALIGNMENT_RIGHT
         ? TABLE_HALIGN_RIGHT
         : in->text_alignment == SPVDX_TEXT_ALIGNMENT_CENTER
         ? TABLE_HALIGN_CENTER
         : in->text_alignment == SPVDX_TEXT_ALIGNMENT_DECIMAL
         ? TABLE_HALIGN_DECIMAL
         : TABLE_HALIGN_MIXED);
  if (in && in->label_location_vertical)
    out->cell_style.valign =
      (in->label_location_vertical == SPVDX_LABEL_LOCATION_VERTICAL_NEGATIVE
       ? TABLE_VALIGN_BOTTOM
       : in->label_location_vertical == SPVDX_LABEL_LOCATION_VERTICAL_POSITIVE
       ? TABLE_VALIGN_TOP
       : TABLE_VALIGN_CENTER);
  if (in && in->decimal_offset != DBL_MAX)
    out->cell_style.decimal_offset = optional_px (in->decimal_offset, 0);
#if 0
  if (in && in->margin_left != DBL_MAX)
    out->cell_style.margin[TABLE_HORZ][0] = optional_pt (in->margin_left, 8);
  if (in && in->margin_right != DBL_MAX)
    out->cell_style.margin[TABLE_HORZ][1] = optional_pt (in->margin_right, 11);
  if (in && in->margin_top != DBL_MAX)
    out->cell_style.margin[TABLE_VERT][0] = optional_pt (in->margin_top, 1);
  if (in && in->margin_bottom != DBL_MAX)
    out->cell_style.margin[TABLE_VERT][1] = optional_pt (in->margin_bottom, 1);
#endif
}

static void
decode_spvdx_style (const struct spvdx_style *in,
                    const struct spvdx_style *bg,
                    struct table_area_style *out)
{
  *out = (struct table_area_style) TABLE_AREA_STYLE_INITIALIZER;
  decode_spvdx_style_incremental (in, bg, out);
}

static void
add_footnote (struct pivot_value *v, int idx, struct pivot_table *table)
{
  if (idx < 1 || idx > table->n_footnotes)
    return;

  pivot_value_add_footnote (v, table->footnotes[idx - 1]);
}

static char * WARN_UNUSED_RESULT
decode_label_frame (struct pivot_table *table,
                    const struct spvdx_label_frame *lf)
{
  if (!lf->label)
    return NULL;

  struct pivot_value **target;
  struct table_area_style *area;
  if (lf->label->purpose == SPVDX_PURPOSE_TITLE)
    {
      target = &table->title;
      area = &table->look.areas[PIVOT_AREA_TITLE];
    }
  else if (lf->label->purpose == SPVDX_PURPOSE_SUB_TITLE)
    {
      target = &table->caption;
      area = &table->look.areas[PIVOT_AREA_CAPTION];
    }
  else if (lf->label->purpose == SPVDX_PURPOSE_FOOTNOTE)
    {
      if (lf->label->n_text > 0
          && lf->label->text[0]->uses_reference != INT_MIN)
        {
          target = NULL;
          area = &table->look.areas[PIVOT_AREA_FOOTER];
        }
      else
        return NULL;
    }
  else if (lf->label->purpose == SPVDX_PURPOSE_LAYER)
    {
      target = NULL;
      area = &table->look.areas[PIVOT_AREA_LAYERS];
    }
  else
    return NULL;

  table_area_style_uninit (area);
  decode_spvdx_style (lf->label->style, lf->label->text_frame_style, area);

  if (target)
    {
      struct pivot_value *value = xzalloc (sizeof *value);
      value->type = PIVOT_VALUE_TEXT;
      for (size_t i = 0; i < lf->label->n_text; i++)
        {
          const struct spvdx_text *in = lf->label->text[i];
          if (in->defines_reference != INT_MIN)
            add_footnote (value, in->defines_reference, table);
          else if (!value->text.local)
            value->text.local = xstrdup (in->text);
          else
            {
              char *new = xasprintf ("%s%s", value->text.local, in->text);
              free (value->text.local);
              value->text.local = new;
            }
        }
      pivot_value_destroy (*target);
      *target = value;
    }
  else
    for (size_t i = 0; i < lf->label->n_text; i++)
      {
        const struct spvdx_text *in = lf->label->text[i];
        if (in->uses_reference == INT_MIN)
          continue;
        if (i % 2)
          {
            size_t length = strlen (in->text);
            if (length && in->text[length - 1] == '\n')
              length--;

            pivot_table_create_footnote__ (
              table, in->uses_reference - 1, NULL,
              pivot_value_new_user_text (in->text, length));
          }
        else
          {
            size_t length = strlen (in->text);
            if (length && in->text[length - 1] == '.')
              length--;

            pivot_table_create_footnote__ (
              table, in->uses_reference - 1,
              pivot_value_new_user_text (in->text, length), NULL);
          }
      }
  return NULL;
}

/* Special return value for decode_spvdx_variable(). */
static char BAD_REFERENCE;

static char * WARN_UNUSED_RESULT
decode_spvdx_source_variable (const struct spvxml_node *node,
                              struct spv_data *data,
                              struct hmap *series_map)
{
  const struct spvdx_source_variable *sv = spvdx_cast_source_variable (node);

  struct spv_series *label_series = NULL;
  if (sv->label_variable)
    {
      label_series = spv_series_find (series_map,
                                      sv->label_variable->node_.id);
      if (!label_series)
        return &BAD_REFERENCE;

      label_series->is_label_series = true;
    }

  const struct spv_data_variable *var = spv_data_find_variable (
    data, sv->source, sv->source_name);
  if (!var)
    return xasprintf ("sourceVariable %s references nonexistent "
                      "source %s variable %s.",
                      sv->node_.id, sv->source, sv->source_name);

  struct spv_series *s = xzalloc (sizeof *s);
  s->name = xstrdup (node->id);
  s->xml = node;
  s->label = sv->label ? xstrdup (sv->label) : NULL;
  s->label_series = label_series;
  s->values = spv_data_values_clone (var->values, var->n_values);
  s->n_values = var->n_values;
  s->format = F_8_0;
  hmap_init (&s->map);
  hmap_insert (series_map, &s->hmap_node, hash_string (s->name, 0));

  char *error = spv_series_remap_formats (s, sv->seq, sv->n_seq);
  if (error)
    return error;

  if (label_series && !s->remapped)
    {
      for (size_t i = 0; i < s->n_values; i++)
        if (s->values[i].width < 0)
          {
            char *dest;
            if (label_series->values[i].width < 0)
              {
                union value v = { .f = label_series->values[i].d };
                dest = data_out_stretchy (&v, "UTF-8", &s->format, NULL);
              }
            else
              dest = label_series->values[i].s;
            char *error = spv_map_insert (&s->map, s->values[i].d,
                                          dest, false, NULL);
            free (error);   /* Duplicates are OK. */
            if (label_series->values[i].width < 0)
              free (dest);
          }
    }

  return NULL;
}

static char * WARN_UNUSED_RESULT
decode_spvdx_derived_variable (const struct spvxml_node *node,
                               struct hmap *series_map)
{
  const struct spvdx_derived_variable *dv = spvdx_cast_derived_variable (node);

  struct spv_data_value *values;
  size_t n_values;

  struct substring value = ss_cstr (dv->value);
  if (ss_equals (value, ss_cstr ("constant(0)")))
    {
      struct spv_series *existing_series = spv_series_first (series_map);
      if (!existing_series)
        return &BAD_REFERENCE;

      n_values = existing_series->n_values;
      values = XCALLOC (n_values, struct spv_data_value);
      for (size_t i = 0; i < n_values; i++)
        values[i].width = -1;
    }
  else if (ss_starts_with (value, ss_cstr ("constant(")))
    {
      values = NULL;
      n_values = 0;
    }
  else if (ss_starts_with (value, ss_cstr ("map("))
           && ss_ends_with (value, ss_cstr (")")))
    {
      char *dependency_name = ss_xstrdup (ss_substr (value, 4,
                                                     value.length - 5));
      struct spv_series *dependency
        = spv_series_find (series_map, dependency_name);
      free (dependency_name);
      if (!dependency)
        return &BAD_REFERENCE;

      values = spv_data_values_clone (dependency->values,
                                      dependency->n_values);
      n_values = dependency->n_values;
    }
  else
    return xasprintf ("Derived variable %s has unknown value \"%s\"",
                      node->id, dv->value);

  struct spv_series *s = xzalloc (sizeof *s);
  s->format = F_8_0;
  s->name = xstrdup (node->id);
  s->values = values;
  s->n_values = n_values;
  hmap_init (&s->map);
  hmap_insert (series_map, &s->hmap_node, hash_string (s->name, 0));

  char *error = spv_series_remap_vmes (s, dv->value_map_entry,
                                       dv->n_value_map_entry);
  if (error)
    return error;

  error = spv_series_remap_formats (s, dv->seq, dv->n_seq);
  if (error)
    return error;

  if (n_values > 0)
    {
      for (size_t i = 0; i < n_values; i++)
        if (values[i].width != 0)
          goto nonempty;
      for (size_t i = 0; i < n_values; i++)
        spv_data_value_uninit (&s->values[i]);
      free (s->values);

      s->values = NULL;
      s->n_values = 0;

    nonempty:;
    }
  return NULL;
}

struct format_mapping
  {
    struct hmap_node hmap_node;
    uint32_t from;
    struct fmt_spec to;
  };

static const struct format_mapping *
format_map_find (const struct hmap *format_map, uint32_t u32_format)
{
  if (format_map)
    {
      const struct format_mapping *fm;
      HMAP_FOR_EACH_IN_BUCKET (fm, struct format_mapping, hmap_node,
                               hash_int (u32_format, 0), format_map)
        if (fm->from == u32_format)
          return fm;
    }

  return NULL;
}

static char * WARN_UNUSED_RESULT
spv_format_from_data_value (const struct spv_data_value *data,
                            const struct hmap *format_map,
                            struct fmt_spec *out)
{
  if (!data)
    {
      *out = fmt_for_output (FMT_F, 40, 2);
      return NULL;
    }

  uint32_t u32_format = data->width < 0 ? data->d : atoi (data->s);
  const struct format_mapping *fm = format_map_find (format_map, u32_format);
  if (fm)
    {
      *out = fm->to;
      return NULL;
    }
  return spv_decode_fmt_spec (u32_format, out);
}

static char * WARN_UNUSED_RESULT
pivot_value_from_data_value (const struct spv_data_value *data,
                             const struct spv_data_value *format,
                             const struct hmap *format_map,
                             struct pivot_value **vp)
{
  *vp = NULL;

  struct fmt_spec f;
  char *error = spv_format_from_data_value (format, format_map, &f);
  if (error)
    return error;

  struct pivot_value *v = xzalloc (sizeof *v);
  if (data->width >= 0)
    {
      if (format && fmt_get_category (f.type) == FMT_CAT_DATE)
        {
          int year, month, day, hour, minute, second, msec, len = -1;
          if (sscanf (data->s, "%4d-%2d-%2dT%2d:%2d:%2d.%3d%n",
                      &year, &month, &day, &hour, &minute, &second,
                      &msec, &len) == 7
              && len == 23
              && data->s[len] == '\0')
            {
              double date = calendar_gregorian_to_offset (year, month, day,
                                                          NULL);
              if (date != SYSMIS)
                {
                  v->type = PIVOT_VALUE_NUMERIC;
                  v->numeric.x = (date * 60. * 60. * 24.
                                  + hour * 60. * 60.
                                  + minute * 60.
                                  + second
                                  + msec / 1000.0);
                  v->numeric.format = f;
                  *vp = v;
                  return NULL;
                }
            }
        }
      else if (format && fmt_get_category (f.type) == FMT_CAT_TIME)
        {
          int hour, minute, second, msec, len = -1;
          if (sscanf (data->s, "%d:%2d:%2d.%3d%n",
                      &hour, &minute, &second, &msec, &len) == 4
              && len > 0
              && data->s[len] == '\0')
            {
              v->type = PIVOT_VALUE_NUMERIC;
              v->numeric.x = (hour * 60. * 60.
                              + minute * 60.
                              + second
                              + msec / 1000.0);
              v->numeric.format = f;
              *vp = v;
              return NULL;
            }
        }
      v->type = PIVOT_VALUE_STRING;
      v->string.s = xstrdup (data->s);
    }
  else
    {
      v->type = PIVOT_VALUE_NUMERIC;
      v->numeric.x = data->d;
      v->numeric.format = f;
    }
  *vp = v;
  return NULL;
}

static void
add_parents (struct pivot_category *cat, struct pivot_category *parent,
             size_t group_index)
{
  cat->parent = parent;
  cat->group_index = group_index;
  if (pivot_category_is_group (cat))
    for (size_t i = 0; i < cat->n_subs; i++)
      add_parents (cat->subs[i], cat, i);
}

static const struct spvdx_facet_level *
find_facet_level (const struct spvdx_visualization *v, int facet_level)
{
  const struct spvdx_facet_layout *layout = v->graph->facet_layout;
  for (size_t i = 0; i < layout->n_facet_level; i++)
    {
      const struct spvdx_facet_level *fl = layout->facet_level[i];
      if (facet_level == fl->level)
        return fl;
    }
  return NULL;
}

static bool
should_show_label (const struct spvdx_facet_level *fl)
{
  return fl && fl->axis->label && fl->axis->label->style->visible != 0;
}

static size_t
max_category (const struct spv_series *s)
{
  double max_cat = -DBL_MAX;
  for (size_t i = 0; i < s->n_values; i++)
    {
      const struct spv_data_value *dv = &s->values[i];
      double d = dv->width < 0 ? dv->d : dv->index;
      if (d > max_cat)
        max_cat = d;
    }
  assert (max_cat >= 0 && max_cat < SIZE_MAX - 1);

  return max_cat;
}

static void
add_affixes (struct pivot_table *table, struct pivot_value *value,
             struct spvdx_affix **affixes, size_t n_affixes)
{
  for (size_t i = 0; i < n_affixes; i++)
    add_footnote (value, affixes[i]->defines_reference, table);
}

static char * WARN_UNUSED_RESULT
add_dimension (struct spv_series **series, size_t n,
               enum pivot_axis_type axis_type,
               const struct spvdx_visualization *v, struct pivot_table *table,
               struct spv_series **dim_seriesp, size_t *n_dim_seriesp,
               int base_facet_level, struct pivot_dimension **dp)
{
  char *error = NULL;

  const struct spvdx_facet_level *fl
    = find_facet_level (v, base_facet_level + n);
  if (fl)
    {
      struct table_area_style *area
        = (axis_type == PIVOT_AXIS_COLUMN
           ? &table->look.areas[PIVOT_AREA_COLUMN_LABELS]
           : axis_type == PIVOT_AXIS_ROW
           ? &table->look.areas[PIVOT_AREA_ROW_LABELS]
           : NULL);
      if (area && fl->axis->label)
        {
          table_area_style_uninit (area);
          decode_spvdx_style (fl->axis->label->style,
                              fl->axis->label->text_frame_style, area);
        }
    }

  if (axis_type == PIVOT_AXIS_ROW)
    {
      const struct spvdx_facet_level *fl2
        = find_facet_level (v, base_facet_level + (n - 1));
      if (fl2)
        decode_spvdx_style_incremental (
          fl2->axis->major_ticks->style,
          fl2->axis->major_ticks->tick_frame_style,
          &table->look.areas[PIVOT_AREA_ROW_LABELS]);
    }

  const struct spvdx_facet_level *fl3 = find_facet_level (v, base_facet_level);
  if (fl3 && fl3->axis->major_ticks->label_angle == -90)
    {
      if (axis_type == PIVOT_AXIS_COLUMN)
        table->rotate_inner_column_labels = true;
      else
        table->rotate_outer_row_labels = true;
    }

  /* Find the first row for each category. */
  size_t max_cat = max_category (series[0]);
  size_t *cat_rows = xnmalloc (max_cat + 1, sizeof *cat_rows);
  for (size_t k = 0; k <= max_cat; k++)
    cat_rows[k] = SIZE_MAX;
  for (size_t k = 0; k < series[0]->n_values; k++)
    {
      const struct spv_data_value *dv = &series[0]->values[k];
      double d = dv->width < 0 ? dv->d : dv->index;
      if (d >= 0 && d < SIZE_MAX - 1)
        {
          size_t row = d;
          if (cat_rows[row] == SIZE_MAX)
            cat_rows[row] = k;
        }
    }

  /* Drop missing categories and count what's left. */
  size_t n_cats = 0;
  for (size_t k = 0; k <= max_cat; k++)
    if (cat_rows[k] != SIZE_MAX)
      cat_rows[n_cats++] = cat_rows[k];
  assert (n_cats > 0);

  /* Make the categories. */
  struct pivot_dimension *d = xzalloc (sizeof *d);
  table->dimensions[table->n_dimensions++] = d;

  series[0]->n_index = max_cat + 1;
  series[0]->index_to_category = xcalloc (
    max_cat + 1, sizeof *series[0]->index_to_category);
  struct pivot_category **cats = xnmalloc (n_cats, sizeof **cats);
  for (size_t k = 0; k < n_cats; k++)
    {
      struct spv_data_value *dv = &series[0]->values[cat_rows[k]];
      int dv_num = dv ? dv->d : dv->index;
      struct pivot_category *cat = xzalloc (sizeof *cat);
      char *retval = pivot_value_from_data_value (
        spv_map_lookup (&series[0]->map, dv), NULL, NULL, &cat->name);
      if (retval)
        {
          if (error)
            free (retval);
          else
            error = retval;
        }
      cat->parent = NULL;
      cat->dimension = d;
      cat->data_index = k;
      cat->presentation_index = cat_rows[k];
      cats[k] = cat;
      series[0]->index_to_category[dv_num] = cat;

      if (cat->name)
        add_affixes (table, cat->name,
                     series[0]->affixes, series[0]->n_affixes);
    }
  free (cat_rows);

  struct pivot_axis *axis = &table->axes[axis_type];
  d->axis_type = axis_type;
  d->level = axis->n_dimensions;
  d->top_index = table->n_dimensions - 1;
  d->root = xzalloc (sizeof *d->root);
  *d->root = (struct pivot_category) {
    .name = pivot_value_new_user_text (
      series[0]->label ? series[0]->label : "", -1),
    .dimension = d,
    .show_label = should_show_label (fl),
    .data_index = SIZE_MAX,
    .presentation_index = SIZE_MAX,
  };
  d->data_leaves = xmemdup (cats, n_cats * sizeof *cats);
  d->presentation_leaves = xmemdup (cats, n_cats * sizeof *cats);
  d->n_leaves = d->allocated_leaves = n_cats;

  /* Now group them, in one pass per grouping variable, innermost first. */
  for (size_t j = 1; j < n; j++)
    {
      struct pivot_category **new_cats = xnmalloc (n_cats, sizeof **cats);
      size_t n_new_cats = 0;

      /* Allocate a category index. */
      size_t max_cat = max_category (series[j]);
      series[j]->n_index = max_cat + 1;
      series[j]->index_to_category = xcalloc (
        max_cat + 1, sizeof *series[j]->index_to_category);
      for (size_t cat1 = 0; cat1 < n_cats;)
        {
          /* Find a sequence of categories cat1...cat2 (exclusive), that all
             have the same value in series 'j'.  (This might be only a single
             category; we will drop unnamed 1-category groups later.) */
          size_t row1 = cats[cat1]->presentation_index;
          const struct spv_data_value *dv1 = &series[j]->values[row1];
          size_t cat2;
          for (cat2 = cat1 + 1; cat2 < n_cats; cat2++)
            {
              size_t row2 = cats[cat2]->presentation_index;
              const struct spv_data_value *dv2 = &series[j]->values[row2];
              if (!spv_data_value_equal (dv1, dv2))
                break;
            }
          size_t n_subs = cat2 - cat1;

          struct pivot_category *new_cat;
          const struct spv_data_value *name
            = spv_map_lookup (&series[j]->map, dv1);
          if (n_subs == 1 && name->width == 0)
            {
              /* The existing category stands on its own. */
              new_cat = cats[cat1++];
            }
          else
            {
              /* Create a new group with cat...cat2 as subcategories. */
              new_cat = xzalloc (sizeof *new_cat);
              *new_cat = (struct pivot_category) {
                .dimension = d,
                .subs = xnmalloc (n_subs, sizeof *new_cat->subs),
                .n_subs = n_subs,
                .show_label = true,
                .data_index = SIZE_MAX,
                .presentation_index = row1,
              };
              char *retval = pivot_value_from_data_value (name, NULL, NULL,
                                                          &new_cat->name);
              if (retval)
                {
                  if (error)
                    free (retval);
                  else
                    error = retval;
                }
              for (size_t k = 0; k < n_subs; k++)
                new_cat->subs[k] = cats[cat1++];

              int dv1_num = dv1->width < 0 ? dv1->d : dv1->index;
              series[j]->index_to_category[dv1_num] = new_cat;
            }

          if (new_cat->name)
            add_affixes (table, new_cat->name,
                         series[j]->affixes, series[j]->n_affixes);

          /* Append the new group to the list of new groups. */
          new_cats[n_new_cats++] = new_cat;
        }

      free (cats);
      cats = new_cats;
      n_cats = n_new_cats;
    }

  /* Now drop unnamed 1-category groups and add parent pointers. */
  for (size_t j = 0; j < n_cats; j++)
    add_parents (cats[j], d->root, j);

  d->root->subs = cats;
  d->root->n_subs = n_cats;

  if (error)
    {
      pivot_dimension_destroy (d);
      return error;
    }

  dim_seriesp[(*n_dim_seriesp)++] = series[0];
  series[0]->dimension = d;

  axis->dimensions = xnrealloc (axis->dimensions, axis->n_dimensions + 1,
                               sizeof *axis->dimensions);
  axis->dimensions[axis->n_dimensions++] = d;
  axis->extent *= d->n_leaves;

  *dp = d;
  return NULL;
}

static char * WARN_UNUSED_RESULT
add_dimensions (struct hmap *series_map, const struct spvdx_nest *nest,
                enum pivot_axis_type axis_type,
                const struct spvdx_visualization *v, struct pivot_table *table,
                struct spv_series **dim_seriesp, size_t *n_dim_seriesp,
                int level_ofs)
{
  struct pivot_axis *axis = &table->axes[axis_type];
  if (!axis->extent)
    axis->extent = 1;

  if (!nest)
    return NULL;

  struct spv_series **series = xnmalloc (nest->n_vars, sizeof *series);
  for (size_t i = 0; i < nest->n_vars;)
    {
      size_t n;
      for (n = 0; i + n < nest->n_vars; n++)
        {
          series[n] = spv_series_from_ref (series_map, nest->vars[i + n]->ref);
          if (!series[n] || !series[n]->n_values)
            break;
        }

      if (n > 0)
        {
          struct pivot_dimension *d;
          char *error = add_dimension (series, n, axis_type, v, table,
                                       dim_seriesp, n_dim_seriesp,
                                       level_ofs + i, &d);
          if (error)
            {
              free (series);
              return error;
            }
        }

      i += n + 1;
    }
  free (series);

  return NULL;
}

static char * WARN_UNUSED_RESULT
add_layers (struct hmap *series_map,
            struct spvdx_layer **layers, size_t n_layers,
            const struct spvdx_visualization *v, struct pivot_table *table,
            struct spv_series **dim_seriesp, size_t *n_dim_seriesp,
            int level_ofs)
{
  struct pivot_axis *axis = &table->axes[PIVOT_AXIS_LAYER];
  if (!axis->extent)
    axis->extent = 1;

  if (!n_layers)
    return NULL;

  struct spv_series **series = xnmalloc (n_layers, sizeof *series);
  for (size_t i = 0; i < n_layers;)
    {
      size_t n;
      for (n = 0; i + n < n_layers; n++)
        {
          series[n] = spv_series_from_ref (series_map,
                                           layers[i + n]->variable);
          if (!series[n] || !series[n]->n_values)
            break;
        }

      if (n > 0)
        {
          struct pivot_dimension *d;
          char *error = add_dimension (
            series, n, PIVOT_AXIS_LAYER, v, table,
            dim_seriesp, n_dim_seriesp, level_ofs + i, &d);
          if (error)
            {
              free (series);
              return error;
            }

          int index = atoi (layers[i]->value);
          assert (index < d->n_leaves);
          table->current_layer = xrealloc (
            table->current_layer,
            axis->n_dimensions * sizeof *table->current_layer);
          table->current_layer[axis->n_dimensions - 1] = index;
        }
      i += n + 1;
    }
  free (series);

  return NULL;
}

static struct pivot_category *
find_category (struct spv_series *series, int index)
{
  return (index >= 0 && index < series->n_index
          ? series->index_to_category[index]
          : NULL);
}

static bool
int_in_array (int value, const int *array, size_t n)
{
  for (size_t i = 0; i < n; i++)
    if (array[i] == value)
      return true;

  return false;
}

static void
apply_styles_to_value (struct pivot_table *table,
                       struct pivot_value *value,
                       const struct spvdx_set_format *sf,
                       const struct table_area_style *base_area_style,
                       const struct spvdx_style *fg,
                       const struct spvdx_style *bg)
{
  if (sf)
    {
      if (sf->reset > 0)
        {
          free (value->footnotes);
          value->footnotes = NULL;
          value->n_footnotes = 0;
        }

      struct fmt_spec format = { .w = 0 };
      if (sf->format)
        {
          format = decode_format (sf->format);
          add_affixes (table, value, sf->format->affix, sf->format->n_affix);
        }
      else if (sf->number_format)
        {
          format = decode_number_format (sf->number_format);
          add_affixes (table, value, sf->number_format->affix,
                       sf->number_format->n_affix);
        }
      else if (sf->n_string_format)
        {
          for (size_t i = 0; i < sf->n_string_format; i++)
            add_affixes (table, value, sf->string_format[i]->affix,
                         sf->string_format[i]->n_affix);
        }
      else if (sf->date_time_format)
        {
          format = decode_date_time_format (sf->date_time_format);
          add_affixes (table, value, sf->date_time_format->affix,
                       sf->date_time_format->n_affix);
        }
      else if (sf->elapsed_time_format)
        {
          format = decode_elapsed_time_format (sf->elapsed_time_format);
          add_affixes (table, value, sf->elapsed_time_format->affix,
                       sf->elapsed_time_format->n_affix);
        }

      if (format.w)
        {
          if (value->type == PIVOT_VALUE_NUMERIC)
            value->numeric.format = format;

          /* Possibly we should try to apply date and time formats too,
             but none seem to occur in practice so far. */
        }
    }
  if (fg || bg)
    {
      struct table_area_style area;
      pivot_value_get_style (
        value,
        value->font_style ? value->font_style : &base_area_style->font_style,
        value->cell_style ? value->cell_style : &base_area_style->cell_style,
        &area);
      decode_spvdx_style_incremental (fg, bg, &area);
      pivot_value_set_style (value, &area);
      table_area_style_uninit (&area);
    }
}

static void
decode_set_cell_properties__ (struct pivot_table *table,
                              struct hmap *series_map,
                              const struct spvdx_intersect *intersect,
                              const struct spvdx_style *interval,
                              const struct spvdx_style *graph,
                              const struct spvdx_style *labeling,
                              const struct spvdx_style *frame,
                              const struct spvdx_style *major_ticks,
                              const struct spvdx_set_format *set_format)
{
  if (graph && labeling && intersect->alternating
      && !interval && !major_ticks && !frame && !set_format)
    {
      /* Sets alt_fg_color and alt_bg_color. */
      struct table_area_style area;
      decode_spvdx_style (labeling, graph, &area);
      table->look.areas[PIVOT_AREA_DATA].font_style.fg[1]
        = area.font_style.fg[0];
      table->look.areas[PIVOT_AREA_DATA].font_style.bg[1]
        = area.font_style.bg[0];
      table_area_style_uninit (&area);
    }
  else if (graph
           && !labeling && !interval && !major_ticks && !frame && !set_format)
    {
      /* 'graph->width' likely just sets the width of the table as a
         whole.  */
    }
  else if (!graph && !labeling && !interval && !frame && !set_format
           && !major_ticks)
    {
      /* No-op.  (Presumably there's a setMetaData we don't care about.) */
    }
  else if (((set_format && spvdx_is_major_ticks (set_format->target))
            || major_ticks || frame)
           && intersect->n_where == 1)
    {
      /* Formatting for individual row or column labels. */
      const struct spvdx_where *w = intersect->where[0];
      struct spv_series *s = spv_series_find (series_map, w->variable->id);
      assert (s);

      const char *p = w->include;

      while (*p)
        {
          char *tail;
          int include = strtol (p, &tail, 10);

          struct pivot_category *c = find_category (s, include);
          if (c)
            {
              const struct table_area_style *base_area_style
                = (c->dimension->axis_type == PIVOT_AXIS_ROW
                   ? &table->look.areas[PIVOT_AREA_ROW_LABELS]
                   : &table->look.areas[PIVOT_AREA_COLUMN_LABELS]);
              apply_styles_to_value (table, c->name, set_format,
                                     base_area_style, major_ticks, frame);
            }

          if (tail == p)
            break;
          p = tail;
          if (*p == ';')
            p++;
        }
    }
  else if ((set_format && spvdx_is_labeling (set_format->target))
           || labeling || interval)
    {
      /* Formatting for individual cells or groups of them with some dimensions
         in common. */
      int **indexes = XCALLOC (table->n_dimensions, int *);
      size_t *n = XCALLOC (table->n_dimensions, size_t);
      size_t *allocated = XCALLOC (table->n_dimensions, size_t);

      for (size_t i = 0; i < intersect->n_where; i++)
        {
          const struct spvdx_where *w = intersect->where[i];
          struct spv_series *s = spv_series_find (series_map, w->variable->id);
          assert (s);
          if (!s->dimension)
            {
              /* Group indexes may be included even though they are redundant.
                 Ignore them. */
              continue;
            }

          size_t j = s->dimension->top_index;

          const char *p = w->include;
          while (*p)
            {
              char *tail;
              int include = strtol (p, &tail, 10);

              struct pivot_category *c = find_category (s, include);
              if (c)
                {
                  if (n[j] >= allocated[j])
                    indexes[j] = x2nrealloc (indexes[j], &allocated[j],
                                             sizeof *indexes[j]);
                  indexes[j][n[j]++] = c->data_index;
                }

              if (tail == p)
                break;
              p = tail;
              if (*p == ';')
                p++;
            }
        }

#if 0
      printf ("match:");
      for (size_t i = 0; i < table->n_dimensions; i++)
        {
          if (n[i])
            {
              printf (" %d=(", i);
              for (size_t j = 0; j < n[i]; j++)
                {
                  if (j)
                    putchar (',');
                  printf ("%d", indexes[i][j]);
                }
              putchar (')');
            }
        }
      printf ("\n");
#endif

      /* XXX This is inefficient in the common case where all of the dimensions
         are matched.  We should use a heuristic where if all of the dimensions
         are matched and the product of n[*] is less than
         hmap_count(&table->cells) then iterate through all the possibilities
         rather than all the cells.  Or even only do it if there is just one
         possibility. */

      struct pivot_cell *cell;
      HMAP_FOR_EACH (cell, struct pivot_cell, hmap_node, &table->cells)
        {
          for (size_t i = 0; i < table->n_dimensions; i++)
            {
              if (n[i] && !int_in_array (cell->idx[i], indexes[i], n[i]))
                goto skip;
            }
          apply_styles_to_value (table, cell->value, set_format,
                                 &table->look.areas[PIVOT_AREA_DATA],
                                 labeling, interval);

        skip: ;
        }

      for (size_t i = 0; i < table->n_dimensions; i++)
        free (indexes[i]);
      free (indexes);
      free (n);
      free (allocated);
    }
  else
    NOT_REACHED ();
}

static void
decode_set_cell_properties (struct pivot_table *table, struct hmap *series_map,
                            struct spvdx_set_cell_properties **scps,
                            size_t n_scps)
{
  for (size_t i = 0; i < n_scps; i++)
    {
      const struct spvdx_set_cell_properties *scp = scps[i];
      const struct spvdx_style *interval = NULL;
      const struct spvdx_style *graph = NULL;
      const struct spvdx_style *labeling = NULL;
      const struct spvdx_style *frame = NULL;
      const struct spvdx_style *major_ticks = NULL;
      const struct spvdx_set_format *set_format = NULL;
      for (size_t j = 0; j < scp->n_seq; j++)
        {
          const struct spvxml_node *node = scp->seq[j];
          if (spvdx_is_set_style (node))
            {
              const struct spvdx_set_style *set_style
                = spvdx_cast_set_style (node);
              if (spvdx_is_graph (set_style->target))
                graph = set_style->style;
              else if (spvdx_is_labeling (set_style->target))
                labeling = set_style->style;
              else if (spvdx_is_interval (set_style->target))
                interval = set_style->style;
              else if (spvdx_is_major_ticks (set_style->target))
                major_ticks = set_style->style;
              else
                NOT_REACHED ();
            }
          else if (spvdx_is_set_frame_style (node))
            frame = spvdx_cast_set_frame_style (node)->style;
          else if (spvdx_is_set_format (node))
            set_format = spvdx_cast_set_format (node);
          else
            assert (spvdx_is_set_meta_data (node));
        }

      if (scp->union_ && scp->apply_to_converse <= 0)
        {
          for (size_t j = 0; j < scp->union_->n_intersect; j++)
            decode_set_cell_properties__ (
              table, series_map, scp->union_->intersect[j],
              interval, graph, labeling, frame, major_ticks, set_format);
        }
      else if (!scp->union_ && scp->apply_to_converse > 0)
        {
          if ((set_format && spvdx_is_labeling (set_format->target))
              || labeling || interval)
            {
              struct pivot_cell *cell;
              HMAP_FOR_EACH (cell, struct pivot_cell, hmap_node, &table->cells)
                apply_styles_to_value (table, cell->value, set_format,
                                       &table->look.areas[PIVOT_AREA_DATA],
                                       NULL, NULL);
            }
        }
      else if (!scp->union_ && scp->apply_to_converse <= 0)
        {
          /* Appears to be used to set the font for something--but what? */
        }
      else
        NOT_REACHED ();
    }
}

static struct spv_series *
parse_formatting (const struct spvdx_visualization *v,
                  const struct hmap *series_map, struct hmap *format_map)
{
  const struct spvdx_labeling *labeling = v->graph->interval->labeling;
  struct spv_series *cell_format = NULL;
  for (size_t i = 0; i < labeling->n_seq; i++)
    {
      const struct spvdx_formatting *f
        = spvdx_cast_formatting (labeling->seq[i]);
      if (!f)
        continue;

      cell_format = spv_series_from_ref (series_map, f->variable);
      for (size_t j = 0; j < f->n_format_mapping; j++)
        {
          const struct spvdx_format_mapping *fm = f->format_mapping[j];

          if (fm->format)
            {
              struct format_mapping *out = xmalloc (sizeof *out);
              out->from = fm->from;
              out->to = decode_format (fm->format);
              hmap_insert (format_map, &out->hmap_node,
                           hash_int (out->from, 0));
            }
        }
    }

  return cell_format;
}

static void
format_map_destroy (struct hmap *format_map)
{
  struct format_mapping *fm, *next;
  HMAP_FOR_EACH_SAFE (fm, next, struct format_mapping, hmap_node, format_map)
    {
      hmap_delete (format_map, &fm->hmap_node);
      free (fm);
    }
  hmap_destroy (format_map);
}

char * WARN_UNUSED_RESULT
decode_spvdx_table (const struct spvdx_visualization *v, const char *subtype,
                    const struct pivot_table_look *look,
                    struct spv_data *data, struct pivot_table **outp)
{
  struct pivot_table *table = pivot_table_create__ (NULL, subtype);
  pivot_table_set_look (table, look);

  struct hmap series_map = HMAP_INITIALIZER (series_map);
  struct hmap format_map = HMAP_INITIALIZER (format_map);
  struct spv_series **dim_series = NULL;
  char *error;

  struct spvdx_visualization_extension *ve = v->visualization_extension;
  table->show_grid_lines = ve && ve->show_gridline;

  /* Sizing from the legacy properties can get overridden. */
  if (v->graph->cell_style->width)
    {
      int min_width, max_width, n = 0;
      if (sscanf (v->graph->cell_style->width, "%*d%%;%dpt;%dpt%n",
                  &min_width, &max_width, &n)
          && v->graph->cell_style->width[n] == '\0')
        {
          table->look.width_ranges[TABLE_HORZ][0] = min_width;
          table->look.width_ranges[TABLE_HORZ][1] = max_width;
        }
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
  if (v->container)
    for (size_t i = 0; i < v->container->n_label_frame; i++)
      {
        const struct spvdx_label_frame *lf = v->container->label_frame[i];
        if (lf->label
            && lf->label->purpose == SPVDX_PURPOSE_FOOTNOTE
            && lf->label->n_text > 0
            && lf->label->text[0]->uses_reference > 0)
          {
            pivot_table_create_footnote__ (
              table, lf->label->text[0]->uses_reference - 1,
              NULL, NULL);
          }
      }

  if (v->graph->interval->footnotes)
    decode_footnotes (table, v->graph->interval->footnotes);

  struct spv_series *footnotes = NULL;
  for (size_t i = 0; i < v->graph->interval->labeling->n_seq; i++)
    {
      const struct spvxml_node *node = v->graph->interval->labeling->seq[i];
      if (spvdx_is_footnotes (node))
        {
          const struct spvdx_footnotes *f = spvdx_cast_footnotes (node);
          footnotes = spv_series_from_ref (&series_map, f->variable);
          decode_footnotes (table, f);
        }
    }
  for (size_t i = 0; i < v->n_lf1; i++)
    {
      error = decode_label_frame (table, v->lf1[i]);
      if (error)
        goto exit;
    }
  for (size_t i = 0; i < v->n_lf2; i++)
    {
      error = decode_label_frame (table, v->lf2[i]);
      if (error)
        goto exit;
    }
  if (v->container)
    for (size_t i = 0; i < v->container->n_label_frame; i++)
      {
        error = decode_label_frame (table, v->container->label_frame[i]);
        if (error)
          goto exit;
      }
  if (v->graph->interval->labeling->style)
    {
      table_area_style_uninit (&table->look.areas[PIVOT_AREA_DATA]);
      decode_spvdx_style (v->graph->interval->labeling->style,
                          v->graph->cell_style,
                          &table->look.areas[PIVOT_AREA_DATA]);
    }

  /* Decode all of the sourceVariable and derivedVariable  */
  struct spvxml_node **nodes = xmemdup (v->seq, v->n_seq * sizeof *v->seq);
  size_t n_nodes = v->n_seq;
  while (n_nodes > 0)
    {
      bool progress = false;
      for (size_t i = 0; i < n_nodes;)
        {
          error = (spvdx_is_source_variable (nodes[i])
                   ? decode_spvdx_source_variable (nodes[i], data, &series_map)
                   : decode_spvdx_derived_variable (nodes[i], &series_map));
          if (!error)
            {
              nodes[i] = nodes[--n_nodes];
              progress = true;
            }
          else if (error == &BAD_REFERENCE)
            i++;
          else
            {
              free (nodes);
              goto exit;
            }
        }

      if (!progress)
        {
          free (nodes);
          error = xasprintf ("Table has %zu variables with circular or "
                             "unresolved references, including variable %s.",
                             n_nodes, nodes[0]->id);
          goto exit;
        }
    }
  free (nodes);

  const struct spvdx_cross *cross = v->graph->faceting->cross;

  assert (cross->n_seq == 1);
  const struct spvdx_nest *columns = spvdx_cast_nest (cross->seq[0]);
  size_t max_columns = columns ? columns->n_vars : 0;

  assert (cross->n_seq2 == 1);
  const struct spvdx_nest *rows = spvdx_cast_nest (cross->seq2[0]);
  size_t max_rows = rows ? rows->n_vars : 0;

  size_t max_layers = (v->graph->faceting->n_layers1
                       + v->graph->faceting->n_layers2);

  size_t max_dims = max_columns + max_rows + max_layers;
  table->dimensions = xnmalloc (max_dims, sizeof *table->dimensions);
  dim_series = xnmalloc (max_dims, sizeof *dim_series);
  size_t n_dim_series = 0;

  error = add_dimensions (&series_map, columns, PIVOT_AXIS_COLUMN, v, table,
                          dim_series, &n_dim_series, 1);
  if (error)
    goto exit;

  error = add_dimensions (&series_map, rows, PIVOT_AXIS_ROW, v, table,
                          dim_series, &n_dim_series, max_columns + 1);
  if (error)
    goto exit;

  error = add_layers (&series_map, v->graph->faceting->layers1,
                      v->graph->faceting->n_layers1,
                      v, table, dim_series, &n_dim_series,
                      max_rows + max_columns + 1);
  if (error)
    goto exit;

  error = add_layers (&series_map, v->graph->faceting->layers2,
                      v->graph->faceting->n_layers2,
                      v, table, dim_series, &n_dim_series,
                      (max_rows + max_columns + v->graph->faceting->n_layers1
                       + 1));
  if (error)
    goto exit;

  struct spv_series *cell = spv_series_find (&series_map, "cell");
  if (!cell)
    {
      error = xstrdup (_("Table lacks cell data."));
      goto exit;
    }

  struct spv_series *cell_format = parse_formatting (v, &series_map,
                                                     &format_map);

  assert (table->n_dimensions == n_dim_series);
  size_t *dim_indexes = xnmalloc (table->n_dimensions, sizeof *dim_indexes);
  for (size_t i = 0; i < cell->n_values; i++)
    {
      for (size_t j = 0; j < table->n_dimensions; j++)
        {
          const struct spv_data_value *value = &dim_series[j]->values[i];
          const struct pivot_category *cat = find_category (
            dim_series[j], value->width < 0 ? value->d : value->index);
          if (!cat)
            goto skip;
          dim_indexes[j] = cat->data_index;
        }

      struct pivot_value *value;
      error = pivot_value_from_data_value (
        &cell->values[i], cell_format ? &cell_format->values[i] : NULL,
        &format_map, &value);
      if (error)
        goto exit;

      if (footnotes)
        {
          const struct spv_data_value *d = &footnotes->values[i];
          if (d->width >= 0)
            {
              const char *p = d->s;
              while (*p)
                {
                  char *tail;
                  int idx = strtol (p, &tail, 10);
                  add_footnote (value, idx, table);
                  if (tail == p)
                    break;
                  p = tail;
                  if (*p == ',')
                    p++;
                }
            }
        }

      if (value->type == PIVOT_VALUE_NUMERIC
          && value->numeric.x == SYSMIS
          && !value->n_footnotes)
        {
          /* Apparently, system-missing values are just empty cells? */
          pivot_value_destroy (value);
        }
      else
        pivot_table_put (table, dim_indexes, table->n_dimensions, value);
    skip:;
    }
  free (dim_indexes);

  decode_set_cell_properties (table, &series_map, v->graph->facet_layout->scp1,
                              v->graph->facet_layout->n_scp1);
  decode_set_cell_properties (table, &series_map, v->graph->facet_layout->scp2,
                              v->graph->facet_layout->n_scp2);

  pivot_table_assign_label_depth (table);

  format_map_destroy (&format_map);

exit:
  free (dim_series);
  spv_series_destroy (&series_map);
  if (error)
    {
      pivot_table_unref (table);
      *outp = NULL;
    }
  else
    *outp = table;
  return error;
}
