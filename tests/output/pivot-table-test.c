/* PSPP - a program for statistical analysis.
   Copyright (C) 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include <errno.h>
#include <fnmatch.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "data/file-handle-def.h"
#include "language/lexer/lexer.h"
#include "language/lexer/format-parser.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/string-map.h"
#include "output/driver.h"
#include "output/message-item.h"
#include "output/options.h"
#include "output/pivot-table.h"
#include "output/table-item.h"

#include "gl/error.h"
#include "gl/progname.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

/* --emphasis: Enable emphasis in ASCII driver? */
static bool emphasis;

/* --box: ASCII driver box option. */
static char *box;

/* -o, --output: Base name for output files. */
static const char *output_base = "render";

/* --dump: Print table dump to stdout? */
static bool dump;

static const char *parse_options (int argc, char **argv);
static void usage (void) NO_RETURN;
static struct pivot_table *read_table (struct lexer *);
static void output_msg (const struct msg *, void *);

int
main (int argc, char **argv)
{
  const char *input_file_name;

  set_program_name (argv[0]);
  i18n_init ();
  output_engine_push ();
  input_file_name = parse_options (argc, argv);

  settings_init ();

  struct lex_reader *reader = lex_reader_for_file (input_file_name, NULL,
                                                   LEX_SYNTAX_AUTO,
                                                   LEX_ERROR_CONTINUE);
  if (!reader)
    exit (1);

  struct lexer *lexer = lex_create ();
  msg_set_handler (output_msg, lexer);
  lex_include (lexer, reader);
  lex_get (lexer);

  for (;;)
    {
      while (lex_match (lexer, T_ENDCMD))
        continue;
      if (lex_match (lexer, T_STOP))
        break;

      struct pivot_table *pt = read_table (lexer);
      if (dump)
        pivot_table_dump (pt, 0);
      pivot_table_submit (pt);
    }

  lex_destroy (lexer);
  output_engine_pop ();
  fh_done ();

  return 0;
}

static void PRINTF_FORMAT (2, 3)
register_driver (struct string_map *options,
                 const char *output_file, ...)
{
  va_list args;
  va_start (args, output_file);
  string_map_insert_nocopy (options, xstrdup ("output-file"),
                            xvasprintf (output_file, args));
  va_end (args);

  struct output_driver *driver = output_driver_create (options);
  if (driver == NULL)
    exit (EXIT_FAILURE);
  output_driver_register (driver);
}

static void
configure_drivers (int width, int length UNUSED, int min_break)
{
  /* Render to stdout. */
  struct string_map options = STRING_MAP_INITIALIZER (options);
  string_map_insert (&options, "format", "txt");
  string_map_insert_nocopy (&options, xstrdup ("width"),
                            xasprintf ("%d", width));
  if (min_break >= 0)
    string_map_insert_nocopy (&options, xstrdup ("min-hbreak"),
                              xasprintf ("%d", min_break));
  string_map_insert (&options, "emphasis", emphasis ? "true" : "false");
  if (box != NULL)
    string_map_insert (&options, "box", box);
  register_driver (&options, "-");


#ifdef HAVE_CAIRO
  /* Render to <base>.pdf. */
  string_map_insert (&options, "top-margin", "0");
  string_map_insert (&options, "bottom-margin", "0");
  string_map_insert (&options, "left-margin", "0");
  string_map_insert (&options, "right-margin", "0");
  string_map_insert (&options, "paper-size", "99x99in");
  string_map_insert (&options, "trim", "true");
  register_driver (&options, "%s.pdf", output_base);
#endif

  register_driver (&options, "%s.txt", output_base);
  register_driver (&options, "%s.csv", output_base);
  register_driver (&options, "%s.odt", output_base);
  register_driver (&options, "%s.spv", output_base);
  register_driver (&options, "%s.html", output_base);
  register_driver (&options, "%s.tex", output_base);

  string_map_destroy (&options);
}

static const char *
parse_options (int argc, char **argv)
{
  int width = 79;
  int length = 66;
  int min_break = -1;

  for (;;)
    {
      enum {
        OPT_WIDTH = UCHAR_MAX + 1,
        OPT_LENGTH,
        OPT_MIN_BREAK,
        OPT_EMPHASIS,
        OPT_BOX,
        OPT_TABLE_LOOK,
        OPT_DUMP,
        OPT_HELP
      };
      static const struct option options[] =
        {
          {"width", required_argument, NULL, OPT_WIDTH},
          {"length", required_argument, NULL, OPT_LENGTH},
          {"min-break", required_argument, NULL, OPT_MIN_BREAK},
          {"emphasis", no_argument, NULL, OPT_EMPHASIS},
          {"box", required_argument, NULL, OPT_BOX},
          {"output", required_argument, NULL, 'o'},
          {"table-look", required_argument, NULL, OPT_TABLE_LOOK},
          {"dump", no_argument, NULL, OPT_DUMP},
          {"help", no_argument, NULL, OPT_HELP},
          {NULL, 0, NULL, 0},
        };

      int c = getopt_long (argc, argv, "o:", options, NULL);
      if (c == -1)
        break;

      switch (c)
        {
        case OPT_WIDTH:
          width = atoi (optarg);
          break;

        case OPT_LENGTH:
          length = atoi (optarg);
          break;

        case OPT_MIN_BREAK:
          min_break = atoi (optarg);
          break;

        case OPT_EMPHASIS:
          emphasis = true;
          break;

        case OPT_BOX:
          box = optarg;
          break;

        case 'o':
          output_base = optarg;
          break;

        case OPT_TABLE_LOOK:
          {
            struct pivot_table_look *look;
            char *err = pivot_table_look_read (optarg, &look);
            if (err)
              error (1, 0, "%s", err);
            pivot_table_look_set_default (look);
            pivot_table_look_unref (look);
          }
          break;

        case OPT_DUMP:
          dump = true;
          break;

        case OPT_HELP:
          usage ();

        case 0:
          break;

        case '?':
          exit (EXIT_FAILURE);
          break;

        default:
          NOT_REACHED ();
        }

    }

  configure_drivers (width, length, min_break);

  if (optind + 1 != argc)
    error (1, 0, "exactly one non-option argument required; "
           "use --help for help");
  return argv[optind];
}

static void
usage (void)
{
  printf ("%s, to test rendering of PSPP tables\n"
          "usage: %s [OPTIONS] INPUT\n"
          "\nOptions:\n"
          "  --width=WIDTH   set page width in characters\n"
          "  --length=LINE   set page length in lines\n",
          program_name, program_name);
  exit (EXIT_SUCCESS);
}

static void
force_match (struct lexer *lexer, enum token_type type)
{
  if (!lex_force_match (lexer, type))
    exit (1);
}

static void
force_string (struct lexer *lexer)
{
  if (!lex_force_string (lexer))
    exit (1);
}

static void
force_int (struct lexer *lexer)
{
  if (!lex_force_int (lexer))
    exit (1);
}

static void
force_num (struct lexer *lexer)
{
  if (!lex_force_num (lexer))
    exit (1);
}

static bool
parse_settings_value_show (struct lexer *lexer, const char *name,
                           enum settings_value_show *show)
{
  if (lex_match_id (lexer, name))
    {
      lex_match (lexer, T_EQUALS);

      if (lex_match_id (lexer, "DEFAULT"))
        *show = SETTINGS_VALUE_SHOW_DEFAULT;
      else if (lex_match_id (lexer, "VALUE"))
        *show = SETTINGS_VALUE_SHOW_VALUE;
      else if (lex_match_id (lexer, "LABEL"))
        *show = SETTINGS_VALUE_SHOW_LABEL;
      else if (lex_match_id (lexer, "BOTH"))
        *show = SETTINGS_VALUE_SHOW_BOTH;
      else
        {
          lex_error_expecting (lexer, "DEFAULT", "VALUE", "LABEL", "BOTH");
          exit (1);
        }

      return true;
    }
  else
    return false;
}

static bool
parse_string_setting (struct lexer *lexer, const char *name, char **stringp)
{
  if (lex_match_id (lexer, name))
    {
      lex_match (lexer, T_EQUALS);
      force_string (lexer);

      free (*stringp);
      *stringp = xstrdup (lex_tokcstr (lexer));

      lex_get (lexer);
      return true;
    }
  else
    return false;
}

static bool
match_kw (struct lexer *lexer, const char *kw)
{
  return (!strcmp (kw, "ALL")
          ? lex_match (lexer, T_ALL)
          : lex_match_id (lexer, kw));
}

static bool
parse_bool_setting_with_default (struct lexer *lexer, const char *name,
                                 const char *true_kw, const char *false_kw,
                                 int default_value, bool *out)
{
  if (lex_match_id (lexer, name))
    {
      if (default_value >= 0)
        {
          if (!lex_match (lexer, T_EQUALS))
            *out = default_value;
          return true;
        }
      else
        force_match (lexer, T_EQUALS);

      if (match_kw (lexer, true_kw))
        *out = true;
      else if (match_kw (lexer, false_kw))
        *out = false;
      else
        {
          lex_error_expecting (lexer, true_kw, false_kw);
          exit (1);
        }

      return true;
    }
  else
    return false;
}

static bool
parse_bool_setting (struct lexer *lexer, const char *name,
                    const char *true_kw, const char *false_kw,
                    bool *out)
{
  return parse_bool_setting_with_default (lexer, name, true_kw, false_kw, -1,
                                          out);
}

static bool
parse_yesno_setting (struct lexer *lexer, const char *name, bool *out)
{
  return parse_bool_setting_with_default (lexer, name, "YES", "NO", true, out);
}

static struct cell_color
read_color (struct lexer *lexer)
{
  struct cell_color color;
  if (!parse_color__ (lex_tokcstr (lexer), &color))
    {
      msg (SE, "%s: unknown color", lex_tokcstr (lexer));
      exit (1);
    }
  lex_get (lexer);
  return color;
}

static bool
parse_color_pair_setting (struct lexer *lexer, const char *name,
                          struct cell_color out[2])
{
  if (lex_match_id (lexer, name))
    {
      lex_match (lexer, T_EQUALS);
      out[0] = read_color (lexer);
      out[1] = lex_is_string (lexer) ? read_color (lexer) : out[0];
      return true;
    }
  else
    return false;
}

static bool
parse_int_setting (struct lexer *lexer, const char *name, int *out)
{
  if (lex_match_id (lexer, name))
    {
      lex_match (lexer, T_EQUALS);
      force_int (lexer);
      *out = lex_integer (lexer);
      lex_get (lexer);
      return true;
    }
  else
    return false;
}

static void
read_font_style (struct lexer *lexer, struct font_style *fs)
{
  while (parse_yesno_setting (lexer, "BOLD", &fs->bold)
         || parse_yesno_setting (lexer, "ITALIC", &fs->italic)
         || parse_yesno_setting (lexer, "UNDERLINE", &fs->underline)
         || parse_yesno_setting (lexer, "MARKUP", &fs->markup)
         || parse_color_pair_setting (lexer, "FG", fs->fg)
         || parse_color_pair_setting (lexer, "BG", fs->bg)
         || parse_string_setting (lexer, "FACE", &fs->typeface)
         || parse_int_setting (lexer, "SIZE", &fs->size))
    continue;
}

static bool
parse_halign_setting (struct lexer *lexer, enum table_halign *halign,
                      double *decimal_offset)
{
  if (lex_match_id (lexer, "RIGHT"))
    *halign = TABLE_HALIGN_RIGHT;
  else if (lex_match_id (lexer, "LEFT"))
    *halign = TABLE_HALIGN_LEFT;
  else if (lex_match_id (lexer, "CELL"))
    *halign = TABLE_HALIGN_CENTER;
  else if (lex_match_id (lexer, "MIXED"))
    *halign = TABLE_HALIGN_MIXED;
  else if (lex_match_id (lexer, "DECIMAL"))
    {
      if (lex_is_number (lexer))
        {
          *decimal_offset = lex_number (lexer);
          lex_get (lexer);
        }
    }
  else
    return false;

  return true;
}

static bool
parse_valign_setting (struct lexer *lexer, enum table_valign *valign)
{
  if (lex_match_id (lexer, "TOP"))
    *valign = TABLE_VALIGN_TOP;
  else if (lex_match_id (lexer, "MIDDLE"))
    *valign = TABLE_VALIGN_CENTER;
  else if (lex_match_id (lexer, "BOTTOM"))
    *valign = TABLE_VALIGN_BOTTOM;
  else
    return false;

  return true;
}

static bool
parse_margin_setting (struct lexer *lexer, int margin[TABLE_N_AXES][2])
{
  if (lex_match_id (lexer, "MARGINS"))
    {
      int values[4];
      int n = 0;

      lex_match (lexer, T_EQUALS);
      force_num (lexer);
      while (lex_is_number (lexer) && n < 4)
        {
          values[n++] = lex_number (lexer);
          lex_get (lexer);
        }

      if (n == 1)
        {
          margin[TABLE_HORZ][0] = margin[TABLE_HORZ][1] = values[0];
          margin[TABLE_VERT][0] = margin[TABLE_VERT][1] = values[0];
        }
      else if (n == 2)
        {
          margin[TABLE_HORZ][0] = margin[TABLE_HORZ][1] = values[1];
          margin[TABLE_VERT][0] = margin[TABLE_VERT][1] = values[0];
        }
      else if (n == 3)
        {
          margin[TABLE_VERT][0] = values[0];
          margin[TABLE_HORZ][0] = margin[TABLE_HORZ][1] = values[1];
          margin[TABLE_VERT][1] = values[2];
        }
      else
        {
          assert (n == 4);
          margin[TABLE_VERT][0] = values[0];
          margin[TABLE_HORZ][1] = values[1];
          margin[TABLE_VERT][1] = values[2];
          margin[TABLE_HORZ][0] = values[3];
        }

      return true;
    }
  else
    return false;
}

static void
read_cell_style (struct lexer *lexer, struct cell_style *cs)
{
  while (parse_halign_setting (lexer, &cs->halign, &cs->decimal_offset)
         || parse_valign_setting (lexer, &cs->valign)
         || parse_margin_setting (lexer, cs->margin))
    continue;
}

static void
read_value_option (struct lexer *lexer, const struct pivot_table *pt,
                   struct pivot_value *value,
                   const struct table_area_style *base_style)
{
  enum settings_value_show *show
    = (value->type == PIVOT_VALUE_NUMERIC ? &value->numeric.show
       : value->type == PIVOT_VALUE_STRING ? &value->string.show
       : value->type == PIVOT_VALUE_VARIABLE ? &value->variable.show
       : NULL);
  if (show && parse_settings_value_show (lexer, "SHOW", show))
    return;

  char **var_name
    = (value->type == PIVOT_VALUE_NUMERIC ? &value->numeric.var_name
       : value->type == PIVOT_VALUE_STRING ? &value->string.var_name
       : NULL);
  if (var_name && parse_string_setting (lexer, "VAR", var_name))
    return;

  char **label
    = (value->type == PIVOT_VALUE_NUMERIC ? &value->numeric.value_label
       : value->type == PIVOT_VALUE_STRING ? &value->string.value_label
       : value->type == PIVOT_VALUE_VARIABLE ? &value->variable.var_label
       : NULL);
  if (label && parse_string_setting (lexer, "LABEL", label))
    return;

  if (value->type == PIVOT_VALUE_STRING && lex_match_id (lexer, "HEX"))
    {
      value->string.hex = true;
      return;
    }

  if (value->type == PIVOT_VALUE_NUMERIC)
    {
      msg_disable ();
      struct fmt_spec fmt;
      bool ok = parse_format_specifier (lexer, &fmt);
      msg_enable ();

      if (ok)
        {
          if (!fmt_check_output (&fmt)
              || !fmt_check_type_compat (&fmt, VAL_NUMERIC))
            exit (1);

          value->numeric.format = fmt;
          return;
        }
    }

  if (lex_match_id (lexer, "SUBSCRIPTS"))
    {
      lex_match (lexer, T_EQUALS);
      size_t allocated_subscripts = value->n_subscripts;
      while (lex_token (lexer) == T_STRING)
        {
          if (value->n_subscripts >= allocated_subscripts)
            value->subscripts = x2nrealloc (value->subscripts,
                                            &allocated_subscripts,
                                            sizeof *value->subscripts);

          value->subscripts[value->n_subscripts++] = xstrdup (
            lex_tokcstr (lexer));
          lex_get (lexer);
        }
      return;
    }

  if (lex_match_id (lexer, "FONT") && base_style)
    {
      lex_match (lexer, T_EQUALS);

      if (!value->font_style)
        {
          value->font_style = xmalloc (sizeof *value->font_style);
          font_style_copy (NULL, value->font_style, &base_style->font_style);
        }
      read_font_style (lexer, value->font_style);
      return;
    }

  if (lex_match_id (lexer, "CELL") && base_style)
    {
      lex_match (lexer, T_EQUALS);

      if (!value->cell_style)
        {
          value->cell_style = xmalloc (sizeof *value->cell_style);
          *value->cell_style = base_style->cell_style;
        }
      read_cell_style (lexer, value->cell_style);
      return;
    }

  if (lex_match_id (lexer, "FOOTNOTE"))
    {
      lex_match (lexer, T_EQUALS);

      while (lex_is_integer (lexer))
        {
          size_t idx = lex_integer (lexer);
          lex_get (lexer);

          if (idx >= pt->n_footnotes)
            {
              msg (SE, "Footnote %zu not available "
                   "(only %zu footnotes defined)", idx, pt->n_footnotes);
              exit (1);
            }
          pivot_value_add_footnote (value, pt->footnotes[idx]);
        }
      return;
    }

  lex_error (lexer, "Expecting valid value option");
  exit (1);
}

static struct pivot_value *
read_value (struct lexer *lexer, const struct pivot_table *pt,
            const struct table_area_style *base_style)
{
  struct pivot_value *value;
  if (lex_is_number (lexer))
    {
      value = pivot_value_new_number (lex_number (lexer));
      lex_get (lexer);
    }
  else if (lex_is_string (lexer))
    {
      value = xmalloc (sizeof *value);
      *value = (struct pivot_value) {
        .type = PIVOT_VALUE_STRING,
        .string = { .s = xstrdup (lex_tokcstr (lexer)) },
      };
      lex_get (lexer);
    }
  else if (lex_token (lexer) == T_ID)
    {
      value = xmalloc (sizeof *value);
      *value = (struct pivot_value) {
        .type = PIVOT_VALUE_VARIABLE,
        .variable = { .var_name = xstrdup (lex_tokcstr (lexer)) },
      };
      lex_get (lexer);
    }
  else
    {
      msg (SE, "Expecting pivot_value");
      exit (1);
    }

  while (lex_match (lexer, T_LBRACK))
    {
      read_value_option (lexer, pt, value, base_style);
      force_match (lexer, T_RBRACK);
    }

  return value;
}

static void
read_group (struct lexer *lexer, struct pivot_table *pt,
            struct pivot_category *group,
            const struct table_area_style *label_style)
{
  if (lex_match (lexer, T_ASTERISK))
    group->show_label = true;

  force_match (lexer, T_LPAREN);
  if (lex_match (lexer, T_RPAREN))
    return;

  do
    {
      struct pivot_value *name = read_value (lexer, pt, label_style);
      if (lex_token (lexer) == T_ASTERISK
          || lex_token (lexer) == T_LPAREN)
        read_group (lexer, pt, pivot_category_create_group__ (group, name),
                    label_style);
      else
        {
          char *rc;
          if (lex_token (lexer) == T_ID
              && is_pivot_result_class (lex_tokcstr (lexer)))
            {
              rc = xstrdup (lex_tokcstr (lexer));
              lex_get (lexer);
            }
          else
            rc = NULL;

          pivot_category_create_leaf_rc (group, name, rc);

          free (rc);
        }
    }
  while (lex_match (lexer, T_COMMA));
  force_match (lexer, T_RPAREN);
}

static void
read_dimension (struct lexer *lexer, struct pivot_table *pt,
                enum pivot_axis_type a,
                const struct table_area_style *label_style)
{
  if (!pivot_table_is_empty (pt))
    error (1, 0, "can't add dimensions after adding data");

  lex_match (lexer, T_EQUALS);

  struct pivot_value *name = read_value (lexer, pt, label_style);
  struct pivot_dimension *dim = pivot_dimension_create__ (pt, a, name);
  read_group (lexer, pt, dim->root, label_style);
}

static void
read_look (struct lexer *lexer, struct pivot_table *pt)
{
  lex_match (lexer, T_EQUALS);

  if (lex_is_string (lexer))
    {
      struct pivot_table_look *look;
      char *error = pivot_table_look_read (lex_tokcstr (lexer), &look);
      if (error)
        {
          msg (SE, "%s", error);
          exit (1);
        }
      lex_get (lexer);

      pivot_table_set_look (pt, look);
      pivot_table_look_unref (look);
    }

  struct pivot_table_look *look = pivot_table_look_unshare (
    pivot_table_look_ref (pt->look));
  for (;;)
    {
      if (!parse_bool_setting (lexer, "EMPTY", "HIDE", "SHOW",
                               &look->omit_empty)
          && !parse_bool_setting (lexer, "ROWLABELS", "CORNER", "NESTED",
                                  &look->row_labels_in_corner)
          && !parse_bool_setting (lexer, "MARKERS", "NUMERIC", "ALPHA",
                                  &look->show_numeric_markers)
          && !parse_bool_setting (lexer, "LEVEL", "SUPER", "SUB",
                                  &look->footnote_marker_superscripts)
          && !parse_bool_setting (lexer, "LAYERS", "ALL", "CURRENT",
                                  &look->print_all_layers)
          && !parse_bool_setting (lexer, "PAGINATELAYERS", "YES", "NO",
                                  &look->paginate_layers)
          && !parse_bool_setting (lexer, "HSHRINK", "YES", "NO",
                                  &look->shrink_to_fit[TABLE_HORZ])
          && !parse_bool_setting (lexer, "VSHRINK", "YES", "NO",
                                  &look->shrink_to_fit[TABLE_VERT])
          && !parse_bool_setting (lexer, "TOPCONTINUATION", "YES", "NO",
                                  &look->top_continuation)
          && !parse_bool_setting (lexer, "BOTTOMCONTINUATION", "YES", "NO",
                                  &look->bottom_continuation)
          && !parse_string_setting (lexer, "CONTINUATION",
                                    &look->continuation))
        break;
    }
  pivot_table_set_look (pt, look);
  pivot_table_look_unref (look);
}

static enum table_stroke
read_stroke (struct lexer *lexer)
{
  for (int stroke = 0; stroke < TABLE_N_STROKES; stroke++)
    if (lex_match_id (lexer, table_stroke_to_string (stroke)))
      return stroke;

  lex_error (lexer, "expecting stroke");
  exit (1);
}

static bool
parse_value_setting (struct lexer *lexer, const struct pivot_table *pt,
                     const char *name,
                     struct pivot_value **valuep,
                     struct table_area_style *base_style)
{
  if (lex_match_id (lexer, name))
    {
      lex_match (lexer, T_EQUALS);

      pivot_value_destroy (*valuep);
      *valuep = read_value (lexer, pt, base_style);

      return true;
    }
  else
    return false;
}

static void
read_border (struct lexer *lexer, struct pivot_table *pt)
{
  static const char *const pivot_border_ids[PIVOT_N_BORDERS] = {
    [PIVOT_BORDER_TITLE] = "title",
    [PIVOT_BORDER_OUTER_LEFT] = "outer-left",
    [PIVOT_BORDER_OUTER_TOP] = "outer-top",
    [PIVOT_BORDER_OUTER_RIGHT] = "outer-right",
    [PIVOT_BORDER_OUTER_BOTTOM] = "outer-bottom",
    [PIVOT_BORDER_INNER_LEFT] = "inner-left",
    [PIVOT_BORDER_INNER_TOP] = "inner-top",
    [PIVOT_BORDER_INNER_RIGHT] = "inner-right",
    [PIVOT_BORDER_INNER_BOTTOM] = "inner-bottom",
    [PIVOT_BORDER_DATA_LEFT] = "data-left",
    [PIVOT_BORDER_DATA_TOP] = "data-top",
    [PIVOT_BORDER_DIM_ROW_HORZ] = "dim-row-horz",
    [PIVOT_BORDER_DIM_ROW_VERT] = "dim-row-vert",
    [PIVOT_BORDER_DIM_COL_HORZ] = "dim-col-horz",
    [PIVOT_BORDER_DIM_COL_VERT] = "dim-col-vert",
    [PIVOT_BORDER_CAT_ROW_HORZ] = "cat-row-horz",
    [PIVOT_BORDER_CAT_ROW_VERT] = "cat-row-vert",
    [PIVOT_BORDER_CAT_COL_HORZ] = "cat-col-horz",
    [PIVOT_BORDER_CAT_COL_VERT] = "cat-col-vert",
  };

  lex_match (lexer, T_EQUALS);

  struct pivot_table_look *look = pivot_table_look_unshare (
    pivot_table_look_ref (pt->look));
  while (lex_token (lexer) == T_STRING)
    {
      char *s = xstrdup (lex_tokcstr (lexer));
      lex_get (lexer);
      force_match (lexer, T_LPAREN);

      struct table_border_style style = TABLE_BORDER_STYLE_INITIALIZER;
      style.stroke = read_stroke (lexer);
      if (lex_is_string (lexer))
        style.color = read_color (lexer);
      force_match (lexer, T_RPAREN);

      int n = 0;
      for (int b = 0; b < PIVOT_N_BORDERS; b++)
        {
          if (!fnmatch (s, pivot_border_ids[b], 0))
            {
              look->borders[b] = style;
              n++;
            }
        }
      if (!n)
        {
          msg (SE, "%s: no matching borders", s);
          exit (1);
        }
      free (s);
    }
  pivot_table_set_look (pt, look);
  pivot_table_look_unref (look);
}

static void
read_footnote (struct lexer *lexer, struct pivot_table *pt)
{
  size_t idx;
  if (lex_match (lexer, T_LBRACK))
    {
      force_int (lexer);

      idx = lex_integer (lexer);
      lex_get (lexer);

      force_match (lexer, T_RBRACK);
    }
  else
    idx = pt->n_footnotes;
  lex_match (lexer, T_EQUALS);

  struct pivot_value *content
    = read_value (lexer, pt, &pt->look->areas[PIVOT_AREA_FOOTER]);

  struct pivot_value *marker;
  if (lex_match_id (lexer, "MARKER"))
    {
      lex_match (lexer, T_EQUALS);
      marker = read_value (lexer, pt, &pt->look->areas[PIVOT_AREA_FOOTER]);
    }
  else
    marker = NULL;

  pivot_table_create_footnote__ (pt, idx, marker, content);
}

static void
read_cell (struct lexer *lexer, struct pivot_table *pt)
{
  force_match (lexer, T_LBRACK);

  size_t *lo = xnmalloc (pt->n_dimensions, sizeof *lo);
  size_t *hi = xnmalloc (pt->n_dimensions, sizeof *hi);
  for (size_t i = 0; i < pt->n_dimensions; i++)
    {
      const struct pivot_dimension *d = pt->dimensions[i];

      if (i)
        force_match (lexer, T_COMMA);

      if (!d->n_leaves)
        {
          msg (SE, "can't define data because dimension %zu has no categories",
               i);
          exit (1);
        }

      if (lex_match (lexer, T_ALL))
        {
          lo[i] = 0;
          hi[i] = d->n_leaves - 1;
        }
      else
        {
          force_int (lexer);
          lo[i] = hi[i] = lex_integer (lexer);
          lex_get (lexer);

          if (lex_match_id (lexer, "THRU"))
            {
              force_int (lexer);
              hi[i] = lex_integer (lexer);
              lex_get (lexer);
            }

          if (hi[i] < lo[i])
            {
              msg (SE, "%zu THRU %zu is not a valid range", lo[i], hi[i]);
              exit (1);
            }
          if (hi[i] >= d->n_leaves)
            {
              msg (SE, "dimension %zu (%s) has only %zu categories",
                   i, pivot_value_to_string (d->root->name, pt),
                   d->n_leaves);
              exit (1);
            }
        }
    }
  force_match (lexer, T_RBRACK);

  struct pivot_value *value = NULL;
  bool delete = false;
  if (lex_match (lexer, T_EQUALS))
    {
      if (lex_match_id (lexer, "DELETE"))
        delete = true;
      else
        value = read_value (lexer, pt, &pt->look->areas[PIVOT_AREA_DATA]);
    }

  size_t *dindexes = xmemdup (lo, pt->n_dimensions * sizeof *lo);
  for (size_t i = 0; ; i++)
    {
      if (delete)
        pivot_table_delete (pt, dindexes);
      else
        pivot_table_put (pt, dindexes, pt->n_dimensions,
                         (value
                          ? pivot_value_clone (value)
                          : pivot_value_new_integer (i)));

      for (size_t j = 0; j < pt->n_dimensions; j++)
        {
          if (++dindexes[j] <= hi[j])
            goto next;
          dindexes[j] = lo[j];
        }
        break;
    next:;
    }
  free (dindexes);

  pivot_value_destroy (value);

  free (lo);
  free (hi);
}

static struct pivot_dimension *
parse_dim_name (struct lexer *lexer, struct pivot_table *table)
{
  force_string (lexer);
  for (size_t i = 0; i < table->n_dimensions; i++)
    {
      struct pivot_dimension *dim = table->dimensions[i];

      struct string s = DS_EMPTY_INITIALIZER;
      pivot_value_format_body (dim->root->name, table, &s);
      bool match = !strcmp (ds_cstr (&s), lex_tokcstr (lexer));
      ds_destroy (&s);

      if (match)
        {
          lex_get (lexer);
          return dim;
        }
    }

  lex_error (lexer, "unknown dimension");
  exit (1);
}

static enum pivot_axis_type
parse_axis_type (struct lexer *lexer)
{
  if (lex_match_id (lexer, "ROW"))
    return PIVOT_AXIS_ROW;
  else if (lex_match_id (lexer, "COLUMN"))
    return PIVOT_AXIS_COLUMN;
  else if (lex_match_id (lexer, "LAYER"))
    return PIVOT_AXIS_LAYER;
  else
    {
      lex_error_expecting (lexer, "ROW", "COLUMN", "LAYER");
      exit (1);
    }
}

static void
move_dimension (struct lexer *lexer, struct pivot_table *table)
{
  struct pivot_dimension *dim = parse_dim_name (lexer, table);

  enum pivot_axis_type axis = parse_axis_type (lexer);

  size_t position;
  if (lex_is_integer (lexer))
    {
      position = lex_integer (lexer);
      lex_get (lexer);
    }
  else
    position = 0;

  pivot_table_move_dimension (table, dim, axis, position);
}

static void
swap_axes (struct lexer *lexer, struct pivot_table *table)
{
  enum pivot_axis_type a = parse_axis_type (lexer);
  enum pivot_axis_type b = parse_axis_type (lexer);
  pivot_table_swap_axes (table, a, b);
}

static void
read_current_layer (struct lexer *lexer, struct pivot_table *table)
{
  lex_match (lexer, T_EQUALS);

  const struct pivot_axis *layer_axis = &table->axes[PIVOT_AXIS_LAYER];
  for (size_t i = 0; i < layer_axis->n_dimensions; i++)
    {
      const struct pivot_dimension *dim = layer_axis->dimensions[i];

      force_int (lexer);
      size_t index = lex_integer (lexer);
      if (index >= dim->n_leaves)
        {
          lex_error (lexer, "only %zu dimensions", dim->n_leaves);
          exit (1);
        }
      lex_get (lexer);

      table->current_layer[i] = index;
    }
}

static struct pivot_table *
read_table (struct lexer *lexer)
{
  struct pivot_table *pt = pivot_table_create ("Default Title");
  while (lex_match (lexer, T_SLASH))
    {
      assert (!pivot_table_is_shared (pt));

      if (lex_match_id (lexer, "ROW"))
        read_dimension (lexer, pt, PIVOT_AXIS_ROW,
                        &pt->look->areas[PIVOT_AREA_ROW_LABELS]);
      else if (lex_match_id (lexer, "COLUMN"))
        read_dimension (lexer, pt, PIVOT_AXIS_COLUMN,
                        &pt->look->areas[PIVOT_AREA_COLUMN_LABELS]);
      else if (lex_match_id (lexer, "LAYER"))
        read_dimension (lexer, pt, PIVOT_AXIS_LAYER,
                        &pt->look->areas[PIVOT_AREA_LAYERS]);
      else if (lex_match_id (lexer, "LOOK"))
        read_look (lexer, pt);
      else if (lex_match_id (lexer, "ROTATE"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) == T_ID)
            if (!parse_bool_setting (lexer, "INNERCOLUMNS", "YES", "NO",
                                     &pt->rotate_inner_column_labels)
                && !parse_bool_setting (lexer, "OUTERROWS", "YES", "NO",
                                        &pt->rotate_outer_row_labels))
              break;
        }
      else if (lex_match_id (lexer, "SHOW"))
        {
          lex_match (lexer, T_EQUALS);
          while (lex_token (lexer) == T_ID)
            {
              if (parse_bool_setting (lexer, "GRID", "YES", "NO",
                                      &pt->show_grid_lines)
                  || parse_bool_setting (lexer, "CAPTION", "YES", "NO",
                                         &pt->show_caption)
                  || parse_bool_setting (lexer, "TITLE", "YES", "NO",
                                         &pt->show_title))
                continue;

              if (parse_settings_value_show (lexer, "VALUES", &pt->show_values)
                  || parse_settings_value_show (lexer, "VARIABLES",
                                                &pt->show_variables))
                continue;

              if (lex_match_id (lexer, "LAYER"))
                read_current_layer (lexer, pt);

              break;
            }
        }
      else if (parse_value_setting (lexer, pt, "TITLE", &pt->title,
                                    &pt->look->areas[PIVOT_AREA_TITLE])
               || parse_value_setting (lexer, pt, "SUBTYPE", &pt->subtype,
                                       NULL)
               || parse_value_setting (lexer, pt, "CORNER", &pt->corner_text,
                                       &pt->look->areas[PIVOT_AREA_CORNER])
               || parse_value_setting (lexer, pt, "CAPTION", &pt->caption,
                                       &pt->look->areas[PIVOT_AREA_CAPTION])
               || parse_string_setting (lexer, "NOTES", &pt->notes))
        {
          /* Nothing. */
        }
      else if (lex_match_id (lexer, "BORDER"))
        read_border (lexer, pt);
      else if (lex_match_id (lexer, "TRANSPOSE"))
        pivot_table_transpose (pt);
      else if (lex_match_id (lexer, "SWAP"))
        swap_axes (lexer, pt);
      else if (lex_match_id (lexer, "MOVE"))
        move_dimension (lexer, pt);
      else if (lex_match_id (lexer, "CELLS"))
        read_cell (lexer, pt);
      else if (lex_match_id (lexer, "FOOTNOTE"))
        read_footnote (lexer, pt);
      else if (lex_match_id (lexer, "DUMP"))
        pivot_table_dump (pt, 0);
      else if (lex_match_id (lexer, "DISPLAY"))
        {
          pivot_table_submit (pivot_table_ref (pt));
          pt = pivot_table_unshare (pt);
        }
      else
        {
          msg (SE, "Expecting keyword");
          exit (1);
        }
    }

  force_match (lexer, T_ENDCMD);
  return pt;
}

static void
output_msg (const struct msg *m_, void *lexer_)
{
  struct lexer *lexer = lexer_;
  struct msg m = *m_;

  if (m.file_name == NULL)
    {
      m.file_name = CONST_CAST (char *, lex_get_file_name (lexer));
      m.first_line = lex_get_first_line_number (lexer, 0);
      m.last_line = lex_get_last_line_number (lexer, 0);
    }

  m.command_name = output_get_command_name ();

  message_item_submit (message_item_create (&m));

  free (m.command_name);
}
