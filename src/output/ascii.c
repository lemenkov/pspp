/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <unilbrk.h>
#include <unistd.h>
#include <unistr.h>
#include <uniwidth.h>

#include "data/file-name.h"
#include "data/file-handle-def.h"
#include "data/settings.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/start-date.h"
#include "libpspp/string-map.h"
#include "libpspp/u8-line.h"
#include "libpspp/version.h"
#include "output/ascii.h"
#include "output/cairo.h"
#include "output/chart-item-provider.h"
#include "output/driver-provider.h"
#include "output/message-item.h"
#include "output/options.h"
#include "output/render.h"
#include "output/tab.h"
#include "output/table-item.h"
#include "output/text-item.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xsize.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* This file uses TABLE_HORZ and TABLE_VERT enough to warrant abbreviating. */
#define H TABLE_HORZ
#define V TABLE_VERT

enum
  {
    ASCII_LINE_NONE,
    ASCII_LINE_SINGLE,
    ASCII_LINE_DOUBLE,
    ASCII_N_LINES
  };

#define N_BOX (ASCII_N_LINES * ASCII_N_LINES \
               * ASCII_N_LINES * ASCII_N_LINES)

static const ucs4_t ascii_box_chars[N_BOX] =
  {
    ' ', '|', '#',
    '-', '+', '#',
    '=', '#', '#',
    '|', '|', '#',
    '+', '+', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '-', '+', '#',
    '-', '+', '#',
    '#', '#', '#',
    '+', '+', '#',
    '+', '+', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '=', '#', '#',
    '#', '#', '#',
    '=', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
    '#', '#', '#',
  };

static const ucs4_t unicode_box_chars[N_BOX] =
  {
    0x0020, 0x2575, 0x2551,
    0x2574, 0x256f, 0x255c,
    0x2550, 0x255b, 0x255d,
    0x2577, 0x2502, 0x2551,
    0x256e, 0x2524, 0x2562,
    0x2555, 0x2561, 0x2563,
    0x2551, 0x2551, 0x2551,
    0x2556, 0x2562, 0x2562,
    0x2557, 0x2563, 0x2563,
    0x2576, 0x2570, 0x2559,
    0x2500, 0x2534, 0x2568,
    0x2550, 0x2567, 0x2569,
    0x256d, 0x251c, 0x255f,
    0x252c, 0x253c, 0x256a,
    0x2564, 0x256a, 0x256c,
    0x2553, 0x255f, 0x255f,
    0x2565, 0x256b, 0x256b,
    0x2566, 0x256c, 0x256c,
    0x2550, 0x2558, 0x255a,
    0x2550, 0x2567, 0x2569,
    0x2550, 0x2567, 0x2569,
    0x2552, 0x255e, 0x2560,
    0x2564, 0x256a, 0x256c,
    0x2564, 0x256a, 0x256c,
    0x2554, 0x2560, 0x2560,
    0x2560, 0x256c, 0x256c,
    0x2566, 0x256c, 0x256c,
  };

static int
ascii_line_from_render_line (int render_line)
{
  switch (render_line)
    {
    case RENDER_LINE_NONE:
      return ASCII_LINE_NONE;

    case RENDER_LINE_SINGLE:
    case RENDER_LINE_DASHED:
    case RENDER_LINE_THICK:
    case RENDER_LINE_THIN:
      return ASCII_LINE_SINGLE;

    case RENDER_LINE_DOUBLE:
      return ASCII_LINE_DOUBLE;

    default:
      return ASCII_LINE_NONE;
    }

}

static int
make_box_index (int left_, int right_, int top_, int bottom_)
{
  bool rtl = render_direction_rtl ();
  int left = ascii_line_from_render_line (rtl ? right_ : left_);
  int right = ascii_line_from_render_line (rtl ? left_ : right_);
  int top = ascii_line_from_render_line (top_);
  int bottom = ascii_line_from_render_line (bottom_);

  int idx = right;
  idx = idx * ASCII_N_LINES + bottom;
  idx = idx * ASCII_N_LINES + left;
  idx = idx * ASCII_N_LINES + top;
  return idx;
}

/* ASCII output driver. */
struct ascii_driver
  {
    struct output_driver driver;

    /* User parameters. */
    bool append;                /* Append if output file already exists? */
    bool emphasis;              /* Enable bold and underline in output? */
    char *chart_file_name;      /* Name of files used for charts. */

#ifdef HAVE_CAIRO
    /* Colours for charts */
    struct xr_color fg;
    struct xr_color bg;
#endif

    int width;                  /* Page width. */
    bool auto_width;            /* Use viewwidth as page width? */

    int min_break[TABLE_N_AXES]; /* Min cell size to break across pages. */

    const ucs4_t *box;          /* Line & box drawing characters. */

    /* Internal state. */
    struct file_handle *handle;
    FILE *file;                 /* Output file. */
    bool error;                 /* Output error? */
    struct u8_line *lines;      /* Page content. */
    int allocated_lines;        /* Number of lines allocated. */
    int chart_cnt;              /* Number of charts so far. */
  };

static const struct output_driver_class ascii_driver_class;

static void ascii_submit (struct output_driver *, const struct output_item *);

static bool update_page_size (struct ascii_driver *, bool issue_error);
static int parse_page_size (struct driver_option *);

static bool ascii_open_page (struct ascii_driver *);

static void ascii_draw_line (void *, int bb[TABLE_N_AXES][2],
                             enum render_line_style styles[TABLE_N_AXES][2],
                             struct cell_color colors[TABLE_N_AXES][2]);
static void ascii_measure_cell_width (void *, const struct table_cell *,
                                      int *min, int *max);
static int ascii_measure_cell_height (void *, const struct table_cell *,
                                      int width);
static void ascii_draw_cell (void *, const struct table_cell *, int color_idx,
                             int bb[TABLE_N_AXES][2],
                             int spill[TABLE_N_AXES][2],
                             int clip[TABLE_N_AXES][2]);

static struct ascii_driver *
ascii_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &ascii_driver_class);
  return UP_CAST (driver, struct ascii_driver, driver);
}

static struct driver_option *
opt (struct output_driver *d, struct string_map *options, const char *key,
     const char *default_value)
{
  return driver_option_get (d, options, key, default_value);
}

static struct output_driver *
ascii_create (struct  file_handle *fh, enum settings_output_devices device_type,
              struct string_map *o)
{
  enum { BOX_ASCII, BOX_UNICODE } box;
  int min_break[TABLE_N_AXES];
  struct output_driver *d;
  struct ascii_driver *a;

  a = xzalloc (sizeof *a);
  d = &a->driver;
  output_driver_init (&a->driver, &ascii_driver_class, fh_get_file_name (fh), device_type);
  a->append = parse_boolean (opt (d, o, "append", "false"));
  a->emphasis = parse_boolean (opt (d, o, "emphasis", "false"));

  a->chart_file_name = parse_chart_file_name (opt (d, o, "charts", fh_get_file_name (fh)));
  a->handle = fh;

  min_break[H] = parse_int (opt (d, o, "min-hbreak", "-1"), -1, INT_MAX);

  a->width = parse_page_size (opt (d, o, "width", "79"));
  a->auto_width = a->width < 0;
  a->min_break[H] = min_break[H] >= 0 ? min_break[H] : a->width / 2;
#ifdef HAVE_CAIRO
  parse_color (d, o, "background-color", "#FFFFFFFFFFFF", &a->bg);
  parse_color (d, o, "foreground-color", "#000000000000", &a->fg);
#endif
  box = parse_enum (opt (d, o, "box", "ascii"),
                    "ascii", BOX_ASCII,
                    "unicode", BOX_UNICODE,
                    NULL_SENTINEL);
  a->box = box == BOX_ASCII ? ascii_box_chars : unicode_box_chars;

  a->file = NULL;
  a->error = false;
  a->lines = NULL;
  a->allocated_lines = 0;
  a->chart_cnt = 1;

  if (!update_page_size (a, true))
    goto error;

  return d;

error:
  output_driver_destroy (d);
  return NULL;
}

static int
parse_page_size (struct driver_option *option)
{
  int dim = atol (option->default_value);

  if (option->value != NULL)
    {
      if (!strcmp (option->value, "auto"))
        dim = -1;
      else
        {
          int value;
          char *tail;

          errno = 0;
          value = strtol (option->value, &tail, 0);
          if (dim >= 1 && errno != ERANGE && *tail == '\0')
            dim = value;
          else
            msg (MW, _("%s: %s must be positive integer or `auto'"),
                   option->driver_name, option->name);
        }
    }

  driver_option_destroy (option);

  return dim;
}

/* Re-calculates the page width based on settings, margins, and, if "auto" is
   set, the size of the user's terminal window or GUI output window. */
static bool
update_page_size (struct ascii_driver *a, bool issue_error)
{
  enum { MIN_WIDTH = 6, MIN_LENGTH = 6 };

  if (a->auto_width)
    {
      a->width = settings_get_viewwidth ();
      a->min_break[H] = a->width / 2;
    }

  if (a->width < MIN_WIDTH)
    {
      if (issue_error)
        msg (ME,
               _("ascii: page must be at least %d characters wide, but "
                 "as configured is only %d characters"),
               MIN_WIDTH,
               a->width);
      if (a->width < MIN_WIDTH)
        a->width = MIN_WIDTH;
      return false;
    }

  return true;
}

static void
ascii_destroy (struct output_driver *driver)
{
  struct ascii_driver *a = ascii_driver_cast (driver);
  int i;

  if (a->file != NULL)
    fn_close (a->handle, a->file);
  fh_unref (a->handle);
  free (a->chart_file_name);
  for (i = 0; i < a->allocated_lines; i++)
    u8_line_destroy (&a->lines[i]);
  free (a->lines);
  free (a);
}

static void
ascii_flush (struct output_driver *driver)
{
  struct ascii_driver *a = ascii_driver_cast (driver);
  if (a->file)
    fflush (a->file);
}

static void
ascii_output_lines (struct ascii_driver *a, size_t n_lines)
{
  for (size_t y = 0; y < n_lines; y++)
    {
      struct u8_line *line = &a->lines[y];

      while (ds_chomp_byte (&line->s, ' '))
        continue;
      fwrite (ds_data (&line->s), 1, ds_length (&line->s), a->file);
      putc ('\n', a->file);

      u8_line_clear (&a->lines[y]);
    }
}

static void
ascii_output_table_item (struct ascii_driver *a,
                         const struct table_item *table_item)
{
  struct render_params params;
  struct render_pager *p;
  int i;

  update_page_size (a, false);

  params.draw_line = ascii_draw_line;
  params.measure_cell_width = ascii_measure_cell_width;
  params.measure_cell_height = ascii_measure_cell_height;
  params.adjust_break = NULL;
  params.draw_cell = ascii_draw_cell;
  params.aux = a;
  params.size[H] = a->width;
  params.size[V] = INT_MAX;
  params.font_size[H] = 1;
  params.font_size[V] = 1;
  for (i = 0; i < RENDER_N_LINES; i++)
    {
      int width = i == RENDER_LINE_NONE ? 0 : 1;
      params.line_widths[H][i] = width;
      params.line_widths[V][i] = width;
    }
  for (i = 0; i < TABLE_N_AXES; i++)
    params.min_break[i] = a->min_break[i];
  params.supports_margins = false;

  if (a->file)
    putc ('\n', a->file);
  else if (!ascii_open_page (a))
    return;

  p = render_pager_create (&params, table_item);
  for (int i = 0; render_pager_has_next (p); i++)
    {
      if (i)
        putc ('\n', a->file);
      ascii_output_lines (a, render_pager_draw_next (p, INT_MAX));
    }
  render_pager_destroy (p);
}

static void
ascii_output_text (struct ascii_driver *a, const char *text)
{
  struct table_item *table_item;

  table_item = table_item_create (table_from_string (TAB_LEFT, text),
                                  NULL, NULL);
  ascii_output_table_item (a, table_item);
  table_item_unref (table_item);
}

static void
ascii_submit (struct output_driver *driver,
              const struct output_item *output_item)
{
  struct ascii_driver *a = ascii_driver_cast (driver);

  if (a->error)
    return;

  if (is_table_item (output_item))
    ascii_output_table_item (a, to_table_item (output_item));
#ifdef HAVE_CAIRO
  else if (is_chart_item (output_item) && a->chart_file_name != NULL)
    {
      struct chart_item *chart_item = to_chart_item (output_item);
      char *file_name;

      file_name = xr_draw_png_chart (chart_item, a->chart_file_name,
                                     a->chart_cnt++,
				     &a->fg,
				     &a->bg);
      if (file_name != NULL)
        {
          struct text_item *text_item;

          text_item = text_item_create_format (
            TEXT_ITEM_PARAGRAPH, _("See %s for a chart."), file_name);

          ascii_submit (driver, &text_item->output_item);
          text_item_unref (text_item);
          free (file_name);
        }
    }
#endif  /* HAVE_CAIRO */
  else if (is_text_item (output_item))
    {
      const struct text_item *text_item = to_text_item (output_item);
      enum text_item_type type = text_item_get_type (text_item);
      const char *text = text_item_get_text (text_item);

      switch (type)
        {
        case TEXT_ITEM_TITLE:
        case TEXT_ITEM_SUBTITLE:
        case TEXT_ITEM_COMMAND_OPEN:
        case TEXT_ITEM_COMMAND_CLOSE:
          break;

        case TEXT_ITEM_BLANK_LINE:
          break;

        case TEXT_ITEM_EJECT_PAGE:
          break;

        default:
          ascii_output_text (a, text);
          break;
        }
    }
  else if (is_message_item (output_item))
    {
      const struct message_item *message_item = to_message_item (output_item);
      const struct msg *msg = message_item_get_msg (message_item);
      char *s = msg_to_string (msg, message_item->command_name);
      ascii_output_text (a, s);
      free (s);
    }
}

const struct output_driver_factory txt_driver_factory =
  { "txt", "-", ascii_create };
const struct output_driver_factory list_driver_factory =
  { "list", "-", ascii_create };

static const struct output_driver_class ascii_driver_class =
  {
    "text",
    ascii_destroy,
    ascii_submit,
    ascii_flush,
  };

static char *ascii_reserve (struct ascii_driver *, int y, int x0, int x1,
                            int n);
static void ascii_layout_cell (struct ascii_driver *,
                               const struct table_cell *,
                               int bb[TABLE_N_AXES][2],
                               int clip[TABLE_N_AXES][2],
                               int *width, int *height);

static void
ascii_draw_line (void *a_, int bb[TABLE_N_AXES][2],
                 enum render_line_style styles[TABLE_N_AXES][2],
                 struct cell_color colors[TABLE_N_AXES][2] UNUSED)
{
  struct ascii_driver *a = a_;
  char mbchar[6];
  int x0, y0, x1, y1;
  ucs4_t uc;
  int mblen;
  int x, y;

  /* Clip to the page. */
  x0 = MAX (bb[H][0], 0);
  y0 = MAX (bb[V][0], 0);
  x1 = MIN (bb[H][1], a->width);
  y1 = bb[V][1];
  if (x1 <= 0 || y1 <= 0 || x0 >= a->width)
    return;

  /* Draw. */
  uc = a->box[make_box_index (styles[V][0], styles[V][1],
                              styles[H][0], styles[H][1])];
  mblen = u8_uctomb (CHAR_CAST (uint8_t *, mbchar), uc, 6);
  for (y = y0; y < y1; y++)
    {
      char *p = ascii_reserve (a, y, x0, x1, mblen * (x1 - x0));
      for (x = x0; x < x1; x++)
        {
          memcpy (p, mbchar, mblen);
          p += mblen;
        }
    }
}

static void
ascii_measure_cell_width (void *a_, const struct table_cell *cell,
                          int *min_width, int *max_width)
{
  struct ascii_driver *a = a_;
  int bb[TABLE_N_AXES][2];
  int clip[TABLE_N_AXES][2];
  int h;

  bb[H][0] = 0;
  bb[H][1] = INT_MAX;
  bb[V][0] = 0;
  bb[V][1] = INT_MAX;
  clip[H][0] = clip[H][1] = clip[V][0] = clip[V][1] = 0;
  ascii_layout_cell (a, cell, bb, clip, max_width, &h);

  if (cell->n_contents != 1
      || cell->contents[0].n_footnotes
      || strchr (cell->contents[0].text, ' '))
    {
      bb[H][1] = 1;
      ascii_layout_cell (a, cell, bb, clip, min_width, &h);
    }
  else
    *min_width = *max_width;
}

static int
ascii_measure_cell_height (void *a_, const struct table_cell *cell, int width)
{
  struct ascii_driver *a = a_;
  int bb[TABLE_N_AXES][2];
  int clip[TABLE_N_AXES][2];
  int w, h;

  bb[H][0] = 0;
  bb[H][1] = width;
  bb[V][0] = 0;
  bb[V][1] = INT_MAX;
  clip[H][0] = clip[H][1] = clip[V][0] = clip[V][1] = 0;
  ascii_layout_cell (a, cell, bb, clip, &w, &h);
  return h;
}

static void
ascii_draw_cell (void *a_, const struct table_cell *cell, int color_idx UNUSED,
                 int bb[TABLE_N_AXES][2],
                 int spill[TABLE_N_AXES][2] UNUSED,
                 int clip[TABLE_N_AXES][2])
{
  struct ascii_driver *a = a_;
  int w, h;

  ascii_layout_cell (a, cell, bb, clip, &w, &h);
}

static char *
ascii_reserve (struct ascii_driver *a, int y, int x0, int x1, int n)
{
  if (y >= a->allocated_lines)
    {
      size_t new_alloc = MAX (25, a->allocated_lines);
      while (new_alloc <= y)
        new_alloc = xtimes (new_alloc, 2);
      a->lines = xnrealloc (a->lines, new_alloc, sizeof *a->lines);
      for (size_t i = a->allocated_lines; i < new_alloc; i++)
        u8_line_init (&a->lines[i]);
      a->allocated_lines = new_alloc;
    }
  return u8_line_reserve (&a->lines[y], x0, x1, n);
}

static void
text_draw (struct ascii_driver *a, unsigned int options,
           bool bold, bool underline,
           int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2],
           int y, const uint8_t *string, int n, size_t width)
{
  int x0 = MAX (0, clip[H][0]);
  int y0 = MAX (0, clip[V][0]);
  int x1 = MIN (a->width, clip[H][1]);
  int y1 = clip[V][1];
  int x;

  if (y < y0 || y >= y1)
    return;

  switch (options & TAB_HALIGN)
    {
    case TAB_LEFT:
      x = bb[H][0];
      break;
    case TAB_CENTER:
      x = (bb[H][0] + bb[H][1] - width + 1) / 2;
      break;
    case TAB_RIGHT:
      x = bb[H][1] - width;
      break;
    default:
      NOT_REACHED ();
    }
  if (x >= x1)
    return;

  while (x < x0)
    {
      ucs4_t uc;
      int mblen;
      int w;

      if (n == 0)
        return;
      mblen = u8_mbtouc (&uc, string, n);

      string += mblen;
      n -= mblen;

      w = uc_width (uc, "UTF-8");
      if (w > 0)
        {
          x += w;
          width -= w;
        }
    }
  if (n == 0)
    return;

  if (x + width > x1)
    {
      int ofs;

      ofs = width = 0;
      for (ofs = 0; ofs < n; )
        {
          ucs4_t uc;
          int mblen;
          int w;

          mblen = u8_mbtouc (&uc, string + ofs, n - ofs);

          w = uc_width (uc, "UTF-8");
          if (w > 0)
            {
              if (width + w > x1 - x)
                break;
              width += w;
            }
          ofs += mblen;
        }
      n = ofs;
      if (n == 0)
        return;
    }

  if (!a->emphasis || (!bold && !underline))
    memcpy (ascii_reserve (a, y, x, x + width, n), string, n);
  else
    {
      size_t n_out;
      size_t ofs;
      char *out;
      int mblen;

      /* First figure out how many bytes need to be inserted. */
      n_out = n;
      for (ofs = 0; ofs < n; ofs += mblen)
        {
          ucs4_t uc;
          int w;

          mblen = u8_mbtouc (&uc, string + ofs, n - ofs);
          w = uc_width (uc, "UTF-8");

          if (w > 0)
            {
              if (bold)
                n_out += 1 + mblen;
              if (underline)
                n_out += 2;
            }
        }

      /* Then insert them. */
      out = ascii_reserve (a, y, x, x + width, n_out);
      for (ofs = 0; ofs < n; ofs += mblen)
        {
          ucs4_t uc;
          int w;

          mblen = u8_mbtouc (&uc, string + ofs, n - ofs);
          w = uc_width (uc, "UTF-8");

          if (w > 0)
            {
              if (bold)
                {
                  out = mempcpy (out, string + ofs, mblen);
                  *out++ = '\b';
                }
              if (underline)
                {
                  *out++ = '_';
                  *out++ = '\b';
                }
            }
          out = mempcpy (out, string + ofs, mblen);
        }
    }
}

static int
ascii_layout_cell_text (struct ascii_driver *a,
                        const struct cell_contents *contents,
                        bool bold, bool underline,
                        int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2],
                        int *widthp)
{
  size_t length;
  const char *text;
  char *breaks;
  int bb_width;
  size_t pos;
  int y;

  y = bb[V][0];
  length = strlen (contents->text);
  if (contents->n_footnotes)
    {
      struct string s;
      int i;

      ds_init_empty (&s);
      ds_extend (&s, length + contents->n_footnotes * 4);
      ds_put_cstr (&s, contents->text);
      for (i = 0; i < contents->n_footnotes; i++)
        ds_put_format (&s, "[%s]", contents->footnotes[i]->marker);

      length = ds_length (&s);
      text = ds_steal_cstr (&s);
    }
  else
    {
      if (length == 0)
        return y;
      text = contents->text;
    }

  breaks = xmalloc (length + 1);
  u8_possible_linebreaks (CHAR_CAST (const uint8_t *, text), length,
                          "UTF-8", breaks);
  breaks[length] = (breaks[length - 1] == UC_BREAK_MANDATORY
                    ? UC_BREAK_PROHIBITED : UC_BREAK_POSSIBLE);

  pos = 0;
  bb_width = bb[H][1] - bb[H][0];
  for (y = bb[V][0]; y < bb[V][1] && pos < length; y++)
    {
      const uint8_t *line = CHAR_CAST (const uint8_t *, text + pos);
      const char *b = breaks + pos;
      size_t n = length - pos;

      size_t last_break_ofs = 0;
      int last_break_width = 0;
      int width = 0;
      size_t graph_ofs;
      size_t ofs;

      for (ofs = 0; ofs < n; )
        {
          ucs4_t uc;
          int mblen;
          int w;

          mblen = u8_mbtouc (&uc, line + ofs, n - ofs);
          if (b[ofs] == UC_BREAK_MANDATORY)
            break;
          else if (b[ofs] == UC_BREAK_POSSIBLE)
            {
              last_break_ofs = ofs;
              last_break_width = width;
            }

          w = uc_width (uc, "UTF-8");
          if (w > 0)
            {
              if (width + w > bb_width)
                {
                  if (isspace (line[ofs]))
                    break;
                  else if (last_break_ofs != 0)
                    {
                      ofs = last_break_ofs;
                      width = last_break_width;
                      break;
                    }
                }
              width += w;
            }
          ofs += mblen;
        }

      /* Trim any trailing spaces off the end of the text to be drawn. */
      for (graph_ofs = ofs; graph_ofs > 0; graph_ofs--)
        if (!isspace (line[graph_ofs - 1]))
          break;
      width -= ofs - graph_ofs;

      /* Draw text. */
      text_draw (a, contents->options, bold, underline,
                 bb, clip, y, line, graph_ofs, width);

      /* If a new-line ended the line, just skip the new-line.  Otherwise, skip
         past any spaces past the end of the line (but not past a new-line). */
      if (b[ofs] == UC_BREAK_MANDATORY)
        ofs++;
      else
        while (ofs < n && isspace (line[ofs]) && b[ofs] != UC_BREAK_MANDATORY)
          ofs++;

      if (width > *widthp)
        *widthp = width;
      pos += ofs;
    }

  free (breaks);
  if (text != contents->text)
    free (CONST_CAST (char *, text));

  return y;
}

static void
ascii_layout_cell (struct ascii_driver *a, const struct table_cell *cell,
                   int bb_[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2],
                   int *widthp, int *heightp)
{
  int bb[TABLE_N_AXES][2];
  size_t i;

  *widthp = 0;
  *heightp = 0;

  memcpy (bb, bb_, sizeof bb);
  for (i = 0; i < cell->n_contents && bb[V][0] < bb[V][1]; i++)
    {
      const struct cell_contents *contents = &cell->contents[i];

      /* Put a blank line between contents. */
      if (i > 0)
        {
          bb[V][0]++;
          if (bb[V][0] >= bb[V][1])
            break;
        }

      bb[V][0] = ascii_layout_cell_text (a, contents, cell->style->bold,
                                         cell->style->underline,
                                         bb, clip, widthp);
    }
  *heightp = bb[V][0] - bb_[V][0];
}

void
ascii_test_write (struct output_driver *driver,
                  const char *s, int x, int y, bool bold, bool underline)
{
  struct ascii_driver *a = ascii_driver_cast (driver);
  int bb[TABLE_N_AXES][2];
  int width, height;

  if (a->file == NULL && !ascii_open_page (a))
    return;

  struct cell_contents contents = {
    .options = TAB_LEFT,
    .text = CONST_CAST (char *, s),
  };
  struct cell_style cell_style = {
    .bold = bold,
    .underline = underline,
  };
  struct table_cell cell = {
    .contents = &contents,
    .n_contents = 1,
    .style = &cell_style,
  };

  bb[TABLE_HORZ][0] = x;
  bb[TABLE_HORZ][1] = a->width;
  bb[TABLE_VERT][0] = y;
  bb[TABLE_VERT][1] = INT_MAX;

  ascii_layout_cell (a, &cell, bb, bb, &width, &height);
}

void
ascii_test_set_length (struct output_driver *driver, int y, int length)
{
  struct ascii_driver *a = ascii_driver_cast (driver);

  if (a->file == NULL && !ascii_open_page (a))
    return;

  if (y < 0)
    return;
  u8_line_set_length (&a->lines[y], length);
}

void
ascii_test_flush (struct output_driver *driver)
{
  struct ascii_driver *a = ascii_driver_cast (driver);

  for (size_t i = a->allocated_lines; i-- > 0; )
    if (a->lines[i].width)
      {
        ascii_output_lines (a, i + 1);
        break;
      }
}

/* ascii_close_page () and support routines. */

#if HAVE_DECL_SIGWINCH
static struct ascii_driver *the_driver;

static void
winch_handler (int signum UNUSED)
{
  update_page_size (the_driver, false);
}
#endif

static bool
ascii_open_page (struct ascii_driver *a)
{
  if (a->error)
    return false;

  if (a->file == NULL)
    {
      a->file = fn_open (a->handle, a->append ? "a" : "w");
      if (a->file != NULL)
        {
	  if ( isatty (fileno (a->file)))
	    {
#if HAVE_DECL_SIGWINCH
	      struct sigaction action;
	      sigemptyset (&action.sa_mask);
	      action.sa_flags = 0;
	      action.sa_handler = winch_handler;
	      the_driver = a;
	      sigaction (SIGWINCH, &action, NULL);
#endif
	      a->auto_width = true;
	    }
        }
      else
        {
          msg_error (errno, _("ascii: opening output file `%s'"),
		     fh_get_file_name (a->handle));
          a->error = true;
          return false;
        }
    }

  return true;
}
