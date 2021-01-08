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
#include "output/page-setup-item.h"
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
    struct string zip_errs;
    struct zip_reader *zip;
    struct spv_item *root;
    struct page_setup *page_setup;
  };

const struct page_setup *
spv_get_page_setup (const struct spv_reader *spv)
{
  return spv->page_setup;
}

const char *
spv_item_type_to_string (enum spv_item_type type)
{
  switch (type)
    {
    case SPV_ITEM_HEADING: return "heading";
    case SPV_ITEM_TEXT: return "text";
    case SPV_ITEM_TABLE: return "table";
    case SPV_ITEM_GRAPH: return "graph";
    case SPV_ITEM_MODEL: return "model";
    case SPV_ITEM_IMAGE: return "image";
    default: return "**error**";
    }
}

const char *
spv_item_class_to_string (enum spv_item_class class)
{
  switch (class)
    {
#define SPV_CLASS(ENUM, NAME) case SPV_CLASS_##ENUM: return NAME;
      SPV_CLASSES
#undef SPV_CLASS
    default: return NULL;
    }
}

enum spv_item_class
spv_item_class_from_string (const char *name)
{
#define SPV_CLASS(ENUM, NAME) \
  if (!strcmp (name, NAME)) return SPV_CLASS_##ENUM;
  SPV_CLASSES
#undef SPV_CLASS

  return (enum spv_item_class) SPV_N_CLASSES;
}

enum spv_item_type
spv_item_get_type (const struct spv_item *item)
{
  return item->type;
}

enum spv_item_class
spv_item_get_class (const struct spv_item *item)
{
  const char *label = spv_item_get_label (item);
  if (!label)
    label = "";

  switch (item->type)
    {
    case SPV_ITEM_HEADING:
      return SPV_CLASS_HEADINGS;

    case SPV_ITEM_TEXT:
      return (!strcmp (label, "Title") ? SPV_CLASS_OUTLINEHEADERS
              : !strcmp (label, "Log") ? SPV_CLASS_LOGS
              : !strcmp (label, "Page Title") ? SPV_CLASS_PAGETITLE
              : SPV_CLASS_TEXTS);

    case SPV_ITEM_TABLE:
      return (!strcmp (label, "Warnings") ? SPV_CLASS_WARNINGS
              : !strcmp (label, "Notes") ? SPV_CLASS_NOTES
              : SPV_CLASS_TABLES);

    case SPV_ITEM_GRAPH:
      return SPV_CLASS_CHARTS;

    case SPV_ITEM_MODEL:
      return SPV_CLASS_MODELS;

    case SPV_ITEM_IMAGE:
      return SPV_CLASS_OTHER;

    case SPV_ITEM_TREE:
      return SPV_CLASS_TREES;

    default:
      return SPV_CLASS_UNKNOWN;
    }
}

const char *
spv_item_get_label (const struct spv_item *item)
{
  return item->label;
}

bool
spv_item_is_heading (const struct spv_item *item)
{
  return item->type == SPV_ITEM_HEADING;
}

size_t
spv_item_get_n_children (const struct spv_item *item)
{
  return item->n_children;
}

struct spv_item *
spv_item_get_child (const struct spv_item *item, size_t idx)
{
  assert (idx < item->n_children);
  return item->children[idx];
}

bool
spv_item_is_table (const struct spv_item *item)
{
  return item->type == SPV_ITEM_TABLE;
}

bool
spv_item_is_text (const struct spv_item *item)
{
  return item->type == SPV_ITEM_TEXT;
}

const struct pivot_value *
spv_item_get_text (const struct spv_item *item)
{
  assert (spv_item_is_text (item));
  return item->text;
}

bool
spv_item_is_image (const struct spv_item *item)
{
  return item->type == SPV_ITEM_IMAGE;
}

#ifdef HAVE_CAIRO
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

cairo_surface_t *
spv_item_get_image (const struct spv_item *item_)
{
  struct spv_item *item = CONST_CAST (struct spv_item *, item_);
  assert (spv_item_is_image (item));

  if (!item->image)
    {
      struct zip_member *zm = zip_member_open (item->spv->zip,
                                               item->png_member);
      item->image = cairo_image_surface_create_from_png_stream (
        read_from_zip_member, zm);
      if (zm)
        zip_member_finish (zm);
    }

  return item->image;
}
#endif

struct spv_item *
spv_item_next (const struct spv_item *item)
{
  if (item->n_children)
    return item->children[0];

  while (item->parent)
    {
      size_t idx = item->parent_idx + 1;
      item = item->parent;
      if (idx < item->n_children)
        return item->children[idx];
    }

  return NULL;
}

const struct spv_item *
spv_item_get_parent (const struct spv_item *item)
{
  return item->parent;
}

size_t
spv_item_get_level (const struct spv_item *item)
{
  int level = 0;
  for (; item->parent; item = item->parent)
    level++;
  return level;
}

const char *
spv_item_get_command_id (const struct spv_item *item)
{
  return item->command_id;
}

const char *
spv_item_get_subtype (const struct spv_item *item)
{
  return item->subtype;
}

bool
spv_item_is_visible (const struct spv_item *item)
{
  return item->visible;
}

static void
spv_item_destroy (struct spv_item *item)
{
  if (item)
    {
      free (item->structure_member);

      free (item->label);
      free (item->command_id);

      for (size_t i = 0; i < item->n_children; i++)
        spv_item_destroy (item->children[i]);
      free (item->children);

      pivot_table_unref (item->table);
      pivot_table_look_unref (item->table_look);
      free (item->bin_member);
      free (item->xml_member);
      free (item->subtype);

      pivot_value_destroy (item->text);

      free (item->png_member);
#ifdef HAVE_CAIRO
      if (item->image)
        cairo_surface_destroy (item->image);
#endif

      free (item);
    }
}

static void
spv_heading_add_child (struct spv_item *parent, struct spv_item *child)
{
  assert (parent->type == SPV_ITEM_HEADING);
  assert (!child->parent);

  child->parent = parent;
  child->parent_idx = parent->n_children;

  if (parent->n_children >= parent->allocated_children)
    parent->children = x2nrealloc (parent->children,
                                   &parent->allocated_children,
                                   sizeof *parent->children);
  parent->children[parent->n_children++] = child;
}

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

static char *
xstrdup_if_nonempty (const char *s)
{
  return s && s[0] ? xstrdup (s) : NULL;
}

static void
decode_container_text (const struct spvsx_container_text *ct,
                       struct spv_item *item)
{
  item->type = SPV_ITEM_TEXT;
  item->command_id = xstrdup_if_nonempty (ct->command_name);

  item->text = xzalloc (sizeof *item->text);
  item->text->type = PIVOT_VALUE_TEXT;
  item->text->font_style = xmalloc (sizeof *item->text->font_style);
  item->text->text.local = decode_embedded_html (ct->html->node_.raw,
                                                 item->text->font_style);
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

void
spv_item_load (const struct spv_item *item)
{
  if (spv_item_is_table (item))
    spv_item_get_table (item);
#ifdef HAVE_CAIRO
  else if (spv_item_is_image (item))
    spv_item_get_image (item);
#endif
}

bool
spv_item_is_light_table (const struct spv_item *item)
{
  return item->type == SPV_ITEM_TABLE && !item->xml_member;
}

char * WARN_UNUSED_RESULT
spv_item_get_raw_light_table (const struct spv_item *item,
                              void **data, size_t *size)
{
  return zip_member_read_all (item->spv->zip, item->bin_member, data, size);
}

char * WARN_UNUSED_RESULT
spv_item_get_light_table (const struct spv_item *item,
                          struct spvlb_table **tablep)
{
  *tablep = NULL;

  if (!spv_item_is_light_table (item))
    return xstrdup ("not a light binary table object");

  void *data;
  size_t size;
  char *error = spv_item_get_raw_light_table (item, &data, &size);
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
  if (error)
    {
      struct string s = DS_EMPTY_INITIALIZER;
      spv_item_format_path (item, &s);
      ds_put_format (&s, " (%s): %s", item->bin_member, error);

      free (error);
      error = ds_steal_cstr (&s);
    }
  free (data);
  if (!error)
    *tablep = table;
  return error;
}

static char *
pivot_table_open_light (struct spv_item *item)
{
  assert (spv_item_is_light_table (item));

  struct spvlb_table *raw_table;
  char *error = spv_item_get_light_table (item, &raw_table);
  if (!error)
    error = decode_spvlb_table (raw_table, &item->table);
  spvlb_free_table (raw_table);

  return error;
}

bool
spv_item_is_legacy_table (const struct spv_item *item)
{
  return item->type == SPV_ITEM_TABLE && item->xml_member;
}

char * WARN_UNUSED_RESULT
spv_item_get_raw_legacy_data (const struct spv_item *item,
                              void **data, size_t *size)
{
  if (!spv_item_is_legacy_table (item))
    return xstrdup ("not a legacy table object");

  return zip_member_read_all (item->spv->zip, item->bin_member, data, size);
}

char * WARN_UNUSED_RESULT
spv_item_get_legacy_data (const struct spv_item *item, struct spv_data *data)
{
  void *raw;
  size_t size;
  char *error = spv_item_get_raw_legacy_data (item, &raw, &size);
  if (!error)
    {
      error = spv_legacy_data_decode (raw, size, data);
      free (raw);
    }

  return error;
}

static char * WARN_UNUSED_RESULT
spv_read_xml_member (struct spv_reader *spv, const char *member_name,
                     bool keep_blanks, const char *root_element_name,
                     xmlDoc **docp)
{
  *docp = NULL;

  struct zip_member *zm = zip_member_open (spv->zip, member_name);
  if (!zm)
    return ds_steal_cstr (&spv->zip_errs);

  xmlParserCtxt *parser;
  xmlKeepBlanksDefault (keep_blanks);
  parser = xmlCreatePushParserCtxt(NULL, NULL, NULL, 0, NULL);
  if (!parser)
    {
      zip_member_finish (zm);
      return xasprintf (_("%s: Failed to create XML parser"), member_name);
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
      char *error = ds_steal_cstr (&spv->zip_errs);
      zip_member_finish (zm);
      xmlFreeDoc (doc);
      return error;
    }
  zip_member_finish (zm);

  if (!well_formed)
    {
      xmlFreeDoc (doc);
      return xasprintf(_("%s: document is not well-formed"), member_name);
    }

  const xmlNode *root_node = xmlDocGetRootElement (doc);
  assert (root_node->type == XML_ELEMENT_NODE);
  if (strcmp (CHAR_CAST (char *, root_node->name), root_element_name))
    {
      xmlFreeDoc (doc);
      return xasprintf(_("%s: root node is \"%s\" but \"%s\" was expected"),
                       member_name,
                       CHAR_CAST (char *, root_node->name), root_element_name);
    }

  *docp = doc;
  return NULL;
}

char * WARN_UNUSED_RESULT
spv_item_get_legacy_table (const struct spv_item *item, xmlDoc **docp)
{
  assert (spv_item_is_legacy_table (item));

  return spv_read_xml_member (item->spv, item->xml_member, false,
                              "visualization", docp);
}

char * WARN_UNUSED_RESULT
spv_item_get_structure (const struct spv_item *item, struct _xmlDoc **docp)
{
  return spv_read_xml_member (item->spv, item->structure_member, false,
                              "heading", docp);
}

static const char *
identify_item (const struct spv_item *item)
{
  return (item->label ? item->label
          : item->command_id ? item->command_id
          : spv_item_type_to_string (item->type));
}

void
spv_item_format_path (const struct spv_item *item, struct string *s)
{
  enum { MAX_STACK = 32 };
  const struct spv_item *stack[MAX_STACK];
  size_t n = 0;

  while (item != NULL && item->parent && n < MAX_STACK)
    {
      stack[n++] = item;
      item = item->parent;
    }

  while (n > 0)
    {
      item = stack[--n];
      ds_put_byte (s, '/');

      const char *name = identify_item (item);
      ds_put_cstr (s, name);

      if (item->parent)
        {
          size_t total = 1;
          size_t index = 1;
          for (size_t i = 0; i < item->parent->n_children; i++)
            {
              const struct spv_item *sibling = item->parent->children[i];
              if (sibling == item)
                index = total;
              else if (!strcmp (name, identify_item (sibling)))
                total++;
            }
          if (total > 1)
            ds_put_format (s, "[%zu]", index);
        }
    }
}

static char * WARN_UNUSED_RESULT
pivot_table_open_legacy (struct spv_item *item)
{
  assert (spv_item_is_legacy_table (item));

  struct spv_data data;
  char *error = spv_item_get_legacy_data (item, &data);
  if (error)
    {
      struct string s = DS_EMPTY_INITIALIZER;
      spv_item_format_path (item, &s);
      ds_put_format (&s, " (%s): %s", item->bin_member, error);

      free (error);
      return ds_steal_cstr (&s);
    }

  xmlDoc *doc;
  error = spv_read_xml_member (item->spv, item->xml_member, false,
                               "visualization", &doc);
  if (error)
    {
      spv_data_uninit (&data);
      return error;
    }

  struct spvxml_context ctx = SPVXML_CONTEXT_INIT (ctx);
  struct spvdx_visualization *v;
  spvdx_parse_visualization (&ctx, xmlDocGetRootElement (doc), &v);
  error = spvxml_context_finish (&ctx, &v->node_);

  if (!error)
    error = decode_spvdx_table (v, item->subtype, item->table_look,
                                &data, &item->table);

  if (error)
    {
      struct string s = DS_EMPTY_INITIALIZER;
      spv_item_format_path (item, &s);
      ds_put_format (&s, " (%s): %s", item->xml_member, error);

      free (error);
      error = ds_steal_cstr (&s);
    }

  spv_data_uninit (&data);
  spvdx_free_visualization (v);
  if (doc)
    xmlFreeDoc (doc);

  return error;
}

const struct pivot_table *
spv_item_get_table (const struct spv_item *item_)
{
  struct spv_item *item = CONST_CAST (struct spv_item *, item_);

  assert (spv_item_is_table (item));
  if (!item->table)
    {
      char *error = (item->xml_member
                     ? pivot_table_open_legacy (item)
                     : pivot_table_open_light (item));
      if (error)
        {
          item->error = true;
          msg (ME, "%s", error);
          item->table = pivot_table_create_for_text (
            pivot_value_new_text (N_("Error")),
            pivot_value_new_user_text (error, -1));
          free (error);
        }
    }

  return item->table;
}

/* Constructs a new spv_item from XML and stores it in *ITEMP.  Returns NULL if
   successful, otherwise an error message for the caller to use and free (with
   free()).

   XML should be a 'heading' or 'container' element. */
static char * WARN_UNUSED_RESULT
spv_decode_container (const struct spvsx_container *c,
                      const char *structure_member,
                      struct spv_item *parent)
{
  struct spv_item *item = xzalloc (sizeof *item);
  item->spv = parent->spv;
  item->label = xstrdup (c->label->text);
  item->visible = c->visibility == SPVSX_VISIBILITY_VISIBLE;
  item->structure_member = xstrdup (structure_member);

  assert (c->n_seq == 1);
  struct spvxml_node *content = c->seq[0];
  if (spvsx_is_container_text (content))
    decode_container_text (spvsx_cast_container_text (content), item);
  else if (spvsx_is_table (content))
    {
      item->type = SPV_ITEM_TABLE;

      struct spvsx_table *table = spvsx_cast_table (content);
      const struct spvsx_table_structure *ts = table->table_structure;
      item->bin_member = xstrdup (ts->data_path->text);
      item->command_id = xstrdup_if_nonempty (table->command_name);
      item->subtype = xstrdup_if_nonempty (table->sub_type);
      if (ts->path)
        {
          item->xml_member = ts->path ? xstrdup (ts->path->text) : NULL;
          char *error = (table->table_properties
                         ? spv_table_look_decode (table->table_properties,
                                                  &item->table_look)
                         : xstrdup ("Legacy table lacks tableProperties"));
          if (error)
            {
              spv_item_destroy (item);
              return error;
            }
        }
    }
  else if (spvsx_is_graph (content))
    {
      struct spvsx_graph *graph = spvsx_cast_graph (content);
      item->type = SPV_ITEM_GRAPH;
      item->command_id = xstrdup_if_nonempty (graph->command_name);
      /* XXX */
    }
  else if (spvsx_is_model (content))
    {
      struct spvsx_model *model = spvsx_cast_model (content);
      item->type = SPV_ITEM_MODEL;
      item->command_id = xstrdup_if_nonempty (model->command_name);
      /* XXX */
    }
  else if (spvsx_is_object (content))
    {
      struct spvsx_object *object = spvsx_cast_object (content);
      item->type = SPV_ITEM_IMAGE;
      item->png_member = xstrdup (object->uri);
    }
  else if (spvsx_is_image (content))
    {
      struct spvsx_image *image = spvsx_cast_image (content);
      item->type = SPV_ITEM_IMAGE;
      item->png_member = xstrdup (image->data_path->text);
    }
  else if (spvsx_is_tree (content))
    item->type = SPV_ITEM_TREE;
  else
    NOT_REACHED ();

  spv_heading_add_child (parent, item);
  return NULL;
}

static char * WARN_UNUSED_RESULT
spv_decode_children (struct spv_reader *spv, const char *structure_member,
                     struct spvxml_node **seq, size_t n_seq,
                     struct spv_item *parent)
{
  for (size_t i = 0; i < n_seq; i++)
    {
      const struct spvxml_node *node = seq[i];

      char *error = NULL;
      if (spvsx_is_container (node))
        {
          const struct spvsx_container *container
            = spvsx_cast_container (node);
          error = spv_decode_container (container, structure_member, parent);
        }
      else if (spvsx_is_heading (node))
        {
          const struct spvsx_heading *subheading = spvsx_cast_heading (node);
          struct spv_item *subitem = xzalloc (sizeof *subitem);
          subitem->structure_member = xstrdup (structure_member);
          subitem->spv = parent->spv;
          subitem->type = SPV_ITEM_HEADING;
          subitem->label = xstrdup (subheading->label->text);
          if (subheading->command_name)
            subitem->command_id = xstrdup (subheading->command_name);
          subitem->visible = !subheading->heading_visibility_present;
          spv_heading_add_child (parent, subitem);

          error = spv_decode_children (spv, structure_member,
                                       subheading->seq, subheading->n_seq,
                                       subitem);
        }
      else
        NOT_REACHED ();

      if (error)
        return error;
    }

  return NULL;
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

static char * WARN_UNUSED_RESULT
spv_heading_read (struct spv_reader *spv,
                  const char *file_name, const char *member_name)
{
  xmlDoc *doc;
  char *error = spv_read_xml_member (spv, member_name, true, "heading", &doc);
  if (error)
    return error;

  struct spvxml_context ctx = SPVXML_CONTEXT_INIT (ctx);
  struct spvsx_root_heading *root;
  spvsx_parse_root_heading (&ctx, xmlDocGetRootElement (doc), &root);
  error = spvxml_context_finish (&ctx, &root->node_);

  if (!error && root->page_setup)
    spv->page_setup = decode_page_setup (root->page_setup, file_name);

  for (size_t i = 0; !error && i < root->n_seq; i++)
    error = spv_decode_children (spv, member_name, root->seq, root->n_seq,
                                 spv->root);

  if (error)
    {
      char *s = xasprintf ("%s: %s", member_name, error);
      free (error);
      error = s;
    }

  spvsx_free_root_heading (root);
  xmlFreeDoc (doc);

  return error;
}

struct spv_item *
spv_get_root (const struct spv_reader *spv)
{
  return spv->root;
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
  struct string zip_error;
  struct zip_reader *zip = zip_reader_create (filename, &zip_error);
  if (!zip)
    return ds_steal_cstr (&zip_error);

  char *error;
  if (spv_detect__ (zip, &error) <= 0 && !error)
    error = xasprintf("%s: not an SPV file", filename);
  zip_reader_destroy (zip);
  ds_destroy (&zip_error);
  return error;
}

char * WARN_UNUSED_RESULT
spv_open (const char *filename, struct spv_reader **spvp)
{
  *spvp = NULL;

  struct spv_reader *spv = xzalloc (sizeof *spv);
  ds_init_empty (&spv->zip_errs);
  spv->zip = zip_reader_create (filename, &spv->zip_errs);
  if (!spv->zip)
    {
      char *error = ds_steal_cstr (&spv->zip_errs);
      spv_close (spv);
      return error;
    }

  char *error;
  int detect = spv_detect__ (spv->zip, &error);
  if (detect <= 0)
    {
      spv_close (spv);
      return error ? error : xasprintf("%s: not an SPV file", filename);
    }

  spv->root = xzalloc (sizeof *spv->root);
  spv->root->spv = spv;
  spv->root->type = SPV_ITEM_HEADING;
  for (size_t i = 0; ; i++)
    {
      const char *member_name = zip_reader_get_member_name (spv->zip, i);
      if (!member_name)
        break;

      struct substring member_name_ss = ss_cstr (member_name);
      if (ss_starts_with (member_name_ss, ss_cstr ("outputViewer"))
          && ss_ends_with (member_name_ss, ss_cstr (".xml")))
        {
          char *error = spv_heading_read (spv, filename, member_name);
          if (error)
            {
              spv_close (spv);
              return error;
            }
        }
    }

  *spvp = spv;
  return NULL;
}

void
spv_close (struct spv_reader *spv)
{
  if (spv)
    {
      ds_destroy (&spv->zip_errs);
      zip_reader_destroy (spv->zip);
      spv_item_destroy (spv->root);
      page_setup_destroy (spv->page_setup);
      free (spv);
    }
}

void
spv_item_set_table_look (struct spv_item *item,
                         const struct pivot_table_look *look)
{
  /* If this is a table, install the table look in it.

     (We can't just set item->table_look because light tables ignore it and
     legacy tables sometimes override it.) */
  if (spv_item_is_table (item))
    {
      spv_item_load (item);
      pivot_table_set_look (item->table, look);
    }

  for (size_t i = 0; i < item->n_children; i++)
    spv_item_set_table_look (item->children[i], look);
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
