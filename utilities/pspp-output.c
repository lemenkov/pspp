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

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#include "data/file-handle-def.h"
#include "data/settings.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/string-map.h"
#include "libpspp/string-set.h"
#include "output/driver.h"
#include "output/group-item.h"
#include "output/page-setup-item.h"
#include "output/pivot-table.h"
#include "output/spv/light-binary-parser.h"
#include "output/spv/spv-legacy-data.h"
#include "output/spv/spv-output.h"
#include "output/spv/spv-select.h"
#include "output/spv/spv.h"
#include "output/table-item.h"
#include "output/text-item.h"

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

/* --member-name: Include .zip member name in "dir" output. */
static bool show_member_names;

/* --show-hidden, --select, --commands, ...: Selection criteria. */
static struct spv_criteria *criteria;
static size_t n_criteria, allocated_criteria;

/* --or: Add new element to 'criteria' array. */
static bool new_criteria;

/* --sort: Sort members under dump-light-table, to make comparisons easier. */
static bool sort;

/* --raw: Dump raw binary data in dump-light-table. */
static bool raw;

/* -f, --force: Keep output file even on error. */
static bool force;

/* Number of warnings issued. */
static size_t n_warnings;

static void usage (void);
static void parse_options (int argc, char **argv);

static void
dump_item (const struct spv_item *item)
{
  if (show_member_names && (item->xml_member || item->bin_member))
    {
      const char *x = item->xml_member;
      const char *b = item->bin_member;
      char *s = (x && b
                 ? xasprintf (_("%s and %s:"), x, b)
                 : xasprintf ("%s:", x ? x : b));
      text_item_submit (text_item_create_nocopy (TEXT_ITEM_TITLE, s));
    }

  switch (spv_item_get_type (item))
    {
    case SPV_ITEM_HEADING:
      break;

    case SPV_ITEM_TEXT:
      spv_text_submit (item);
      break;

    case SPV_ITEM_TABLE:
      pivot_table_submit (pivot_table_ref (spv_item_get_table (item)));
      break;

    case SPV_ITEM_GRAPH:
      break;

    case SPV_ITEM_MODEL:
      break;

    case SPV_ITEM_OBJECT:
      break;

    case SPV_ITEM_TREE:
      break;

    default:
      abort ();
    }
}

static void
print_item_directory (const struct spv_item *item)
{
  for (int i = 1; i < spv_item_get_level (item); i++)
    printf ("    ");

  enum spv_item_type type = spv_item_get_type (item);
  printf ("- %s", spv_item_type_to_string (type));

  const char *label = spv_item_get_label (item);
  if (label)
    printf (" \"%s\"", label);

  if (type == SPV_ITEM_TABLE)
    {
      const struct pivot_table *table = spv_item_get_table (item);
      char *title = pivot_value_to_string (table->title,
                                           SETTINGS_VALUE_SHOW_DEFAULT,
                                           SETTINGS_VALUE_SHOW_DEFAULT);
      if (!label || strcmp (title, label))
        printf (" title \"%s\"", title);
      free (title);
    }

  const char *command_id = spv_item_get_command_id (item);
  if (command_id)
    printf (" command \"%s\"", command_id);

  const char *subtype = spv_item_get_subtype (item);
  if (subtype && (!label || strcmp (label, subtype)))
    printf (" subtype \"%s\"", subtype);

  if (!spv_item_is_visible (item))
    printf (" (hidden)");
  if (show_member_names && (item->xml_member || item->bin_member))
    {
      if (item->xml_member && item->bin_member)
        printf (" in %s and %s", item->xml_member, item->bin_member);
      else if (item->xml_member)
        printf (" in %s", item->xml_member);
      else if (item->bin_member)
        printf (" in %s", item->bin_member);
    }
  putchar ('\n');
}

static void
run_detect (int argc UNUSED, char **argv)
{
  char *err = spv_detect (argv[1]);
  if (err)
    error (1, 0, "%s", err);
}

static void
run_directory (int argc UNUSED, char **argv)
{
  struct spv_reader *spv;
  char *err = spv_open (argv[1], &spv);
  if (err)
    error (1, 0, "%s", err);

  struct spv_item **items;
  size_t n_items;
  spv_select (spv, criteria, n_criteria, &items, &n_items);
  for (size_t i = 0; i < n_items; i++)
    print_item_directory (items[i]);
  free (items);

  spv_close (spv);
}

struct item_path
  {
    const struct spv_item **nodes;
    size_t n;

#define N_STUB 10
    const struct spv_item *stub[N_STUB];
  };

static void
swap_nodes (const struct spv_item **a, const struct spv_item **b)
{
  const struct spv_item *tmp = *a;
  *a = *b;
  *b = tmp;
}

static void
get_path (const struct spv_item *item, struct item_path *path)
{
  size_t allocated = 10;
  path->nodes = path->stub;
  path->n = 0;

  while (item)
    {
      if (path->n >= allocated)
        {
          if (path->nodes == path->stub)
            path->nodes = xmemdup (path->stub, sizeof path->stub);
          path->nodes = x2nrealloc (path->nodes, &allocated,
                                    sizeof *path->nodes);
        }
      path->nodes[path->n++] = item;
      item = item->parent;
    }

  for (size_t i = 0; i < path->n / 2; i++)
    swap_nodes (&path->nodes[i], &path->nodes[path->n - i - 1]);
}

static void
free_path (struct item_path *path)
{
  if (path && path->nodes != path->stub)
    free (path->nodes);
}

static void
dump_heading_transition (const struct spv_item *old,
                         const struct spv_item *new)
{
  if (old == new)
    return;

  struct item_path old_path, new_path;
  get_path (old, &old_path);
  get_path (new, &new_path);

  size_t common = 0;
  for (; common < old_path.n && common < new_path.n; common++)
    if (old_path.nodes[common] != new_path.nodes[common])
      break;

  for (size_t i = common; i < old_path.n; i++)
    group_close_item_submit (group_close_item_create ());
  for (size_t i = common; i < new_path.n; i++)
    group_open_item_submit (group_open_item_create (
                              new_path.nodes[i]->command_id));

  free_path (&old_path);
  free_path (&new_path);
}

static void
run_convert (int argc UNUSED, char **argv)
{
  struct spv_reader *spv;
  char *err = spv_open (argv[1], &spv);
  if (err)
    error (1, 0, "%s", err);

  output_engine_push ();
  output_set_filename (argv[1]);
  string_map_replace (&output_options, "output-file", argv[2]);
  struct output_driver *driver = output_driver_create (&output_options);
  if (!driver)
    exit (EXIT_FAILURE);
  output_driver_register (driver);

  const struct page_setup *ps = spv_get_page_setup (spv);
  if (ps)
    page_setup_item_submit (page_setup_item_create (ps));

  struct spv_item **items;
  size_t n_items;
  spv_select (spv, criteria, n_criteria, &items, &n_items);
  struct spv_item *prev_heading = spv_get_root (spv);
  for (size_t i = 0; i < n_items; i++)
    {
      struct spv_item *heading
        = items[i]->type == SPV_ITEM_HEADING ? items[i] : items[i]->parent;
      dump_heading_transition (prev_heading, heading);
      dump_item (items[i]);
      prev_heading = heading;
    }
  dump_heading_transition (prev_heading, spv_get_root (spv));
  free (items);

  spv_close (spv);

  output_engine_pop ();
  fh_done ();

  if (n_warnings && !force)
    {
      /* XXX There could be other files to unlink, e.g. the ascii driver can
         produce additional files with the charts. */
      unlink (argv[2]);
    }
}

static void
run_dump (int argc UNUSED, char **argv)
{
  struct spv_reader *spv;
  char *err = spv_open (argv[1], &spv);
  if (err)
    error (1, 0, "%s", err);

  struct spv_item **items;
  size_t n_items;
  spv_select (spv, criteria, n_criteria, &items, &n_items);
  for (size_t i = 0; i < n_items; i++)
    if (items[i]->type == SPV_ITEM_TABLE)
      {
        pivot_table_dump (spv_item_get_table (items[i]), 0);
        putchar ('\n');
      }
  free (items);

  spv_close (spv);
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

static void
run_dump_light_table (int argc UNUSED, char **argv)
{
  if (raw && isatty (STDOUT_FILENO))
    error (1, 0, "not writing binary data to tty");

  struct spv_reader *spv;
  char *err = spv_open (argv[1], &spv);
  if (err)
    error (1, 0, "%s", err);

  struct spv_item **items;
  size_t n_items;
  spv_select (spv, criteria, n_criteria, &items, &n_items);
  for (size_t i = 0; i < n_items; i++)
    {
      if (!spv_item_is_light_table (items[i]))
        continue;

      char *error;
      if (raw)
        {
          void *data;
          size_t size;
          error = spv_item_get_raw_light_table (items[i], &data, &size);
          if (!error)
            {
              fwrite (data, size, 1, stdout);
              free (data);
            }
        }
      else
        {
          struct spvlb_table *table;
          error = spv_item_get_light_table (items[i], &table);
          if (!error)
            {
              if (sort)
                {
                  qsort (table->borders->borders, table->borders->n_borders,
                         sizeof *table->borders->borders, compare_borders);
                  qsort (table->cells->cells, table->cells->n_cells,
                         sizeof *table->cells->cells, compare_cells);
                }
              spvlb_print_table (items[i]->bin_member, 0, table);
              spvlb_free_table (table);
            }
        }
      if (error)
        {
          msg (ME, "%s", error);
          free (error);
        }
    }

  free (items);

  spv_close (spv);
}

static void
run_dump_legacy_data (int argc UNUSED, char **argv)
{
  struct spv_reader *spv;
  char *err = spv_open (argv[1], &spv);
  if (err)
    error (1, 0, "%s", err);

  struct spv_item **items;
  size_t n_items;
  spv_select (spv, criteria, n_criteria, &items, &n_items);
  for (size_t i = 0; i < n_items; i++)
    if (spv_item_is_legacy_table (items[i]))
      {
        struct spv_data data;
        char *error;
        if (raw)
          {
            void *data;
            size_t size;
            error = spv_item_get_raw_legacy_data (items[i], &data, &size);
            if (!error)
              {
                fwrite (data, size, 1, stdout);
                free (data);
              }
          }
        else
          {
            error = spv_item_get_legacy_data (items[i], &data);
            if (!error)
              {
                printf ("%s:\n", items[i]->bin_member);
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
  free (items);

  spv_close (spv);
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
            putchar ('\n');;
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
run_dump_legacy_table (int argc, char **argv)
{
  struct spv_reader *spv;
  char *err = spv_open (argv[1], &spv);
  if (err)
    error (1, 0, "%s", err);

  struct spv_item **items;
  size_t n_items;
  spv_select (spv, criteria, n_criteria, &items, &n_items);
  for (size_t i = 0; i < n_items; i++)
    if (spv_item_is_legacy_table (items[i]))
      {
        xmlDoc *doc;
        char *error_s = spv_item_get_legacy_table (items[i], &doc);
        dump_xml (argc, argv, items[i]->xml_member, error_s, doc);
      }
  free (items);

  spv_close (spv);
}

static void
run_dump_structure (int argc, char **argv)
{
  struct spv_reader *spv;
  char *err = spv_open (argv[1], &spv);
  if (err)
    error (1, 0, "%s", err);

  struct spv_item **items;
  size_t n_items;
  spv_select (spv, criteria, n_criteria, &items, &n_items);
  const char *last_structure_member = NULL;
  for (size_t i = 0; i < n_items; i++)
    if (!last_structure_member || strcmp (items[i]->structure_member,
                                          last_structure_member))
      {
        last_structure_member = items[i]->structure_member;

        xmlDoc *doc;
        char *error_s = spv_item_get_structure (items[i], &doc);
        dump_xml (argc, argv, items[i]->structure_member, error_s, doc);
      }
  free (items);

  spv_close (spv);
}

static void
run_is_legacy (int argc UNUSED, char **argv)
{
  struct spv_reader *spv;
  char *err = spv_open (argv[1], &spv);
  if (err)
    error (1, 0, "%s", err);

  bool is_legacy = false;

  struct spv_item **items;
  size_t n_items;
  spv_select (spv, criteria, n_criteria, &items, &n_items);
  for (size_t i = 0; i < n_items; i++)
    if (spv_item_is_legacy_table (items[i]))
      {
        is_legacy = true;
        break;
      }
  free (items);

  spv_close (spv);

  exit (is_legacy ? EXIT_SUCCESS : EXIT_FAILURE);
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

    /* Undocumented commands. */
    { "dump", 1, 1, run_dump },
    { "dump-light-table", 1, 1, run_dump_light_table },
    { "dump-legacy-data", 1, 1, run_dump_legacy_data },
    { "dump-legacy-table", 1, INT_MAX, run_dump_legacy_table },
    { "dump-structure", 1, INT_MAX, run_dump_structure },
    { "is-legacy", 1, 1, run_is_legacy },
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

  i18n_done ();

  return n_warnings ? EXIT_FAILURE : EXIT_SUCCESS;
}

static struct spv_criteria *
get_criteria (void)
{
  if (!n_criteria || new_criteria)
    {
      new_criteria = false;
      if (n_criteria >= allocated_criteria)
        criteria = x2nrealloc (criteria, &allocated_criteria,
                               sizeof *criteria);
      criteria[n_criteria++] = (struct spv_criteria) SPV_CRITERIA_INITIALIZER;
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
        classes = SPV_ALL_CLASSES;
      else if (!strcmp (arg, "help"))
        {
          puts (_("The following object classes are supported:"));
          for (int class = 0; class < SPV_N_CLASSES; class++)
            printf ("- %s\n", spv_item_class_to_string (class));
          exit (0);
        }
      else
        {
          int class = spv_item_class_from_string (token);
          if (class == SPV_N_CLASSES)
            error (1, 0, _("%s: unknown object class (use --select=help "
                           "for help"), arg);
          classes |= 1u << class;
        }
    }

  struct spv_criteria *c = get_criteria ();
  c->classes = invert ? classes ^ SPV_ALL_CLASSES : classes;
}

static struct spv_criteria_match *
get_criteria_match (const char **arg)
{
  struct spv_criteria *c = get_criteria ();
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
  struct spv_criteria_match *cm = get_criteria_match (&arg);
  string_array_parse (&cm->commands, ss_cstr (arg), ss_cstr (","));
}

static void
parse_subtypes (const char *arg)
{
  struct spv_criteria_match *cm = get_criteria_match (&arg);
  string_array_parse (&cm->subtypes, ss_cstr (arg), ss_cstr (","));
}

static void
parse_labels (const char *arg)
{
  struct spv_criteria_match *cm = get_criteria_match (&arg);
  string_array_parse (&cm->labels, ss_cstr (arg), ss_cstr (","));
}

static void
parse_instances (char *arg)
{
  struct spv_criteria *c = get_criteria ();
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
parse_members (const char *arg)
{
  struct spv_criteria *cm = get_criteria ();
  string_array_parse (&cm->members, ss_cstr (arg), ss_cstr (","));
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
          OPT_SUBTYPES,
          OPT_LABELS,
          OPT_INSTANCES,
          OPT_MEMBERS,
          OPT_ERRORS,
          OPT_OR,
          OPT_SORT,
          OPT_RAW,
        };
      static const struct option long_options[] =
        {
          /* Input selection options. */
          { "show-hidden", no_argument, NULL, OPT_SHOW_HIDDEN },
          { "select", required_argument, NULL, OPT_SELECT },
          { "commands", required_argument, NULL, OPT_COMMANDS },
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

          /* "dump-light-table" command options. */
          { "sort", no_argument, NULL, OPT_SORT },
          { "raw", no_argument, NULL, OPT_RAW },

          { "help", no_argument, NULL, 'h' },
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
\n\
Input selection options for \"dir\" and \"convert\":\n\
  --select=CLASS...   include only some kinds of objects\n\
  --select=help       print known object classes\n\
  --commands=COMMAND...  include only specified COMMANDs\n\
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
Other options:\n\
  --help              display this help and exit\n\
  --version           output version information and exit\n",
          program_name, program_name, ds_cstr (&s));
  ds_destroy (&s);
}
