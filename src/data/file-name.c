/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "data/file-name.h"
#include "data/file-handle-def.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <unistd.h>

#include "data/settings.h"
#include "libpspp/hash-functions.h"
#include "libpspp/message.h"
#include "libpspp/i18n.h"
#include "libpspp/str.h"
#include "libpspp/version.h"

#include "gl/dirname.h"
#include "gl/dosname.h"
#include "gl/intprops.h"
#include "gl/minmax.h"
#include "gl/relocatable.h"
#include "gl/xalloc.h"
#include "gl/xmalloca.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


/* Functions for performing operations on file names. */


/* Returns the extension part of FILE_NAME as a malloc()'d string.
   If FILE_NAME does not have an extension, returns an empty
   string. */
char *
fn_extension (const struct file_handle *fh)
{
  const char *file_name = fh_get_file_name (fh);

  const char *extension = strrchr (file_name, '.');
  if (extension == NULL)
    extension = "";
  return xstrdup (extension);
}

/* Find out information about files. */

/* Returns true iff NAME specifies an absolute file name. */
static bool
fn_is_absolute (const char *name)
{
  return IS_ABSOLUTE_FILE_NAME (name);
}


/* Searches for a file with name NAME in the directories given in
   PATH, which is terminated by a null pointer.  Returns the full name of the
   first file found, which the caller is responsible for freeing with free(),
   or NULL if none is found. */
char *
fn_search_path (const char *base_name, char **path)
{
  size_t i;

  if (fn_is_absolute (base_name))
    return xstrdup (base_name);

  for (i = 0; path[i] != NULL; i++)
    {
      const char *dir = path[i];
      char *file;

      if (!strcmp (dir, "") || !strcmp (dir, "."))
        file = xstrdup (base_name);
      else if (ISSLASH (dir[strlen (dir) - 1]))
        file = xasprintf ("%s%s", dir, base_name);
      else
        file = xasprintf ("%s/%s", dir, base_name);

      struct stat temp;
      if (( (stat (file, &temp) == 0 ) && ( ! S_ISDIR (temp.st_mode) )))
	return file;
      
      free (file);
    }

  return NULL;
}


/* Returns true if file with name NAME exists, and that file is not a
   directory */
bool
fn_exists (const struct file_handle *fh)
{
  const char *name = fh_get_file_name (fh);
  struct stat temp;
  if ( stat (name, &temp) != 0 )
    return false;

  return ! S_ISDIR (temp.st_mode);
}


/* Basic file handling. */

#if HAVE_POPEN
/* Used for giving an error message on a set_safer security
   violation. */
static FILE *
safety_violation (const char *fn)
{
  msg (SE, _("Not opening pipe file `%s' because %s option set."), fn, "SAFER");
  errno = EPERM;
  return NULL;
}
#endif

/* File open routine that understands `-' as stdin/stdout and `|cmd'
   as a pipe to command `cmd'.  Returns resultant FILE on success,
   NULL on failure.  If NULL is returned then errno is set to a
   sensible value.  */
FILE *
fn_open (const struct file_handle *fh, const char *mode)
{
  const char *fn = fh_get_file_name (fh);

  assert (mode[0] == 'r' || mode[0] == 'w' || mode[0] == 'a');

  if (mode[0] == 'r')
    {
      if (!strcmp (fn, "stdin") || !strcmp (fn, "-"))
        return stdin;
    }
  else
    {
      if (!strcmp (fn, "stdout") || !strcmp (fn, "-"))
        return stdout;
      if (!strcmp (fn, "stderr"))
        return stderr;
    }

#if HAVE_POPEN
  if (fn[0] == '|')
    {
      if (settings_get_safer_mode ())
	return safety_violation (fn);

      return popen (&fn[1], mode[0] == 'r' ? "r" : "w");
    }
  else if (*fn && fn[strlen (fn) - 1] == '|')
    {
      char *s;
      FILE *f;

      if (settings_get_safer_mode ())
	return safety_violation (fn);

      s = xmalloca (strlen (fn));
      memcpy (s, fn, strlen (fn) - 1);
      s[strlen (fn) - 1] = 0;

      f = popen (s, mode[0] == 'r' ? "r" : "w");

      freea (s);

      return f;
    }
  else
#endif

#if WIN32
    {
      wchar_t *ss = convert_to_filename_encoding (fn, strlen (fn), fh_get_file_name_encoding (fh));
      wchar_t *m =  (wchar_t *) recode_string ("UTF-16LE", "ASCII", mode, strlen (mode));
      FILE *fp = _wfopen (ss, m);
      free (m);
      free (ss);
      return fp;
    }
#else    
    return fopen (fn, mode);
#endif    
}

/* Counterpart to fn_open that closes file F with name FN; returns 0
   on success, EOF on failure.  If EOF is returned, errno is set to a
   sensible value. */
int
fn_close (const struct file_handle *fh, FILE *f)
{
  const char *fn = fh_get_file_name (fh);
  if (fileno (f) == STDIN_FILENO
      || fileno (f) == STDOUT_FILENO
      || fileno (f) == STDERR_FILENO)
    return 0;
#if HAVE_POPEN
  else if (fn[0] == '|' || (*fn && fn[strlen (fn) - 1] == '|'))
    {
      pclose (f);
      return 0;
    }
#endif
  else
    return fclose (f);
}


#ifdef WIN32

/* Apparently windoze users like to see output dumped into their home directory,
   not the current directory (!) */
const char *
default_output_path (void)
{
  static char *path = NULL;

  if ( path == NULL)
    {
      /* Windows NT defines HOMEDRIVE and HOMEPATH.  But give preference
	 to HOME, because the user can change HOME.  */
      const char *home_dir = getenv ("HOME");
      int i;

      if (home_dir == NULL)
	{
	  const char *home_drive = getenv ("HOMEDRIVE");
	  const char *home_path = getenv ("HOMEPATH");

	  if (home_drive != NULL && home_path != NULL)
	    home_dir = xasprintf ("%s%s",
				  home_drive, home_path);
	}

      if (home_dir == NULL)
	home_dir = "c:/users/default"; /* poor default */

      /* Copy home_dir into path.  Add a slash at the end but
         only if there isn't already one there, because Windows
         treats // specially. */
      if (home_dir[0] == '\0'
          || strchr ("/\\", home_dir[strlen (home_dir) - 1]) == NULL)
        path = xasprintf ("%s%c", home_dir, '/');
      else
        path = xstrdup (home_dir);

      for(i = 0; i < strlen (path); i++)
	if (path[i] == '\\') path[i] = '/';
    }

  return path;
}

#else

/* ... whereas the rest of the world just likes it to be
   put "here" for easy access. */
const char *
default_output_path (void)
{
  static char current_dir[]  = "";

  return current_dir;
}

#endif

