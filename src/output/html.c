/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2011, 2012, 2013, 2014, 2017,
   2020 Free Software Foundation, Inc.

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
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <locale.h>

#include "data/file-name.h"
#include "data/file-handle-def.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/version.h"
#include "output/cairo-chart.h"
#include "output/chart.h"
#include "output/driver-provider.h"
#include "output/options.h"
#include "output/output-item.h"
#include "output/pivot-output.h"
#include "output/pivot-table.h"
#include "output/table-provider.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* This file uses TABLE_HORZ and TABLE_VERT enough to warrant abbreviating. */
#define H TABLE_HORZ
#define V TABLE_VERT

struct html_driver
  {
    struct output_driver driver;
    struct cell_color fg;
    struct cell_color bg;
    struct file_handle *handle;
    char *chart_file_name;

    FILE *file;
    size_t chart_cnt;

    bool bare;
    bool css;
    bool borders;
  };

static const struct output_driver_class html_driver_class;

static void html_output_table (struct html_driver *,
                               const struct output_item *);
static void escape_string (FILE *file, const char *text,
                           const char *space, const char *newline);
static void print_title_tag (FILE *file, const char *name,
                             const char *content);

static struct html_driver *
html_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &html_driver_class);
  return UP_CAST (driver, struct html_driver, driver);
}

static struct driver_option *
opt (struct output_driver *d, struct string_map *options, const char *key,
     const char *default_value)
{
  return driver_option_get (d, options, key, default_value);
}

static void
put_header (struct html_driver *html)
{
  fputs ("<!doctype html>\n", html->file);
  fprintf (html->file, "<html");
  char *ln = get_language ();
  if (ln)
    fprintf (html->file, " lang=\"%s\"", ln);
  free (ln);
  fprintf (html->file, ">\n");
  fputs ("<head>\n", html->file);
  print_title_tag (html->file, "title", _("PSPP Output"));
  fprintf (html->file, "<meta name=\"generator\" content=\"%s\">\n", version);
  fputs ("<meta http-equiv=\"content-type\" "
         "content=\"text/html; charset=utf-8\">\n", html->file);

  if (html->css)
    {
      fputs ("<style>\n"
	     "<!--\n"
	     "body {\n"
	     "  background: white;\n"
	     "  color: black;\n"
	     "  padding: 0em 12em 0em 3em;\n"
	     "  margin: 0\n"
	     "}\n"
	     "body>p {\n"
	     "  margin: 0pt 0pt 0pt 0em\n"
	     "}\n"
	     "body>p + p {\n"
	     "  text-indent: 1.5em;\n"
	     "}\n"
	     "h1 {\n"
	     "  font-size: 150%;\n"
	     "  margin-left: -1.33em\n"
	     "}\n"
	     "h2 {\n"
	     "  font-size: 125%;\n"
	     "  font-weight: bold;\n"
	     "  margin-left: -.8em\n"
	     "}\n"
	     "h3 {\n"
	     "  font-size: 100%;\n"
	     "  font-weight: bold;\n"
	     "  margin-left: -.5em }\n"
	     "h4 {\n"
	     "  font-size: 100%;\n"
	     "  margin-left: 0em\n"
	     "}\n"
	     "h1, h2, h3, h4, h5, h6 {\n"
	     "  font-family: sans-serif;\n"
	     "  color: blue\n"
	     "}\n"
	     "html {\n"
	     "  margin: 0\n"
	     "}\n"
	     "code {\n"
	     "  font-family: sans-serif\n"
	     "}\n"
	     "table {\n"
	     "  border-collapse: collapse;\n"
	     "  margin-bottom: 1em\n"
	     "}\n"
	     "caption {\n"
	     "  text-align: left\n"
	     "}\n"
	     "th { font-weight: normal }\n"
	     "a:link {\n"
	     "  color: #1f00ff;\n"
	     "}\n"
	     "a:visited {\n"
	     "  color: #9900dd;\n"
	     "}\n"
	     "a:active {\n"
	     "  color: red;\n"
	     "}\n"
	     "-->\n"
	     "</style>\n",
	     html->file);
    }
  fputs ("</head>\n", html->file);
  fputs ("<body>\n", html->file);
}

static struct output_driver *
html_create (struct file_handle *fh, enum settings_output_devices device_type,
             struct string_map *o)
{
  struct output_driver *d;
  struct html_driver *html;

  html = xzalloc (sizeof *html);
  d = &html->driver;
  output_driver_init (&html->driver, &html_driver_class, fh_get_file_name (fh),
                      device_type);
  html->bare = parse_boolean (opt (d, o, "bare", "false"));
  html->css = parse_boolean (opt (d, o, "css", "true"));
  html->borders = parse_boolean (opt (d, o, "borders", "true"));

  html->handle = fh;
  html->chart_file_name = parse_chart_file_name (opt (d, o, "charts",
                                                      fh_get_file_name (fh)));
  html->file = NULL;
  html->chart_cnt = 1;
  html->bg = parse_color (opt (d, o, "background-color", "#FFFFFFFFFFFF"));
  html->fg = parse_color (opt (d, o, "foreground-color", "#000000000000"));
  html->file = fn_open (html->handle, "w");
  if (html->file == NULL)
    {
      msg_error (errno, _("error opening output file `%s'"), fh_get_file_name (html->handle));
      goto error;
    }

  if (!html->bare)
    put_header (html);

  return d;

 error:
  output_driver_destroy (d);
  return NULL;
}

/* Emits <NAME>CONTENT</NAME> to the output, escaping CONTENT as
   necessary for HTML. */
static void
print_title_tag (FILE *file, const char *name, const char *content)
{
  if (content != NULL)
    {
       fprintf (file, "<%s>", name);
      escape_string (file, content, " ", " - ");
      fprintf (file, "</%s>\n", name);
    }
}

static void
html_destroy (struct output_driver *driver)
{
  struct html_driver *html = html_driver_cast (driver);

  if (html->file != NULL)
    {
      if (!html->bare)
        fprintf (html->file,
                 "</body>\n"
                 "</html>\n"
                 "<!-- end of file -->\n");
      fn_close (html->handle, html->file);
    }
  free (html->chart_file_name);
  fh_unref (html->handle);
  free (html);
}

static void
html_submit__ (struct output_driver *driver, const struct output_item *item,
               int level)
{
  struct html_driver *html = html_driver_cast (driver);

  switch (item->type)
    {
    case OUTPUT_ITEM_CHART:
      if (html->chart_file_name)
        {
          char *file_name = xr_draw_png_chart (item->chart,
                                               html->chart_file_name,
                                               html->chart_cnt++,
                                               &html->fg, &html->bg);
          if (file_name != NULL)
            {
              const char *title = chart_get_title (item->chart);
              fprintf (html->file, "<img src=\"%s\" alt=\"chart: %s\">",
                       file_name, title ? title : _("No description"));
              free (file_name);
            }
        }
      break;

    case OUTPUT_ITEM_GROUP:
      for (size_t i = 0; i < item->group.n_children; i++)
        html_submit__ (driver, item->group.children[i], level + 1);
      break;

    case OUTPUT_ITEM_IMAGE:
      if (html->chart_file_name)
        {
          char *file_name = xr_write_png_image (
            item->image, html->chart_file_name, ++html->chart_cnt);
          if (file_name != NULL)
            {
              fprintf (html->file, "<img src=\"%s\">", file_name);
              free (file_name);
            }
        }
      break;

    case OUTPUT_ITEM_MESSAGE:
      fprintf (html->file, "<p>");

      char *s = msg_to_string (item->message);
      escape_string (html->file, s, " ", "<br>");
      free (s);

      fprintf (html->file, "</p>\n");
      break;

    case OUTPUT_ITEM_PAGE_BREAK:
      break;

    case OUTPUT_ITEM_TABLE:
      html_output_table (html, item);
      break;

    case OUTPUT_ITEM_TEXT:
      {
        char *s = text_item_get_plain_text (item);

        switch (item->text.subtype)
          {
          case TEXT_ITEM_PAGE_TITLE:
            break;

          case TEXT_ITEM_TITLE:
            {
              char tag[3] = { 'H', MIN (5, level) + '0', '\0' };
              print_title_tag (html->file, tag, s);
            }
            break;

          case TEXT_ITEM_SYNTAX:
            fprintf (html->file, "<pre class=\"syntax\">");
            escape_string (html->file, s, " ", "<br>");
            fprintf (html->file, "</pre>\n");
            break;

          case TEXT_ITEM_LOG:
            fprintf (html->file, "<p>");
            escape_string (html->file, s, " ", "<br>");
            fprintf (html->file, "</p>\n");
            break;
          }

        free (s);
      }
      break;
    }
}

static void
html_submit (struct output_driver *driver, const struct output_item *item)
{
  html_submit__ (driver, item, 1);
}

/* Write TEXT to file F, escaping characters as necessary for HTML.  Spaces are
   replaced by SPACE, which should be " " or "&nbsp;" New-lines are replaced by
   NEWLINE, which might be "<BR>" or "\n" or something else appropriate. */
static void
escape_string (FILE *file, const char *text,
               const char *space, const char *newline)
{
  for (;;)
    {
      char c = *text++;
      switch (c)
        {
        case 0:
          return;
        case '\n':
          fputs (newline, file);
          break;
        case '&':
          fputs ("&amp;", file);
          break;
        case '<':
          fputs ("&lt;", file);
          break;
        case '>':
          fputs ("&gt;", file);
          break;
        case ' ':
          fputs (space, file);
          break;
        case '"':
          fputs ("&quot;", file);
          break;
        default:
          putc (c, file);
          break;
        }
    }
}

static const char *
border_to_css (int border)
{
  switch (border)
    {
    case TABLE_STROKE_NONE:
      return NULL;

    case TABLE_STROKE_SOLID:
      return "solid";

    case TABLE_STROKE_DASHED:
      return "dashed";

    case TABLE_STROKE_THICK:
      return "thick solid";

    case TABLE_STROKE_THIN:
      return "thin solid";

    case TABLE_STROKE_DOUBLE:
      return "double";

    default:
      return NULL;
    }

}

struct css_style
{
  FILE *file;
  int n_styles;
};

static void
style_start (struct css_style *cs, FILE *file)
{
  *cs = (struct css_style) {
    .file = file,
    .n_styles = 0,
  };
}

static void
style_end (struct css_style *cs)
{
  if (cs->n_styles > 0)
    fputs ("'", cs->file);
}

static void
next_style (struct css_style *st)
{
  bool first = !st->n_styles++;
  fputs (first ? " style='" : "; ", st->file);
}

static void
put_style (struct css_style *st, const char *name, const char *value)
{
  next_style (st);
  fprintf (st->file, "%s: %s", name, value);
}

static bool
format_color (const struct cell_color color,
              const struct cell_color default_color,
              char *buf, size_t bufsize)
{
  bool retval = !cell_color_equal (&color, &default_color);
  if (retval)
    {
      if (color.alpha == 255)
        snprintf (buf, bufsize, "#%02x%02x%02x", color.r, color.g, color.b);
      else
        snprintf (buf, bufsize, "rgba(%d, %d, %d, %.3f)",
                  color.r, color.g, color.b, color.alpha / 255.0);
    }
  return retval;
}

static void
put_border (const struct table *table, const struct table_cell *cell,
            struct css_style *style,
            enum table_axis axis, int h, int v,
            const char *border_name)
{
  struct cell_color color;
  const char *css = border_to_css (
    table_get_rule (table, axis, cell->d[H][h], cell->d[V][v], &color));
  if (css)
    {
      next_style (style);
      fprintf (style->file, "border-%s: %s", border_name, css);

      char buf[32];
      if (format_color (color, (struct cell_color) CELL_COLOR_BLACK,
                        buf, sizeof buf))
        fprintf (style->file, " %s", buf);
    }
}

static void
html_put_table_cell (struct html_driver *html, const struct pivot_table *pt,
                     const struct table_cell *cell,
                     const char *tag, const struct table *t)
{
  fprintf (html->file, "<%s", tag);

  struct css_style style;
  style_start (&style, html->file);

  struct string body = DS_EMPTY_INITIALIZER;
  bool numeric = pivot_value_format_body (cell->value, pt, &body);

  enum table_halign halign = table_halign_interpret (cell->cell_style->halign,
                                                     numeric);

  switch (halign)
    {
    case TABLE_HALIGN_RIGHT:
      put_style (&style, "text-align", "right");
      break;
    case TABLE_HALIGN_CENTER:
      put_style (&style, "text-align", "center");
      break;
    default:
      /* Do nothing */
      break;
    }

  if (cell->options & TAB_ROTATE)
    put_style (&style, "writing-mode", "sideways-lr");

  if (cell->cell_style->valign != TABLE_VALIGN_TOP)
    {
      put_style (&style, "vertical-align",
                 (cell->cell_style->valign == TABLE_VALIGN_BOTTOM
                  ? "bottom" : "middle"));
    }

  const struct font_style *fs = cell->font_style;
  char bgcolor[32];
  if (format_color (fs->bg[cell->d[V][0] % 2],
                    (struct cell_color) CELL_COLOR_WHITE,
                    bgcolor, sizeof bgcolor))
    put_style (&style, "background", bgcolor);

  char fgcolor[32];
  if (format_color (fs->fg[cell->d[V][0] % 2],
                    (struct cell_color) CELL_COLOR_BLACK,
                    fgcolor, sizeof fgcolor))
    put_style (&style, "color", fgcolor);

  if (fs->typeface)
    {
      put_style (&style, "font-family", "\"");
      escape_string (html->file, fs->typeface, " ", "\n");
      putc ('"', html->file);
    }
  if (fs->bold)
    put_style (&style, "font-weight", "bold");
  if (fs->italic)
    put_style (&style, "font-style", "italic");
  if (fs->underline)
    put_style (&style, "text-decoration", "underline");
  if (fs->size)
    {
      char buf[32];
      snprintf (buf, sizeof buf, "%dpt", fs->size);
      put_style (&style, "font-size", buf);
    }

  if (t && html->borders)
    {
      put_border (t, cell, &style, V, 0, 0, "top");
      put_border (t, cell, &style, H, 0, 0, "left");

      if (cell->d[V][1] == t->n[V])
        put_border (t, cell, &style, V, 0, 1, "bottom");
      if (cell->d[H][1] == t->n[H])
        put_border (t, cell, &style, H, 1, 0, "right");
    }
  style_end (&style);

  int colspan = table_cell_colspan (cell);
  if (colspan > 1)
    fprintf (html->file, " colspan=\"%d\"", colspan);

  int rowspan = table_cell_rowspan (cell);
  if (rowspan > 1)
    fprintf (html->file, " rowspan=\"%d\"", rowspan);

  putc ('>', html->file);

  const char *s = ds_cstr (&body);
  s += strspn (s, CC_SPACES);
  escape_string (html->file, s, " ", "<br>");
  ds_destroy (&body);

  const struct pivot_value_ex *ex = pivot_value_ex (cell->value);
  if (ex->n_subscripts)
    {
      fputs ("<sub>", html->file);
      for (size_t i = 0; i < ex->n_subscripts; i++)
        {
          if (i)
            putc (',', html->file);
          escape_string (html->file, ex->subscripts[i], "&nbsp;", "<br>");
        }
      fputs ("</sub>", html->file);
    }
  if (ex->n_footnotes > 0)
    {
      fputs ("<sup>", html->file);
      size_t n_footnotes = 0;
      for (size_t i = 0; i < ex->n_footnotes; i++)
        {
          const struct pivot_footnote *f
            = pt->footnotes[ex->footnote_indexes[i]];
          if (f->show)
            {
              if (n_footnotes++ > 0)
                putc (',', html->file);

              char *marker = pivot_footnote_marker_string (f, pt);
              escape_string (html->file, marker, " ", "<br>");
              free (marker);
            }
        }
      fputs ("</sup>", html->file);
    }

  /* output </th> or </td>. */
  fprintf (html->file, "</%s>\n", tag);
}

static void
html_output_table_layer (struct html_driver *html, const struct pivot_table *pt,
                         const size_t *layer_indexes)
{
  struct table *title, *layers, *body, *caption, *footnotes;
  pivot_output (pt, layer_indexes, true, &title, &layers, &body,
                &caption, &footnotes, NULL, NULL);

  fputs ("<table", html->file);
  if (pt->notes)
    {
      fputs (" title=\"", html->file);
      escape_string (html->file, pt->notes, " ", "\n");
      putc ('"', html->file);
    }
  fputs (">\n", html->file);

  if (title)
    {
      struct table_cell cell;
      table_get_cell (title, 0, 0, &cell);
      html_put_table_cell (html, pt, &cell, "caption", NULL);
    }

  if (layers)
    {
      fputs ("<thead>\n", html->file);
      for (size_t y = 0; y < layers->n[V]; y++)
        {
          fputs ("<tr>\n", html->file);

          struct table_cell cell;
          table_get_cell (layers, 0, y, &cell);
          cell.d[H][1] = body->n[H];
          html_put_table_cell (html, pt, &cell, "td", NULL);

          fputs ("</tr>\n", html->file);
        }
      fputs ("</thead>\n", html->file);
    }

  fputs ("<tbody>\n", html->file);
  for (int y = 0; y < body->n[V]; y++)
    {
      fputs ("<tr>\n", html->file);
      for (int x = 0; x < body->n[H]; )
        {
          struct table_cell cell;
          table_get_cell (body, x, y, &cell);
          if (x == cell.d[TABLE_HORZ][0] && y == cell.d[TABLE_VERT][0])
            {
              bool is_header = (y < body->h[V][0]
                                || y >= body->n[V] - body->h[V][1]
                                || x < body->h[H][0]
                                || x >= body->n[H] - body->h[H][1]);
              const char *tag = is_header ? "th" : "td";
              html_put_table_cell (html, pt, &cell, tag, body);
            }

          x = cell.d[TABLE_HORZ][1];
        }
      fputs ("</tr>\n", html->file);
    }
  fputs ("</tbody>\n", html->file);

  if (caption || footnotes)
    {
      fprintf (html->file, "<tfoot>\n");

      if (caption)
        {
          fputs ("<tr>\n", html->file);

          struct table_cell cell;
          table_get_cell (caption, 0, 0, &cell);
          cell.d[H][1] = body->n[H];
          html_put_table_cell (html, pt, &cell, "td", NULL);

          fputs ("</tr>\n", html->file);
        }

      if (footnotes)
        for (size_t y = 0; y < footnotes->n[V]; y++)
          {
            fputs ("<tr>\n", html->file);

            struct table_cell cell;
            table_get_cell (footnotes, 0, y, &cell);
            cell.d[H][1] = body->n[H];
            html_put_table_cell (html, pt, &cell, "td", NULL);

            fputs ("</tr>\n", html->file);
          }
      fputs ("</tfoot>\n", html->file);
    }

  fputs ("</table>\n\n", html->file);

  table_unref (title);
  table_unref (layers);
  table_unref (body);
  table_unref (caption);
  table_unref (footnotes);
}

static void
html_output_table (struct html_driver *html, const struct output_item *item)
{
  size_t *layer_indexes;
  PIVOT_OUTPUT_FOR_EACH_LAYER (layer_indexes, item->table, true)
    html_output_table_layer (html, item->table, layer_indexes);
}

struct output_driver_factory html_driver_factory =
  { "html", "pspp.html", html_create };

static const struct output_driver_class html_driver_class =
  {
    .name = "html",
    .destroy = html_destroy,
    .submit = html_submit,
    .handles_groups = true,
  };
