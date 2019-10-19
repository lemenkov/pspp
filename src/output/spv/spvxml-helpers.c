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

#include "output/spv/spvxml-helpers.h"

#include <errno.h>
#include <float.h>
#include <string.h>

#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/str.h"

#include "gl/xvasprintf.h"

char * WARN_UNUSED_RESULT
spvxml_context_finish (struct spvxml_context *ctx, struct spvxml_node *root)
{
  if (!ctx->error)
    root->class_->spvxml_node_collect_ids (ctx, root);
  if (!ctx->error)
    root->class_->spvxml_node_resolve_refs (ctx, root);

  hmap_destroy (&ctx->id_map);

  return ctx->error;
}

void
spvxml_node_context_uninit (struct spvxml_node_context *nctx)
{
  for (struct spvxml_attribute *a = nctx->attrs;
       a < &nctx->attrs[nctx->n_attrs]; a++)
    free (a->value);
}

static const char *
xml_element_type_to_string (xmlElementType type)
{
  switch (type)
    {
    case XML_ELEMENT_NODE: return "element";
    case XML_ATTRIBUTE_NODE: return "attribute";
    case XML_TEXT_NODE: return "text";
    case XML_CDATA_SECTION_NODE: return "CDATA section";
    case XML_ENTITY_REF_NODE: return "entity reference";
    case XML_ENTITY_NODE: return "entity";
    case XML_PI_NODE: return "PI";
    case XML_COMMENT_NODE: return "comment";
    case XML_DOCUMENT_NODE: return "document";
    case XML_DOCUMENT_TYPE_NODE: return "document type";
    case XML_DOCUMENT_FRAG_NODE: return "document fragment";
    case XML_NOTATION_NODE: return "notation";
    case XML_HTML_DOCUMENT_NODE: return "HTML document";
    case XML_DTD_NODE: return "DTD";
    case XML_ELEMENT_DECL: return "element declaration";
    case XML_ATTRIBUTE_DECL: return "attribute declaration";
    case XML_ENTITY_DECL: return "entity declaration";
    case XML_NAMESPACE_DECL: return "namespace declaration";
    case XML_XINCLUDE_START: return "XInclude start";
    case XML_XINCLUDE_END: return "XInclude end";
    case XML_DOCB_DOCUMENT_NODE: return "docb document";
    default: return "<error>";
    }
}

static void
spvxml_format_node_path (const xmlNode *node, struct string *s)
{
  enum { MAX_STACK = 32 };
  const xmlNode *stack[MAX_STACK];
  size_t n = 0;

  while (node != NULL && node->type != XML_DOCUMENT_NODE && n < MAX_STACK)
    {
      stack[n++] = node;
      node = node->parent;
    }

  while (n > 0)
    {
      node = stack[--n];
      ds_put_byte (s, '/');
      if (node->name)
        ds_put_cstr (s, CHAR_CAST (char *, node->name));
      if (node->type == XML_ELEMENT_NODE)
        {
          if (node->parent)
            {
              size_t total = 1;
              size_t index = 1;
              for (const xmlNode *sibling = node->parent->children;
                   sibling; sibling = sibling->next)
                {
                  if (sibling == node)
                    index = total;
                  else if (sibling->type == XML_ELEMENT_NODE
                           && !strcmp (CHAR_CAST (char *, sibling->name),
                                       CHAR_CAST (char *, node->name)))
                    total++;
                }
              if (total > 1)
                ds_put_format (s, "[%zu]", index);
            }
        }
      else
        ds_put_format (s, "(%s)", xml_element_type_to_string (node->type));
    }
}

static struct spvxml_node *
spvxml_node_find (struct spvxml_context *ctx, const char *name,
                  unsigned int hash)
{
  struct spvxml_node *node;
  HMAP_FOR_EACH_WITH_HASH (node, struct spvxml_node, id_node, hash,
                           &ctx->id_map)
    if (!strcmp (node->id, name))
      return node;

  return NULL;
}

void
spvxml_node_collect_id (struct spvxml_context *ctx, struct spvxml_node *node)
{
  if (!node->id)
    return;

  unsigned int hash = hash_string (node->id, 0);
  struct spvxml_node *other = spvxml_node_find (ctx, node->id, hash);
  if (other)
    {
      if (!ctx->error)
        {
          struct string node_path = DS_EMPTY_INITIALIZER;
          spvxml_format_node_path (node->raw, &node_path);

          struct string other_path = DS_EMPTY_INITIALIZER;
          spvxml_format_node_path (other->raw, &other_path);

          ctx->error = xasprintf ("Nodes %s and %s both have ID \"%s\".",
                                  ds_cstr (&node_path),
                                  ds_cstr (&other_path), node->id);

          ds_destroy (&node_path);
          ds_destroy (&other_path);
        }

      return;
    }

  hmap_insert (&ctx->id_map, &node->id_node, hash);
}

struct spvxml_node *
spvxml_node_resolve_ref (struct spvxml_context *ctx,
                         const xmlNode *src, const char *attr_name,
                         const struct spvxml_node_class *const *classes,
                         size_t n)
{
  char *dst_id = CHAR_CAST (
    char *, xmlGetProp (CONST_CAST (xmlNode *, src),
                        CHAR_CAST (xmlChar *, attr_name)));
  if (!dst_id)
    return NULL;

  struct spvxml_node *dst = spvxml_node_find (ctx, dst_id,
                                              hash_string (dst_id, 0));
  if (!dst)
    {
      struct string node_path = DS_EMPTY_INITIALIZER;
      spvxml_format_node_path (src, &node_path);

      ctx->error = xasprintf (
        "%s: Attribute %s has unknown target ID \"%s\".",
        ds_cstr (&node_path), attr_name, dst_id);

      ds_destroy (&node_path);
      free (dst_id);
      return NULL;
    }

  if (!n)
    {
      free (dst_id);
      return dst;
    }
  for (size_t i = 0; i < n; i++)
    if (classes[i] == dst->class_)
      {
        free (dst_id);
        return dst;
      }

  if (!ctx->error)
    {
      struct string s = DS_EMPTY_INITIALIZER;
      spvxml_format_node_path (src, &s);

      ds_put_format (&s, ": Attribute \"%s\" should refer to a \"%s\"",
                     attr_name, classes[0]->name);
      if (n == 2)
        ds_put_format (&s, " or \"%s\"", classes[1]->name);
      else if (n > 2)
        {
          for (size_t i = 1; i < n - 1; i++)
            ds_put_format (&s, ", \"%s\"", classes[i]->name);
          ds_put_format (&s, ", or \"%s\"", classes[n - 1]->name);
        }
      ds_put_format (&s, " element, but its target ID \"%s\" "
                     "actually refers to a \"%s\" element.",
                     dst_id, dst->class_->name);

      ctx->error = ds_steal_cstr (&s);
    }

  free (dst_id);
  return NULL;
}

void PRINTF_FORMAT (2, 3)
spvxml_attr_error (struct spvxml_node_context *nctx, const char *format, ...)
{
  if (nctx->up->error)
    return;

  struct string s = DS_EMPTY_INITIALIZER;
  ds_put_cstr (&s, "error parsing attributes of ");
  spvxml_format_node_path (nctx->parent, &s);

  va_list args;
  va_start (args, format);
  ds_put_cstr (&s, ": ");
  ds_put_vformat (&s, format, args);
  va_end (args);

  nctx->up->error = ds_steal_cstr (&s);
}

/* xmlGetPropNodeValueInternal() is from tree.c in libxml2 2.9.4+dfsg1, which
   is covered by the following copyright and license:

   Except where otherwise noted in the source code (e.g. the files hash.c,
   list.c and the trio files, which are covered by a similar licence but with
   different Copyright notices) all the files are:

   Copyright (C) 1998-2012 Daniel Veillard.  All Rights Reserved.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to
   deal in the Software without restriction, including without limitation the
   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
   sell copies of the Software, and to permit persons to whom the Software is
   fur- nished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FIT- NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
   IN THE SOFTWARE.
*/
static xmlChar*
xmlGetPropNodeValueInternal(const xmlAttr *prop)
{
    if (prop == NULL)
        return(NULL);
    if (prop->type == XML_ATTRIBUTE_NODE) {
        /*
        * Note that we return at least the empty string.
        *   TODO: Do we really always want that?
        */
        if (prop->children != NULL) {
            if ((prop->children->next == NULL) &&
                ((prop->children->type == XML_TEXT_NODE) ||
                (prop->children->type == XML_CDATA_SECTION_NODE)))
            {
                /*
                * Optimization for the common case: only 1 text node.
                */
                return(xmlStrdup(prop->children->content));
            } else {
                xmlChar *ret;

                ret = xmlNodeListGetString(prop->doc, prop->children, 1);
                if (ret != NULL)
                    return(ret);
            }
        }
        return(xmlStrdup((xmlChar *)""));
    } else if (prop->type == XML_ATTRIBUTE_DECL) {
        return(xmlStrdup(((xmlAttributePtr)prop)->defaultValue));
    }
    return(NULL);
}

static struct spvxml_attribute *
find_attribute (struct spvxml_node_context *nctx, const char *name)
{
  /* XXX This is linear search but we could use binary search. */
  for (struct spvxml_attribute *a = nctx->attrs;
       a < &nctx->attrs[nctx->n_attrs]; a++)
    if (!strcmp (a->name, name))
      return a;

  return NULL;
}

static void
format_attribute (struct string *s, const xmlAttr *attr)
{
  const char *name = CHAR_CAST (char *, attr->name);
  char *value = CHAR_CAST (char *, xmlGetPropNodeValueInternal (attr));
  ds_put_format (s, "%s=\"%s\"", name, value);
  free (value);
}

void
spvxml_parse_attributes (struct spvxml_node_context *nctx)
{
  for (const xmlAttr *node = nctx->parent->properties; node; node = node->next)
    {
      const char *node_name = CHAR_CAST (char *, node->name);
      struct spvxml_attribute *a = find_attribute (nctx, node_name);
      if (!a)
        {
          if (!strcmp (node_name, "id"))
            continue;

          struct string unexpected = DS_EMPTY_INITIALIZER;
          format_attribute (&unexpected, node);
          int n = 1;

          for (node = node->next; node; node = node->next)
            {
              node_name = CHAR_CAST (char *, node->name);
              if (!find_attribute (nctx, node_name)
                  && strcmp (node_name, "id"))
                {
                  ds_put_byte (&unexpected, ' ');
                  format_attribute (&unexpected, node);
                  n++;
                }
            }

          spvxml_attr_error (nctx, "Node has unexpected attribute%s: %s",
                             n > 1 ? "s" : "", ds_cstr (&unexpected));
          ds_destroy (&unexpected);
          return;
        }
      if (a->value)
        {
          spvxml_attr_error (nctx, "Duplicate attribute \"%s\".", a->name);
          return;
        }
      a->value = CHAR_CAST (char *, xmlGetPropNodeValueInternal (node));
    }

  for (struct spvxml_attribute *a = nctx->attrs;
       a < &nctx->attrs[nctx->n_attrs]; a++)
    {
      if (a->required && !a->value)
        spvxml_attr_error (nctx, "Missing required attribute \"%s\".",
                           a->name);
      return;
    }
}

int
spvxml_attr_parse_enum (struct spvxml_node_context *nctx,
                        const struct spvxml_attribute *a,
                        const struct spvxml_enum enums[])
{
  if (!a->value)
    return 0;

  for (const struct spvxml_enum *e = enums; e->name; e++)
    if (!strcmp (a->value, e->name))
      return e->value;

  for (const struct spvxml_enum *e = enums; e->name; e++)
    if (!strcmp (e->name, "OTHER"))
      return e->value;

  spvxml_attr_error (nctx, "Attribute %s has unexpected value \"%s\".",
                a->name, a->value);
  return 0;
}

int
spvxml_attr_parse_bool (struct spvxml_node_context *nctx,
                        const struct spvxml_attribute *a)
{
  static const struct spvxml_enum bool_enums[] = {
    { "true", 1 },
    { "false", 0 },
    { NULL, 0 },
  };

  return !a->value ? -1 : spvxml_attr_parse_enum (nctx, a, bool_enums);
}

bool
spvxml_attr_parse_fixed (struct spvxml_node_context *nctx,
                         const struct spvxml_attribute *a,
                         const char *attr_value)
{
  const struct spvxml_enum fixed_enums[] = {
    { attr_value, true },
    { NULL, 0 },
  };

  return spvxml_attr_parse_enum (nctx, a, fixed_enums);
}

int
spvxml_attr_parse_int (struct spvxml_node_context *nctx,
                       const struct spvxml_attribute *a)
{
  if (!a->value)
    return INT_MIN;

  char *tail = NULL;
  int save_errno = errno;
  errno = 0;
  long int integer = strtol (a->value, &tail, 10);
  if (errno || *tail || integer <= INT_MIN || integer > INT_MAX)
    {
      spvxml_attr_error (nctx, "Attribute %s has unexpected value "
                         "\"%s\" expecting small integer.", a->name, a->value);
      integer = INT_MIN;
    }
  errno = save_errno;

  return integer;
}

static int
lookup_color_name (const char *s)
{
  struct color
    {
      struct hmap_node hmap_node;
      const char *name;
      int code;
    };

  static struct color colors[] =
    {
      { .name = "aliceblue", .code = 0xf0f8ff },
      { .name = "antiquewhite", .code = 0xfaebd7 },
      { .name = "aqua", .code = 0x00ffff },
      { .name = "aquamarine", .code = 0x7fffd4 },
      { .name = "azure", .code = 0xf0ffff },
      { .name = "beige", .code = 0xf5f5dc },
      { .name = "bisque", .code = 0xffe4c4 },
      { .name = "black", .code = 0x000000 },
      { .name = "blanchedalmond", .code = 0xffebcd },
      { .name = "blue", .code = 0x0000ff },
      { .name = "blueviolet", .code = 0x8a2be2 },
      { .name = "brown", .code = 0xa52a2a },
      { .name = "burlywood", .code = 0xdeb887 },
      { .name = "cadetblue", .code = 0x5f9ea0 },
      { .name = "chartreuse", .code = 0x7fff00 },
      { .name = "chocolate", .code = 0xd2691e },
      { .name = "coral", .code = 0xff7f50 },
      { .name = "cornflowerblue", .code = 0x6495ed },
      { .name = "cornsilk", .code = 0xfff8dc },
      { .name = "crimson", .code = 0xdc143c },
      { .name = "cyan", .code = 0x00ffff },
      { .name = "darkblue", .code = 0x00008b },
      { .name = "darkcyan", .code = 0x008b8b },
      { .name = "darkgoldenrod", .code = 0xb8860b },
      { .name = "darkgray", .code = 0xa9a9a9 },
      { .name = "darkgreen", .code = 0x006400 },
      { .name = "darkgrey", .code = 0xa9a9a9 },
      { .name = "darkkhaki", .code = 0xbdb76b },
      { .name = "darkmagenta", .code = 0x8b008b },
      { .name = "darkolivegreen", .code = 0x556b2f },
      { .name = "darkorange", .code = 0xff8c00 },
      { .name = "darkorchid", .code = 0x9932cc },
      { .name = "darkred", .code = 0x8b0000 },
      { .name = "darksalmon", .code = 0xe9967a },
      { .name = "darkseagreen", .code = 0x8fbc8f },
      { .name = "darkslateblue", .code = 0x483d8b },
      { .name = "darkslategray", .code = 0x2f4f4f },
      { .name = "darkslategrey", .code = 0x2f4f4f },
      { .name = "darkturquoise", .code = 0x00ced1 },
      { .name = "darkviolet", .code = 0x9400d3 },
      { .name = "deeppink", .code = 0xff1493 },
      { .name = "deepskyblue", .code = 0x00bfff },
      { .name = "dimgray", .code = 0x696969 },
      { .name = "dimgrey", .code = 0x696969 },
      { .name = "dodgerblue", .code = 0x1e90ff },
      { .name = "firebrick", .code = 0xb22222 },
      { .name = "floralwhite", .code = 0xfffaf0 },
      { .name = "forestgreen", .code = 0x228b22 },
      { .name = "fuchsia", .code = 0xff00ff },
      { .name = "gainsboro", .code = 0xdcdcdc },
      { .name = "ghostwhite", .code = 0xf8f8ff },
      { .name = "gold", .code = 0xffd700 },
      { .name = "goldenrod", .code = 0xdaa520 },
      { .name = "gray", .code = 0x808080 },
      { .name = "green", .code = 0x008000 },
      { .name = "greenyellow", .code = 0xadff2f },
      { .name = "grey", .code = 0x808080 },
      { .name = "honeydew", .code = 0xf0fff0 },
      { .name = "hotpink", .code = 0xff69b4 },
      { .name = "indianred", .code = 0xcd5c5c },
      { .name = "indigo", .code = 0x4b0082 },
      { .name = "ivory", .code = 0xfffff0 },
      { .name = "khaki", .code = 0xf0e68c },
      { .name = "lavender", .code = 0xe6e6fa },
      { .name = "lavenderblush", .code = 0xfff0f5 },
      { .name = "lawngreen", .code = 0x7cfc00 },
      { .name = "lemonchiffon", .code = 0xfffacd },
      { .name = "lightblue", .code = 0xadd8e6 },
      { .name = "lightcoral", .code = 0xf08080 },
      { .name = "lightcyan", .code = 0xe0ffff },
      { .name = "lightgoldenrodyellow", .code = 0xfafad2 },
      { .name = "lightgray", .code = 0xd3d3d3 },
      { .name = "lightgreen", .code = 0x90ee90 },
      { .name = "lightgrey", .code = 0xd3d3d3 },
      { .name = "lightpink", .code = 0xffb6c1 },
      { .name = "lightsalmon", .code = 0xffa07a },
      { .name = "lightseagreen", .code = 0x20b2aa },
      { .name = "lightskyblue", .code = 0x87cefa },
      { .name = "lightslategray", .code = 0x778899 },
      { .name = "lightslategrey", .code = 0x778899 },
      { .name = "lightsteelblue", .code = 0xb0c4de },
      { .name = "lightyellow", .code = 0xffffe0 },
      { .name = "lime", .code = 0x00ff00 },
      { .name = "limegreen", .code = 0x32cd32 },
      { .name = "linen", .code = 0xfaf0e6 },
      { .name = "magenta", .code = 0xff00ff },
      { .name = "maroon", .code = 0x800000 },
      { .name = "mediumaquamarine", .code = 0x66cdaa },
      { .name = "mediumblue", .code = 0x0000cd },
      { .name = "mediumorchid", .code = 0xba55d3 },
      { .name = "mediumpurple", .code = 0x9370db },
      { .name = "mediumseagreen", .code = 0x3cb371 },
      { .name = "mediumslateblue", .code = 0x7b68ee },
      { .name = "mediumspringgreen", .code = 0x00fa9a },
      { .name = "mediumturquoise", .code = 0x48d1cc },
      { .name = "mediumvioletred", .code = 0xc71585 },
      { .name = "midnightblue", .code = 0x191970 },
      { .name = "mintcream", .code = 0xf5fffa },
      { .name = "mistyrose", .code = 0xffe4e1 },
      { .name = "moccasin", .code = 0xffe4b5 },
      { .name = "navajowhite", .code = 0xffdead },
      { .name = "navy", .code = 0x000080 },
      { .name = "oldlace", .code = 0xfdf5e6 },
      { .name = "olive", .code = 0x808000 },
      { .name = "olivedrab", .code = 0x6b8e23 },
      { .name = "orange", .code = 0xffa500 },
      { .name = "orangered", .code = 0xff4500 },
      { .name = "orchid", .code = 0xda70d6 },
      { .name = "palegoldenrod", .code = 0xeee8aa },
      { .name = "palegreen", .code = 0x98fb98 },
      { .name = "paleturquoise", .code = 0xafeeee },
      { .name = "palevioletred", .code = 0xdb7093 },
      { .name = "papayawhip", .code = 0xffefd5 },
      { .name = "peachpuff", .code = 0xffdab9 },
      { .name = "peru", .code = 0xcd853f },
      { .name = "pink", .code = 0xffc0cb },
      { .name = "plum", .code = 0xdda0dd },
      { .name = "powderblue", .code = 0xb0e0e6 },
      { .name = "purple", .code = 0x800080 },
      { .name = "red", .code = 0xff0000 },
      { .name = "rosybrown", .code = 0xbc8f8f },
      { .name = "royalblue", .code = 0x4169e1 },
      { .name = "saddlebrown", .code = 0x8b4513 },
      { .name = "salmon", .code = 0xfa8072 },
      { .name = "sandybrown", .code = 0xf4a460 },
      { .name = "seagreen", .code = 0x2e8b57 },
      { .name = "seashell", .code = 0xfff5ee },
      { .name = "sienna", .code = 0xa0522d },
      { .name = "silver", .code = 0xc0c0c0 },
      { .name = "skyblue", .code = 0x87ceeb },
      { .name = "slateblue", .code = 0x6a5acd },
      { .name = "slategray", .code = 0x708090 },
      { .name = "slategrey", .code = 0x708090 },
      { .name = "snow", .code = 0xfffafa },
      { .name = "springgreen", .code = 0x00ff7f },
      { .name = "steelblue", .code = 0x4682b4 },
      { .name = "tan", .code = 0xd2b48c },
      { .name = "teal", .code = 0x008080 },
      { .name = "thistle", .code = 0xd8bfd8 },
      { .name = "tomato", .code = 0xff6347 },
      { .name = "turquoise", .code = 0x40e0d0 },
      { .name = "violet", .code = 0xee82ee },
      { .name = "wheat", .code = 0xf5deb3 },
      { .name = "white", .code = 0xffffff },
      { .name = "whitesmoke", .code = 0xf5f5f5 },
      { .name = "yellow", .code = 0xffff00 },
      { .name = "yellowgreen", .code = 0x9acd32 },
    };

  static struct hmap color_table = HMAP_INITIALIZER (color_table);

  if (hmap_is_empty (&color_table))
    for (size_t i = 0; i < sizeof colors / sizeof *colors; i++)
      hmap_insert (&color_table, &colors[i].hmap_node,
                   hash_string (colors[i].name, 0));

  const struct color *color;
  HMAP_FOR_EACH_WITH_HASH (color, struct color, hmap_node,
                           hash_string (s, 0), &color_table)
    if (!strcmp (color->name, s))
      return color->code;
  return -1;
}

int
spvxml_attr_parse_color (struct spvxml_node_context *nctx,
                         const struct spvxml_attribute *a)
{
  if (!a->value || !strcmp (a->value, "transparent"))
    return -1;

  int r, g, b;
  if (sscanf (a->value, "#%2x%2x%2x", &r, &g, &b) == 3
      || sscanf (a->value, "%2x%2x%2x", &r, &g, &b) == 3)
    return (r << 16) | (g << 8) | b;

  int code = lookup_color_name (a->value);
  if (code >= 0)
    return code;

  spvxml_attr_error (nctx, "Attribute %s has unexpected value "
                     "\"%s\" expecting #rrggbb or rrggbb or web color name.",
                     a->name, a->value);
  return 0;
}

static bool
try_strtod (char *s, char **tail, double *real)
{
  char *comma = strchr (s, ',');
  if (comma)
    *comma = '.';

  int save_errno = errno;
  errno = 0;
  *tail = NULL;
  *real = strtod (s, tail);
  bool ok = errno == 0;
  errno = save_errno;

  if (!ok)
    *real = DBL_MAX;
  return ok;
}

double
spvxml_attr_parse_real (struct spvxml_node_context *nctx,
                        const struct spvxml_attribute *a)
{
  if (!a->value)
    return DBL_MAX;

  char *tail;
  double real;
  if (!try_strtod (a->value, &tail, &real) || *tail)
    spvxml_attr_error (nctx, "Attribute %s has unexpected value "
                       "\"%s\" expecting real number.", a->name, a->value);

  return real;
}

double
spvxml_attr_parse_dimension (struct spvxml_node_context *nctx,
                             const struct spvxml_attribute *a)
{
  if (!a->value)
    return DBL_MAX;

  char *tail;
  double real;
  if (!try_strtod (a->value, &tail, &real))
    goto error;

  tail += strspn (tail, " \t\r\n");

  struct unit
    {
      const char *name;
      double divisor;
    };
  static const struct unit units[] = {

/* If you add anything to this table, update the table in
   doc/dev/spv-file-format.texi also.  */

    /* Inches. */
    { "in", 1.0 },
    { "인치", 1.0 },
    { "pol.", 1.0 },
    { "cala", 1.0 },
    { "cali", 1.0 },

    /* Device-independent pixels. */
    { "px", 96.0 },

    /* Points. */
    { "pt", 72.0 },
    { "пт", 72.0 },
    { "", 72.0 },

    /* Centimeters. */
    { "cm", 2.54 },
    { "см", 2.54 },
  };

  for (size_t i = 0; i < sizeof units / sizeof *units; i++)
    if (!strcmp (units[i].name, tail))
      return real / units[i].divisor;
  goto error;

error:
  spvxml_attr_error (nctx, "Attribute %s has unexpected value "
                     "\"%s\" expecting dimension.", a->name, a->value);
  return DBL_MAX;
}

struct spvxml_node *
spvxml_attr_parse_ref (struct spvxml_node_context *nctx UNUSED,
                       const struct spvxml_attribute *a UNUSED)
{
  return NULL;
}

void PRINTF_FORMAT (3, 4)
spvxml_content_error (struct spvxml_node_context *nctx, const xmlNode *node,
                      const char *format, ...)
{
  if (nctx->up->error)
    return;

  struct string s = DS_EMPTY_INITIALIZER;

  ds_put_cstr (&s, "error parsing content of ");
  spvxml_format_node_path (nctx->parent, &s);

  if (node)
    {
      ds_put_format (&s, " at %s", xml_element_type_to_string (node->type));
      if (node->name)
        ds_put_format (&s, " \"%s\"", node->name);
    }
  else
    ds_put_format (&s, " at end of content");

  va_list args;
  va_start (args, format);
  ds_put_cstr (&s, ": ");
  ds_put_vformat (&s, format, args);
  va_end (args);

  //puts (ds_cstr (&s));

  nctx->up->error = ds_steal_cstr (&s);
}

bool
spvxml_content_parse_element (struct spvxml_node_context *nctx,
                              xmlNode **nodep,
                              const char *elem_name, xmlNode **outp)
{
  xmlNode *node = *nodep;
  while (node)
    {
      if (node->type == XML_ELEMENT_NODE
          && (!strcmp (CHAR_CAST (char *, node->name), elem_name)
              || !strcmp (elem_name, "any")))
        {
          *outp = node;
          *nodep = node->next;
          return true;
        }
      else if (node->type != XML_COMMENT_NODE)
        break;

      node = node->next;
    }

  spvxml_content_error (nctx, node, "\"%s\" element expected.", elem_name);
  *outp = NULL;
  return false;
}

bool
spvxml_content_parse_text (struct spvxml_node_context *nctx UNUSED, xmlNode **nodep,
                           char **textp)
{
  struct string text = DS_EMPTY_INITIALIZER;

  xmlNode *node = *nodep;
  while (node)
    {
      if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE)
        {
          char *segment = CHAR_CAST (char *, xmlNodeGetContent (node));
          if (!text.ss.string)
            {
              text.ss = ss_cstr (segment);
              text.capacity = text.ss.length;
            }
          else
            {
              ds_put_cstr (&text, segment);
              free (segment);
            }
        }
      else if (node->type != XML_COMMENT_NODE)
        break;

      node = node->next;
    }
  *nodep = node;

  *textp = ds_steal_cstr (&text);

  return true;
}

bool
spvxml_content_parse_end (struct spvxml_node_context *nctx, xmlNode *node)
{
  for (;;)
    {
      if (!node)
        return true;
      else if (node->type != XML_COMMENT_NODE)
        break;

      node = node->next;
    }

  struct string s = DS_EMPTY_INITIALIZER;

  for (int i = 0; i < 4 && node; i++, node = node->next)
    {
      if (i)
        ds_put_cstr (&s, ", ");
      ds_put_cstr (&s, xml_element_type_to_string (node->type));
      if (node->name)
        ds_put_format (&s, " \"%s\"", node->name);
    }
  if (node)
    ds_put_format (&s, ", ...");

  spvxml_content_error (nctx, node, "Extra content found expecting end: %s",
                        ds_cstr (&s));
  ds_destroy (&s);

  return false;
}

