/* PSPP - a program for statistical analysis.
   Copyright (C) 2017, 2018, 2020 Free Software Foundation, Inc.

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

#include "output/spv/spv-table-look.h"

#include <errno.h>
#include <inttypes.h>
#include <libxml/xmlreader.h>
#include <libxml/xmlwriter.h>
#include <string.h>

#include "output/spv/structure-xml-parser.h"

#include "gl/read-file.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

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

static int
optional_int (int x, int default_value)
{
  return x != INT_MIN ? x : default_value;
}

static int
optional_pt (double inches, int default_pt)
{
  return inches != DBL_MAX ? inches * 72.0 + .5 : default_pt;
}

static const char *pivot_area_names[PIVOT_N_AREAS] = {
  [PIVOT_AREA_TITLE] = "title",
  [PIVOT_AREA_CAPTION] = "caption",
  [PIVOT_AREA_FOOTER] = "footnotes",
  [PIVOT_AREA_CORNER] = "cornerLabels",
  [PIVOT_AREA_COLUMN_LABELS] = "columnLabels",
  [PIVOT_AREA_ROW_LABELS] = "rowLabels",
  [PIVOT_AREA_DATA] = "data",
  [PIVOT_AREA_LAYERS] = "layers",
};

static enum pivot_area
pivot_area_from_name (const char *name)
{
  enum pivot_area area;
  for (area = 0; area < PIVOT_N_AREAS; area++)
    if (!strcmp (name, pivot_area_names[area]))
      break;
  return area;
}

static const char *pivot_border_names[PIVOT_N_BORDERS] = {
  [PIVOT_BORDER_TITLE] = "titleLayerSeparator",
  [PIVOT_BORDER_OUTER_LEFT] = "leftOuterFrame",
  [PIVOT_BORDER_OUTER_TOP] = "topOuterFrame",
  [PIVOT_BORDER_OUTER_RIGHT] = "rightOuterFrame",
  [PIVOT_BORDER_OUTER_BOTTOM] = "bottomOuterFrame",
  [PIVOT_BORDER_INNER_LEFT] = "leftInnerFrame",
  [PIVOT_BORDER_INNER_TOP] = "topInnerFrame",
  [PIVOT_BORDER_INNER_RIGHT] = "rightInnerFrame",
  [PIVOT_BORDER_INNER_BOTTOM] = "bottomInnerFrame",
  [PIVOT_BORDER_DATA_LEFT] = "dataAreaLeft",
  [PIVOT_BORDER_DATA_TOP] = "dataAreaTop",
  [PIVOT_BORDER_DIM_ROW_HORZ] = "horizontalDimensionBorderRows",
  [PIVOT_BORDER_DIM_ROW_VERT] = "verticalDimensionBorderRows",
  [PIVOT_BORDER_DIM_COL_HORZ] = "horizontalDimensionBorderColumns",
  [PIVOT_BORDER_DIM_COL_VERT] = "verticalDimensionBorderColumns",
  [PIVOT_BORDER_CAT_ROW_HORZ] = "horizontalCategoryBorderRows",
  [PIVOT_BORDER_CAT_ROW_VERT] = "verticalCategoryBorderRows",
  [PIVOT_BORDER_CAT_COL_HORZ] = "horizontalCategoryBorderColumns",
  [PIVOT_BORDER_CAT_COL_VERT] = "verticalCategoryBorderColumns",
};

static enum pivot_border
pivot_border_from_name (const char *name)
{
  enum pivot_border border;
  for (border = 0; border < PIVOT_N_BORDERS; border++)
    if (!strcmp (name, pivot_border_names[border]))
      break;
  return border;
}

char * WARN_UNUSED_RESULT
spv_table_look_decode (const struct spvsx_table_properties *in,
                       struct spv_table_look **outp)
{
  struct spv_table_look *out = xzalloc (sizeof *out);
  char *error = NULL;

  out->name = in->name ? xstrdup (in->name) : NULL;

  const struct spvsx_general_properties *g = in->general_properties;
  out->omit_empty = g->hide_empty_rows != 0;
  out->width_ranges[TABLE_HORZ][0] = optional_pt (g->minimum_column_width, -1);
  out->width_ranges[TABLE_HORZ][1] = optional_pt (g->maximum_column_width, -1);
  out->width_ranges[TABLE_VERT][0] = optional_pt (g->minimum_row_width, -1);
  out->width_ranges[TABLE_VERT][1] = optional_pt (g->maximum_row_width, -1);
  out->row_labels_in_corner
    = g->row_dimension_labels != SPVSX_ROW_DIMENSION_LABELS_NESTED;

  const struct spvsx_footnote_properties *f = in->footnote_properties;
  out->footnote_marker_superscripts
    = (f->marker_position != SPVSX_MARKER_POSITION_SUBSCRIPT);
  out->show_numeric_markers
    = (f->number_format == SPVSX_NUMBER_FORMAT_NUMERIC);

  for (int i = 0; i < PIVOT_N_AREAS; i++)
    area_style_copy (NULL, &out->areas[i], pivot_area_get_default_style (i));

  const struct spvsx_cell_format_properties *cfp = in->cell_format_properties;
  for (size_t i = 0; i < cfp->n_cell_style; i++)
    {
      const struct spvsx_cell_style *c = cfp->cell_style[i];
      const char *name = CHAR_CAST (const char *, c->node_.raw->name);
      enum pivot_area area = pivot_area_from_name (name);
      if (area == PIVOT_N_AREAS)
        {
          error = xasprintf ("unknown area \"%s\" in cellFormatProperties",
                             name);
          goto error;
        }

      struct area_style *a = &out->areas[area];
      const struct spvsx_style *s = c->style;
      if (s->font_weight)
        a->font_style.bold = s->font_weight == SPVSX_FONT_WEIGHT_BOLD;
      if (s->font_style)
        a->font_style.italic = s->font_style == SPVSX_FONT_STYLE_ITALIC;
      if (s->font_underline)
        a->font_style.underline
          = s->font_underline == SPVSX_FONT_UNDERLINE_UNDERLINE;
      if (s->color >= 0)
        a->font_style.fg[0] = optional_color (
          s->color, (struct cell_color) CELL_COLOR_BLACK);
      if (c->alternating_text_color >= 0 || s->color >= 0)
        a->font_style.fg[1] = optional_color (c->alternating_text_color,
                                              a->font_style.fg[0]);
      if (s->color2 >= 0)
        a->font_style.bg[0] = optional_color (
          s->color2, (struct cell_color) CELL_COLOR_WHITE);
      if (c->alternating_color >= 0 || s->color2 >= 0)
        a->font_style.bg[1] = optional_color (c->alternating_color,
                                              a->font_style.bg[0]);
      if (s->font_family)
        {
          free (a->font_style.typeface);
          a->font_style.typeface = xstrdup (s->font_family);
        }

      if (s->font_size)
        a->font_style.size = optional_length (s->font_size, 0);

      if (s->text_alignment)
        a->cell_style.halign
          = (s->text_alignment == SPVSX_TEXT_ALIGNMENT_LEFT
             ? TABLE_HALIGN_LEFT
             : s->text_alignment == SPVSX_TEXT_ALIGNMENT_RIGHT
             ? TABLE_HALIGN_RIGHT
             : s->text_alignment == SPVSX_TEXT_ALIGNMENT_CENTER
             ? TABLE_HALIGN_CENTER
             : s->text_alignment == SPVSX_TEXT_ALIGNMENT_DECIMAL
             ? TABLE_HALIGN_DECIMAL
             : TABLE_HALIGN_MIXED);
      if (s->label_location_vertical)
        a->cell_style.valign
          = (s->label_location_vertical == SPVSX_LABEL_LOCATION_VERTICAL_NEGATIVE
             ? TABLE_VALIGN_BOTTOM
             : s->label_location_vertical == SPVSX_LABEL_LOCATION_VERTICAL_POSITIVE
             ? TABLE_VALIGN_TOP
             : TABLE_VALIGN_CENTER);

      if (s->decimal_offset != DBL_MAX)
        a->cell_style.decimal_offset = optional_px (s->decimal_offset, 0);

      if (s->margin_left != DBL_MAX)
        a->cell_style.margin[TABLE_HORZ][0] = optional_px (s->margin_left, 8);
      if (s->margin_right != DBL_MAX)
        a->cell_style.margin[TABLE_HORZ][1] = optional_px (s->margin_right,
                                                           11);
      if (s->margin_top != DBL_MAX)
        a->cell_style.margin[TABLE_VERT][0] = optional_px (s->margin_top, 1);
      if (s->margin_bottom != DBL_MAX)
        a->cell_style.margin[TABLE_VERT][1] = optional_px (s->margin_bottom,
                                                           1);
    }

  for (int i = 0; i < PIVOT_N_BORDERS; i++)
    pivot_border_get_default_style (i, &out->borders[i]);

  const struct spvsx_border_properties *bp = in->border_properties;
  for (size_t i = 0; i < bp->n_border_style; i++)
    {
      const struct spvsx_border_style *bin = bp->border_style[i];
      const char *name = CHAR_CAST (const char *, bin->node_.raw->name);
      enum pivot_border border = pivot_border_from_name (name);
      if (border == PIVOT_N_BORDERS)
        {
          error = xasprintf ("unknown border \"%s\" parsing borderProperties",
                             name);
          goto error;
        }

      struct table_border_style *bout = &out->borders[border];
      bout->stroke
        = (bin->border_style_type == SPVSX_BORDER_STYLE_TYPE_NONE
           ? TABLE_STROKE_NONE
           : bin->border_style_type == SPVSX_BORDER_STYLE_TYPE_DASHED
           ? TABLE_STROKE_DASHED
           : bin->border_style_type == SPVSX_BORDER_STYLE_TYPE_THICK
           ? TABLE_STROKE_THICK
           : bin->border_style_type == SPVSX_BORDER_STYLE_TYPE_THIN
           ? TABLE_STROKE_THIN
           : bin->border_style_type == SPVSX_BORDER_STYLE_TYPE_DOUBLE
           ? TABLE_STROKE_DOUBLE
           : TABLE_STROKE_SOLID);
      bout->color = optional_color (bin->color,
                                    (struct cell_color) CELL_COLOR_BLACK);
    }

  const struct spvsx_printing_properties *pp = in->printing_properties;
  out->print_all_layers = pp->print_all_layers > 0;
  out->paginate_layers = pp->print_each_layer_on_separate_page > 0;
  out->shrink_to_width = pp->rescale_wide_table_to_fit_page > 0;
  out->shrink_to_length = pp->rescale_long_table_to_fit_page > 0;
  out->top_continuation = pp->continuation_text_at_top > 0;
  out->bottom_continuation = pp->continuation_text_at_bottom > 0;
  out->continuation = xstrdup (pp->continuation_text
                               ? pp->continuation_text : "(cont.)");
  out->n_orphan_lines = optional_int (pp->window_orphan_lines, 2);

  *outp = out;
  return NULL;

error:
  spv_table_look_destroy (out);
  *outp = NULL;
  return error;
}

char * WARN_UNUSED_RESULT
spv_table_look_read (const char *filename, struct spv_table_look **outp)
{
  *outp = NULL;

  size_t length;
  char *file = read_file (filename, 0, &length);
  if (!file)
    return xasprintf ("%s: failed to read file (%s)",
                      filename, strerror (errno));

  xmlDoc *doc = xmlReadMemory (file, length, NULL, NULL, XML_PARSE_NOBLANKS);
  free (file);
  if (!doc)
    return xasprintf ("%s: failed to parse XML", filename);

  struct spvxml_context ctx = SPVXML_CONTEXT_INIT (ctx);
  struct spvsx_table_properties *tp;
  spvsx_parse_table_properties (&ctx, xmlDocGetRootElement (doc), &tp);
  char *error = spvxml_context_finish (&ctx, &tp->node_);

  if (!error)
    error = spv_table_look_decode (tp, outp);

  spvsx_free_table_properties (tp);
  xmlFreeDoc (doc);

  return error;
}

static void
write_attr (xmlTextWriter *xml, const char *name, const char *value)
{
  xmlTextWriterWriteAttribute (xml,
                               CHAR_CAST (xmlChar *, name),
                               CHAR_CAST (xmlChar *, value));
}

static void PRINTF_FORMAT (3, 4)
write_attr_format (xmlTextWriter *xml, const char *name,
                   const char *format, ...)
{
  va_list args;
  va_start (args, format);
  char *value = xvasprintf (format, args);
  va_end (args);

  write_attr (xml, name, value);
  free (value);
}

static void
write_attr_color (xmlTextWriter *xml, const char *name,
                  const struct cell_color *color)
{
  write_attr_format (xml, name, "#%02"PRIx8"%02"PRIx8"%02"PRIx8,
                     color->r, color->g, color->b);
}

static void
write_attr_dimension (xmlTextWriter *xml, const char *name, int px)
{
  int pt = px / 96.0 * 72.0;
  write_attr_format (xml, name, "%dpt", pt);
}

static void
write_attr_bool (xmlTextWriter *xml, const char *name, bool b)
{
  write_attr (xml, name, b ? "true" : "false");
}

static void
start_elem (xmlTextWriter *xml, const char *name)
{
  xmlTextWriterStartElement (xml, CHAR_CAST (xmlChar *, name));
}

static void
end_elem (xmlTextWriter *xml)
{
  xmlTextWriterEndElement (xml);
}

char * WARN_UNUSED_RESULT
spv_table_look_write (const char *filename, const struct spv_table_look *look)
{
  FILE *file = fopen (filename, "w");
  if (!file)
    return xasprintf (_("%s: create failed (%s)"), filename, strerror (errno));

  xmlTextWriter *xml = xmlNewTextWriter (xmlOutputBufferCreateFile (
                                           file, NULL));
  if (!xml)
    {
      fclose (file);
      return xasprintf (_("%s: failed to start writing XML"), filename);
    }

  xmlTextWriterSetIndent (xml, 1);
  xmlTextWriterSetIndentString (xml, CHAR_CAST (xmlChar *, "    "));

  xmlTextWriterStartDocument (xml, NULL, "UTF-8", NULL);
  start_elem (xml, "tableProperties");
  if (look->name)
    write_attr (xml, "name", look->name);
  write_attr (xml, "xmlns", "http://www.ibm.com/software/analytics/spss/xml/table-looks");
  write_attr (xml, "xmlns:vizml", "http://www.ibm.com/software/analytics/spss/xml/visualization");
  write_attr (xml, "xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
  write_attr (xml, "xsi:schemaLocation", "http://www.ibm.com/software/analytics/spss/xml/table-looks http://www.ibm.com/software/analytics/spss/xml/table-looks/table-looks-1.4.xsd");

  start_elem (xml, "generalProperties");
  write_attr_bool (xml, "hideEmptyRows", look->omit_empty);
  const int (*wr)[2] = look->width_ranges;
  write_attr_format (xml, "maximumColumnWidth", "%d", wr[TABLE_HORZ][1]);
  write_attr_format (xml, "maximumRowWidth", "%d", wr[TABLE_VERT][1]);
  write_attr_format (xml, "minimumColumnWidth", "%d", wr[TABLE_HORZ][0]);
  write_attr_format (xml, "minimumRowWidth", "%d", wr[TABLE_VERT][0]);
  write_attr (xml, "rowDimensionLabels",
              look->row_labels_in_corner ? "inCorner" : "nested");
  end_elem (xml);

  start_elem (xml, "footnoteProperties");
  write_attr (xml, "markerPosition",
              look->footnote_marker_superscripts ? "superscript" : "subscript");
  write_attr (xml, "numberFormat",
              look->show_numeric_markers ? "numeric" : "alphabetic");
  end_elem (xml);

  start_elem (xml, "cellFormatProperties");
  for (enum pivot_area a = 0; a < PIVOT_N_AREAS; a++)
    {
      const struct area_style *area = &look->areas[a];
      const struct font_style *font = &area->font_style;
      const struct cell_style *cell = &area->cell_style;

      start_elem (xml, pivot_area_names[a]);
      if (a == PIVOT_AREA_DATA
          && (!cell_color_equal (&font->fg[0], &font->fg[1])
              || !cell_color_equal (&font->bg[0], &font->bg[1])))
        {
          write_attr_color (xml, "alternatingColor", &font->bg[1]);
          write_attr_color (xml, "alternatingTextColor", &font->fg[1]);
        }

      start_elem (xml, "vizml:style");
      write_attr_color (xml, "color", &font->fg[0]);
      write_attr_color (xml, "color2", &font->bg[0]);
      write_attr (xml, "font-family", font->typeface);
      write_attr_format (xml, "font-size", "%dpt", font->size);
      write_attr (xml, "font-weight", font->bold ? "bold" : "regular");
      write_attr (xml, "font-underline",
                  font->underline ? "underline" : "none");
      write_attr (xml, "labelLocationVertical",
                  cell->valign == TABLE_VALIGN_BOTTOM ? "negative"
                  : cell->valign == TABLE_VALIGN_TOP ? "positive"
                  : "center");
      write_attr_dimension (xml, "margin-bottom", cell->margin[TABLE_VERT][1]);
      write_attr_dimension (xml, "margin-left", cell->margin[TABLE_HORZ][0]);
      write_attr_dimension (xml, "margin-right", cell->margin[TABLE_HORZ][1]);
      write_attr_dimension (xml, "margin-top", cell->margin[TABLE_VERT][0]);
      write_attr (xml, "textAlignment",
                  cell->halign == TABLE_HALIGN_LEFT ? "left"
                  : cell->halign == TABLE_HALIGN_RIGHT ? "right"
                  : cell->halign == TABLE_HALIGN_CENTER ? "center"
                  : cell->halign == TABLE_HALIGN_DECIMAL ? "decimal"
                  : "mixed");
      if (cell->halign == TABLE_HALIGN_DECIMAL)
        write_attr_dimension (xml, "decimal-offset", cell->decimal_offset);
      end_elem (xml);

      end_elem (xml);
    }
  end_elem (xml);

  start_elem (xml, "borderProperties");
  for (enum pivot_border b = 0; b < PIVOT_N_BORDERS; b++)
    {
      const struct table_border_style *border = &look->borders[b];

      start_elem (xml, pivot_border_names[b]);

      static const char *table_stroke_names[TABLE_N_STROKES] =
        {
          [TABLE_STROKE_NONE] = "none",
          [TABLE_STROKE_SOLID] = "solid",
          [TABLE_STROKE_DASHED] = "dashed",
          [TABLE_STROKE_THICK] = "thick",
          [TABLE_STROKE_THIN] = "thin",
          [TABLE_STROKE_DOUBLE] = "double",
        };
      write_attr (xml, "borderStyleType", table_stroke_names[border->stroke]);
      write_attr_color (xml, "color", &border->color);
      end_elem (xml);
    }
  end_elem (xml);

  start_elem (xml, "printingProperties");
  write_attr_bool (xml, "printAllLayers", look->print_all_layers);
  write_attr_bool (xml, "rescaleLongTableToFitPage", look->shrink_to_length);
  write_attr_bool (xml, "rescaleWideTableToFitPage", look->shrink_to_width);
  write_attr_format (xml, "windowOrphanLines", "%zu", look->n_orphan_lines);
  if (look->continuation && look->continuation[0]
      && (look->top_continuation || look->bottom_continuation))
    {
      write_attr_format (xml, "continuationText", look->continuation);
      write_attr_bool (xml, "continuationTextAtTop", look->top_continuation);
      write_attr_bool (xml, "continuationTextAtBottom",
                       look->bottom_continuation);
    }
  end_elem (xml);

  xmlTextWriterEndDocument (xml);

  xmlFreeTextWriter (xml);

  fflush (file);
  bool ok = !ferror (file);
  if (fclose (file) == EOF)
    ok = false;

  if (!ok)
    return xasprintf (_("%s: error writing file (%s)"),
                      filename, strerror (errno));

  return NULL;
}

void
spv_table_look_destroy (struct spv_table_look *look)
{
  if (look)
    {
      free (look->name);
      for (size_t i = 0; i < PIVOT_N_AREAS; i++)
        area_style_uninit (&look->areas[i]);
      free (look->continuation);
      free (look);
    }
}

void
spv_table_look_install (const struct spv_table_look *look,
                        struct pivot_table *table)
{
  free (table->table_look);
  if (look->name)
    table->table_look = xstrdup (look->name);

  table->omit_empty = look->omit_empty;

  for (enum table_axis axis = 0; axis < TABLE_N_AXES; axis++)
    for (int i = 0; i < 2; i++)
      if (look->width_ranges[axis][i] > 0)
        table->sizing[axis].range[i] = look->width_ranges[axis][i];
  table->row_labels_in_corner = look->row_labels_in_corner;

  table->footnote_marker_superscripts = look->footnote_marker_superscripts;
  table->show_numeric_markers = look->show_numeric_markers;

  for (size_t i = 0; i < PIVOT_N_AREAS; i++)
    {
      area_style_uninit (&table->areas[i]);
      area_style_copy (NULL, &table->areas[i], &look->areas[i]);
    }
  for (size_t i = 0; i < PIVOT_N_BORDERS; i++)
    table->borders[i] = look->borders[i];

  table->print_all_layers = look->print_all_layers;
  table->paginate_layers = look->paginate_layers;
  table->shrink_to_fit[TABLE_HORZ] = look->shrink_to_width;
  table->shrink_to_fit[TABLE_VERT] = look->shrink_to_length;
  table->top_continuation = look->top_continuation;
  table->bottom_continuation = look->bottom_continuation;
  table->continuation = xstrdup (look->continuation);
  table->n_orphan_lines = look->n_orphan_lines;
}

struct spv_table_look *
spv_table_look_get (const struct pivot_table *table)
{
  struct spv_table_look *look = xzalloc (sizeof *look);

  look->name = table->table_look ? xstrdup (table->table_look) : NULL;

  look->omit_empty = table->omit_empty;

  for (enum table_axis axis = 0; axis < TABLE_N_AXES; axis++)
    for (int i = 0; i < 2; i++)
      look->width_ranges[axis][i] = table->sizing[axis].range[i];
  look->row_labels_in_corner = table->row_labels_in_corner;

  look->footnote_marker_superscripts = table->footnote_marker_superscripts;
  look->show_numeric_markers = table->show_numeric_markers;

  for (size_t i = 0; i < PIVOT_N_AREAS; i++)
    area_style_copy (NULL, &look->areas[i], &table->areas[i]);
  for (size_t i = 0; i < PIVOT_N_BORDERS; i++)
    look->borders[i] = table->borders[i];

  look->print_all_layers = table->print_all_layers;
  look->paginate_layers = table->paginate_layers;
  look->shrink_to_width = table->shrink_to_fit[TABLE_HORZ];
  look->shrink_to_length = table->shrink_to_fit[TABLE_VERT];
  look->top_continuation = table->top_continuation;
  look->bottom_continuation = table->bottom_continuation;
  look->continuation = xstrdup (table->continuation);
  look->n_orphan_lines = table->n_orphan_lines;

  return look;
}
