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
#include "output/options.h"
#include "output/table.h"

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

int
spvxml_attr_parse_color (struct spvxml_node_context *nctx,
                         const struct spvxml_attribute *a)
{
  if (!a->value || !strcmp (a->value, "transparent"))
    return -1;

  struct cell_color color;
  if (parse_color__ (a->value, &color))
    return (color.r << 16) | (color.g << 8) | color.b;

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

