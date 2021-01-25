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

#include "output/spv/spv.h"

#include <assert.h>
#include <cairo.h>
#include <inttypes.h>
#include <libxml/HTMLparser.h>
#include <libxml/xmlreader.h>
#include <stdarg.h>
#include <stdlib.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/hash-functions.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/zip-reader.h"
#include "output/output-item.h"
#include "output/page-setup.h"
#include "output/pivot-table.h"
#include "output/spv/detail-xml-parser.h"
#include "output/spv/light-binary-parser.h"
#include "output/spv/spv-css-parser.h"
#include "output/spv/spv-legacy-data.h"
#include "output/spv/spv-legacy-decoder.h"
#include "output/spv/spv-light-decoder.h"
#include "output/spv/spv-table-look.h"
#include "output/spv/structure-xml-parser.h"

#include "gl/c-ctype.h"
#include "gl/intprops.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"
#include "gl/xsize.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

struct spv_reader
  {
    struct zip_reader *zip;
    struct spv_item *root;
    struct page_setup *page_setup;
  };

static xmlNode *
find_xml_child_element (xmlNode *parent, const char *child_name)
{
  for (xmlNode *node = parent->children; node; node = node->next)
    if (node->type == XML_ELEMENT_NODE
        && node->name
        && !strcmp (CHAR_CAST (char *, node->name), child_name))
      return node;

  return NULL;
}

static char *
get_xml_attr (const xmlNode *node, const char *name)
{
  return CHAR_CAST (char *, xmlGetProp (node, CHAR_CAST (xmlChar *, name)));
}

static void
put_xml_attr (const char *name, const char *value, struct string *dst)
{
  if (!value)
    return;

  ds_put_format (dst, " %s=\"", name);
  for (const char *p = value; *p; p++)
    {
      switch (*p)
        {
        case '\n':
          ds_put_cstr (dst, "&#10;");
          break;
        case '&':
          ds_put_cstr (dst, "&amp;");
          break;
        case '<':
          ds_put_cstr (dst, "&lt;");
          break;
        case '>':
          ds_put_cstr (dst, "&gt;");
          break;
        case '"':
          ds_put_cstr (dst, "&quot;");
          break;
        default:
          ds_put_byte (dst, *p);
          break;
        }
    }
  ds_put_byte (dst, '"');
}

static void
extract_html_text (const xmlNode *node, int base_font_size, struct string *s)
{
  if (node->type == XML_ELEMENT_NODE)
    {
      const char *name = CHAR_CAST (char *, node->name);
      if (!strcmp (name, "br"))
        ds_put_byte (s, '\n');
      else if (strcmp (name, "style"))
        {
          const char *tag = NULL;
          if (strchr ("biu", name[0]) && name[1] == '\0')
            {
              tag = name;
              ds_put_format (s, "<%s>", tag);
            }
          else if (!strcmp (name, "font"))
            {
              tag = "span";
              ds_put_format (s, "<%s", tag);

              char *face = get_xml_attr (node, "face");
              put_xml_attr ("face", face, s);
              free (face);

              char *color = get_xml_attr (node, "color");
              if (color)
                {
                  if (color[0] == '#')
                    put_xml_attr ("color", color, s);
                  else
                    {
                      uint8_t r, g, b;
                      if (sscanf (color, "rgb (%"SCNu8", %"SCNu8", %"SCNu8")",
                                  &r, &g, &b) == 3)
                        {
                          char color2[8];
                          snprintf (color2, sizeof color2,
                                    "#%02"PRIx8"%02"PRIx8"%02"PRIx8,
                                    r, g, b);
                          put_xml_attr ("color", color2, s);
                        }
                    }
                }
              free (color);

              char *size_s = get_xml_attr (node, "size");
              int html_size = size_s ? atoi (size_s) : 0;
              free (size_s);
              if (html_size >= 1 && html_size <= 7)
                {
                  static const double scale[7] = {
                    .444, .556, .667, .778, 1.0, 1.33, 2.0
                  };
                  double size = base_font_size * scale[html_size - 1];

                  char size2[INT_BUFSIZE_BOUND (int)];
                  snprintf (size2, sizeof size2, "%.0f", size * 1024.);
                  put_xml_attr ("size", size2, s);
                }

              ds_put_cstr (s, ">");
            }
          for (const xmlNode *child = node->children; child;
               child = child->next)
            extract_html_text (child, base_font_size, s);
          if (tag)
            ds_put_format (s, "</%s>", tag);
        }
    }
  else if (node->type == XML_TEXT_NODE)
    {
      /* U+00A0 NONBREAKING SPACE is really, really common in SPV text and it
         makes it impossible to break syntax across lines.  Translate it into a
         regular space.  (Note that U+00A0 is C2 A0 in UTF-8.)

         Do the same for U+2007 FIGURE SPACE, which also crops out weirdly
         sometimes. */
      ds_extend (s, ds_length (s) + xmlStrlen (node->content));
      for (const uint8_t *p = node->content; *p;)
        {
          int c;
          if (p[0] == 0xc2 && p[1] == 0xa0)
            {
              c = ' ';
              p += 2;
            }
          else if (p[0] == 0xe2 && p[1] == 0x80 && p[2] == 0x87)
            {
              c = ' ';
              p += 3;
            }
          else
            c = *p++;

          if (c_isspace (c))
            {
              int last = ds_last (s);
              if (last != EOF && !c_isspace (last))
                ds_put_byte (s, c);
            }
          else if (c == '<')
            ds_put_cstr (s, "&lt;");
          else if (c == '>')
            ds_put_cstr (s, "&gt;");
          else if (c == '&')
            ds_put_cstr (s, "&amp;");
          else
            ds_put_byte (s, c);
        }
    }
}

static xmlDoc *
parse_embedded_html (const xmlNode *node)
{
  /* Extract HTML from XML node. */
  char *html_s = CHAR_CAST (char *, xmlNodeGetContent (node));
  if (!html_s)
    xalloc_die ();

  xmlDoc *html_doc = htmlReadMemory (
    html_s, strlen (html_s),
    NULL, "UTF-8", (HTML_PARSE_RECOVER | HTML_PARSE_NOERROR
                    | HTML_PARSE_NOWARNING | HTML_PARSE_NOBLANKS
                    | HTML_PARSE_NONET));
  free (html_s);

  return html_doc;
}

/* Given NODE, which should contain HTML content, returns the text within that
   content as an allocated string.  The caller must eventually free the
   returned string (with xmlFree()). */
static char *
decode_embedded_html (const xmlNode *node, struct font_style *font_style)
{
  struct string markup = DS_EMPTY_INITIALIZER;
  *font_style = (struct font_style) FONT_STYLE_INITIALIZER;
  font_style->size = 10;

  xmlDoc *html_doc = parse_embedded_html (node);
  if (html_doc)
    {
      xmlNode *root = xmlDocGetRootElement (html_doc);
      xmlNode *head = root ? find_xml_child_element (root, "head") : NULL;
      xmlNode *style = head ? find_xml_child_element (head, "style") : NULL;
      if (style)
        {
          uint8_t *style_s = xmlNodeGetContent (style);
          spv_parse_css_style (CHAR_CAST (char *, style_s), font_style);
          xmlFree (style_s);
        }

      if (root)
        extract_html_text (root, font_style->size, &markup);
      xmlFreeDoc (html_doc);
    }

  font_style->markup = true;
  return ds_steal_cstr (&markup);
}

static struct output_item *
decode_container_text (const struct spvsx_container_text *ct)
{
  struct font_style *font_style = xmalloc (sizeof *font_style);
  char *text = decode_embedded_html (ct->html->node_.raw, font_style);
  struct pivot_value *value = xmalloc (sizeof *value);
  *value = (struct pivot_value) {
    .font_style = font_style,
    .type = PIVOT_VALUE_TEXT,
    .text = {
      .local = text,
      .c = text,
      .id = text,
      .user_provided = true,
    },
  };

  struct output_item *item = text_item_create_value (TEXT_ITEM_LOG,
                                                     value, NULL);
  output_item_set_command_name (item, ct->command_name);
  return item;
}

static void
decode_page_p (const xmlNode *in, struct page_paragraph *out)
{
  char *style = get_xml_attr (in, "style");
  out->halign = (style && strstr (style, "center") ? TABLE_HALIGN_CENTER
                 : style && strstr (style, "right") ? TABLE_HALIGN_RIGHT
                 : TABLE_HALIGN_LEFT);
  free (style);

  struct font_style font_style;
  out->markup = decode_embedded_html (in, &font_style);
  font_style_uninit (&font_style);
}

static void
decode_page_paragraph (const struct spvsx_page_paragraph *page_paragraph,
                       struct page_heading *ph)
{
  memset (ph, 0, sizeof *ph);

  const struct spvsx_page_paragraph_text *page_paragraph_text
    = page_paragraph->page_paragraph_text;
  if (!page_paragraph_text)
    return;

  xmlDoc *html_doc = parse_embedded_html (page_paragraph_text->node_.raw);
  if (!html_doc)
    return;

  xmlNode *root = xmlDocGetRootElement (html_doc);
  xmlNode *body = find_xml_child_element (root, "body");
  if (body)
    for (const xmlNode *node = body->children; node; node = node->next)
      if (node->type == XML_ELEMENT_NODE
          && !strcmp (CHAR_CAST (const char *, node->name), "p"))
        {
          ph->paragraphs = xrealloc (ph->paragraphs,
                                     (ph->n + 1) * sizeof *ph->paragraphs);
          decode_page_p (node, &ph->paragraphs[ph->n++]);
        }
  xmlFreeDoc (html_doc);
}

char * WARN_UNUSED_RESULT
spv_read_light_table (struct zip_reader *zip, const char *bin_member,
                      struct spvlb_table **tablep)
{
  *tablep = NULL;

  void *data;
  size_t size;
  char *error = zip_member_read_all (zip, bin_member, &data, &size);
  if (error)
    return error;

  struct spvbin_input input;
  spvbin_input_init (&input, data, size);

  struct spvlb_table *table = NULL;
  error = (!size
           ? xasprintf ("light table member is empty")
           : !spvlb_parse_table (&input, &table)
           ? spvbin_input_to_error (&input, NULL)
           : input.ofs != input.size
           ? xasprintf ("expected end of file at offset %#zx", input.ofs)
           : NULL);
  free (data);
  if (!error)
    *tablep = table;
  return error;
}

static char * WARN_UNUSED_RESULT
pivot_table_open_light (struct zip_reader *zip, const char *bin_member,
                        struct pivot_table **tablep)
{
  *tablep = NULL;

  struct spvlb_table *raw_table;
  char *error = spv_read_light_table (zip, bin_member, &raw_table);
  if (!error)
    error = decode_spvlb_table (raw_table, tablep);
  spvlb_free_table (raw_table);

  return error;
}

char * WARN_UNUSED_RESULT
spv_read_legacy_data (struct zip_reader *zip, const char *bin_member,
                      struct spv_data *data)
{
  void *raw;
  size_t size;
  char *error = zip_member_read_all (zip, bin_member, &raw, &size);
  if (!error)
    {
      error = spv_legacy_data_decode (raw, size, data);
      free (raw);
    }

  return error;
}

char * WARN_UNUSED_RESULT
spv_read_xml_member (struct zip_reader *zip, const char *xml_member,
                     bool keep_blanks, const char *root_element_name,
                     xmlDoc **docp)
{
  *docp = NULL;

  struct zip_member *zm;
  char *error = zip_member_open (zip, xml_member, &zm);
  if (error)
    return error;

  xmlParserCtxt *parser;
  xmlKeepBlanksDefault (keep_blanks);
  parser = xmlCreatePushParserCtxt(NULL, NULL, NULL, 0, NULL);
  if (!parser)
    {
      zip_member_finish (zm);
      return xasprintf (_("%s: Failed to create XML parser"), xml_member);
    }

  int retval;
  char buf[4096];
  while ((retval = zip_member_read (zm, buf, sizeof buf)) > 0)
    xmlParseChunk (parser, buf, retval, false);
  xmlParseChunk (parser, NULL, 0, true);

  xmlDoc *doc = parser->myDoc;
  bool well_formed = parser->wellFormed;
  xmlFreeParserCtxt (parser);

  if (retval < 0)
    {
      char *error = zip_member_steal_error (zm);
      zip_member_finish (zm);
      xmlFreeDoc (doc);
      return error;
    }
  zip_member_finish (zm);

  if (!well_formed)
    {
      xmlFreeDoc (doc);
      return xasprintf(_("%s: document is not well-formed"), xml_member);
    }

  const xmlNode *root_node = xmlDocGetRootElement (doc);
  assert (root_node->type == XML_ELEMENT_NODE);
  if (strcmp (CHAR_CAST (char *, root_node->name), root_element_name))
    {
      xmlFreeDoc (doc);
      return xasprintf(_("%s: root node is \"%s\" but \"%s\" was expected"),
                       xml_member,
                       CHAR_CAST (char *, root_node->name), root_element_name);
    }

  *docp = doc;
  return NULL;
}

static char * WARN_UNUSED_RESULT
pivot_table_open_legacy (struct zip_reader *zip, const char *bin_member,
                         const char *xml_member, const char *subtype,
                         const struct pivot_table_look *look,
                         struct pivot_table **tablep)
{
  *tablep = NULL;

  struct spv_data data = SPV_DATA_INITIALIZER;
  char *error = spv_read_legacy_data (zip, bin_member, &data);
  if (error)
    goto exit;

  xmlDoc *doc;
  error = spv_read_xml_member (zip, xml_member, false,
                               "visualization", &doc);
  if (error)
    goto exit_free_data;

  struct spvxml_context ctx = SPVXML_CONTEXT_INIT (ctx);
  struct spvdx_visualization *v;
  spvdx_parse_visualization (&ctx, xmlDocGetRootElement (doc), &v);
  error = spvxml_context_finish (&ctx, &v->node_);
  if (error)
    goto exit_free_doc;

  error = decode_spvdx_table (v, subtype, look, &data, tablep);

  spvdx_free_visualization (v);
exit_free_doc:
  if (doc)
    xmlFreeDoc (doc);
exit_free_data:
  spv_data_uninit (&data);
exit:
  return error;
}

static struct output_item *
spv_read_table_item (struct zip_reader *zip,
                     const struct spvsx_table *table)
{
  const struct spvsx_table_structure *ts = table->table_structure;
  const char *bin_member = ts->data_path->text;
  const char *xml_member = ts->path ? ts->path->text : NULL;

  struct pivot_table *pt = NULL;
  char *error;
  if (xml_member)
    {
      struct pivot_table_look *look;
      error = (table->table_properties
               ? spv_table_look_decode (table->table_properties, &look)
               : xstrdup ("Legacy table lacks tableProperties"));
      if (!error)
        {
          error = pivot_table_open_legacy (zip, bin_member, xml_member,
                                           table->sub_type, look, &pt);
          pivot_table_look_unref (look);
        }
    }
  else
    error = pivot_table_open_light (zip, bin_member, &pt);
  if (error)
    pt = pivot_table_create_for_text (
      pivot_value_new_text (N_("Error")),
      pivot_value_new_user_text_nocopy (error));

  struct output_item *item = table_item_create (pt);
  output_item_set_command_name (item, table->command_name);
  output_item_add_spv_info (item);
  item->spv_info->error = error != NULL;
  item->spv_info->zip_reader = zip_reader_ref (zip);
  item->spv_info->bin_member = xstrdup (bin_member);
  item->spv_info->xml_member = xstrdup_if_nonnull (xml_member);
  return item;
}

static cairo_status_t
read_from_zip_member (void *zm_, unsigned char *data, unsigned int length)
{
  struct zip_member *zm = zm_;
  if (!zm)
    return CAIRO_STATUS_READ_ERROR;

  while (length > 0)
    {
      int n = zip_member_read (zm, data, length);
      if (n <= 0)
        return CAIRO_STATUS_READ_ERROR;

      data += n;
      length -= n;
    }

  return CAIRO_STATUS_SUCCESS;
}

static char * WARN_UNUSED_RESULT
spv_read_image (struct zip_reader *zip, const char *png_member,
                const char *command_name, struct output_item **itemp)
{
  struct zip_member *zm;
  char *error = zip_member_open (zip, png_member, &zm);
  if (error)
    return error;

  cairo_surface_t *surface = cairo_image_surface_create_from_png_stream (
    read_from_zip_member, zm);
  if (zm)
    zip_member_finish (zm);

  if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
    return xstrdup ("reading image failed");

  struct output_item *item = image_item_create (surface);
  output_item_set_command_name (item, command_name);
  output_item_add_spv_info (item);
  item->spv_info->zip_reader = zip_reader_ref (zip);
  item->spv_info->png_member = xstrdup (png_member);
  *itemp = item;
  return NULL;
}

static struct output_item *
error_item_create (char *s)
{
  struct output_item *item = text_item_create_nocopy (TEXT_ITEM_LOG, s,
                                                      xstrdup ("Error"));
  output_item_add_spv_info (item);
  item->spv_info->error = true;
  return item;
}

static struct output_item *
spv_decode_container (struct zip_reader *zip,
                      const struct spvsx_container *c)
{
  assert (c->n_seq == 1);
  struct spvxml_node *content = c->seq[0];

  struct output_item *item = NULL;
  char *error;
  if (spvsx_is_container_text (content))
    {
      item = decode_container_text (spvsx_cast_container_text (content));
      error = NULL;
    }
  else if (spvsx_is_table (content))
    {
      item = spv_read_table_item (zip, spvsx_cast_table (content));
      error = NULL;
    }
  else if (spvsx_is_object (content))
    {
      struct spvsx_object *object = spvsx_cast_object (content);
      error = spv_read_image (zip, object->uri, object->command_name, &item);
    }
  else if (spvsx_is_image (content))
    {
      struct spvsx_image *image = spvsx_cast_image (content);
      error = spv_read_image (zip, image->data_path->text, image->command_name,
                              &item);
    }
  else if (spvsx_is_graph (content))
    error = xstrdup ("graphs not yet implemented");
  else if (spvsx_is_model (content))
    error = xstrdup ("models not yet implemented");
  else if (spvsx_is_tree (content))
    error = xstrdup ("trees not yet implemented");
  else
    NOT_REACHED ();

  if (error)
    item = error_item_create (error);
  else
    output_item_set_label (item, c->label->text);
  item->show = c->visibility == SPVSX_VISIBILITY_VISIBLE;

  return item;
}

static void
set_structure_member (struct output_item *item, struct zip_reader *zip,
                      const char *structure_member)
{
  if (structure_member)
    {
      output_item_add_spv_info (item);
      if (!item->spv_info->zip_reader)
        item->spv_info->zip_reader = zip_reader_ref (zip);
      if (!item->spv_info->structure_member)
        item->spv_info->structure_member = xstrdup (structure_member);
    }
}

static void
spv_decode_children (struct zip_reader *zip, const char *structure_member,
                     struct spvxml_node **seq, size_t n_seq,
                     struct output_item *parent)
{
  for (size_t i = 0; i < n_seq; i++)
    {
      const struct spvxml_node *node = seq[i];

      struct output_item *child;
      if (spvsx_is_container (node))
        {
          const struct spvsx_container *container
            = spvsx_cast_container (node);

          if (container->page_break_before_present)
            group_item_add_child (parent, page_break_item_create ());

          child = spv_decode_container (zip, container);
        }
      else if (spvsx_is_heading (node))
        {
          const struct spvsx_heading *subheading = spvsx_cast_heading (node);

          child = group_item_create (subheading->command_name,
                                     subheading->label->text);
          child->show = !subheading->heading_visibility_present;

          /* Pass NULL for 'structure_member' so that only top-level items get
             tagged that way.  Lower-level items are always in the same
             structure member as their parent anyway. */
           spv_decode_children (zip, NULL, subheading->seq,
                                subheading->n_seq, child);
        }
      else
        NOT_REACHED ();

      set_structure_member (child, zip, structure_member);
      group_item_add_child (parent, child);
    }
}

static struct page_setup *
decode_page_setup (const struct spvsx_page_setup *in, const char *file_name)
{
  struct page_setup *out = xmalloc (sizeof *out);
  *out = (struct page_setup) PAGE_SETUP_INITIALIZER;

  out->initial_page_number = in->initial_page_number;

  if (in->paper_width != DBL_MAX)
    out->paper[TABLE_HORZ] = in->paper_width;
  if (in->paper_height != DBL_MAX)
    out->paper[TABLE_VERT] = in->paper_height;

  if (in->margin_left != DBL_MAX)
    out->margins[TABLE_HORZ][0] = in->margin_left;
  if (in->margin_right != DBL_MAX)
    out->margins[TABLE_HORZ][1] = in->margin_right;
  if (in->margin_top != DBL_MAX)
    out->margins[TABLE_VERT][0] = in->margin_top;
  if (in->margin_bottom != DBL_MAX)
    out->margins[TABLE_VERT][1] = in->margin_bottom;

  if (in->space_after != DBL_MAX)
    out->object_spacing = in->space_after;

  if (in->chart_size)
    out->chart_size = (in->chart_size == SPVSX_CHART_SIZE_FULL_HEIGHT
                       ? PAGE_CHART_FULL_HEIGHT
                       : in->chart_size == SPVSX_CHART_SIZE_HALF_HEIGHT
                       ? PAGE_CHART_HALF_HEIGHT
                       : in->chart_size == SPVSX_CHART_SIZE_QUARTER_HEIGHT
                       ? PAGE_CHART_QUARTER_HEIGHT
                       : PAGE_CHART_AS_IS);

  decode_page_paragraph (in->page_header->page_paragraph, &out->headings[0]);
  decode_page_paragraph (in->page_footer->page_paragraph, &out->headings[1]);

  out->file_name = xstrdup (file_name);

  return out;
}

static void
spv_add_error_heading (struct output_item *root_item,
                       struct zip_reader *zip, const char *structure_member,
                       char *error)
{
  struct output_item *item = error_item_create (
    xasprintf ("%s: %s", structure_member, error));
  free (error);
  set_structure_member (item, zip, structure_member);
  group_item_add_child (root_item, item);
}

static void
spv_heading_read (struct zip_reader *zip, struct output_item *root_item,
                  struct page_setup **psp, const char *file_name,
                  const char *structure_member)
{
  xmlDoc *doc;
  char *error = spv_read_xml_member (zip, structure_member, true,
                                     "heading", &doc);
  if (error)
    {
      spv_add_error_heading (root_item, zip, structure_member, error);
      return;
    }

  struct spvxml_context ctx = SPVXML_CONTEXT_INIT (ctx);
  struct spvsx_root_heading *root;
  spvsx_parse_root_heading (&ctx, xmlDocGetRootElement (doc), &root);
  error = spvxml_context_finish (&ctx, &root->node_);
  if (error)
    {
      xmlFreeDoc (doc);
      spv_add_error_heading (root_item, zip, structure_member, error);
      return;
    }

  if (root->page_setup && psp && !*psp)
    *psp = decode_page_setup (root->page_setup, file_name);

  for (size_t i = 0; i < root->n_seq; i++)
    spv_decode_children (zip, structure_member, root->seq, root->n_seq,
                         root_item);

  spvsx_free_root_heading (root);
  xmlFreeDoc (doc);
}

static int
spv_detect__ (struct zip_reader *zip, char **errorp)
{
  *errorp = NULL;

  const char *member = "META-INF/MANIFEST.MF";
  if (!zip_reader_contains_member (zip, member))
    return 0;

  void *data;
  size_t size;
  *errorp = zip_member_read_all (zip, "META-INF/MANIFEST.MF",
                                 &data, &size);
  if (*errorp)
    return -1;

  const char *magic = "allowPivoting=true";
  bool is_spv = size == strlen (magic) && !memcmp (magic, data, size);
  free (data);

  return is_spv;
}

/* Returns NULL if FILENAME is an SPV file, otherwise an error string that the
   caller must eventually free(). */
char * WARN_UNUSED_RESULT
spv_detect (const char *filename)
{
  struct zip_reader *zip;
  char *error = zip_reader_create (filename, &zip);
  if (error)
    return error;

  if (spv_detect__ (zip, &error) <= 0 && !error)
    error = xasprintf("%s: not an SPV file", filename);
  zip_reader_unref (zip);
  return error;
}

char * WARN_UNUSED_RESULT
spv_read (const char *filename, struct output_item **outp,
          struct page_setup **psp)
{
  *outp = NULL;
  if (psp)
    *psp = NULL;

  struct spv_reader *spv = xzalloc (sizeof *spv);
  struct zip_reader *zip;
  char *error = zip_reader_create (filename, &zip);
  if (error)
    return error;

  int detect = spv_detect__ (zip, &error);
  if (detect <= 0)
    {
      zip_reader_unref (zip);
      return error ? error : xasprintf ("%s: not an SPV file", filename);
    }

  *outp = root_item_create ();
  for (size_t i = 0; ; i++)
    {
      const char *structure_member = zip_reader_get_member_name (zip, i);
      if (!structure_member)
        break;

      struct substring structure_member_ss = ss_cstr (structure_member);
      if (ss_starts_with (structure_member_ss, ss_cstr ("outputViewer"))
          && ss_ends_with (structure_member_ss, ss_cstr (".xml")))
        spv_heading_read (zip, *outp, psp, filename, structure_member);
    }

  zip_reader_unref (zip);
  return NULL;
}

char * WARN_UNUSED_RESULT
spv_decode_fmt_spec (uint32_t u32, struct fmt_spec *out)
{
  if (!u32
      || (u32 == 0x10000 || u32 == 1 /* both used as string formats */))
    {
      *out = fmt_for_output (FMT_F, 40, 2);
      return NULL;
    }

  uint8_t raw_type = u32 >> 16;
  uint8_t w = u32 >> 8;
  uint8_t d = u32;

  msg_disable ();
  *out = (struct fmt_spec) { .type = FMT_F, .w = w, .d = d };
  bool ok = raw_type >= 40 || fmt_from_io (raw_type, &out->type);
  if (ok)
    {
      fmt_fix_output (out);
      ok = fmt_check_width_compat (out, 0);
    }
  msg_enable ();

  if (!ok)
    {
      *out = fmt_for_output (FMT_F, 40, 2);
      return xasprintf ("bad format %#"PRIx32, u32);
    }

  return NULL;
}
