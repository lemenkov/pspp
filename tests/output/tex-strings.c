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

#include "tex-rendering.h"
#include "tex-glyphs.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <gl/xalloc.h>

static void
tex_render (FILE *fp, const char *str)
{
  fputs (str, fp);
  fputc ('\n', fp);
}

#define BLOCK_SIZE 16

/* Reads an entire file FP and returns it as a string.
   Any single instance of newline will be mutated to a space.
   However multiple consecutive newlines will be mutated to
   a single newline.  */
static char *
read_whole_file (FILE *fp)
{
  char *result = 0;
  size_t bytes = 0;
  size_t len = 0;
  int c = -1;
  int consecutive_nl = 0;
  while ((c = fgetc (fp)) >= 0)
    {
      if (len <= bytes + 1)
        {
          result = xrealloc (result, len + BLOCK_SIZE);
          memset (result + len, 0, BLOCK_SIZE);
          len += BLOCK_SIZE;
        }
      if (c != '\n')
        {
          if (consecutive_nl > 1)
            result[bytes++] = '\n';
          if (consecutive_nl == 1)
            result[bytes++] = ' ';

          result[bytes++] = c;
        }

      if (c == '\n')
        consecutive_nl++;
      else
        consecutive_nl = 0;
    }

  return result;
}

static long macro_insertion_point = 0;

static void
tex_preamble (FILE *fp, const char *str)
{
  long where = ftell (fp);
  fseek (fp, macro_insertion_point, SEEK_SET);
  tex_render (fp, str);
  fputc ('\n', fp);
  macro_insertion_point = ftell (fp);
  fseek (fp, where, SEEK_SET);
}


int
main (int argc, char **argv)
{
  char *outfile = NULL;
  int opt;
  while ((opt = getopt (argc, argv, "o:")) != -1)
    {
      switch (opt)
        {
        case 'o':
          outfile = argv[optind-1];
          break;
        default:
          fprintf (stderr, "Usage: tex-strings  -o <outfile> <infile1> <infile2> ... <infileN>\n");
          return 1;
        }
    }

  if (optind >= argc)
    {
      fprintf (stderr, "Usage: tex-strings  -o <outfile> <infile1> <infile2> ... <infileN>\n");
      return 1;
    }

  FILE *fpout = fopen (outfile, "w");
  if (!fpout)
    {
      int err = errno;
      fprintf (stderr, "Cannot open output file %s: %s\n", outfile, strerror (err));
      return 1;
    }

  struct hmap macros;
  hmap_init (&macros);

  fseek (fpout, 4096, SEEK_SET);

  for (int arg = optind; arg < argc; ++arg)
  {
    FILE *fpin = fopen (argv[arg], "r");
    if (!fpin)
      {
        int err = errno;
        fprintf (stderr, "Cannot open input file %s: %s\n", argv[arg], strerror (err));
        return 1;
      }

    tex_render (fpout, "\\noindent");

    char *str = read_whole_file (fpin);
    const char *s = str;
    size_t n = strlen (str);
    const char *frag = 0;
    while (n > 0)
      {
        frag = u8_to_tex_fragments (&s, &n, &macros);
        fputs (frag, fpout);
      }
    free (str);


    fclose (fpin);

    tex_render(fpout, "\\par\\vskip 1em");
  }

  {
    struct tex_macro *m;
    struct tex_macro *next;
    HMAP_FOR_EACH_SAFE (m, next, struct tex_macro, node, &macros)
      {
        tex_preamble (fpout, tex_macro[m->index]);
        free (m);
      }
  }
  hmap_destroy (&macros);

  tex_render (fpout, "\\bye");

  fclose (fpout);
  return 0;
}
