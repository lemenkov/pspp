/* PSPP - a program for statistical analysis.
   Copyright (C) 2019 Free Software Foundation, Inc.

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

#include "output/spv/spv-writer.h"

#include <inttypes.h>
#include <libxml/xmlwriter.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/float-format.h"
#include "libpspp/integer-format.h"
#include "libpspp/temp-file.h"
#include "libpspp/version.h"
#include "libpspp/zip-writer.h"
#include "output/page-setup-item.h"
#include "output/pivot-table.h"
#include "output/text-item.h"

#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

struct spv_writer
  {
    struct zip_writer *zw;

    FILE *heading;
    int heading_depth;
    xmlTextWriter *xml;

    int n_tables;

    int n_headings;
    struct page_setup *page_setup;
    bool need_page_break;
  };

char * WARN_UNUSED_RESULT
spv_writer_open (const char *filename, struct spv_writer **writerp)
{
  *writerp = NULL;

  struct zip_writer *zw = zip_writer_create (filename);
  if (!zw)
    return xasprintf (_("%s: create failed"), filename);

  struct spv_writer *w = xmalloc (sizeof *w);
  *w = (struct spv_writer) { .zw = zw };
  *writerp = w;
  return NULL;
}

char * WARN_UNUSED_RESULT
spv_writer_close (struct spv_writer *w)
{
  if (!w)
    return NULL;

  zip_writer_add_string (w->zw, "META-INF/MANIFEST.MF", "allowPivoting=true");

  while (w->heading_depth)
    spv_writer_close_heading (w);

  char *error = NULL;
  if (!zip_writer_close (w->zw))
    error = xstrdup (_("I/O error writing SPV file"));

  page_setup_destroy (w->page_setup);
  free (w);
  return error;
}

void
spv_writer_set_page_setup (struct spv_writer *w,
                           const struct page_setup *page_setup)
{
  page_setup_destroy (w->page_setup);
  w->page_setup = page_setup_clone (page_setup);
}

static void
write_attr (struct spv_writer *w, const char *name, const char *value)
{
  xmlTextWriterWriteAttribute (w->xml,
                               CHAR_CAST (xmlChar *, name),
                               CHAR_CAST (xmlChar *, value));
}

static void PRINTF_FORMAT (3, 4)
write_attr_format (struct spv_writer *w, const char *name,
                   const char *format, ...)
{
  va_list args;
  va_start (args, format);
  char *value = xvasprintf (format, args);
  va_end (args);

  write_attr (w, name, value);
  free (value);
}

static void
start_elem (struct spv_writer *w, const char *name)
{
  xmlTextWriterStartElement (w->xml, CHAR_CAST (xmlChar *, name));
}

static void
end_elem (struct spv_writer *w)
{
  xmlTextWriterEndElement (w->xml);
}

static void
write_text (struct spv_writer *w, const char *text)
{
  xmlTextWriterWriteString (w->xml, CHAR_CAST (xmlChar *, text));
}

static void
write_page_heading (struct spv_writer *w, const struct page_heading *h,
                    const char *name)
{
  start_elem (w, name);
  if (h->n)
    {
      start_elem (w, "pageParagraph");
      for (size_t i = 0; i < h->n; i++)
        {
          start_elem (w, "text");
          write_attr (w, "type", "title");
          write_text (w, h->paragraphs[i].markup); /* XXX */
          end_elem (w);
        }
      end_elem (w);
    }
  end_elem (w);
}

static void
write_page_setup (struct spv_writer *w, const struct page_setup *ps)
{
  start_elem (w, "pageSetup");
  write_attr_format (w, "initial-page-number", "%d", ps->initial_page_number);
  write_attr (w, "chart-size",
              (ps->chart_size == PAGE_CHART_AS_IS ? "as-is"
               : ps->chart_size == PAGE_CHART_FULL_HEIGHT ? "full-height"
               : ps->chart_size == PAGE_CHART_HALF_HEIGHT ? "half-height"
               : "quarter-height"));
  write_attr_format (w, "margin-left", "%.2fin", ps->margins[TABLE_HORZ][0]);
  write_attr_format (w, "margin-right", "%.2fin", ps->margins[TABLE_HORZ][1]);
  write_attr_format (w, "margin-top", "%.2fin", ps->margins[TABLE_VERT][0]);
  write_attr_format (w, "margin-bottom", "%.2fin", ps->margins[TABLE_VERT][1]);
  write_attr_format (w, "paper-height", "%.2fin", ps->paper[TABLE_VERT]);
  write_attr_format (w, "paper-width", "%.2fin", ps->paper[TABLE_HORZ]);
  write_attr (w, "reference-orientation",
              ps->orientation == PAGE_PORTRAIT ? "portrait" : "landscape");
  write_attr_format (w, "space-after", "%.1fpt", ps->object_spacing * 72.0);
  write_page_heading (w, &ps->headings[0], "pageHeader");
  write_page_heading (w, &ps->headings[1], "pageFooter");
  end_elem (w);
}

static bool
spv_writer_open_file (struct spv_writer *w)
{
  w->heading = create_temp_file ();
  if (!w->heading)
    return false;

  w->xml = xmlNewTextWriter (xmlOutputBufferCreateFile (w->heading, NULL));
  xmlTextWriterStartDocument (w->xml, NULL, "UTF-8", NULL);
  start_elem (w, "heading");

  time_t t = time (NULL);
  struct tm *tm = gmtime (&t);
  char *tm_s = asctime (tm);
  write_attr (w, "creation-date-time", tm_s);

  write_attr (w, "creator", version);

  write_attr (w, "creator-version", "21");

  write_attr (w, "xmlns", "http://xml.spss.com/spss/viewer/viewer-tree");
  write_attr (w, "xmlns:vps", "http://xml.spss.com/spss/viewer/viewer-pagesetup");
  write_attr (w, "xmlns:vtx", "http://xml.spss.com/spss/viewer/viewer-text");
  write_attr (w, "xmlns:vtb", "http://xml.spss.com/spss/viewer/viewer-table");

  start_elem (w, "label");
  write_text (w, _("Output"));
  end_elem (w);

  if (w->page_setup)
    {
      write_page_setup (w, w->page_setup);

      page_setup_destroy (w->page_setup);
      w->page_setup = NULL;
    }

  return true;
}

void
spv_writer_open_heading (struct spv_writer *w, const char *command_id,
                         const char *label)
{
  if (!w->heading)
    {
      if (!spv_writer_open_file (w))
        return;
    }

  w->heading_depth++;
  start_elem (w, "heading");
  write_attr (w, "commandName", command_id);
  /* XXX locale */
  /* XXX olang */

  start_elem (w, "label");
  write_text (w, label);
  end_elem (w);
}

static void
spv_writer_close_file (struct spv_writer *w, const char *infix)
{
  if (!w->heading)
    return;

  end_elem (w);
  xmlTextWriterEndDocument (w->xml);
  xmlFreeTextWriter (w->xml);

  char *member_name = xasprintf ("outputViewer%010d%s.xml",
                                 w->n_headings++, infix);
  zip_writer_add (w->zw, w->heading, member_name);
  free (member_name);

  w->heading = NULL;
}

void
spv_writer_close_heading (struct spv_writer *w)
{
  const char *infix = "";
  if (w->heading_depth)
    {
      infix = "_heading";
      end_elem (w);
      w->heading_depth--;
    }

  if (!w->heading_depth)
    spv_writer_close_file (w, infix);
}

static void
start_container (struct spv_writer *w)
{
  start_elem (w, "container");
  write_attr (w, "visibility", "visible");
  if (w->need_page_break)
    {
      write_attr (w, "page-break-before", "always");
      w->need_page_break = false;
    }
}

void
spv_writer_put_text (struct spv_writer *w, const struct text_item *text,
                     const char *command_id)
{
  bool initial_depth = w->heading_depth;
  if (!initial_depth)
    spv_writer_open_file (w);

  start_container (w);

  start_elem (w, "label");
  write_text (w, (text->type == TEXT_ITEM_TITLE ? "Title"
                  : text->type == TEXT_ITEM_PAGE_TITLE ? "Page Title"
                  : "Log"));
  end_elem (w);

  start_elem (w, "vtx:text");
  write_attr (w, "type", (text->type == TEXT_ITEM_TITLE ? "title"
                          : text->type == TEXT_ITEM_PAGE_TITLE ? "page-title"
                          : "log"));
  if (command_id)
    write_attr (w, "commandName", command_id);

  start_elem (w, "html");
  write_text (w, text->text);   /* XXX */
  end_elem (w); /* html */
  end_elem (w); /* vtx:text */
  end_elem (w); /* container */

  if (!initial_depth)
    spv_writer_close_file (w, "");
}

static cairo_status_t
write_to_zip (void *zw_, const unsigned char *data, unsigned int length)
{
  struct zip_writer *zw = zw_;

  zip_writer_add_write (zw, data, length);
  return CAIRO_STATUS_SUCCESS;
}

void
spv_writer_put_image (struct spv_writer *w, cairo_surface_t *image)
{
  bool initial_depth = w->heading_depth;
  if (!initial_depth)
    spv_writer_open_file (w);

  char *uri = xasprintf ("%010d_Imagegeneric.png", ++w->n_tables);

  start_container (w);

  start_elem (w, "label");
  write_text (w, "Image");
  end_elem (w);

  start_elem (w, "object");
  write_attr (w, "type", "unknown");
  write_attr (w, "uri", uri);
  end_elem (w); /* object */
  end_elem (w); /* container */

  if (!initial_depth)
    spv_writer_close_file (w, "");

  zip_writer_add_start (w->zw, uri);
  cairo_surface_write_to_png_stream (image, write_to_zip, w->zw);
  zip_writer_add_finish (w->zw);

  free (uri);
}

void
spv_writer_eject_page (struct spv_writer *w)
{
  w->need_page_break = true;
}

#define H TABLE_HORZ
#define V TABLE_VERT

struct buf
  {
    uint8_t *data;
    size_t len;
    size_t allocated;
  };

static uint8_t *
put_uninit (struct buf *b, size_t n)
{
  while (b->allocated - b->len < n)
    b->data = x2nrealloc (b->data, &b->allocated, sizeof b->data);
  uint8_t *p = &b->data[b->len];
  b->len += n;
  return p;
}

static void
put_byte (struct buf *b, uint8_t byte)
{
  *put_uninit (b, 1) = byte;
}

static void
put_bool (struct buf *b, bool boolean)
{
  put_byte (b, boolean);
}

static void
put_bytes (struct buf *b, const char *bytes, size_t n)
{
  memcpy (put_uninit (b, n), bytes, n);
}

static void
put_u16 (struct buf *b, uint16_t x)
{
  put_uint16 (native_to_le16 (x), put_uninit (b, sizeof x));
}

static void
put_u32 (struct buf *b, uint32_t x)
{
  put_uint32 (native_to_le32 (x), put_uninit (b, sizeof x));
}

static void
put_u64 (struct buf *b, uint64_t x)
{
  put_uint64 (native_to_le64 (x), put_uninit (b, sizeof x));
}

static void
put_be32 (struct buf *b, uint32_t x)
{
  put_uint32 (native_to_be32 (x), put_uninit (b, sizeof x));
}

static void
put_double (struct buf *b, double x)
{
  float_convert (FLOAT_NATIVE_DOUBLE, &x,
                 FLOAT_IEEE_DOUBLE_LE, put_uninit (b, 8));
}

static void
put_float (struct buf *b, float x)
{
  float_convert (FLOAT_NATIVE_FLOAT, &x,
                 FLOAT_IEEE_SINGLE_LE, put_uninit (b, 4));
}

static void
put_string (struct buf *b, const char *s_)
{
  const char *s = s_ ? s_ : "";
  size_t len = strlen (s);
  put_u32 (b, len);
  memcpy (put_uninit (b, len), s, len);
}

static void
put_bestring (struct buf *b, const char *s_)
{
  const char *s = s_ ? s_ : "";
  size_t len = strlen (s);
  put_be32 (b, len);
  memcpy (put_uninit (b, len), s, len);
}

static size_t
start_count (struct buf *b)
{
  put_u32 (b, 0);
  return b->len;
}

static void
end_count_u32 (struct buf *b, size_t start)
{
  put_uint32 (native_to_le32 (b->len - start), &b->data[start - 4]);
}

static void
end_count_be32 (struct buf *b, size_t start)
{
  put_uint32 (native_to_be32 (b->len - start), &b->data[start - 4]);
}

static void
put_color (struct buf *buf, const struct cell_color *color)
{
  char *s = xasprintf ("#%02"PRIx8"%02"PRIx8"%02"PRIx8,
                       color->r, color->g, color->b);
  put_string (buf, s);
  free (s);
}

static void
put_font_style (struct buf *buf, const struct font_style *font_style)
{
  put_bool (buf, font_style->bold);
  put_bool (buf, font_style->italic);
  put_bool (buf, font_style->underline);
  put_bool (buf, 1);
  put_color (buf, &font_style->fg[0]);
  put_color (buf, &font_style->bg[0]);
  put_string (buf, font_style->typeface ? font_style->typeface : "SansSerif");
  put_byte (buf, ceil (font_style->size * 1.33));
}

static void
put_halign (struct buf *buf, enum table_halign halign,
            uint32_t mixed, uint32_t decimal)
{
  put_u32 (buf, (halign == TABLE_HALIGN_RIGHT ? 4
                 : halign == TABLE_HALIGN_LEFT ? 2
                 : halign == TABLE_HALIGN_CENTER ? 0
                 : halign == TABLE_HALIGN_MIXED ? mixed
                 : decimal));
}

static void
put_valign (struct buf *buf, enum table_valign valign)
{
  put_u32 (buf, (valign == TABLE_VALIGN_TOP ? 1
                 : valign == TABLE_VALIGN_CENTER ? 0
                 : 3));
}

static void
put_cell_style (struct buf *buf, const struct cell_style *cell_style)
{
  put_halign (buf, cell_style->halign, 0xffffffad, 6);
  put_valign (buf, cell_style->valign);
  put_double (buf, cell_style->decimal_offset);
  put_u16 (buf, cell_style->margin[H][0]);
  put_u16 (buf, cell_style->margin[H][1]);
  put_u16 (buf, cell_style->margin[V][0]);
  put_u16 (buf, cell_style->margin[V][1]);
}

static void UNUSED
put_style_pair (struct buf *buf, const struct font_style *font_style,
                const struct cell_style *cell_style)
{
  if (font_style)
    {
      put_byte (buf, 0x31);
      put_font_style (buf, font_style);
    }
  else
    put_byte (buf, 0x58);

  if (cell_style)
    {
      put_byte (buf, 0x31);
      put_cell_style (buf, cell_style);
    }
  else
    put_byte (buf, 0x58);
}

static void
put_value_mod (struct buf *buf, const struct pivot_value *value,
               const char *template)
{
  if (value->n_footnotes || value->n_subscripts
      || template || value->font_style || value->cell_style)
    {
      put_byte (buf, 0x31);

      /* Footnotes. */
      put_u32 (buf, value->n_footnotes);
      for (size_t i = 0; i < value->n_footnotes; i++)
        put_u16 (buf, value->footnote_indexes[i]);

      /* Subscripts. */
      put_u32 (buf, value->n_subscripts);
      for (size_t i = 0; i < value->n_subscripts; i++)
        put_string (buf, value->subscripts[i]);

      /* Template and style. */
      uint32_t v3_start = start_count (buf);
      uint32_t template_string_start = start_count (buf);
      if (template)
        {
          uint32_t inner_start = start_count (buf);
          end_count_u32 (buf, inner_start);

          put_byte (buf, 0x31);
          put_string (buf, template);
        }
      end_count_u32 (buf, template_string_start);
      put_style_pair (buf, value->font_style, value->cell_style);
      end_count_u32 (buf, v3_start);
    }
  else
    put_byte (buf, 0x58);
}

static void
put_format (struct buf *buf, const struct fmt_spec *f, bool honor_small)
{
  int type = f->type == FMT_F && honor_small ? 40 : fmt_to_io (f->type);
  put_u32 (buf, (type << 16) | (f->w << 8) | f->d);
}

static int
show_values_to_spvlb (enum settings_value_show show)
{
  return (show == SETTINGS_VALUE_SHOW_DEFAULT ? 0
          : show == SETTINGS_VALUE_SHOW_VALUE ? 1
          : show == SETTINGS_VALUE_SHOW_LABEL ? 2
          : 3);
}

static void
put_show_values (struct buf *buf, enum settings_value_show show)
{
  put_byte (buf, show_values_to_spvlb (show));
}

static void
put_value (struct buf *buf, const struct pivot_value *value)
{
  switch (value->type)
    {
    case PIVOT_VALUE_NUMERIC:
      if (value->numeric.var_name || value->numeric.value_label)
        {
          put_byte (buf, 2);
          put_value_mod (buf, value, NULL);
          put_format (buf, &value->numeric.format, value->numeric.honor_small);
          put_double (buf, value->numeric.x);
          put_string (buf, value->numeric.var_name);
          put_string (buf, value->numeric.value_label);
          put_show_values (buf, value->numeric.show);
        }
      else
        {
          put_byte (buf, 1);
          put_value_mod (buf, value, NULL);
          put_format (buf, &value->numeric.format, value->numeric.honor_small);
          put_double (buf, value->numeric.x);
        }
      break;

    case PIVOT_VALUE_STRING:
      put_byte (buf, 4);
      put_value_mod (buf, value, NULL);
      size_t len = strlen (value->string.s);
      if (value->string.hex)
        put_format (buf, &(struct fmt_spec) { FMT_AHEX, len * 2, 0 }, false);
      else
        put_format (buf, &(struct fmt_spec) { FMT_A, len, 0 }, false);
      put_string (buf, value->string.value_label);
      put_string (buf, value->string.var_name);
      put_show_values (buf, value->string.show);
      put_string (buf, value->string.s);
      break;

    case PIVOT_VALUE_VARIABLE:
      put_byte (buf, 5);
      put_value_mod (buf, value, NULL);
      put_string (buf, value->variable.var_name);
      put_string (buf, value->variable.var_label);
      put_show_values (buf, value->variable.show);
      break;

    case PIVOT_VALUE_TEXT:
      put_byte (buf, 3);
      put_string (buf, value->text.local);
      put_value_mod (buf, value, NULL);
      put_string (buf, value->text.id);
      put_string (buf, value->text.c);
      put_byte (buf, 1);        /* XXX user-provided */
      break;

    case PIVOT_VALUE_TEMPLATE:
      put_byte (buf, 0);
      put_value_mod (buf, value, value->template.id);
      put_string (buf, value->template.local);
      put_u32 (buf, value->template.n_args);
      for (size_t i = 0; i < value->template.n_args; i++)
        {
          const struct pivot_argument *arg = &value->template.args[i];
          assert (arg->n >= 1);
          if (arg->n > 1)
            {
              put_u32 (buf, arg->n);
              put_u32 (buf, 0);
              for (size_t j = 0; j < arg->n; j++)
                {
                  if (j > 0)
                    put_bytes (buf, "\0\0\0\0", 4);
                  put_value (buf, arg->values[j]);
                }
            }
          else
            {
              put_u32 (buf, 0);
              put_value (buf, arg->values[0]);
            }
        }
      break;

    default:
      NOT_REACHED ();
    }
}

static void
put_optional_value (struct buf *buf, const struct pivot_value *value)
{
  if (value)
    {
      put_byte (buf, 0x31);
      put_value (buf, value);
    }
  else
    put_byte (buf, 0x58);
}

static void
put_category (struct buf *buf, const struct pivot_category *c)
{
  put_value (buf, c->name);
  if (pivot_category_is_leaf (c))
    {
      put_bytes (buf, "\0\0\0", 3);
      put_u32 (buf, 2);
      put_u32 (buf, c->data_index);
      put_u32 (buf, 0);
    }
  else
    {
      put_bytes (buf, "\0\0\1", 3);
      put_u32 (buf, 0);         /* x23 */
      put_u32 (buf, -1);
      put_u32 (buf, c->n_subs);
      for (size_t i = 0; i < c->n_subs; i++)
        put_category (buf, c->subs[i]);
    }
}

static void
put_y0 (struct buf *buf, const struct pivot_table *table)
{
  put_u32 (buf, table->settings.epoch);
  put_byte (buf, table->settings.decimal);
  put_byte (buf, ',');
}

static void
put_custom_currency (struct buf *buf, const struct pivot_table *table)
{
  put_u32 (buf, 5);
  for (int i = 0; i < 5; i++)
    {
      enum fmt_type types[5] = { FMT_CCA, FMT_CCB, FMT_CCC, FMT_CCD, FMT_CCE };
      char *cc = fmt_number_style_to_string (fmt_settings_get_style (
                                               &table->settings, types[i]));
      put_string (buf, cc);
      free (cc);
    }
}

static void
put_x1 (struct buf *buf, const struct pivot_table *table)
{
  put_byte (buf, 0);            /* x14 */
  put_byte (buf, table->show_title ? 1 : 10);
  put_byte (buf, 0);            /* x16 */
  put_byte (buf, 0);            /* lang */
  put_show_values (buf, table->show_variables);
  put_show_values (buf, table->show_values);
  put_u32 (buf, -1);            /* x18 */
  put_u32 (buf, -1);            /* x19 */
  for (int i = 0; i < 17; i++)
    put_byte (buf, 0);
  put_bool (buf, false);        /* x20 */
  put_byte (buf, table->show_caption);
}

static void
put_x2 (struct buf *buf)
{
  put_u32 (buf, 0);             /* n-row-heights */
  put_u32 (buf, 0);             /* n-style-map */
  put_u32 (buf, 0);             /* n-styles */
  put_u32 (buf, 0);
}

static void
put_y1 (struct buf *buf, const struct pivot_table *table)
{
  put_string (buf, table->command_c);
  put_string (buf, table->command_local);
  put_string (buf, table->language);
  put_string (buf, "UTF-8");    /* XXX */
  put_string (buf, table->locale);
  put_bytes (buf, "\0\0\1\1", 4);
  put_y0 (buf, table);
}

static void
put_y2 (struct buf *buf, const struct pivot_table *table)
{
  put_custom_currency (buf, table);
  put_byte (buf, '.');
  put_bool (buf, 0);
}

static void
put_x3 (struct buf *buf, const struct pivot_table *table)
{
  put_byte (buf, 1);
  put_byte (buf, 0);
  put_byte (buf, 4);            /* x21 */
  put_byte (buf, 0);
  put_byte (buf, 0);
  put_byte (buf, 0);
  put_y1 (buf, table);
  put_double (buf, table->small);
  put_byte (buf, 1);
  put_string (buf, table->dataset);
  put_string (buf, table->datafile);
  put_u32 (buf, 0);
  put_u32 (buf, table->date);
  put_u32 (buf, 0);
  put_y2 (buf, table);
}

static uint32_t
encode_current_layer (const struct pivot_table *table)
{
  uint32_t current_layer = 0;

  const struct pivot_axis *axis = &table->axes[PIVOT_AXIS_LAYER];
  for (size_t i = axis->n_dimensions - 1; i < axis->n_dimensions; i--)
    {
      const struct pivot_dimension *d = axis->dimensions[i];
      current_layer = current_layer * d->n_leaves + table->current_layer[i];
    }

  return current_layer;
}

static void
put_light_table (struct buf *buf, uint64_t table_id,
                 const struct pivot_table *table)
{
  /* Header. */
  put_bytes (buf, "\1\0", 2);
  put_u32 (buf, 3);
  put_bool (buf, true);
  put_bool (buf, false);
  put_bool (buf, table->rotate_inner_column_labels);
  put_bool (buf, table->rotate_outer_row_labels);
  put_bool (buf, true);
  put_u32 (buf, 0x15);
  put_u32 (buf, table->look->width_ranges[H][0]);
  put_u32 (buf, table->look->width_ranges[H][1]);
  put_u32 (buf, table->look->width_ranges[V][0]);
  put_u32 (buf, table->look->width_ranges[V][1]);
  put_u64 (buf, table_id);

  /* Titles. */
  put_value (buf, table->title);
  put_value (buf, table->subtype);
  put_optional_value (buf, table->title);
  put_optional_value (buf, table->corner_text);
  put_optional_value (buf, table->caption);

  /* Footnotes. */
  put_u32 (buf, table->n_footnotes);
  for (size_t i = 0; i < table->n_footnotes; i++)
    {
      const struct pivot_footnote *f = table->footnotes[i];
      put_value (buf, f->content);
      put_optional_value (buf, f->marker);
      put_u32 (buf, f->show ? 1 : -1);
    }

  /* Areas. */
  for (size_t i = 0; i < PIVOT_N_AREAS; i++)
    {
      const struct table_area_style *a = &table->look->areas[i];
      put_byte (buf, i + 1);
      put_byte (buf, 0x31);
      put_string (buf, (a->font_style.typeface
                        ? a->font_style.typeface
                        : "SansSerif"));
      put_float (buf, ceil (a->font_style.size * 1.33));
      put_u32 (buf, ((a->font_style.bold ? 1 : 0)
                     | (a->font_style.italic ? 2 : 0)));
      put_bool (buf, a->font_style.underline);
      put_halign (buf, a->cell_style.halign, 64173, 61453);
      put_valign (buf, a->cell_style.valign);

      put_color (buf, &a->font_style.fg[0]);
      put_color (buf, &a->font_style.bg[0]);

      bool alt
        = (!cell_color_equal (&a->font_style.fg[0], &a->font_style.fg[1])
           || !cell_color_equal (&a->font_style.bg[0], &a->font_style.bg[1]));
      put_bool (buf, alt);
      if (alt)
        {
          put_color (buf, &a->font_style.fg[1]);
          put_color (buf, &a->font_style.bg[1]);
        }
      else
        {
          put_string (buf, "");
          put_string (buf, "");
        }

      put_u32 (buf, a->cell_style.margin[H][0]);
      put_u32 (buf, a->cell_style.margin[H][1]);
      put_u32 (buf, a->cell_style.margin[V][0]);
      put_u32 (buf, a->cell_style.margin[V][1]);
    }

  /* Borders. */
  uint32_t borders_start = start_count (buf);
  put_be32 (buf, 1);
  put_be32 (buf, PIVOT_N_BORDERS);
  for (size_t i = 0; i < PIVOT_N_BORDERS; i++)
    {
      const struct table_border_style *b = &table->look->borders[i];
      put_be32 (buf, i);
      put_be32 (buf, (b->stroke == TABLE_STROKE_NONE ? 0
                      : b->stroke == TABLE_STROKE_SOLID ? 1
                      : b->stroke == TABLE_STROKE_DASHED ? 2
                      : b->stroke == TABLE_STROKE_THICK ? 3
                      : b->stroke == TABLE_STROKE_THIN ? 4
                      : 5));
      put_be32 (buf, ((b->color.alpha << 24)
                      | (b->color.r << 16)
                      | (b->color.g << 8)
                      | b->color.b));
    }
  put_bool (buf, table->show_grid_lines);
  put_bytes (buf, "\0\0\0", 3);
  end_count_u32 (buf, borders_start);

  /* Print Settings. */
  uint32_t ps_start = start_count (buf);
  put_be32 (buf, 1);
  put_bool (buf, table->look->print_all_layers);
  put_bool (buf, table->look->paginate_layers);
  put_bool (buf, table->look->shrink_to_fit[H]);
  put_bool (buf, table->look->shrink_to_fit[V]);
  put_bool (buf, table->look->top_continuation);
  put_bool (buf, table->look->bottom_continuation);
  put_be32 (buf, table->look->n_orphan_lines);
  put_bestring (buf, table->look->continuation);
  end_count_u32 (buf, ps_start);

  /* Table Settings. */
  uint32_t ts_start = start_count (buf);
  put_be32 (buf, 1);
  put_be32 (buf, 4);
  put_be32 (buf, encode_current_layer (table));
  put_bool (buf, table->look->omit_empty);
  put_bool (buf, table->look->row_labels_in_corner);
  put_bool (buf, !table->look->show_numeric_markers);
  put_bool (buf, table->look->footnote_marker_superscripts);
  put_byte (buf, 0);
  uint32_t keep_start = start_count (buf);
  put_be32 (buf, 0);            /* n-row-breaks */
  put_be32 (buf, 0);            /* n-column-breaks */
  put_be32 (buf, 0);            /* n-row-keeps */
  put_be32 (buf, 0);            /* n-column-keeps */
  put_be32 (buf, 0);            /* n-row-point-keeps */
  put_be32 (buf, 0);            /* n-column-point-keeps */
  end_count_be32 (buf, keep_start);
  put_bestring (buf, table->notes);
  put_bestring (buf, table->look->name);
  for (size_t i = 0; i < 82; i++)
    put_byte (buf, 0);
  end_count_u32 (buf, ts_start);

  /* Formats. */
  put_u32 (buf, 0);             /* n-widths */
  put_string (buf, "en_US.ISO_8859-1:1987"); /* XXX */
  put_u32 (buf, 0);                /* XXX current-layer */
  put_bool (buf, 0);
  put_bool (buf, 0);
  put_bool (buf, 1);
  put_y0 (buf, table);
  put_custom_currency (buf, table);
  uint32_t formats_start = start_count (buf);
  uint32_t x1_start = start_count (buf);
  put_x1 (buf, table);
  uint32_t x2_start = start_count (buf);
  put_x2 (buf);
  end_count_u32 (buf, x2_start);
  end_count_u32 (buf, x1_start);
  uint32_t x3_start = start_count (buf);
  put_x3 (buf, table);
  end_count_u32 (buf, x3_start);
  end_count_u32 (buf, formats_start);

  /* Dimensions. */
  put_u32 (buf, table->n_dimensions);
  int *x2 = xnmalloc (table->n_dimensions, sizeof *x2);
  for (size_t i = 0; i < table->axes[PIVOT_AXIS_LAYER].n_dimensions; i++)
    x2[i] = 2;
  for (size_t i = 0; i < table->axes[PIVOT_AXIS_ROW].n_dimensions; i++)
    x2[i + table->axes[PIVOT_AXIS_LAYER].n_dimensions] = 0;
  for (size_t i = 0; i < table->axes[PIVOT_AXIS_COLUMN].n_dimensions; i++)
    x2[i
       + table->axes[PIVOT_AXIS_LAYER].n_dimensions
       + table->axes[PIVOT_AXIS_ROW].n_dimensions] = 1;
  for (size_t i = 0; i < table->n_dimensions; i++)
    {
      const struct pivot_dimension *d = table->dimensions[i];
      put_value (buf, d->root->name);
      put_byte (buf, 0);        /* x1 */
      put_byte (buf, x2[i]);
      put_u32 (buf, 2);         /* x3 */
      put_bool (buf, !d->root->show_label);
      put_bool (buf, d->hide_all_labels);
      put_bool (buf, 1);
      put_u32 (buf, i);

      put_u32 (buf, d->root->n_subs);
      for (size_t j = 0; j < d->root->n_subs; j++)
        put_category (buf, d->root->subs[j]);
    }
  free (x2);

  /* Axes. */
  put_u32 (buf, table->axes[PIVOT_AXIS_LAYER].n_dimensions);
  put_u32 (buf, table->axes[PIVOT_AXIS_ROW].n_dimensions);
  put_u32 (buf, table->axes[PIVOT_AXIS_COLUMN].n_dimensions);
  for (size_t i = 0; i < table->axes[PIVOT_AXIS_LAYER].n_dimensions; i++)
    put_u32 (buf, table->axes[PIVOT_AXIS_LAYER].dimensions[i]->top_index);
  for (size_t i = 0; i < table->axes[PIVOT_AXIS_ROW].n_dimensions; i++)
    put_u32 (buf, table->axes[PIVOT_AXIS_ROW].dimensions[i]->top_index);
  for (size_t i = 0; i < table->axes[PIVOT_AXIS_COLUMN].n_dimensions; i++)
    put_u32 (buf, table->axes[PIVOT_AXIS_COLUMN].dimensions[i]->top_index);

  /* Cells. */
  put_u32 (buf, hmap_count (&table->cells));
  const struct pivot_cell *cell;
  HMAP_FOR_EACH (cell, struct pivot_cell, hmap_node, &table->cells)
    {
      uint64_t index = 0;
      for (size_t j = 0; j < table->n_dimensions; j++)
        index = (table->dimensions[j]->n_leaves * index) + cell->idx[j];
      put_u64 (buf, index);

      put_value (buf, cell->value);
    }
}

void
spv_writer_put_table (struct spv_writer *w, const struct pivot_table *table)
{
  struct pivot_table *table_rw = CONST_CAST (struct pivot_table *, table);
  if (!table_rw->subtype)
    table_rw->subtype = pivot_value_new_user_text ("unknown", -1);

  int table_id = ++w->n_tables;

  bool initial_depth = w->heading_depth;
  if (!initial_depth)
    spv_writer_open_file (w);

  start_container (w);

  char *title = pivot_value_to_string (table->title, table);
  char *subtype = pivot_value_to_string (table->subtype, table);
  
  start_elem (w, "label");
  write_text (w, title);
  end_elem (w);

  start_elem (w, "vtb:table");
  write_attr (w, "commandName", table->command_c);
  write_attr (w, "type", "table"); /* XXX */
  write_attr (w, "subType", subtype);
  write_attr_format (w, "tableId", "%d", table_id);

  free (subtype);
  free (title);

  start_elem (w, "vtb:tableStructure");
  start_elem (w, "vtb:dataPath");
  char *data_path = xasprintf ("%010d_lightTableData.bin", table_id);
  write_text (w, data_path);
  end_elem (w); /* vtb:dataPath */
  end_elem (w); /* vtb:tableStructure */
  end_elem (w); /* vtb:table */
  end_elem (w); /* container */

  if (!initial_depth)
    spv_writer_close_file (w, "");

  struct buf buf = { NULL, 0, 0 };
  put_light_table (&buf, table_id, table);
  zip_writer_add_memory (w->zw, data_path, buf.data, buf.len);
  free (buf.data);

  free (data_path);
}
