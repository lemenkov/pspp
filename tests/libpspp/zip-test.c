/* PSPP - a program for statistical analysis.
   Copyright (C) 2011 Free Software Foundation, Inc.

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


/* A simple program to zip or unzip a file */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdio.h>
#include "libpspp/assertion.h"
#include <libpspp/compiler.h>
#include <libpspp/zip-writer.h>
#include <libpspp/zip-reader.h>
#include <libpspp/str.h>

#include <errno.h>

/* Exit with a failure code.
   (Place a breakpoint on this function while debugging.) */
static void
check_die (void)
{
  exit (EXIT_FAILURE);
}

int
main (int argc, char **argv)
{
  if (argc < 4)
    {
      fprintf (stderr, "Usage zip-test: {r|w} archive file0 file1 ... filen\n");
      check_die ();
    }

  if (0 == strcmp ("w", argv[1]))
    {
      int i;
      struct zip_writer *zw = zip_writer_create (argv[2]);
      for (i = 3; i < argc; ++i)
	{
	  FILE *fp = fopen (argv[i], "rb");
	  if (!fp) check_die ();
	  zip_writer_add (zw, fp, argv[i]);
	}
      zip_writer_close (zw);
    }
  else if (0  == strcmp ("r", argv[1]))
    {
      const int BUFSIZE=256;
      char buf[BUFSIZE];
      int i;
      struct zip_reader *zr;
      char *error = zip_reader_create (argv[2], &zr);
      if (error)
	{
	  fprintf (stderr, "Could not create zip reader: %s\n", error);
	  check_die ();
	}
      for (i = 3; i < argc; ++i)
	{
	  int x = 0;
	  FILE *fp = fopen (argv[i], "wb");
	  if (NULL == fp)
	    {
	      int e = errno;
	      fprintf (stderr, "Could not create file %s: %s\n", argv[i], strerror(e));
	      check_die ();
	    }

	  struct zip_member *zm ;
	  char *error = zip_member_open (zr, argv[i], &zm);
	  if (error)
	    {
	      fprintf (stderr, "Could not open zip member %s from archive: %s\n",
		       argv[i], error);
	      check_die ();
	    }

	  while ((x = zip_member_read (zm, buf, BUFSIZE)) > 0)
	    {
	      fwrite (buf, x, 1, fp);
	    }
          error = zip_member_steal_error (zm);
          zip_member_finish (zm);
	  fclose (fp);

          assert ((error != NULL) == (x < 0));
	  if (x < 0)
	    {
	      fprintf (stderr, "Unzip failed: %s\n", error);
	      check_die ();
	    }
	}
      zip_reader_unref (zr);
    }
  else
    exit (1);

  return 0;
}
