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

#include <cairo.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <unistr.h>

#include "data/file-handle-def.h"
#include "data/settings.h"
#include "libpspp/encoding-guesser.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/string-map.h"
#include "libpspp/string-set.h"
#include "libpspp/zip-reader.h"
#include "output/driver.h"
#include "output/output-item.h"
#include "output/pivot-table.h"
#include "output/page-setup.h"
#include "output/select.h"
#include "output/spv/light-binary-parser.h"
#include "output/spv/spv-legacy-data.h"
#include "output/spv/spv-light-decoder.h"
#include "output/spv/spv-table-look.h"
#include "output/spv/spv.h"

#include "gl/c-ctype.h"
#include "gl/error.h"
#include "gl/progname.h"
#include "gl/version-etc.h"
#include "gl/xalloc.h"

#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* -O key=value: Output driver options. */
static struct string_map output_options
    = STRING_MAP_INITIALIZER (output_options);

/* --member-names: Include .zip member name in "dir" output. */
static bool show_member_names;

/* --show-hidden, --select, --commands, ...: Selection criteria. */
static struct output_criteria *criteria;
static size_t n_criteria, allocated_criteria;

/* --or: Add new element to 'criteria' array. */
static bool new_criteria;

/* --sort: Sort members under dump-light-table, to make comparisons easier. */
static bool sort;

/* --raw: Dump raw binary data in "dump-light-table"; dump all strings in
     "strings". */
static bool raw;

/* --no-ascii-only: Drop all-ASCII strings in "strings". */
static bool exclude_ascii_only;

/* --utf8-only: Only print strings that have UTF-8 multibyte sequences in
 * "strings". */
static bool include_utf8_only;

/* -f, --force: Keep output file even on error. */
static bool force;

/* --table-look: TableLook to replace table style for conversion. */
static struct pivot_table_look *table_look;

/* Number of warnings issued. */
static size_t n_warnings;

static void usage (void);
static void developer_usage (void);
static void parse_options (int argc, char **argv);

static struct output_item *
annotate_member_names (const struct output_item *in)
{
  if (in->type == OUTPUT_ITEM_GROUP)
    {
      struct output_item *out = group_item_clone_empty (in);
      for (size_t i = 0; i < in->group.n_children; i++)
        {
          const struct output_item *item = in->group.children[i];
          const char *members[4];
          size_t n = spv_info_get_members (item->spv_info, members,
                                           sizeof members / sizeof *members);
          if (n)
            {
              struct string s = DS_EMPTY_INITIALIZER;
              ds_put_cstr (&s, members[0]);
              for (size_t i = 1; i < n; i++)
                ds_put_format (&s, " and %s", members[i]);
              group_item_add_child (out, text_item_create_nocopy (
                                      TEXT_ITEM_TITLE, ds_steal_cstr (&s),
                                      xstrdup ("Member Names")));
            }

          group_item_add_child (out, output_item_ref (item));
        }
      return out;
    }
  else
    return output_item_ref (in);
}

static void
print_item_directory (const struct output_item *item, int level)
{
  for (int i = 0; i < level; i++)
    printf ("    ");

  printf ("- %s", output_item_type_to_string (item->type));

  const char *label = output_item_get_label (item);
  if (label)
    printf (" \"%s\"", label);

  if (item->type == OUTPUT_ITEM_TABLE)
    {
      char *title = pivot_value_to_string (item->table->title, item->table);
      if (!label || strcmp (title, label))
        printf (" title \"%s\"", title);
      free (title);
    }

  if (item->command_name)
    printf (" command \"%s\"", item->command_name);

  char *subtype = output_item_get_subtype (item);
  if (subtype)
    {
      if (!label || strcmp (label, subtype))
        printf (" subtype \"%s\"", subtype);
      free (subtype);
    }

  if (!item->show)
    printf (" (%s)", item->type == OUTPUT_ITEM_GROUP ? "collapsed" : "hidden");

  if (show_member_names)
    {
      const char *members[4];
      size_t n = spv_info_get_members (item->spv_info, members,
                                       sizeof members / sizeof *members);

      for (size_t i = 0; i < n; i++)
          printf (" %s %s", i == 0 ? "in" : "and", members[i]);
    }
  putchar ('\n');

  if (item->type == OUTPUT_ITEM_GROUP)
    for (size_t i = 0; i < item->group.n_children; i++)
      print_item_directory (item->group.children[i], level + 1);
}

static void
run_detect (int argc UNUSED, char **argv)
{
  char *err = spv_detect (argv[1]);
  if (err)
    error (1, 0, "%s", err);
}

static struct output_item *
read_and_filter_spv (const char *name, struct page_setup **psp)
{
  struct output_item *root;
  char *err = spv_read (name, &root, psp);
  if (err)
    error (1, 0, "%s", err);
  return output_select (root, criteria, n_criteria);
}

static void
run_directory (int argc UNUSED, char **argv)
{
  struct output_item *root = read_and_filter_spv (argv[1], NULL);
  for (size_t i = 0; i < root->group.n_children; i++)
    print_item_directory (root->group.children[i], 0);
  output_item_unref (root);
}

static void
set_table_look_recursively (struct output_item *item,
                            const struct pivot_table_look *look)
{
  if (item->type == OUTPUT_ITEM_TABLE)
    pivot_table_set_look (item->table, look);
  else if (item->type == OUTPUT_ITEM_GROUP)
    for (size_t i = 0; i < item->group.n_children; i++)
      set_table_look_recursively (item->group.children[i], look);
}

static void
run_convert (int argc UNUSED, char **argv)
{
  struct page_setup *ps;
  struct output_item *root = read_and_filter_spv (argv[1], &ps);
  if (table_look)
    set_table_look_recursively (root, table_look);
  if (show_member_names)
    {
      struct output_item *new_root = annotate_member_names (root);
      output_item_unref (root);
      root = new_root;
    }

  output_engine_push ();
  output_set_filename (argv[1]);
  string_map_replace (&output_options, "output-file", argv[2]);
  struct output_driver *driver = output_driver_create (&output_options);
  if (!driver)
    exit (EXIT_FAILURE);
  output_driver_register (driver);

  if (ps)
    {
      output_item_submit (page_setup_item_create (ps));
      page_setup_destroy (ps);
    }
  output_item_submit_children (root);

  output_engine_pop ();
  fh_done ();

  if (n_warnings && !force)
    {
      /* XXX There could be other files to unlink, e.g. the ascii driver can
         produce additional files with the charts. */
      unlink (argv[2]);
    }
}

static const struct pivot_table *
get_first_table (const struct output_item *item)
{
  if (item->type == OUTPUT_ITEM_TABLE)
    return item->table;
  else if (item->type == OUTPUT_ITEM_GROUP)
    for (size_t i = 0; i < item->group.n_children; i++)
      {
        const struct pivot_table *table
          = get_first_table (item->group.children[i]);
        if (table)
          return table;
      }

  return NULL;
}

static void
run_get_table_look (int argc UNUSED, char **argv)
{
  struct pivot_table_look *look;
  if (strcmp (argv[1], "-"))
    {
      struct output_item *root = read_and_filter_spv (argv[1], NULL);
      const struct pivot_table *table = get_first_table (root);
      if (!table)
        error (1, 0, "%s: no tables found", argv[1]);

      look = pivot_table_look_ref (pivot_table_get_look (table));

      output_item_unref (root);
    }
  else
    look = pivot_table_look_ref (pivot_table_look_builtin_default ());

  char *err = spv_table_look_write (argv[2], look);
  if (err)
    error (1, 0, "%s", err);

  pivot_table_look_unref (look);
}

static void
run_convert_table_look (int argc UNUSED, char **argv)
{
  struct pivot_table_look *look;
  char *err = spv_table_look_read (argv[1], &look);
  if (err)
    error (1, 0, "%s", err);

  err = spv_table_look_write (argv[2], look);
  if (err)
    error (1, 0, "%s", err);

  pivot_table_look_unref (look);
  free (look);
}

static void
run_dump (int argc UNUSED, char **argv)
{
  struct output_item *root = read_and_filter_spv (argv[1], NULL);
  output_item_dump (root, 0);
  output_item_unref (root);
}

static int
compare_borders (const void *a_, const void *b_)
{
  const struct spvlb_border *const *ap = a_;
  const struct spvlb_border *const *bp = b_;
  uint32_t a = (*ap)->border_type;
  uint32_t b = (*bp)->border_type;

  return a < b ? -1 : a > b;
}

static int
compare_cells (const void *a_, const void *b_)
{
  const struct spvlb_cell *const *ap = a_;
  const struct spvlb_cell *const *bp = b_;
  uint64_t a = (*ap)->index;
  uint64_t b = (*bp)->index;

  return a < b ? -1 : a > b;
}

static char * WARN_UNUSED_RESULT
dump_raw (struct zip_reader *zr, const char *member_name)
{
  void *data;
  size_t size;
  char *error = zip_member_read_all (zr, member_name, &data, &size);
  if (!error)
    {
      fwrite (data, size, 1, stdout);
      free (data);
    }
  return error;
}

static void
dump_light_table (const struct output_item *item)
{
  char *error;
  if (raw)
    error = dump_raw (item->spv_info->zip_reader,
                      item->spv_info->bin_member);
  else
    {
      struct spvlb_table *table;
      error = spv_read_light_table (item->spv_info->zip_reader,
                                    item->spv_info->bin_member, &table);
      if (!error)
        {
          if (sort)
            {
              qsort (table->borders->borders, table->borders->n_borders,
                     sizeof *table->borders->borders, compare_borders);
              qsort (table->cells->cells, table->cells->n_cells,
                     sizeof *table->cells->cells, compare_cells);
            }
          spvlb_print_table (item->spv_info->bin_member, 0, table);
          spvlb_free_table (table);
        }
    }
  if (error)
    {
      msg (ME, "%s", error);
      free (error);
    }
}

static void
run_dump_light_table (int argc UNUSED, char **argv)
{
  if (raw && isatty (STDOUT_FILENO))
    error (1, 0, "not writing binary data to tty");

  struct output_item *root = read_and_filter_spv (argv[1], NULL);
  struct output_iterator iter;
  OUTPUT_ITEM_FOR_EACH (&iter, root)
    if (iter.cur->type == OUTPUT_ITEM_TABLE && !iter.cur->spv_info->xml_member)
      dump_light_table (iter.cur);
  output_item_unref (root);
}

static void
dump_legacy_data (const struct output_item *item)
{
  char *error;
  if (raw)
    error = dump_raw (item->spv_info->zip_reader,
                      item->spv_info->bin_member);
  else
    {
      struct spv_data data;
      error = spv_read_legacy_data (item->spv_info->zip_reader,
                                    item->spv_info->bin_member, &data);
      if (!error)
        {
          printf ("%s:\n", item->spv_info->bin_member);
          spv_data_dump (&data, stdout);
          spv_data_uninit (&data);
          printf ("\n");
        }
    }

  if (error)
    {
      msg (ME, "%s", error);
      free (error);
    }
}

static void
run_dump_legacy_data (int argc UNUSED, char **argv)
{
  if (raw && isatty (STDOUT_FILENO))
    error (1, 0, "not writing binary data to tty");

  struct output_item *root = read_and_filter_spv (argv[1], NULL);
  struct output_iterator iter;
  OUTPUT_ITEM_FOR_EACH (&iter, root)
    if (iter.cur->type == OUTPUT_ITEM_TABLE
        && iter.cur->spv_info->xml_member
        && iter.cur->spv_info->bin_member)
      dump_legacy_data (iter.cur);
  output_item_unref (root);
}

/* This is really bogus.

   XPath doesn't have any notion of a default XML namespace, but all of the
   elements in the documents we're interested in have a namespace.  Thus, we'd
   need to require the XPath expressions to have a namespace on every single
   element: vis:sourceVariable, vis:graph, and so on.  That's a pain.  So,
   instead, we remove the default namespace from everyplace it occurs.  XPath
   does support the null namespace, so this allows sourceVariable, graph,
   etc. to work.

   See http://plasmasturm.org/log/259/ and
   https://mail.gnome.org/archives/xml/2003-April/msg00144.html for more
   information.*/
static void
remove_default_xml_namespace (xmlNode *node)
{
  if (node->ns && !node->ns->prefix)
    node->ns = NULL;

  for (xmlNode *child = node->children; child; child = child->next)
    remove_default_xml_namespace (child);
}

static void
register_ns (xmlXPathContext *ctx, const char *prefix, const char *uri)
{
  xmlXPathRegisterNs (ctx, CHAR_CAST (xmlChar *, prefix),
                      CHAR_CAST (xmlChar *, uri));
}

static xmlXPathContext *
create_xpath_context (xmlDoc *doc)
{
  xmlXPathContext *ctx = xmlXPathNewContext (doc);
  register_ns (ctx, "vgr", "http://xml.spss.com/spss/viewer/viewer-graph");
  register_ns (ctx, "vizml", "http://xml.spss.com/visualization");
  register_ns (ctx, "vmd", "http://xml.spss.com/spss/viewer/viewer-model");
  register_ns (ctx, "vps", "http://xml.spss.com/spss/viewer/viewer-pagesetup");
  register_ns (ctx, "vst", "http://xml.spss.com/spss/viewer/viewer-style");
  register_ns (ctx, "vtb", "http://xml.spss.com/spss/viewer/viewer-table");
  register_ns (ctx, "vtl", "http://xml.spss.com/spss/viewer/table-looks");
  register_ns (ctx, "vtt", "http://xml.spss.com/spss/viewer/viewer-treemodel");
  register_ns (ctx, "vtx", "http://xml.spss.com/spss/viewer/viewer-text");
  register_ns (ctx, "xsi", "http://www.w3.org/2001/XMLSchema-instance");
  return ctx;
}

static void
dump_xml (int argc, char **argv, const char *member_name,
          char *error_s, xmlDoc *doc)
{
  if (!error_s)
    {
      if (argc == 2)
        {
          printf ("<!-- %s -->\n", member_name);
          xmlElemDump (stdout, NULL, xmlDocGetRootElement (doc));
          putchar ('\n');
        }
      else
        {
          bool any_results = false;

          remove_default_xml_namespace (xmlDocGetRootElement (doc));
          for (int i = 2; i < argc; i++)
            {
              xmlXPathContext *xpath_ctx = create_xpath_context (doc);
              xmlXPathSetContextNode (xmlDocGetRootElement (doc),
                                      xpath_ctx);
              xmlXPathObject *xpath_obj = xmlXPathEvalExpression(
                CHAR_CAST (xmlChar *, argv[i]), xpath_ctx);
              if (!xpath_obj)
                error (1, 0, _("%s: invalid XPath expression"), argv[i]);

              const xmlNodeSet *nodes = xpath_obj->nodesetval;
              if (nodes && nodes->nodeNr > 0)
                {
                  if (!any_results)
                    {
                      printf ("<!-- %s -->\n", member_name);
                      any_results = true;
                    }
                  for (size_t j = 0; j < nodes->nodeNr; j++)
                    {
                      xmlElemDump (stdout, doc, nodes->nodeTab[j]);
                      putchar ('\n');
                    }
                }

              xmlXPathFreeObject (xpath_obj);
              xmlXPathFreeContext (xpath_ctx);
            }
          if (any_results)
            putchar ('\n');
        }
      xmlFreeDoc (doc);
    }
  else
    {
      printf ("<!-- %s -->\n", member_name);
      msg (ME, "%s", error_s);
      free (error_s);
    }
}

static void
dump_legacy_table (int argc, char **argv, const struct output_item *item)
{
  xmlDoc *doc;
  char *error_s = spv_read_xml_member (item->spv_info->zip_reader,
                                       item->spv_info->xml_member,
                                       false, "visualization", &doc);
  dump_xml (argc, argv, item->spv_info->xml_member, error_s, doc);
}

static void
run_dump_legacy_table (int argc, char **argv)
{
  struct output_item *root = read_and_filter_spv (argv[1], NULL);
  struct output_iterator iter;
  OUTPUT_ITEM_FOR_EACH (&iter, root)
    if (iter.cur->type == OUTPUT_ITEM_TABLE
        && iter.cur->spv_info->xml_member)
      dump_legacy_table (argc, argv, iter.cur);
  output_item_unref (root);
}

static void
dump_structure (int argc, char **argv, const struct output_item *item)
{
  xmlDoc *doc;
  char *error_s = spv_read_xml_member (item->spv_info->zip_reader,
                                       item->spv_info->structure_member,
                                       true, "heading", &doc);
  dump_xml (argc, argv, item->spv_info->structure_member, error_s, doc);
}

static void
run_dump_structure (int argc, char **argv)
{
  struct output_item *root = read_and_filter_spv (argv[1], NULL);

  const char *last_structure_member = NULL;
  struct output_iterator iter;
  OUTPUT_ITEM_FOR_EACH (&iter, root)
    {
      const struct output_item *item = iter.cur;
      if (item->spv_info->structure_member
          && (!last_structure_member
              || strcmp (item->spv_info->structure_member,
                         last_structure_member)))
        {
          last_structure_member = item->spv_info->structure_member;
          dump_structure (argc, argv, item);
        }
    }
  output_item_unref (root);
}

static bool
is_any_legacy (const struct output_item *item)
{
  if (item->type == OUTPUT_ITEM_TABLE)
    return item->spv_info->xml_member != NULL;
  else if (item->type == OUTPUT_ITEM_GROUP)
    for (size_t i = 0; i < item->group.n_children; i++)
      if (is_any_legacy (item->group.children[i]))
        return true;

  return false;
}

static void
run_is_legacy (int argc UNUSED, char **argv)
{
  struct output_item *root = read_and_filter_spv (argv[1], NULL);
  bool is_legacy = is_any_legacy (root);
  output_item_unref (root);

  exit (is_legacy ? EXIT_SUCCESS : EXIT_FAILURE);
}

static bool
is_all_ascii (const char *s)
{
  for (; *s; s++)
    if (!encoding_guess_is_ascii_text (*s))
      return false;

  return true;
}

static void
dump_strings (const char *encoding, struct string_array *strings)
{
  string_array_sort (strings);
  string_array_uniq (strings);

  if (raw)
    {
      if (exclude_ascii_only || include_utf8_only)
        {
          size_t i = 0;
          for (size_t j = 0; j < strings->n; j++)
            {
              char *s = strings->strings[j];
              bool is_ascii = is_all_ascii (s);
              bool is_utf8 = !u8_check (CHAR_CAST (uint8_t *, s), strlen (s));
              if (!is_ascii && (!include_utf8_only || is_utf8))
                strings->strings[i++] = s;
              else
                free (s);
            }
          strings->n = i;
        }
      for (size_t i = 0; i < strings->n; i++)
        puts (strings->strings[i]);
    }
  else
    {
      size_t n_nonascii = 0;
      size_t n_utf8 = 0;
      for (size_t i = 0; i < strings->n; i++)
        {
          const char *s = strings->strings[i];
          if (!is_all_ascii (s))
            {
              n_nonascii++;
              if (!u8_check (CHAR_CAST (uint8_t *, s), strlen (s)))
                n_utf8++;
            }
        }
      printf ("%s: %zu unique strings, %zu non-ASCII, %zu UTF-8.\n",
              encoding, strings->n, n_nonascii, n_utf8);
    }
}

struct encoded_strings
  {
    char *encoding;
    struct string_array strings;
  };

struct encoded_strings_table
  {
    struct encoded_strings *es;
    size_t n, allocated;
  };

static void
collect_strings (const struct output_item *item,
                 struct encoded_strings_table *t)
{
  char *error;
  struct spvlb_table *table;
  error = spv_read_light_table (item->spv_info->zip_reader,
                                item->spv_info->bin_member, &table);
  if (error)
    {
      msg (ME, "%s", error);
      free (error);
      return;
    }

  const char *table_encoding = spvlb_table_get_encoding (table);
  size_t j = 0;
  for (j = 0; j < t->n; j++)
    if (!strcmp (t->es[j].encoding, table_encoding))
      break;
  if (j >= t->n)
    {
      if (t->n >= t->allocated)
        t->es = x2nrealloc (t->es, &t->allocated, sizeof *t->es);
      t->es[t->n++] = (struct encoded_strings) {
        .encoding = xstrdup (table_encoding),
        .strings = STRING_ARRAY_INITIALIZER,
      };
    }
  collect_spvlb_strings (table, &t->es[j].strings);
}

static void
run_strings (int argc UNUSED, char **argv)
{
  struct output_item *root = read_and_filter_spv (argv[1], NULL);

  struct encoded_strings_table t = { .es = NULL };
  struct output_iterator iter;
  OUTPUT_ITEM_FOR_EACH (&iter, root)
    {
      const struct output_item *item = iter.cur;
      if (item->type == OUTPUT_ITEM_TABLE
          && !item->spv_info->xml_member
          && item->spv_info->bin_member)
        collect_strings (item, &t);
    }

  for (size_t i = 0; i < t.n; i++)
    {
      dump_strings (t.es[i].encoding, &t.es[i].strings);
      free (t.es[i].encoding);
      string_array_destroy (&t.es[i].strings);
    }
  free (t.es);

  output_item_unref (root);
}

struct command
  {
    const char *name;
    int min_args, max_args;
    void (*run) (int argc, char **argv);
  };

static const struct command commands[] =
  {
    { "detect", 1, 1, run_detect },
    { "dir", 1, 1, run_directory },
    { "convert", 2, 2, run_convert },
    { "get-table-look", 2, 2, run_get_table_look },
    { "convert-table-look", 2, 2, run_convert_table_look },

    /* Undocumented commands. */
    { "dump", 1, 1, run_dump },
    { "dump-light-table", 1, 1, run_dump_light_table },
    { "dump-legacy-data", 1, 1, run_dump_legacy_data },
    { "dump-legacy-table", 1, INT_MAX, run_dump_legacy_table },
    { "dump-structure", 1, INT_MAX, run_dump_structure },
    { "is-legacy", 1, 1, run_is_legacy },
    { "strings", 1, 1, run_strings },
  };
static const int n_commands = sizeof commands / sizeof *commands;

static const struct command *
find_command (const char *name)
{
  for (size_t i = 0; i < n_commands; i++)
    {
      const struct command *c = &commands[i];
      if (!strcmp (name, c->name))
        return c;
    }
  return NULL;
}

static void
emit_msg (const struct msg *m, void *aux UNUSED)
{
  if (m->severity == MSG_S_ERROR || m->severity == MSG_S_WARNING)
    n_warnings++;

  char *s = msg_to_string (m);
  fprintf (stderr, "%s\n", s);
  free (s);
}

int
main (int argc, char **argv)
{
  set_program_name (argv[0]);
  msg_set_handler (emit_msg, NULL);
  settings_init ();
  i18n_init ();

  parse_options (argc, argv);

  argc -= optind;
  argv += optind;

  if (argc < 1)
    error (1, 0, _("missing command name (use --help for help)"));

  const struct command *c = find_command (argv[0]);
  if (!c)
    error (1, 0, _("unknown command \"%s\" (use --help for help)"), argv[0]);

  int n_args = argc - 1;
  if (n_args < c->min_args || n_args > c->max_args)
    {
      if (c->min_args == c->max_args)
        {
          error (1, 0,
                 ngettext ("\"%s\" command takes exactly %d argument",
                           "\"%s\" command takes exactly %d arguments",
                           c->min_args), c->name, c->min_args);
        }
      else if (c->max_args == INT_MAX)
        {
          error (1, 0,
                 ngettext ("\"%s\" command requires at least %d argument",
                           "\"%s\" command requires at least %d arguments",
                           c->min_args), c->name, c->min_args);
        }
      else
        {
          error (1, 0,
                 _("\"%s\" command requires between %d and %d arguments"),
                 c->name, c->min_args, c->max_args);
        }
    }

  c->run (argc, argv);

  pivot_table_look_unref (table_look);
  i18n_done ();

  return n_warnings ? EXIT_FAILURE : EXIT_SUCCESS;
}

static struct output_criteria *
get_criteria (void)
{
  if (!n_criteria || new_criteria)
    {
      new_criteria = false;
      if (n_criteria >= allocated_criteria)
        criteria = x2nrealloc (criteria, &allocated_criteria,
                               sizeof *criteria);
      criteria[n_criteria++]
        = (struct output_criteria) OUTPUT_CRITERIA_INITIALIZER;
    }

  return &criteria[n_criteria - 1];
}

static void
parse_select (char *arg)
{
  bool invert = arg[0] == '^';
  arg += invert;

  unsigned classes = 0;
  for (char *token = strtok (arg, ","); token; token = strtok (NULL, ","))
    {
      if (!strcmp (arg, "all"))
        classes = OUTPUT_ALL_CLASSES;
      else if (!strcmp (arg, "help"))
        {
          puts (_("The following object classes are supported:"));
          for (int class = 0; class < OUTPUT_N_CLASSES; class++)
            printf ("- %s\n", output_item_class_to_string (class));
          exit (0);
        }
      else
        {
          int class = output_item_class_from_string (token);
          if (class == OUTPUT_N_CLASSES)
            error (1, 0, _("unknown object class \"%s\" (use --select=help "
                           "for help)"), arg);
          classes |= 1u << class;
        }
    }

  struct output_criteria *c = get_criteria ();
  c->classes = invert ? classes ^ OUTPUT_ALL_CLASSES : classes;
}

static struct output_criteria_match *
get_criteria_match (const char **arg)
{
  struct output_criteria *c = get_criteria ();
  if ((*arg)[0] == '^')
    {
      (*arg)++;
      return &c->exclude;
    }
  else
    return &c->include;
}

static void
parse_commands (const char *arg)
{
  struct output_criteria_match *cm = get_criteria_match (&arg);
  string_array_parse (&cm->commands, ss_cstr (arg), ss_cstr (","));
}

static void
parse_subtypes (const char *arg)
{
  struct output_criteria_match *cm = get_criteria_match (&arg);
  string_array_parse (&cm->subtypes, ss_cstr (arg), ss_cstr (","));
}

static void
parse_labels (const char *arg)
{
  struct output_criteria_match *cm = get_criteria_match (&arg);
  string_array_parse (&cm->labels, ss_cstr (arg), ss_cstr (","));
}

static void
parse_instances (char *arg)
{
  struct output_criteria *c = get_criteria ();
  size_t allocated_instances = c->n_instances;

  for (char *token = strtok (arg, ","); token; token = strtok (NULL, ","))
    {
      if (c->n_instances >= allocated_instances)
        c->instances = x2nrealloc (c->instances, &allocated_instances,
                                   sizeof *c->instances);

      c->instances[c->n_instances++] = (!strcmp (token, "last") ? -1
                                        : atoi (token));
    }
}

static void
parse_nth_commands (char *arg)
{
  struct output_criteria *c = get_criteria ();
  size_t allocated_commands = c->n_commands;

  for (char *token = strtok (arg, ","); token; token = strtok (NULL, ","))
    {
      if (c->n_commands >= allocated_commands)
        c->commands = x2nrealloc (c->commands, &allocated_commands,
                                   sizeof *c->commands);

      c->commands[c->n_commands++] = atoi (token);
    }
}

static void
parse_members (const char *arg)
{
  struct output_criteria *cm = get_criteria ();
  string_array_parse (&cm->members, ss_cstr (arg), ss_cstr (","));
}

static void
parse_table_look (const char *arg)
{
  pivot_table_look_unref (table_look);

  char *error_s = pivot_table_look_read (arg, &table_look);
  if (error_s)
    error (1, 0, "%s", error_s);
}

static void
parse_options (int argc, char *argv[])
{
  for (;;)
    {
      enum
        {
          OPT_MEMBER_NAMES = UCHAR_MAX + 1,
          OPT_SHOW_HIDDEN,
          OPT_SELECT,
          OPT_COMMANDS,
          OPT_NTH_COMMANDS,
          OPT_SUBTYPES,
          OPT_LABELS,
          OPT_INSTANCES,
          OPT_MEMBERS,
          OPT_ERRORS,
          OPT_OR,
          OPT_SORT,
          OPT_RAW,
          OPT_NO_ASCII_ONLY,
          OPT_UTF8_ONLY,
          OPT_TABLE_LOOK,
          OPT_HELP_DEVELOPER,
        };
      static const struct option long_options[] =
        {
          /* Input selection options. */
          { "show-hidden", no_argument, NULL, OPT_SHOW_HIDDEN },
          { "select", required_argument, NULL, OPT_SELECT },
          { "commands", required_argument, NULL, OPT_COMMANDS },
          { "nth-commands", required_argument, NULL, OPT_NTH_COMMANDS },
          { "subtypes", required_argument, NULL, OPT_SUBTYPES },
          { "labels", required_argument, NULL, OPT_LABELS },
          { "instances", required_argument, NULL, OPT_INSTANCES },
          { "members", required_argument, NULL, OPT_MEMBERS },
          { "errors", no_argument, NULL, OPT_ERRORS },
          { "or", no_argument, NULL, OPT_OR },

          /* "dir" command options. */
          { "member-names", no_argument, NULL, OPT_MEMBER_NAMES },

          /* "convert" command options. */
          { "force", no_argument, NULL, 'f' },
          { "table-look", required_argument, NULL, OPT_TABLE_LOOK },

          /* "dump-light-table" command options. */
          { "sort", no_argument, NULL, OPT_SORT },
          { "raw", no_argument, NULL, OPT_RAW },

          /* "strings" command options. */
          { "no-ascii-only", no_argument, NULL, OPT_NO_ASCII_ONLY },
          { "utf8-only", no_argument, NULL, OPT_UTF8_ONLY },

          { "help", no_argument, NULL, 'h' },
          { "help-developer", no_argument, NULL, OPT_HELP_DEVELOPER },
          { "version", no_argument, NULL, 'v' },

          { NULL, 0, NULL, 0 },
        };

      int c;

      c = getopt_long (argc, argv, "O:hvf", long_options, NULL);
      if (c == -1)
        break;

      switch (c)
        {
        case 'O':
          output_driver_parse_option (optarg, &output_options);
          break;

        case OPT_MEMBER_NAMES:
          show_member_names = true;
          break;

        case OPT_SHOW_HIDDEN:
          get_criteria ()->include_hidden = true;
          break;

        case OPT_SELECT:
          parse_select (optarg);
          break;

        case OPT_COMMANDS:
          parse_commands (optarg);
          break;

        case OPT_NTH_COMMANDS:
          parse_nth_commands (optarg);
          break;

        case OPT_SUBTYPES:
          parse_subtypes (optarg);
          break;

        case OPT_LABELS:
          parse_labels (optarg);
          break;

        case OPT_INSTANCES:
          parse_instances (optarg);
          break;

        case OPT_MEMBERS:
          parse_members (optarg);
          break;

        case OPT_ERRORS:
          get_criteria ()->error = true;
          break;

        case OPT_OR:
          new_criteria = true;
          break;

        case OPT_SORT:
          sort = true;
          break;

        case OPT_RAW:
          raw = true;
          break;

        case OPT_TABLE_LOOK:
          parse_table_look (optarg);
          break;

        case OPT_NO_ASCII_ONLY:
          exclude_ascii_only = true;
          break;

        case OPT_UTF8_ONLY:
          include_utf8_only = true;
          break;

        case 'f':
          force = true;
          break;

        case 'v':
          version_etc (stdout, "pspp-output", PACKAGE_NAME, PACKAGE_VERSION,
                       "Ben Pfaff", "John Darrington", NULL_SENTINEL);
          exit (EXIT_SUCCESS);

        case 'h':
          usage ();
          exit (EXIT_SUCCESS);

        case OPT_HELP_DEVELOPER:
          developer_usage ();
          exit (EXIT_SUCCESS);

        default:
          exit (EXIT_FAILURE);
        }
    }
}

static void
usage (void)
{
  struct string s = DS_EMPTY_INITIALIZER;
  struct string_set formats = STRING_SET_INITIALIZER(formats);
  output_get_supported_formats (&formats);
  const char *format;
  const struct string_set_node *node;
  STRING_SET_FOR_EACH (format, node, &formats)
    {
      if (!ds_is_empty (&s))
        ds_put_byte (&s, ' ');
      ds_put_cstr (&s, format);
    }
  string_set_destroy (&formats);

  printf ("\
%s, a utility for working with SPSS viewer (.spv) files.\n\
Usage: %s [OPTION]... COMMAND ARG...\n\
\n\
The following commands are available:\n\
  detect FILE            Detect whether FILE is an SPV file.\n\
  dir FILE               List tables and other items in FILE.\n\
  convert SOURCE DEST    Convert .spv SOURCE to DEST.\n\
  get-table-look SOURCE DEST  Copies first selected TableLook into DEST\n\
  convert-table-look SOURCE DEST  Copies .tlo or .stt SOURCE into DEST\n\
\n\
Input selection options for \"dir\" and \"convert\":\n\
  --select=CLASS...   include only some kinds of objects\n\
  --select=help       print known object classes\n\
  --commands=COMMAND...  include only specified COMMANDs\n\
  --nth-commands=N...  include only the Nth instance of selected commands\n\
  --subtypes=SUBTYPE...  include only specified SUBTYPEs of output\n\
  --labels=LABEL...   include only output objects with the given LABELs\n\
  --instances=INSTANCE...  include only the given object INSTANCEs\n\
  --show-hidden       include hidden output objects\n\
  --or                separate two sets of selection options\n\
\n\
\"convert\" by default infers the destination's format from its extension.\n\
The known extensions are: %s\n\
The following options override \"convert\" behavior:\n\
  -O format=FORMAT          set destination format to FORMAT\n\
  -O OPTION=VALUE           set output option\n\
  -f, --force               keep output file even given errors\n\
  --table-look=FILE         override tables' style with TableLook from FILE\n\
Other options:\n\
  --help              display this help and exit\n\
  --help-developer    display help for developer commands and exit\n\
  --version           output version information and exit\n",
          program_name, program_name, ds_cstr (&s));
  ds_destroy (&s);
}

static void
developer_usage (void)
{
  printf ("\
The following developer commands are available:\n\
  dump FILE              Dump pivot table structure\n\
  [--raw | --sort] dump-light-table FILE  Dump light tables\n\
  [--raw] dump-legacy-data FILE  Dump legacy table data\n\
  dump-legacy-table FILE [XPATH]...  Dump legacy table XML\n\
  dump-structure FILE [XPATH]...  Dump structure XML\n\
  is-legacy FILE         Exit with status 0 if any legacy table selected\n\
  strings FILE           Dump analysis of strings\n\
\n\
Additional input selection options:\n\
  --members=MEMBER...    include only objects with these Zip member names\n\
  --errors               include only objects that cannot be loaded\n\
\n\
Additional options for \"dir\" command:\n\
  --member-names         show Zip member names with objects\n\
\n\
Options for the \"strings\" command:\n\
  --raw                  Dump all (unique) strings\n\
  --raw --no-ascii-only  Dump all strings that contain non-ASCII characters\n\
  --raw --utf8-only      Dump all non-ASCII strings that are valid UTF-8\n\
\n\
Other options:\n\
  --raw                  print raw binary data instead of a parsed version\n\
  --sort                 sort borders and areas for shorter \"diff\" output\n");
}
