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
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <unilbrk.h>
#include <unistd.h>
#include <unistr.h>
#include <uniwidth.h>

#ifdef HAVE_TERMIOS_H
# include <sys/ioctl.h>
# include <termios.h>
#endif

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
#include "output/cairo-chart.h"
#include "output/chart-provider.h"
#include "output/driver-provider.h"
#include "output/options.h"
#include "output/pivot-output.h"
#include "output/pivot-table.h"
#include "output/render.h"
#include "output/output-item.h"

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
    ASCII_LINE_DASHED,
    ASCII_LINE_SINGLE,
    ASCII_LINE_DOUBLE,
    ASCII_N_LINES
  };

struct box_chars
  {
    ucs4_t c[ASCII_N_LINES][ASCII_N_LINES][ASCII_N_LINES][ASCII_N_LINES];
  };

static const struct box_chars *
get_ascii_box (void)
{
  enum {
    _ = ASCII_LINE_NONE,
    d = ASCII_LINE_DASHED,
    S = ASCII_LINE_SINGLE,
    D = ASCII_LINE_DOUBLE,
  };

  static const struct box_chars ascii_box =
    {
      /* r  b  l   t:  _    d    S    D */
      .c[_][_][_] = { ' ', '|', '|', '#', },
      .c[_][_][d] = { '-', '+', '+', '#', },
      .c[_][_][S] = { '-', '+', '+', '#', },
      .c[_][_][D] = { '=', '#', '#', '#', },
      .c[_][d][_] = { '|', '|', '|', '#', },
      .c[_][d][d] = { '+', '+', '+', '#', },
      .c[_][d][S] = { '+', '+', '+', '#', },
      .c[_][d][D] = { '#', '#', '#', '#', },
      .c[_][S][_] = { '|', '|', '|', '#', },
      .c[_][S][d] = { '+', '+', '+', '#', },
      .c[_][S][S] = { '+', '+', '+', '#', },
      .c[_][S][D] = { '#', '#', '#', '#', },
      .c[_][D][_] = { '#', '#', '#', '#', },
      .c[_][D][d] = { '#', '#', '#', '#', },
      .c[_][D][S] = { '#', '#', '#', '#', },
      .c[_][D][D] = { '#', '#', '#', '#', },
      .c[d][_][_] = { '-', '+', '+', '#', },
      .c[d][_][d] = { '-', '+', '+', '#', },
      .c[d][_][S] = { '-', '+', '+', '#', },
      .c[d][_][D] = { '#', '#', '#', '#', },
      .c[d][d][_] = { '+', '+', '+', '#', },
      .c[d][d][d] = { '+', '+', '+', '#', },
      .c[d][d][S] = { '+', '+', '+', '#', },
      .c[d][d][D] = { '#', '#', '#', '#', },
      .c[d][S][_] = { '+', '+', '+', '#', },
      .c[d][S][d] = { '+', '+', '+', '#', },
      .c[d][S][S] = { '+', '+', '+', '#', },
      .c[d][S][D] = { '#', '#', '#', '#', },
      .c[d][D][_] = { '#', '#', '#', '#', },
      .c[d][D][d] = { '#', '#', '#', '#', },
      .c[d][D][S] = { '#', '#', '#', '#', },
      .c[d][D][D] = { '#', '#', '#', '#', },
      .c[S][_][_] = { '-', '+', '+', '#', },
      .c[S][_][d] = { '-', '+', '+', '#', },
      .c[S][_][S] = { '-', '+', '+', '#', },
      .c[S][_][D] = { '#', '#', '#', '#', },
      .c[S][d][_] = { '+', '+', '+', '#', },
      .c[S][d][d] = { '+', '+', '+', '#', },
      .c[S][d][S] = { '+', '+', '+', '#', },
      .c[S][d][D] = { '#', '#', '#', '#', },
      .c[S][S][_] = { '+', '+', '+', '#', },
      .c[S][S][d] = { '+', '+', '+', '#', },
      .c[S][S][S] = { '+', '+', '+', '#', },
      .c[S][S][D] = { '#', '#', '#', '#', },
      .c[S][D][_] = { '#', '#', '#', '#', },
      .c[S][D][d] = { '#', '#', '#', '#', },
      .c[S][D][S] = { '#', '#', '#', '#', },
      .c[S][D][D] = { '#', '#', '#', '#', },
      .c[D][_][_] = { '=', '#', '#', '#', },
      .c[D][_][d] = { '#', '#', '#', '#', },
      .c[D][_][S] = { '#', '#', '#', '#', },
      .c[D][_][D] = { '=', '#', '#', '#', },
      .c[D][d][_] = { '#', '#', '#', '#', },
      .c[D][d][d] = { '#', '#', '#', '#', },
      .c[D][d][S] = { '#', '#', '#', '#', },
      .c[D][d][D] = { '#', '#', '#', '#', },
      .c[D][S][_] = { '#', '#', '#', '#', },
      .c[D][S][d] = { '#', '#', '#', '#', },
      .c[D][S][S] = { '#', '#', '#', '#', },
      .c[D][S][D] = { '#', '#', '#', '#', },
      .c[D][D][_] = { '#', '#', '#', '#', },
      .c[D][D][d] = { '#', '#', '#', '#', },
      .c[D][D][S] = { '#', '#', '#', '#', },
      .c[D][D][D] = { '#', '#', '#', '#', },
    };
  return &ascii_box;
}

static const struct box_chars *
get_unicode_box (void)
{
  enum {
    _ = ASCII_LINE_NONE,
    d = ASCII_LINE_DASHED,
    S = ASCII_LINE_SINGLE,
    D = ASCII_LINE_DOUBLE,
  };

  static const struct box_chars unicode_box =
    {
      /* r  b  l   t:   _       d       S       D */
      .c[_][_][_] = { 0x0020, 0x2575, 0x2575, 0x2551, }, /*  ╵╵║ */
      .c[_][_][d] = { 0x2574, 0x256f, 0x256f, 0x255c, }, /* ╴╯╯╜ */
      .c[_][_][S] = { 0x2574, 0x256f, 0x256f, 0x255c, }, /* ╴╯╯╜ */
      .c[_][_][D] = { 0x2550, 0x255b, 0x255b, 0x255d, }, /* ═╛╛╝ */
      .c[_][S][_] = { 0x2577, 0x2502, 0x2502, 0x2551, }, /* ╷││║ */
      .c[_][S][d] = { 0x256e, 0x2524, 0x2524, 0x2562, }, /* ╮┤┤╢ */
      .c[_][S][S] = { 0x256e, 0x2524, 0x2524, 0x2562, }, /* ╮┤┤╢ */
      .c[_][S][D] = { 0x2555, 0x2561, 0x2561, 0x2563, }, /* ╕╡╡╣ */
      .c[_][d][_] = { 0x2577, 0x250a, 0x2502, 0x2551, }, /* ╷┊│║ */
      .c[_][d][d] = { 0x256e, 0x2524, 0x2524, 0x2562, }, /* ╮┤┤╢ */
      .c[_][d][S] = { 0x256e, 0x2524, 0x2524, 0x2562, }, /* ╮┤┤╢ */
      .c[_][d][D] = { 0x2555, 0x2561, 0x2561, 0x2563, }, /* ╕╡╡╣ */
      .c[_][D][_] = { 0x2551, 0x2551, 0x2551, 0x2551, }, /* ║║║║ */
      .c[_][D][d] = { 0x2556, 0x2562, 0x2562, 0x2562, }, /* ╖╢╢╢ */
      .c[_][D][S] = { 0x2556, 0x2562, 0x2562, 0x2562, }, /* ╖╢╢╢ */
      .c[_][D][D] = { 0x2557, 0x2563, 0x2563, 0x2563, }, /* ╗╣╣╣ */
      .c[d][_][_] = { 0x2576, 0x2570, 0x2570, 0x2559, }, /* ╶╰╰╙ */
      .c[d][_][d] = { 0x254c, 0x2534, 0x2534, 0x2568, }, /* ╌┴┴╨ */
      .c[d][_][S] = { 0x2500, 0x2534, 0x2534, 0x2568, }, /* ─┴┴╨ */
      .c[d][_][D] = { 0x2550, 0x2567, 0x2567, 0x2569, }, /* ═╧╧╩ */
      .c[d][d][_] = { 0x256d, 0x251c, 0x251c, 0x255f, }, /* ╭├├╟ */
      .c[d][d][d] = { 0x252c, 0x002b, 0x253c, 0x256a, }, /* ┬+┼╪ */
      .c[d][d][S] = { 0x252c, 0x253c, 0x253c, 0x256a, }, /* ┬┼┼╪ */
      .c[d][d][D] = { 0x2564, 0x256a, 0x256a, 0x256c, }, /* ╤╪╪╬ */
      .c[d][S][_] = { 0x256d, 0x251c, 0x251c, 0x255f, }, /* ╭├├╟ */
      .c[d][S][d] = { 0x252c, 0x253c, 0x253c, 0x256a, }, /* ┬┼┼╪ */
      .c[d][S][S] = { 0x252c, 0x253c, 0x253c, 0x256a, }, /* ┬┼┼╪ */
      .c[d][S][D] = { 0x2564, 0x256a, 0x256a, 0x256c, }, /* ╤╪╪╬ */
      .c[d][D][_] = { 0x2553, 0x255f, 0x255f, 0x255f, }, /* ╓╟╟╟ */
      .c[d][D][d] = { 0x2565, 0x256b, 0x256b, 0x256b, }, /* ╥╫╫╫ */
      .c[d][D][S] = { 0x2565, 0x256b, 0x256b, 0x256b, }, /* ╥╫╫╫ */
      .c[d][D][D] = { 0x2566, 0x256c, 0x256c, 0x256c, }, /* ╦╬╬╬ */
      .c[S][_][_] = { 0x2576, 0x2570, 0x2570, 0x2559, }, /* ╶╰╰╙ */
      .c[S][_][d] = { 0x2500, 0x2534, 0x2534, 0x2568, }, /* ─┴┴╨ */
      .c[S][_][S] = { 0x2500, 0x2534, 0x2534, 0x2568, }, /* ─┴┴╨ */
      .c[S][_][D] = { 0x2550, 0x2567, 0x2567, 0x2569, }, /* ═╧╧╩ */
      .c[S][d][_] = { 0x256d, 0x251c, 0x251c, 0x255f, }, /* ╭├├╟ */
      .c[S][d][d] = { 0x252c, 0x253c, 0x253c, 0x256a, }, /* ┬┼┼╪ */
      .c[S][d][S] = { 0x252c, 0x253c, 0x253c, 0x256a, }, /* ┬┼┼╪ */
      .c[S][d][D] = { 0x2564, 0x256a, 0x256a, 0x256c, }, /* ╤╪╪╬ */
      .c[S][S][_] = { 0x256d, 0x251c, 0x251c, 0x255f, }, /* ╭├├╟ */
      .c[S][S][d] = { 0x252c, 0x253c, 0x253c, 0x256a, }, /* ┬┼┼╪ */
      .c[S][S][S] = { 0x252c, 0x253c, 0x253c, 0x256a, }, /* ┬┼┼╪ */
      .c[S][S][D] = { 0x2564, 0x256a, 0x256a, 0x256c, }, /* ╤╪╪╬ */
      .c[S][D][_] = { 0x2553, 0x255f, 0x255f, 0x255f, }, /* ╓╟╟╟ */
      .c[S][D][d] = { 0x2565, 0x256b, 0x256b, 0x256b, }, /* ╥╫╫╫ */
      .c[S][D][S] = { 0x2565, 0x256b, 0x256b, 0x256b, }, /* ╥╫╫╫ */
      .c[S][D][D] = { 0x2566, 0x256c, 0x256c, 0x256c, }, /* ╦╬╬╬ */
      .c[D][_][_] = { 0x2550, 0x2558, 0x2558, 0x255a, }, /* ═╘╘╚ */
      .c[D][_][d] = { 0x2550, 0x2567, 0x2567, 0x2569, }, /* ═╧╧╩ */
      .c[D][_][S] = { 0x2550, 0x2567, 0x2567, 0x2569, }, /* ═╧╧╩ */
      .c[D][_][D] = { 0x2550, 0x2567, 0x2567, 0x2569, }, /* ═╧╧╩ */
      .c[D][d][_] = { 0x2552, 0x255e, 0x255e, 0x2560, }, /* ╒╞╞╠ */
      .c[D][d][d] = { 0x2564, 0x256a, 0x256a, 0x256c, }, /* ╤╪╪╬ */
      .c[D][d][S] = { 0x2564, 0x256a, 0x256a, 0x256c, }, /* ╤╪╪╬ */
      .c[D][d][D] = { 0x2564, 0x256a, 0x256a, 0x256c, }, /* ╤╪╪╬ */
      .c[D][S][_] = { 0x2552, 0x255e, 0x255e, 0x2560, }, /* ╒╞╞╠ */
      .c[D][S][d] = { 0x2564, 0x256a, 0x256a, 0x256c, }, /* ╤╪╪╬ */
      .c[D][S][S] = { 0x2564, 0x256a, 0x256a, 0x256c, }, /* ╤╪╪╬ */
      .c[D][S][D] = { 0x2564, 0x256a, 0x256a, 0x256c, }, /* ╤╪╪╬ */
      .c[D][D][_] = { 0x2554, 0x2560, 0x2560, 0x2560, }, /* ╔╠╠╠ */
      .c[D][D][d] = { 0x2560, 0x256c, 0x256c, 0x256c, }, /* ╠╬╬╬ */
      .c[D][D][S] = { 0x2560, 0x256c, 0x256c, 0x256c, }, /* ╠╬╬╬ */
      .c[D][D][D] = { 0x2566, 0x256c, 0x256c, 0x256c, }, /* ╦╬╬╬ */
    };
  return &unicode_box;
}

static int
ascii_line_from_render_line (int render_line)
{
  switch (render_line)
    {
    case RENDER_LINE_NONE:
      return ASCII_LINE_NONE;

    case RENDER_LINE_DASHED:
      return ASCII_LINE_DASHED;

    case RENDER_LINE_SINGLE:
    case RENDER_LINE_THICK:
    case RENDER_LINE_THIN:
      return ASCII_LINE_SINGLE;

    case RENDER_LINE_DOUBLE:
      return ASCII_LINE_DOUBLE;

    default:
      return ASCII_LINE_NONE;
    }

}

static ucs4_t
box_get (const struct box_chars *box,
         int left_, int right_, int top_, int bottom_)
{
  bool rtl = render_direction_rtl ();
  int left = ascii_line_from_render_line (rtl ? right_ : left_);
  int right = ascii_line_from_render_line (rtl ? left_ : right_);
  int top = ascii_line_from_render_line (top_);
  int bottom = ascii_line_from_render_line (bottom_);

  return box->c[right][bottom][left][top];
}

/* ASCII output driver. */
struct ascii_driver
  {
    struct output_driver driver;

    /* User parameters. */
    bool append;                /* Append if output file already exists? */
    bool emphasis;              /* Enable bold and underline in output? */
    char *chart_file_name;      /* Name of files used for charts. */

    /* Colours for charts */
    struct cell_color fg;
    struct cell_color bg;

    /* How the page width is determined: */
    enum {
      FIXED_WIDTH,              /* Specified by configuration. */
      VIEW_WIDTH,               /* From SET WIDTH. */
      TERMINAL_WIDTH            /* From the terminal's width. */
    } width_mode;
    int width;                  /* Page width. */

    int min_hbreak;             /* Min cell size to break across pages. */

    const struct box_chars *box; /* Line & box drawing characters. */

    /* Internal state. */
    struct file_handle *handle;
    FILE *file;                 /* Output file. */
    bool error;                 /* Output error? */
    struct u8_line *lines;      /* Page content. */
    int allocated_lines;        /* Number of lines allocated. */
    int chart_cnt;              /* Number of charts so far. */
    int object_cnt;             /* Number of objects so far. */
    const struct pivot_table *pt;
    struct render_params params;
  };

static const struct output_driver_class ascii_driver_class;

static void ascii_submit (struct output_driver *, const struct output_item *);

static int get_terminal_width (void);

static bool update_page_size (struct ascii_driver *, bool issue_error);
static int parse_page_size (struct driver_option *);

static void ascii_draw_line (void *, int bb[TABLE_N_AXES][2],
                             enum render_line_style styles[TABLE_N_AXES][2],
                             struct cell_color colors[TABLE_N_AXES][2]);
static void ascii_measure_cell_width (void *, const struct table_cell *,
                                      int *min, int *max);
static int ascii_measure_cell_height (void *, const struct table_cell *,
                                      int width);
static void ascii_draw_cell (void *, const struct table_cell *, int color_idx,
                             int bb[TABLE_N_AXES][2], int valign_offset,
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

/* Return true iff the terminal appears to be an xterm with
   UTF-8 capabilities */
static bool
term_is_utf8_xterm (void)
{
  const char *term = getenv ("TERM");
  const char *xterm_locale = getenv ("XTERM_LOCAL");
  return (term && xterm_locale
          && !strcmp (term, "xterm")
          && (strcasestr (xterm_locale, "utf8")
              || strcasestr (xterm_locale, "utf-8")));
}

static struct output_driver *
ascii_create (struct  file_handle *fh, enum settings_output_devices device_type,
              struct string_map *o)
{
  enum { BOX_ASCII, BOX_UNICODE } box;
  struct output_driver *d;
  struct ascii_driver *a;

  a = xzalloc (sizeof *a);
  d = &a->driver;
  output_driver_init (&a->driver, &ascii_driver_class, fh_get_file_name (fh), device_type);
  a->append = parse_boolean (opt (d, o, "append", "false"));
  a->emphasis = parse_boolean (opt (d, o, "emphasis", "false"));

  a->chart_file_name = parse_chart_file_name (opt (d, o, "charts", fh_get_file_name (fh)));
  a->handle = fh;


  bool terminal = !strcmp (fh_get_file_name (fh), "-") && isatty (1);
  a->width = parse_page_size (opt (d, o, "width", "-1"));
  a->width_mode = (a->width > 0 ? FIXED_WIDTH
                   : terminal ? TERMINAL_WIDTH
                   : VIEW_WIDTH);
  a->min_hbreak = parse_int (opt (d, o, "min-hbreak", "-1"), -1, INT_MAX);

  a->bg = parse_color (opt (d, o, "background-color", "#FFFFFFFFFFFF"));
  a->fg = parse_color (opt (d, o, "foreground-color", "#000000000000"));

  const char *default_box = (terminal && (!strcmp (locale_charset (), "UTF-8")
                                          || term_is_utf8_xterm ())
                             ? "unicode" : "ascii");
  box = parse_enum (opt (d, o, "box", default_box),
                    "ascii", BOX_ASCII,
                    "unicode", BOX_UNICODE,
                    NULL_SENTINEL);
  a->box = box == BOX_ASCII ? get_ascii_box () : get_unicode_box ();

  a->file = NULL;
  a->error = false;
  a->lines = NULL;
  a->allocated_lines = 0;
  a->chart_cnt = 0;
  a->object_cnt = 0;

  static const struct render_ops ascii_render_ops = {
    .draw_line = ascii_draw_line,
    .measure_cell_width = ascii_measure_cell_width,
    .measure_cell_height = ascii_measure_cell_height,
    .adjust_break = NULL,
    .draw_cell = ascii_draw_cell,
  };
  a->params.ops = &ascii_render_ops;
  a->params.aux = a;
  a->params.size[H] = a->width;
  a->params.size[V] = INT_MAX;
  a->params.font_size[H] = 1;
  a->params.font_size[V] = 1;

  static const int ascii_line_widths[RENDER_N_LINES] = {
    [RENDER_LINE_NONE] = 0,
    [RENDER_LINE_SINGLE] = 1,
    [RENDER_LINE_DASHED] = 1,
    [RENDER_LINE_THICK] = 1,
    [RENDER_LINE_THIN] = 1,
    [RENDER_LINE_DOUBLE] = 1,
  };
  a->params.line_widths = ascii_line_widths;
  a->params.supports_margins = false;
  a->params.rtl = render_direction_rtl ();
  a->params.printing = true;

  if (!update_page_size (a, true))
    goto error;

  a->file = fn_open (a->handle, a->append ? "a" : "w");
  if (!a->file)
    {
      msg_error (errno, _("ascii: opening output file `%s'"),
                 fh_get_file_name (a->handle));
      goto error;
    }

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
          if (value >= 1 && errno != ERANGE && *tail == '\0')
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
  enum { MIN_WIDTH = 6 };

  int want_width = (a->width_mode == VIEW_WIDTH ? settings_get_viewwidth ()
                    : a->width_mode == TERMINAL_WIDTH ? get_terminal_width ()
                    : a->width);
  bool ok = want_width >= MIN_WIDTH;
  if (!ok && issue_error)
    msg (ME, _("ascii: page must be at least %d characters wide, but "
               "as configured is only %d characters"),
         MIN_WIDTH, want_width);

  a->width = ok ? want_width : MIN_WIDTH;
  a->params.size[H] = a->width;
  a->params.min_break[H] = a->min_hbreak >= 0 ? a->min_hbreak : a->width / 2;

  return ok;
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
      if (y < a->allocated_lines)
        {
          struct u8_line *line = &a->lines[y];

          while (ds_chomp_byte (&line->s, ' '))
            continue;
          fwrite (ds_data (&line->s), 1, ds_length (&line->s), a->file);
          u8_line_clear (&a->lines[y]);
        }
      putc ('\n', a->file);
    }
}

static void
ascii_output_table_item (struct ascii_driver *a,
                         const struct output_item *item)
{
  update_page_size (a, false);
  a->pt = item->table;

  size_t *layer_indexes;
  PIVOT_OUTPUT_FOR_EACH_LAYER (layer_indexes, item->table, true)
    {
      struct render_pager *p = render_pager_create (&a->params, item->table,
                                                    layer_indexes);
      for (int i = 0; render_pager_has_next (p); i++)
        {
          if (a->object_cnt++)
            putc ('\n', a->file);

          ascii_output_lines (a, render_pager_draw_next (p, INT_MAX));
        }
      render_pager_destroy (p);
    }

  a->pt = NULL;
}

static void
ascii_output_table_item_unref (struct ascii_driver *a,
                               struct output_item *table_item)
{
  ascii_output_table_item (a, table_item);
  output_item_unref (table_item);
}

static void
ascii_submit (struct output_driver *driver, const struct output_item *item)
{
  struct ascii_driver *a = ascii_driver_cast (driver);
  if (a->error)
    return;

  switch (item->type)
    {
    case OUTPUT_ITEM_TABLE:
      ascii_output_table_item (a, item);
      break;

    case OUTPUT_ITEM_IMAGE:
      if (a->chart_file_name != NULL)
        {
          char *file_name = xr_write_png_image (
            item->image, a->chart_file_name, ++a->chart_cnt);
          if (file_name != NULL)
            {
              struct output_item *text_item = text_item_create_nocopy (
                TEXT_ITEM_LOG,
                xasprintf (_("See %s for an image."), file_name),
                NULL);

              ascii_submit (driver, text_item);
              output_item_unref (text_item);
              free (file_name);
            }
        }
      break;

    case OUTPUT_ITEM_CHART:
      if (a->chart_file_name != NULL)
        {
          char *file_name = xr_draw_png_chart (
            item->chart, a->chart_file_name, ++a->chart_cnt, &a->fg, &a->bg);
          if (file_name != NULL)
            {
              struct output_item *text_item = text_item_create_nocopy (
                TEXT_ITEM_LOG,
                xasprintf (_("See %s for a chart."), file_name),
                NULL);

              ascii_submit (driver, text_item);
              output_item_unref (text_item);
              free (file_name);
            }
        }
      break;

    case OUTPUT_ITEM_TEXT:
      if (item->text.subtype != TEXT_ITEM_PAGE_TITLE)
        ascii_output_table_item_unref (
          a, text_item_to_table_item (output_item_ref (item)));
      break;

    case OUTPUT_ITEM_MESSAGE:
      ascii_output_table_item_unref (
        a, text_item_to_table_item (
          message_item_to_text_item (
            output_item_ref (item))));
      break;

    case OUTPUT_ITEM_GROUP:
      NOT_REACHED ();

    case OUTPUT_ITEM_PAGE_BREAK:
      break;
    }
}

const struct output_driver_factory txt_driver_factory =
  { "txt", "-", ascii_create };
const struct output_driver_factory list_driver_factory =
  { "list", "-", ascii_create };

static const struct output_driver_class ascii_driver_class =
  {
    .name = "text",
    .destroy = ascii_destroy,
    .submit = ascii_submit,
    .flush = ascii_flush,
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
  uc = box_get (a->box, styles[V][0], styles[V][1], styles[H][0], styles[H][1]);
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

  bb[H][1] = 1;
  ascii_layout_cell (a, cell, bb, clip, min_width, &h);
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
                 int bb[TABLE_N_AXES][2], int valign_offset,
                 int spill[TABLE_N_AXES][2] UNUSED,
                 int clip[TABLE_N_AXES][2])
{
  struct ascii_driver *a = a_;
  int w, h;

  bb[V][0] += valign_offset;
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
text_draw (struct ascii_driver *a, enum table_halign halign, bool numeric,
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

  switch (table_halign_interpret (halign, numeric))
    {
    case TABLE_HALIGN_LEFT:
      x = bb[H][0];
      break;
    case TABLE_HALIGN_CENTER:
      x = (bb[H][0] + bb[H][1] - width + 1) / 2;
      break;
    case TABLE_HALIGN_RIGHT:
    case TABLE_HALIGN_DECIMAL:
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
      for (ofs = 0; ofs < n;)
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

static void
ascii_layout_cell (struct ascii_driver *a, const struct table_cell *cell,
                   int bb[TABLE_N_AXES][2], int clip[TABLE_N_AXES][2],
                   int *widthp, int *heightp)
{
  *widthp = 0;
  *heightp = 0;

  struct string body = DS_EMPTY_INITIALIZER;
  bool numeric = pivot_value_format (cell->value, a->pt, &body);

  /* Calculate length; if it's zero, then there's nothing to do. */
  if (ds_is_empty (&body))
    {
      ds_destroy (&body);
      return;
    }

  size_t length = ds_length (&body);
  const uint8_t *text = CHAR_CAST (uint8_t *, ds_cstr (&body));

  char *breaks = xmalloc (length + 1);
  u8_possible_linebreaks (text, length, "UTF-8", breaks);
  breaks[length] = (breaks[length - 1] == UC_BREAK_MANDATORY
                    ? UC_BREAK_PROHIBITED : UC_BREAK_POSSIBLE);

  size_t pos = 0;
  int bb_width = bb[H][1] - bb[H][0];
  for (int y = bb[V][0]; y < bb[V][1] && pos < length; y++)
    {
      const uint8_t *line = text + pos;
      const char *b = breaks + pos;
      size_t n = length - pos;

      size_t last_break_ofs = 0;
      int last_break_width = 0;
      int width = 0;
      size_t graph_ofs;
      size_t ofs;

      for (ofs = 0; ofs < n;)
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
      text_draw (a, cell->cell_style->halign, numeric,
                 cell->font_style->bold,
                 cell->font_style->underline,
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
      ++*heightp;
      pos += ofs;
    }

  free (breaks);
  ds_destroy (&body);
}

void
ascii_test_write (struct output_driver *driver,
                  const char *s, int x, int y, bool bold, bool underline)
{
  struct ascii_driver *a = ascii_driver_cast (driver);
  int bb[TABLE_N_AXES][2];
  int width, height;

  if (!a->file)
    return;

  struct cell_style cell_style = { .halign = TABLE_HALIGN_LEFT };
  struct font_style font_style = {
    .bold = bold,
    .underline = underline,
  };
  const struct pivot_value value = {
    .text = {
      .type = PIVOT_VALUE_TEXT,
      .local = CONST_CAST (char *, s),
      .c = CONST_CAST (char *, s),
      .id = CONST_CAST (char *, s),
      .user_provided = true,
    },
  };
  struct table_cell cell = {
    .value = &value,
    .font_style = &font_style,
    .cell_style = &cell_style,
  };

  bb[TABLE_HORZ][0] = x;
  bb[TABLE_HORZ][1] = a->width;
  bb[TABLE_VERT][0] = y;
  bb[TABLE_VERT][1] = INT_MAX;

  struct pivot_table pt = {
    .show_values = SETTINGS_VALUE_SHOW_DEFAULT,
    .show_variables = SETTINGS_VALUE_SHOW_DEFAULT,
  };
  a->pt = &pt;
  ascii_layout_cell (a, &cell, bb, bb, &width, &height);
  a->pt = NULL;
}

void
ascii_test_set_length (struct output_driver *driver, int y, int length)
{
  struct ascii_driver *a = ascii_driver_cast (driver);

  if (!a->file)
    return;

  if (y < 0)
    return;
  u8_line_set_length (&a->lines[y], length);
}

void
ascii_test_flush (struct output_driver *driver)
{
  struct ascii_driver *a = ascii_driver_cast (driver);

  for (size_t i = a->allocated_lines; i-- > 0;)
    if (a->lines[i].width)
      {
        ascii_output_lines (a, i + 1);
        break;
      }
}

static sig_atomic_t terminal_changed = true;
static int terminal_width;

#if HAVE_DECL_SIGWINCH
static void
winch_handler (int signum UNUSED)
{
  terminal_changed = true;
}
#endif

int
get_terminal_width (void)
{
#if HAVE_DECL_SIGWINCH
  static bool setup_signal;
  if (!setup_signal)
    {
      setup_signal = true;

      struct sigaction action = { .sa_handler = winch_handler };
      sigemptyset (&action.sa_mask);
      sigaction (SIGWINCH, &action, NULL);
    }
#endif

  if (terminal_changed)
    {
      terminal_changed = false;

#ifdef HAVE_TERMIOS_H
      struct winsize ws;
      if (!ioctl (0, TIOCGWINSZ, &ws))
        terminal_width = ws.ws_col;
      else
#endif
        {
          if (getenv ("COLUMNS"))
            terminal_width = atoi (getenv ("COLUMNS"));
        }

      if (terminal_width <= 0 || terminal_width > 1024)
        terminal_width = 79;
    }

  return terminal_width;
}
