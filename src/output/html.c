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
#ifdef HAVE_CAIRO
#include "output/cairo-chart.h"
#endif
#include "output/chart-item.h"
#include "output/driver-provider.h"
#include "output/message-item.h"
#include "output/options.h"
#include "output/output-item-provider.h"
#include "output/table-provider.h"
#include "output/table-item.h"
#include "output/text-item.h"

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
#ifdef HAVE_CAIRO
    struct cell_color fg;
    struct cell_color bg;
#endif
    struct file_handle *handle;
    char *chart_file_name;

    FILE *file;
    size_t chart_cnt;

    bool bare;
    bool css;
    bool borders;
  };

static const struct output_driver_class html_driver_class;

static void html_output_table (struct html_driver *, const struct table_item *);
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
#ifdef HAVE_CAIRO
  html->bg = parse_color (opt (d, o, "background-color", "#FFFFFFFFFFFF"));
  html->fg = parse_color (opt (d, o, "foreground-color", "#000000000000"));
#endif
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
html_submit (struct output_driver *driver,
             const struct output_item *output_item)
{
  struct html_driver *html = html_driver_cast (driver);

  if (is_table_item (output_item))
    {
      struct table_item *table_item = to_table_item (output_item);
      html_output_table (html, table_item);
    }
#ifdef HAVE_CAIRO
  else if (is_chart_item (output_item) && html->chart_file_name != NULL)
    {
      struct chart_item *chart_item = to_chart_item (output_item);
      char *file_name;

      file_name = xr_draw_png_chart (chart_item, html->chart_file_name,
                                     html->chart_cnt++,
				     &html->fg,
				     &html->bg
				);
      if (file_name != NULL)
        {
	  const char *title = chart_item_get_title (chart_item);
          fprintf (html->file, "<img src=\"%s\" alt=\"chart: %s\">",
		   file_name, title ? title : _("No description"));
          free (file_name);
        }
    }
#endif  /* HAVE_CAIRO */
  else if (is_text_item (output_item))
    {
      struct text_item *text_item = to_text_item (output_item);
      const char *s = text_item_get_text (text_item);

      switch (text_item_get_type (text_item))
        {
        case TEXT_ITEM_PAGE_TITLE:
          break;

        case TEXT_ITEM_TITLE:
          {
            int level = MIN (5, output_get_group_level ()) + 1;
            char tag[3] = { 'H', level + '1', '\0' };
            print_title_tag (html->file, tag, s);
          }
          break;

        case TEXT_ITEM_SYNTAX:
          fprintf (html->file, "<pre class=\"syntax\">");
          escape_string (html->file, s, " ", "<br>");
          fprintf (html->file, "</pre>\n");
          break;

        case TEXT_ITEM_LOG:
          print_title_tag (html->file, "pre", s); /* should be <P><TT> */
          break;
        }
    }
  else if (is_message_item (output_item))
    {
      const struct message_item *message_item = to_message_item (output_item);
      char *s = msg_to_string (message_item_get_msg (message_item));
      print_title_tag (html->file, "p", s);
      free (s);
    }
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

static void
escape_tag (FILE *file, const char *tag,
            const char *text, const char *space, const char *newline)
{
  if (!text || !*text)
    return;

  fprintf (file, "<%s>", tag);
  escape_string (file, text, space, newline);
  fprintf (file, "</%s>", tag);
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
put_style (struct css_style *st, const char *name, const char *value)
{
  bool first = !st->n_styles++;
  fprintf (st->file, "%s%s: %s", first ? " style='" : "; ", name, value);
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
put_border (struct css_style *st, int style, const struct cell_color *color,
            const char *border_name)
{
  const char *css = border_to_css (style);
  if (css)
    {
      if (st->n_styles++ > 0)
        fputs ("; ", st->file);
      fprintf (st->file, "border-%s: %s", border_name, css);

      char buf[32];
      if (format_color (*color, (struct cell_color) CELL_COLOR_BLACK,
                        buf, sizeof buf))
        fprintf (st->file, " %s", buf);
    }
}

static void
put_tfoot (struct html_driver *html, const struct table *t, bool *tfoot)
{
  if (!*tfoot)
    {
      fputs ("<tfoot>\n", html->file);
      fputs ("<tr>\n", html->file);
      fprintf (html->file, "<td colspan=%d>\n", t->n[H]);
      *tfoot = true;
    }
  else
    fputs ("\n<br>", html->file);
}

static void
html_put_footnote_markers (struct html_driver *html,
                           const struct footnote **footnotes,
                           size_t n_footnotes)
{
  if (n_footnotes > 0)
    {
      fputs ("<sup>", html->file);
      for (size_t i = 0; i < n_footnotes; i++)
        {
          const struct footnote *f = footnotes[i];

          if (i > 0)
            putc (',', html->file);
          escape_string (html->file, f->marker, " ", "<br>");
        }
      fputs ("</sup>", html->file);
    }
}

static void
html_put_table_item_text (struct html_driver *html,
                          const struct table_item_text *text)
{
  escape_string (html->file, text->content, " ", "<br>");
  html_put_footnote_markers (html, text->footnotes, text->n_footnotes);
}

static void
html_put_table_cell_text (struct html_driver *html,
                          const struct table_cell *cell)
{
  const char *s = cell->text;
  if (cell->options & TAB_FIX)
    escape_tag (html->file, "tt", s, "&nbsp;", "<br>");
  else
    {
      s += strspn (s, CC_SPACES);
      escape_string (html->file, s, " ", "<br>");
    }

  if (cell->n_subscripts)
    {
      fputs ("<sub>", html->file);
      for (size_t i = 0; i < cell->n_subscripts; i++)
        {
          if (i)
            putc (',', html->file);
          escape_string (html->file, cell->subscripts[i],
                         "&nbsp;", "<br>");
        }
      fputs ("</sub>", html->file);
    }
  if (cell->superscript)
    escape_tag (html->file, "sup", cell->superscript, "&nbsp;", "<br>");
  html_put_footnote_markers (html, cell->footnotes, cell->n_footnotes);
}

static void
html_put_table_item_layers (struct html_driver *html,
                            const struct table_item_layers *layers)
{
  for (size_t i = 0; i < layers->n_layers; i++)
    {
      if (i)
        fputs ("<br>\n", html->file);

      const struct table_item_layer *layer = &layers->layers[i];
      escape_string (html->file, layer->content, " ", "<br>");
      html_put_footnote_markers (html, layer->footnotes, layer->n_footnotes);
    }
}

static void
html_output_table (struct html_driver *html, const struct table_item *item)
{
  const struct table *t = table_item_get_table (item);
  bool tfoot = false;
  int y;

  fputs ("<table", html->file);
  if (item->notes)
    {
      fputs (" title=\"", html->file);
      escape_string (html->file, item->notes, " ", "\n");
      putc ('"', html->file);
    }
  fputs (">\n", html->file);

  const struct table_item_text *caption = table_item_get_caption (item);
  if (caption)
    {
      put_tfoot (html, t, &tfoot);
      html_put_table_item_text (html, caption);
    }
  const struct footnote **f;
  size_t n_footnotes = table_collect_footnotes (item, &f);

  for (size_t i = 0; i < n_footnotes; i++)
    {
      put_tfoot (html, t, &tfoot);
      escape_tag (html->file, "sup", f[i]->marker, " ", "<br>");
      escape_string (html->file, f[i]->content, " ", "<br>");
    }
  free (f);
  if (tfoot)
    {
      fputs ("</td>\n", html->file);
      fputs ("</tr>\n", html->file);
      fputs ("</tfoot>\n", html->file);
    }

  const struct table_item_text *title = table_item_get_title (item);
  const struct table_item_layers *layers = table_item_get_layers (item);
  if (title || layers)
    {
      fputs ("<caption>", html->file);
      if (title)
        html_put_table_item_text (html, title);
      if (title && layers)
        fputs ("<br>\n", html->file);
      if (layers)
        html_put_table_item_layers (html, layers);
      fputs ("</caption>\n", html->file);
    }

  fputs ("<tbody>\n", html->file);

  for (y = 0; y < t->n[V]; y++)
    {
      int x;

      fputs ("<tr>\n", html->file);
      for (x = 0; x < t->n[H];)
        {
          struct table_cell cell;
          const char *tag;

          table_get_cell (t, x, y, &cell);
          if (x != cell.d[TABLE_HORZ][0] || y != cell.d[TABLE_VERT][0])
            goto next_1;

          /* output <td> or <th> tag. */
          bool is_header = (y < t->h[V][0]
                            || y >= t->n[V] - t->h[V][1]
                            || x < t->h[H][0]
                            || x >= t->n[H] - t->h[H][1]);
          tag = is_header ? "th" : "td";
          fprintf (html->file, "<%s", tag);

          struct css_style style;
          style_start (&style, html->file);
          enum table_halign halign = table_halign_interpret (
            cell.style->cell_style.halign, cell.options & TAB_NUMERIC);

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

          if (cell.options & TAB_ROTATE)
            put_style (&style, "writing-mode", "sideways-lr");

          if (cell.style->cell_style.valign != TABLE_VALIGN_TOP)
            {
              put_style (&style, "vertical-align",
                         (cell.style->cell_style.valign == TABLE_VALIGN_BOTTOM
                          ? "bottom" : "middle"));
            }

          const struct font_style *fs = &cell.style->font_style;
          char bgcolor[32];
          if (format_color (fs->bg[y % 2],
                            (struct cell_color) CELL_COLOR_WHITE,
                            bgcolor, sizeof bgcolor))
            put_style (&style, "background", bgcolor);

          char fgcolor[32];
          if (format_color (fs->fg[y % 2],
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

          int colspan = table_cell_colspan (&cell);
          int rowspan = table_cell_rowspan (&cell);

	  if (html->borders)
	    {
	      /* Cell borders. */
              struct cell_color color;

	      int top = table_get_rule (t, TABLE_VERT, x, y, &color);
              put_border (&style, top, &color, "top");

	      if (y + rowspan == t->n[V])
		{
		  int bottom = table_get_rule (t, TABLE_VERT, x, y + rowspan,
                                           &color);
                  put_border (&style, bottom, &color, "bottom");
		}

	      int left = table_get_rule (t, TABLE_HORZ, x, y, &color);
              put_border (&style, left, &color, "left");

	      if (x + colspan == t->n[H])
		{
		  int right = table_get_rule (t, TABLE_HORZ, x + colspan, y,
                                          &color);
                  put_border (&style, right, &color, "right");
		}
	    }
          style_end (&style);

          if (colspan > 1)
            fprintf (html->file, " colspan=\"%d\"", colspan);

          if (rowspan > 1)
            fprintf (html->file, " rowspan=\"%d\"", rowspan);

          putc ('>', html->file);

          html_put_table_cell_text (html, &cell);

          /* output </th> or </td>. */
          fprintf (html->file, "</%s>\n", tag);

        next_1:
          x = cell.d[TABLE_HORZ][1];
        }
      fputs ("</tr>\n", html->file);
    }

  fputs ("</tbody>\n", html->file);
  fputs ("</table>\n\n", html->file);
}

struct output_driver_factory html_driver_factory =
  { "html", "pspp.html", html_create };

static const struct output_driver_class html_driver_class =
  {
    "html",
    html_destroy,
    html_submit,
    NULL,
  };
