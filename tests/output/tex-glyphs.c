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

#include "libpspp/hmap.h"
#include "tex-rendering.h"
#include "tex-glyphs.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

static void
tex_render (FILE *fp, const char *str)
{
  fputs (str, fp);
  fputc ('\n', fp);
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
  if (argc < 2)
    {
      fprintf (stderr, "Usage: tex-glyphs <file>\n");
      return 1;
    }

  FILE *fp = fopen (argv[1], "w");
  if (!fp)
    {
      perror ("Cannot open output file");
      return 1;
    }

  struct hmap macros;
  hmap_init (&macros);

  fseek (fp, 4096, SEEK_SET);

  tex_render (fp, "\\raggedbottom");

  tex_render (fp, "\\halign{{\\tt #}\\qquad&{\\font\\xx=cmr7 \\xx #}\\hfil&\\quad{\\rm #}");
  tex_render (fp, "\\hfil&\\quad{\\sl #}");
  tex_render (fp, "\\hfil&\\quad{\\it #}");
  tex_render (fp, "\\hfil&\\quad{\\bf #}");
  tex_render (fp, "\\hfil&\\quad{\\tt #}\\cr");

  for (const struct glyph_block *gb = defined_blocks; gb->start; ++gb)
  {
    ucs4_t x = gb->start->code_point;
    for (const struct glyph *g = gb->start; x < gb->n_glyphs + gb->start->code_point; ++g)
      {
        assert (g->code_point == x++);
        fprintf (fp, "U+%04X&%s&M%sM", g->code_point, g->name,
                 code_point_to_tex (g->code_point, &macros));
        fprintf (fp, "&M%sM",
                 code_point_to_tex (g->code_point, &macros));
        fprintf (fp, "&M%sM",
                 code_point_to_tex (g->code_point, &macros));
        fprintf (fp, "&M%sM",
                 code_point_to_tex (g->code_point, &macros));
        fprintf (fp, "&M%sM\\cr\n",
                 code_point_to_tex (g->code_point, &macros));
      }
  }

  {
    struct tex_macro *m;
    struct tex_macro *next;
    HMAP_FOR_EACH_SAFE (m, next, struct tex_macro, node, &macros)
      {
        tex_preamble (fp, tex_macro[m->index]);
        free (m);
      }
  }
  hmap_destroy (&macros);

  tex_render (fp, "}");
  tex_render (fp, "\\bye");

  fclose (fp);
  return 0;
}
