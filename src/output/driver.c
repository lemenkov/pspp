/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2011, 2012, 2014, 2019 Free Software Foundation, Inc.

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

#include "output/driver.h"
#include "output/driver-provider.h"

#include <ctype.h>
#include <errno.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "data/file-handle-def.h"
#include "data/settings.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/llx.h"
#include "libpspp/string-map.h"
#include "libpspp/string-set.h"
#include "libpspp/str.h"
#include "output/group-item.h"
#include "output/message-item.h"
#include "output/output-item.h"
#include "output/text-item.h"

#include "gl/error.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct output_engine
  {
    struct ll ll;                  /* Node for this engine. */
    struct llx_list drivers;       /* Contains "struct output_driver"s. */
    struct text_item *deferred_text;   /* Output text being accumulated. */
    char *command_name;            /* Name of command being processed. */
    char *title, *subtitle;        /* Components of page title. */

    /* Output grouping stack.

       TEXT_ITEM_GROUP_OPEN pushes a group on the stack and
       TEXT_ITEM_GROUP_CLOSE pops one off. */
    char **groups;               /* Command names of nested sections. */
    size_t n_groups;
    size_t allocated_groups;

    struct string_map heading_vars;
  };

static struct ll_list engine_stack = LL_INITIALIZER (engine_stack);

static const struct output_driver_factory *factories[];

static struct output_engine *
engine_stack_top (void)
{
  struct ll *head = ll_head (&engine_stack);
  if (ll_is_empty (&engine_stack))
    return NULL;
  return ll_data (head, struct output_engine, ll);
}

static void
put_strftime (const char *key, const char *format,
              const struct tm *tm, struct string_map *vars)
{
  if (!string_map_find (vars, key))
    {
      char value[128];
      strftime (value, sizeof value, format, tm);
      string_map_insert (vars, key, value);
    }
}

void
output_engine_push (void)
{
  struct output_engine *e = xzalloc (sizeof (*e));

  llx_init (&e->drivers);

  string_map_init (&e->heading_vars);

  time_t t = time (NULL);
  const struct tm *tm = localtime (&t);
  put_strftime ("Date", "%x", tm, &e->heading_vars);
  put_strftime ("Time", "%X", tm, &e->heading_vars);

  ll_push_head (&engine_stack, &e->ll);
}

void
output_engine_pop (void)
{
  struct ll *head = ll_pop_head (&engine_stack);
  struct output_engine *e =ll_data (head, struct output_engine, ll);

  while (!llx_is_empty (&e->drivers))
    {
      struct output_driver *d = llx_pop_head (&e->drivers, &llx_malloc_mgr);
      output_driver_destroy (d);
    }
  text_item_unref (e->deferred_text);
  free (e->command_name);
  free (e->title);
  free (e->subtitle);
  for (size_t i = 0; i < e->n_groups; i++)
    free (e->groups[i]);
  free (e->groups);
  string_map_destroy (&e->heading_vars);
  free (e);
}

void
output_get_supported_formats (struct string_set *formats)
{
  const struct output_driver_factory **fp;

  for (fp = factories; *fp != NULL; fp++)
    string_set_insert (formats, (*fp)->extension);
}

static void
output_submit__ (struct output_engine *e, struct output_item *item)
{
  struct llx *llx, *next;

  for (llx = llx_head (&e->drivers); llx != llx_null (&e->drivers); llx = next)
    {
      struct output_driver *d = llx_data (llx);
      enum settings_output_type type;

      next = llx_next (llx);

      if (is_message_item (item))
        {
          const struct msg *m = message_item_get_msg (to_message_item (item));
          if (m->severity == MSG_S_NOTE)
            type = SETTINGS_OUTPUT_NOTE;
          else
            type = SETTINGS_OUTPUT_ERROR;
        }
      else if (is_text_item (item)
               && text_item_get_type (to_text_item (item)) == TEXT_ITEM_SYNTAX)
        type = SETTINGS_OUTPUT_SYNTAX;
      else
        type = SETTINGS_OUTPUT_RESULT;

      if (settings_get_output_routing (type) & d->device_type)
        d->class->submit (d, item);
    }

  output_item_unref (item);
}

static void
flush_deferred_text (struct output_engine *e)
{
  struct text_item *deferred_text = e->deferred_text;
  if (deferred_text)
    {
      e->deferred_text = NULL;
      output_submit__ (e, text_item_super (deferred_text));
    }
}

static bool
defer_text (struct output_engine *e, struct output_item *output_item)
{
  if (!is_text_item (output_item))
    return false;

  struct text_item *text = to_text_item (output_item);
  if (!e->deferred_text)
    e->deferred_text = text_item_unshare (text);
  else if (text_item_append (e->deferred_text, text))
    text_item_unref (text);
  else
    {
      flush_deferred_text (e);
      e->deferred_text = text_item_unshare (text);
    }
  return true;
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
output_submit (struct output_item *item)
{
  struct output_engine *e = engine_stack_top ();

  if (e == NULL)
    return;

  if (item == NULL)
    return;

  if (defer_text (e, item))
    return;
  flush_deferred_text (e);

  if (is_group_open_item (item))
    {
      const struct group_open_item *group_open_item
        = to_group_open_item (item);
      if (e->n_groups >= e->allocated_groups)
        e->groups = x2nrealloc (e->groups, &e->allocated_groups,
                                sizeof *e->groups);
      e->groups[e->n_groups] = (group_open_item->command_name
                                ? xstrdup (group_open_item->command_name)
                                : NULL);
      e->n_groups++;
    }
  else if (is_group_close_item (item))
    {
      assert (e->n_groups > 0);

      size_t idx = --e->n_groups;
      free (e->groups[idx]);
      if (idx >= 1 && idx <= 4)
        {
          char *key = xasprintf ("Head%zu", idx);
          free (string_map_find_and_delete (&e->heading_vars, key));
          free (key);
        }
    }
  else if (is_text_item (item))
    {
      const struct text_item *text_item = to_text_item (item);
      enum text_item_type type = text_item_get_type (text_item);
      const char *text = text_item_get_text (text_item);
      if (type == TEXT_ITEM_TITLE
          && e->n_groups >= 1 && e->n_groups <= 4)
        {
          char *key = xasprintf ("Head%zu", e->n_groups);
          string_map_replace (&e->heading_vars, key, text);
          free (key);
        }
      else if (type == TEXT_ITEM_PAGE_TITLE)
        string_map_replace (&e->heading_vars, "PageTitle", text);
    }

  output_submit__ (e, item);
}

/* Returns the name of the command currently being parsed, in all uppercase.
   The caller must free the returned value.

   Returns NULL if no command is being parsed. */
char *
output_get_command_name (void)
{
  struct output_engine *e = engine_stack_top ();
  if (e == NULL)
    return NULL;

  for (size_t i = e->n_groups; i-- > 0;)
    if (e->groups[i])
      return utf8_to_upper (e->groups[i]);

  return NULL;
}

/* Flushes output to screen devices, so that the user can see
   output that doesn't fill up an entire page. */
void
output_flush (void)
{
  struct output_engine *e = engine_stack_top ();
  struct llx *llx;

  flush_deferred_text (e);
  for (llx = llx_head (&e->drivers); llx != llx_null (&e->drivers);
       llx = llx_next (llx))
    {
      struct output_driver *d = llx_data (llx);
      if (d->device_type & SETTINGS_DEVICE_TERMINAL && d->class->flush != NULL)
        d->class->flush (d);
    }
}

static void
output_set_title__ (struct output_engine *e, char **dst, const char *src)
{
  free (*dst);
  *dst = src ? xstrdup (src) : NULL;

  char *page_title
    = (e->title && e->subtitle ? xasprintf ("%s\n%s", e->title, e->subtitle)
       : e->title ? xstrdup (e->title)
       : e->subtitle ? xstrdup (e->subtitle)
       : xzalloc (1));
  text_item_submit (text_item_create_nocopy (TEXT_ITEM_PAGE_TITLE,
                                             page_title, NULL));
}

void
output_set_title (const char *title)
{
  struct output_engine *e = engine_stack_top ();

  output_set_title__ (e, &e->title, title);
}

void
output_set_subtitle (const char *subtitle)
{
  struct output_engine *e = engine_stack_top ();

  output_set_title__ (e, &e->subtitle, subtitle);
}

void
output_set_filename (const char *filename)
{
  struct output_engine *e = engine_stack_top ();

  string_map_replace (&e->heading_vars, "Filename", filename);
}

size_t
output_get_group_level (void)
{
  struct output_engine *e = engine_stack_top ();

  return e->n_groups;
}

void
output_driver_init (struct output_driver *driver,
                    const struct output_driver_class *class,
                    const char *name, enum settings_output_devices type)
{
  driver->class = class;
  driver->name = xstrdup (name);
  driver->device_type = type;
}

void
output_driver_destroy (struct output_driver *driver)
{
  if (driver != NULL)
    {
      char *name = driver->name;
      if (output_driver_is_registered (driver))
        output_driver_unregister (driver);
      if (driver->class->destroy)
        driver->class->destroy (driver);
      free (name);
    }
}

const char *
output_driver_get_name (const struct output_driver *driver)
{
  return driver->name;
}

static struct output_engine *
output_driver_get_engine (const struct output_driver *driver)
{
  struct output_engine *e;

  ll_for_each (e, struct output_engine, ll, &engine_stack)
    {
      if (llx_find (llx_head (&e->drivers), llx_null (&e->drivers), driver))
	return e;
    }

  return NULL;
}

void
output_driver_register (struct output_driver *driver)
{
  struct output_engine *e = engine_stack_top ();

  assert (!output_driver_is_registered (driver));
  llx_push_tail (&e->drivers, driver, &llx_malloc_mgr);
}

void
output_driver_unregister (struct output_driver *driver)
{
  struct output_engine *e = output_driver_get_engine (driver);

  assert (e != NULL);
  llx_remove (llx_find (llx_head (&e->drivers), llx_null (&e->drivers), driver),
              &llx_malloc_mgr);
}

bool
output_driver_is_registered (const struct output_driver *driver)
{
  return output_driver_get_engine (driver) != NULL;
}

extern const struct output_driver_factory csv_driver_factory;
extern const struct output_driver_factory html_driver_factory;
extern const struct output_driver_factory list_driver_factory;
extern const struct output_driver_factory odt_driver_factory;
extern const struct output_driver_factory pdf_driver_factory;
extern const struct output_driver_factory png_driver_factory;
extern const struct output_driver_factory ps_driver_factory;
extern const struct output_driver_factory spv_driver_factory;
extern const struct output_driver_factory svg_driver_factory;
extern const struct output_driver_factory tex_driver_factory;
extern const struct output_driver_factory txt_driver_factory;

static const struct output_driver_factory *factories[] =
  {
    &txt_driver_factory,
    &list_driver_factory,
    &html_driver_factory,
    &csv_driver_factory,
    &odt_driver_factory,
    &spv_driver_factory,
    &pdf_driver_factory,
    &ps_driver_factory,
    &svg_driver_factory,
    &png_driver_factory,
    &tex_driver_factory,
    NULL
  };

static const struct output_driver_factory *
find_factory (const char *format)
{
  const struct output_driver_factory **fp;

  for (fp = factories; *fp != NULL; fp++)
    {
      const struct output_driver_factory *f = *fp;

      if (!strcmp (f->extension, format))
        return f;
    }
  return &txt_driver_factory;
}

static enum settings_output_devices
default_device_type (const char *file_name)
{
  return (!strcmp (file_name, "-")
          ? SETTINGS_DEVICE_TERMINAL
          : SETTINGS_DEVICE_LISTING);
}

struct output_driver *
output_driver_create (struct string_map *options)
{
  enum settings_output_devices device_type;
  const struct output_driver_factory *f;
  struct output_driver *driver;
  char *device_string;
  char *file_name;
  char *format;

  format = string_map_find_and_delete (options, "format");
  file_name = string_map_find_and_delete (options, "output-file");

  if (format == NULL)
    {
      if (file_name != NULL)
        {
          const char *extension = strrchr (file_name, '.');
          format = xstrdup (extension != NULL ? extension + 1 : "");
        }
      else
        format = xstrdup ("txt");
    }
  f = find_factory (format);

  if (file_name == NULL)
    file_name = xstrdup (f->default_file_name);

  /* XXX should use parse_enum(). */
  device_string = string_map_find_and_delete (options, "device");
  if (device_string == NULL || device_string[0] == '\0')
    device_type = default_device_type (file_name);
  else if (!strcmp (device_string, "terminal"))
    device_type = SETTINGS_DEVICE_TERMINAL;
  else if (!strcmp (device_string, "listing"))
    device_type = SETTINGS_DEVICE_LISTING;
  else
    {
      msg (MW, _("%s is not a valid device type (the choices are `%s' and `%s')"),
                     device_string, "terminal", "listing");
      device_type = default_device_type (file_name);
    }

  struct file_handle *fh = fh_create_file (NULL, file_name, NULL, fh_default_properties ());

  driver = f->create (fh, device_type, options);
  if (driver != NULL)
    {
      const struct string_map_node *node;
      const char *key;

      STRING_MAP_FOR_EACH_KEY (key, node, options)
        msg (MW, _("%s: unknown option `%s'"), file_name, key);
    }
  string_map_clear (options);

  free (file_name);
  free (format);
  free (device_string);

  return driver;
}

void
output_driver_parse_option (const char *option, struct string_map *options)
{
  const char *equals = strchr (option, '=');
  if (equals == NULL)
    {
      error (0, 0, _("%s: output option missing `='"), option);
      return;
    }

  char *key = xmemdup0 (option, equals - option);
  if (string_map_contains (options, key))
    {
      error (0, 0, _("%s: output option specified more than once"), key);
      free (key);
      return;
    }

  char *value = xmemdup0 (equals + 1, strlen (equals + 1));
  string_map_insert_nocopy (options, key, value);
}

/* Extracts the actual text content from the given Pango MARKUP and returns it
   as as a malloc()'d string. */
char *
output_get_text_from_markup (const char *markup)
{
  xmlParserCtxt *parser = xmlCreatePushParserCtxt (NULL, NULL, NULL, 0, NULL);
  if (!parser)
    return xstrdup (markup);

  xmlParseChunk (parser, "<xml>", strlen ("<xml>"), false);
  xmlParseChunk (parser, markup, strlen (markup), false);
  xmlParseChunk (parser, "</xml>", strlen ("</xml>"), true);

  char *content
    = (parser->wellFormed
       ? CHAR_CAST (char *,
                    xmlNodeGetContent (xmlDocGetRootElement (parser->myDoc)))
       : xstrdup (markup));
  xmlFreeDoc (parser->myDoc);
  xmlFreeParserCtxt (parser);

  return content;
}

char *
output_driver_substitute_heading_vars (const char *src, int page_number)
{
  struct output_engine *e = engine_stack_top ();
  struct string dst = DS_EMPTY_INITIALIZER;
  ds_extend (&dst, strlen (src));
  for (const char *p = src; *p;)
    {
      if (!strncmp (p, "&amp;[", 6))
        {
          if (page_number != INT_MIN)
            {
              const char *start = p + 6;
              const char *end = strchr (start, ']');
              if (end)
                {
                  const char *value = string_map_find__ (&e->heading_vars,
                                                         start, end - start);
                  if (value)
                    ds_put_cstr (&dst, value);
                  else if (ss_equals (ss_buffer (start, end - start),
                                      ss_cstr ("Page")))
                    ds_put_format (&dst, "%d", page_number);
                  p = end + 1;
                  continue;
                }
            }
          ds_put_cstr (&dst, "&amp;");
          p += 5;
        }
      else
        ds_put_byte (&dst, *p++);
    }
  return ds_steal_cstr (&dst);
}
