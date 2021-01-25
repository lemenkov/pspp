/* PSPP - a program for statistical analysis.
   Copyright (C) 2020 Free Software Foundation, Inc.

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

#include "gl/mbiter.h"
#include "data/file-name.h"
#include "data/file-handle-def.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/hmap.h"
#include "libpspp/ll.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/temp-file.h"
#include "libpspp/version.h"
#include "output/cairo-chart.h"
#include "output/driver-provider.h"
#include "output/options.h"
#include "output/output-item.h"
#include "output/pivot-output.h"
#include "output/pivot-table.h"
#include "output/table-provider.h"
#include "output/tex-rendering.h"
#include "output/tex-parsing.h"


#include "tex-glyphs.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/c-vasnprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* This file uses TABLE_HORZ and TABLE_VERT enough to warrant abbreviating. */
#define H TABLE_HORZ
#define V TABLE_VERT

/* The desired maximum line length in the TeX file.  */
#define TEX_LINE_MAX 80

struct tex_driver
  {
    struct output_driver driver;
    /* A hash table containing any Tex macros which need to be emitted.  */
    struct hmap macros;
    bool require_graphics;
    struct cell_color fg;
    struct cell_color bg;
    struct file_handle *handle;
    char *chart_file_name;

    FILE *file;
    size_t chart_cnt;

    struct ll_list preamble_list;
    struct ll_list token_list;
  };

/* Ships the string STR to the driver.  */
static void
shipout (struct ll_list *list, const char *str, ...)
{
  va_list args;
  va_start (args, str);

  size_t length;
  char *s = c_vasnprintf (NULL, &length, str, args);

  tex_parse (s, list);

  va_end (args);
  free (s);
}

static const struct output_driver_class tex_driver_class;

static void tex_output_table (struct tex_driver *, const struct pivot_table *);

static struct tex_driver *
tex_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &tex_driver_class);
  return UP_CAST (driver, struct tex_driver, driver);
}

static struct driver_option *
opt (struct output_driver *d, struct string_map *options, const char *key,
     const char *default_value)
{
  return driver_option_get (d, options, key, default_value);
}

static struct output_driver *
tex_create (struct file_handle *fh, enum settings_output_devices device_type,
             struct string_map *o)
{
  struct output_driver *d;
  struct tex_driver *tex = XZALLOC (struct tex_driver);
  hmap_init (&tex->macros);
  ll_init (&tex->preamble_list);
  ll_init (&tex->token_list);

  d = &tex->driver;
  output_driver_init (&tex->driver, &tex_driver_class, fh_get_file_name (fh),
                      device_type);
  tex->handle = fh;
  tex->chart_file_name = parse_chart_file_name (opt (d, o, "charts",
                                                      fh_get_file_name (fh)));
  tex->chart_cnt = 1;
  tex->bg = parse_color (opt (d, o, "background-color", "#FFFFFFFFFFFF"));
  tex->fg = parse_color (opt (d, o, "foreground-color", "#000000000000"));

  tex->file = fn_open (tex->handle, "w");
  if (tex->file == NULL)
    {
      msg_error (errno, _("error opening output file `%s'"),
                 fh_get_file_name (tex->handle));
      goto error;
    }

  return d;

 error:
  output_driver_destroy (d);
  return NULL;
}


/* Emit all the tokens in LIST to FILE.
   Then destroy LIST and its contents.  */
static void
post_process_tokens (FILE *file, struct ll_list *list)
{
  size_t line_len = 0;
  struct tex_token *tt;
  struct tex_token *ttnext;
  ll_for_each_safe (tt, ttnext, struct tex_token, ll, list)
    {
      if (tt->cat == CAT_SPACE)
        {
          /* Count the number of characters up to the next space,
             and if it'll not fit on to the line, then make a line
             break here.  */
          size_t word_len = 0;
          struct tex_token *prev_x = NULL;
          for (struct ll *x = ll_next (&tt->ll); x != ll_null (list);
               x = ll_next (x))
            {
              struct tex_token *nt = ll_data (x, struct tex_token, ll);
              if (nt->cat == CAT_SPACE || nt->cat == CAT_EOL)
                break;
              if (prev_x && (prev_x->cat == CAT_COMMENT) && (nt->cat != CAT_COMMENT))
                break;
              word_len += ds_length (&nt->str);
              prev_x = nt;
            }

          if ((word_len < TEX_LINE_MAX) && (line_len + word_len >= TEX_LINE_MAX - 1))
            {
              fputs ("\n", file);
              line_len = 0;
              continue;
            }
        }

      line_len += ds_length (&tt->str);
      if (tt->cat == CAT_EOL)
        line_len = 0;
      if (line_len >= TEX_LINE_MAX)
        {
          fputs ("%\n", file);
          line_len = ds_length (&tt->str);
        }
      if (tt->cat == CAT_COMMENT)
        line_len = 0;
      fputs (ds_cstr (&tt->str), file);
      ds_destroy (&tt->str);
      free (tt);
    }
}


static void
tex_destroy (struct output_driver *driver)
{
  struct tex_driver *tex = tex_driver_cast (driver);

  shipout (&tex->preamble_list, "%%%% TeX output of pspp\n\n");
  shipout (&tex->preamble_list, "%%%% Define the horizontal space between table columns\n");
  shipout (&tex->preamble_list, "\\def\\psppcolumnspace{1mm}\n\n");

  char *ln = get_language ();
  if (ln)
    shipout (&tex->preamble_list, "%%%% Language is \"%s\"\n", ln);
  free (ln);
  shipout (&tex->preamble_list, "\n");

  shipout (&tex->preamble_list, "%%%% Sets the environment for rendering material in table cell\n");
  shipout (&tex->preamble_list, "%%%% The parameter is the number of columns in the table\n");
  shipout (&tex->preamble_list,
           "\\def\\cell#1{\\normalbaselines\\advance\\hsize by -#1.0\\psppcolumnspace"
           "\\advance\\hsize by \\psppcolumnspace"
           "\\divide\\hsize by #1"
           "\\noindent\\raggedright\\hskip0pt}\n\n");

  /* centre macro */
  shipout (&tex->preamble_list,
           "%%%% Render the text centre justified\n"
           "\\def\\startcentre{\\begingroup\\leftskip=0pt plus 1fil\n"
           "\\rightskip=\\leftskip\\parfillskip=0pt}\n");
  shipout (&tex->preamble_list, "\\def\\stopcentre{\\par\\endgroup}\n");
  shipout (&tex->preamble_list, "\\long\\def\\centre#1{\\startcentre#1\\stopcentre}\n\n");


  /* right macro */
  shipout (&tex->preamble_list,
           "%%%% Render the text right justified\n"
           "\\def\\startright{\\begingroup\\leftskip=0pt plus 1fil\n"
           "\\parfillskip=0pt}\n");
  shipout (&tex->preamble_list, "\\def\\stopright{\\par\\endgroup}\n");
  shipout (&tex->preamble_list, "\\long\\def\\right#1{\\startright#1\\stopright}\n\n");


  /* Emit all the macro defintions.  */
  struct tex_macro *m;
  struct tex_macro *next;
  HMAP_FOR_EACH_SAFE (m, next, struct tex_macro, node, &tex->macros)
    {
      shipout (&tex->preamble_list, "%s", tex_macro[m->index]);
      shipout (&tex->preamble_list, "\n\n");
      free (m);
    }
  hmap_destroy (&tex->macros);

  if (tex->require_graphics)
    shipout (&tex->preamble_list, "\\input graphicx\n\n");

  post_process_tokens (tex->file, &tex->preamble_list);

  shipout (&tex->token_list, "\n\\bye\n");

  post_process_tokens (tex->file, &tex->token_list);

  fn_close (tex->handle, tex->file);

  free (tex->chart_file_name);
  fh_unref (tex->handle);
  free (tex);
}

/* Ship out TEXT (which must be a UTF-8 encoded string to the driver's output.
   if TABULAR is true, then this text is within a table.  */
static void
tex_escape_string (struct tex_driver *tex, const char *text,
                   bool tabular)
{
  size_t n = strlen (text);
  while (n > 0)
    {
      const char *frag = u8_to_tex_fragments (&text, &n, &tex->macros);
      shipout (&tex->token_list, "%s", frag);
      if (text[0] != '\0' && tabular && 0 == strcmp (frag, "."))
        {
          /* Peek ahead to the next code sequence */
          size_t nn = n;
          const char *t = text;
          const char *next = u8_to_tex_fragments (&t, &nn, &tex->macros);
          /* If a period followed by whitespace is encountered within tabular
             material, then it is reasonable to assume, that it is an
             abbreviation (like "Sig." or "Std. Deviation") rather than the
             end of a sentance.  */
          if (next && 0 == strcmp (" ", next))
            {
              shipout (&tex->token_list, "\\ ");
            }
        }
    }
}

static void
tex_submit (struct output_driver *driver, const struct output_item *item)
{
  struct tex_driver *tex = tex_driver_cast (driver);

  switch (item->type)
    {
    case OUTPUT_ITEM_CHART:
      if (tex->chart_file_name != NULL)
        {
          char *file_name = xr_draw_png_chart (item->chart,
                                               tex->chart_file_name,
                                               tex->chart_cnt++,
                                               &tex->fg, &tex->bg);
          if (file_name != NULL)
            {
              //const char *title = chart_item_get_title (chart_item);
              //          printf ("The chart title is %s\n", title);

              shipout (&tex->token_list, "\\includegraphics{%s}\n", file_name);
              tex->require_graphics = true;
              free (file_name);
            }
        }
      break;

    case OUTPUT_ITEM_GROUP:
      NOT_REACHED ();

    case OUTPUT_ITEM_IMAGE:
      {
        char *file_name = xr_write_png_image (
          item->image, tex->chart_file_name, tex->chart_cnt++);
        if (file_name != NULL)
          {
            shipout (&tex->token_list, "\\includegraphics{%s}\n", file_name);
            tex->require_graphics = true;
            free (file_name);
          }
      }
      break;

    case OUTPUT_ITEM_MESSAGE:
      {
        char *s = msg_to_string (item->message);
        tex_escape_string (tex, s, false);
        shipout (&tex->token_list, "\\par\n");
        free (s);
      }
      break;

    case OUTPUT_ITEM_PAGE_BREAK:
      break;

    case OUTPUT_ITEM_TABLE:
      tex_output_table (tex, item->table);
      break;

    case OUTPUT_ITEM_TEXT:
      {
        char *s = text_item_get_plain_text (item);

        switch (item->text.subtype)
          {
          case TEXT_ITEM_PAGE_TITLE:
            shipout (&tex->token_list, "\\headline={\\bf ");
            tex_escape_string (tex, s, false);
            shipout (&tex->token_list, "\\hfil}\n");
            break;

          case TEXT_ITEM_LOG:
            shipout (&tex->token_list, "{\\tt ");
            tex_escape_string (tex, s, false);
            shipout (&tex->token_list, "}\\par\n\n");
            break;

          case TEXT_ITEM_SYNTAX:
            /* So far as I'm aware, this can never happen.  */
          default:
            printf ("Unhandled type %d\n", item->text.subtype);
            break;
          }
        free (s);
      }
      break;
    }
}

static void
tex_put_footnote_markers (struct tex_driver *tex,
                          const struct pivot_table *pt,
                          const size_t *footnote_indexes,
                          size_t n_footnotes)
{
  size_t n_visible = 0;
  for (size_t i = 0; i < n_footnotes; i++)
    {
      const struct pivot_footnote *f = pt->footnotes[footnote_indexes[i]];
      if (f->show)
        {
          if (!n_visible++)
            shipout (&tex->token_list, "$^{");

          char *marker = pivot_footnote_marker_string (f, pt);
          tex_escape_string (tex, marker, true);
          free (marker);
        }
    }
  if (n_visible)
    shipout (&tex->token_list, "}$");
}

static void
tex_put_table_cell (struct tex_driver *tex, const struct pivot_table *pt,
                    const struct table_cell *cell)
{
  struct string s = DS_EMPTY_INITIALIZER;
  pivot_value_format_body (cell->value, pt, &s);
  tex_escape_string (tex, ds_cstr (&s), false);
  ds_destroy (&s);

  tex_put_footnote_markers (tex, pt,
                            cell->value->footnote_indexes,
                            cell->value->n_footnotes);
}

static void
tex_output_table_layer (struct tex_driver *tex, const struct pivot_table *pt,
                        const size_t *layer_indexes)
{
  /* Tables are rendered in TeX with the \halign command.
     This is described in the TeXbook Ch. 22 */
  struct table *title, *layers, *body, *caption;
  struct pivot_footnote **footnotes;
  size_t n_footnotes;
  pivot_output (pt, layer_indexes, true, &title, &layers, &body,
                &caption, NULL, &footnotes, &n_footnotes);

  shipout (&tex->token_list, "\n{\\parindent=0pt\n");

  if (caption)
    {
      shipout (&tex->token_list, "{\\sl ");
      struct table_cell cell;
      table_get_cell (caption, 0, 0, &cell);
      tex_put_table_cell (tex, pt, &cell);
      shipout (&tex->token_list, "}\n\n");
    }

  if (title || layers)
    {
      if (title)
        {
          shipout (&tex->token_list, "{\\bf ");
          struct table_cell cell;
          table_get_cell (title, 0, 0, &cell);
          tex_put_table_cell (tex, pt, &cell);
          shipout (&tex->token_list, "}\\par\n");
        }

      if (layers)
        {
          for (size_t y = 0; y < layers->n[V]; y++)
            {
              shipout (&tex->token_list, "{");
              struct table_cell cell;
              table_get_cell (layers, 0, y, &cell);
              tex_put_table_cell (tex, pt, &cell);
              shipout (&tex->token_list, "}\\par\n");
            }
        }
    }

  shipout (&tex->token_list, "\\offinterlineskip\\halign{\\strut%%\n");

  /* Generate the preamble */
  for (int x = 0; x < body->n[H]; ++x)
    {
      shipout (&tex->token_list, "{\\vbox{\\cell{%d}#}}", body->n[H]);

      if (x < body->n[H] - 1)
        {
          shipout (&tex->token_list, "\\hskip\\psppcolumnspace\\hfil");
          shipout (&tex->token_list, "&\\vrule\n");
        }
      else
        shipout (&tex->token_list, "\\cr\n");
    }

  /* Emit the row data */
  for (int y = 0; y < body->n[V]; y++)
    {
      enum { H = TABLE_HORZ, V = TABLE_VERT };
      bool is_column_header = y < body->h[V][0] || y >= body->n[V] - body->h[V][1];
      int prev_x = -1;
      int skipped = 0;
      for (int x = 0; x < body->n[H];)
        {
          struct table_cell cell;

          table_get_cell (body, x, y, &cell);

          int colspan = table_cell_colspan (&cell);
          if (x > 0)
            shipout (&tex->token_list, "&");
          else
            for (int i = 0; i < skipped - colspan; ++i)
              shipout (&tex->token_list, "&");


          if (x != cell.d[TABLE_HORZ][0] || y != cell.d[TABLE_VERT][0])
            goto next_1;

          /* bool is_header = (y < body->h[V][0] */
          /*                   || y >= body->n[V] - body->h[V][1] */
          /*                   || x < body->h[H][0] */
          /*                   || x >= body->n[H] - body->h[H][1]); */

          struct string s = DS_EMPTY_INITIALIZER;
          bool numeric = pivot_value_format_body (cell.value, pt, &s);

          enum table_halign halign = table_halign_interpret (
            cell.cell_style->halign, numeric);

          /* int rowspan = table_cell_rowspan (&cell); */

          /* if (rowspan > 1) */
          /*   fprintf (tex->file, " rowspan=\"%d\"", rowspan); */

          if (colspan > 1)
            {
              shipout (&tex->token_list, "\\multispan{%d}\\span", colspan - 1);
              shipout (&tex->token_list, "\\hsize=%d.0\\hsize", colspan);
              shipout (&tex->token_list, "\\advance\\hsize%d.0\\psppcolumnspace ",
                       colspan - 1);
            }

          if (halign == TABLE_HALIGN_CENTER)
            shipout (&tex->token_list, "\\centre{");

          if (halign == TABLE_HALIGN_RIGHT)
            shipout (&tex->token_list, "\\right{");

          /* Output cell contents. */
          tex_escape_string (tex, ds_cstr (&s), true);
          ds_destroy (&s);

          tex_put_footnote_markers (tex, pt, cell.value->footnote_indexes,
                                    cell.value->n_footnotes);
          if (halign == TABLE_HALIGN_CENTER || halign == TABLE_HALIGN_RIGHT)
            {
              shipout (&tex->token_list, "}");
            }

        next_1:
          skipped = x - prev_x;
          prev_x = x;
          x = cell.d[TABLE_HORZ][1];
        }
      shipout (&tex->token_list, "\\cr\n");
      if (is_column_header)
        shipout (&tex->token_list, "\\noalign{\\hrule\\vskip -\\normalbaselineskip}\\cr\n");
    }

  shipout (&tex->token_list, "}%% End of \\halign\n");

  /* Shipout any footnotes.  */
  if (n_footnotes > 0)
    shipout (&tex->token_list, "\\vskip 0.5ex\n");

  for (int i = 0; i < n_footnotes; ++i)
    {
      char *marker = pivot_footnote_marker_string (footnotes[i], pt);
      char *content = pivot_value_to_string (footnotes[i]->content, pt);

      shipout (&tex->token_list, "$^{");
      tex_escape_string (tex, marker, false);
      shipout (&tex->token_list, "}$");
      tex_escape_string (tex, content, false);

      free (content);
      free (marker);
    }

  shipout (&tex->token_list, "}\n\\vskip 3ex\n\n");

  table_unref (title);
  table_unref (layers);
  table_unref (body);
  table_unref (caption);
  free (footnotes);
}

static void
tex_output_table (struct tex_driver *tex, const struct pivot_table *pt)
{
  size_t *layer_indexes;
  PIVOT_OUTPUT_FOR_EACH_LAYER (layer_indexes, pt, true)
    tex_output_table_layer (tex, pt, layer_indexes);
}

struct output_driver_factory tex_driver_factory =
  { "tex", "pspp.tex", tex_create };

static const struct output_driver_class tex_driver_class =
  {
    .name = "tex",
    .destroy = tex_destroy,
    .submit = tex_submit,
  };
