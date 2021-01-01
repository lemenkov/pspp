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
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "data/file-handle-def.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/string-map.h"
#include "output/ascii.h"
#include "output/driver.h"
#include "output/table.h"
#include "output/table-item.h"

#include "gl/error.h"
#include "gl/progname.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

/* --emphasis: ASCII driver emphasis option. */
static bool bold;
static bool underline;

/* --box: ASCII driver box option. */
static char *box;

/* ASCII driver, for ASCII driver test mode. */
static struct output_driver *ascii_driver;

static const char *parse_options (int argc, char **argv);
static void usage (void) NO_RETURN;
static void draw (FILE *);

int
main (int argc, char **argv)
{
  set_program_name (argv[0]);
  i18n_init ();
  output_engine_push ();
  const char *input_file_name = parse_options (argc, argv);

  FILE *input = (!strcmp (input_file_name, "-")
                 ? stdin
                 : fopen (input_file_name, "r"));
  if (!input)
    error (1, errno, "%s: open failed", input_file_name);

  draw (input);

  if (input != stdin)
    fclose (input);

  output_engine_pop ();
  fh_done ();

  return 0;
}

static struct output_driver *
configure_driver (int width, int min_break)
{
  struct string_map options = STRING_MAP_INITIALIZER (options);
  string_map_insert (&options, "format", "txt");
  string_map_insert (&options, "output-file", "-");
  string_map_insert_nocopy (&options, xstrdup ("width"),
                            xasprintf ("%d", width));
  if (min_break >= 0)
    string_map_insert_nocopy (&options, xstrdup ("min-hbreak"),
                              xasprintf ("%d", min_break));
  if (bold || underline)
    string_map_insert (&options, "emphasis", "true");
  if (box != NULL)
    string_map_insert (&options, "box", box);

  struct output_driver *driver = output_driver_create (&options);
  string_map_destroy (&options);
  if (!driver)
    exit (EXIT_FAILURE);
  output_driver_register (driver);
  return driver;
}

static const char *
parse_options (int argc, char **argv)
{
  int width = 79;
  int min_break = -1;

  for (;;)
    {
      enum {
        OPT_WIDTH = UCHAR_MAX + 1,
        OPT_LENGTH,
        OPT_MIN_BREAK,
        OPT_EMPHASIS,
        OPT_BOX,
        OPT_HELP
      };
      static const struct option options[] =
        {
          {"width", required_argument, NULL, OPT_WIDTH},
          {"length", required_argument, NULL, OPT_LENGTH},
          {"emphasis", required_argument, NULL, OPT_EMPHASIS},
          {"box", required_argument, NULL, OPT_BOX},
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

        case OPT_EMPHASIS:
          if (!strcmp (optarg, "bold"))
            {
              bold = true;
              underline = false;
            }
          else if (!strcmp (optarg, "underline"))
            {
              bold = false;
              underline = true;
            }
          else if (!strcmp (optarg, "none"))
            {
              bold = underline = false;
            }
          else
            error (1, 0, "argument to --emphasis must be \"bold\" or "
                   "\"underline\" or \"none\"");
          break;

        case OPT_BOX:
          box = optarg;
          break;

        case OPT_HELP:
          usage ();

        case 0:
          break;

        case '?':
          exit(EXIT_FAILURE);
          break;

        default:
          NOT_REACHED ();
        }

    }

  ascii_driver = configure_driver (width, min_break);

  if (optind + 1 != argc)
    error (1, 0, "exactly one non-option argument required; "
           "use --help for help");
  return argv[optind];
}

static void
usage (void)
{
  printf ("%s, to test PSPP ASCII driver drawing\n"
          "usage: %s [OPTIONS] INPUT\n"
          "\nOptions:\n"
          "  --width=WIDTH   set page width in characters\n"
          "  --length=LINE   set page length in lines\n",
          program_name, program_name);
  exit (EXIT_SUCCESS);
}

static void
draw (FILE *stream)
{
  char buffer[1024];
  int line = 0;

  while (fgets (buffer, sizeof buffer, stream))
    {
      char text[sizeof buffer];
      int length;
      int emph;
      int x, y;

      line++;
      if (strchr ("#\r\n", buffer[0]))
        continue;

      if (sscanf (buffer, "%d %d %d %[^\n]", &x, &y, &emph, text) == 4)
        ascii_test_write (ascii_driver, text, x, y, emph ? bold : false,
                          emph ? underline : false);
      else if (sscanf (buffer, "set-length %d %d", &y, &length) == 2)
        ascii_test_set_length (ascii_driver, y, length);
      else
        error (1, 0, "line %d has invalid format", line);
    }
  ascii_test_flush (ascii_driver);
}
