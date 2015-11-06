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

#if defined _WIN32 || defined __WIN32__
#define WIN32_LEAN_AND_MEAN  /* avoid including junk */
#include <windows.h>
#endif

/* Functions for performing operations on file names. */

/* Searches for a configuration file with name NAME in the directories given in
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

      if (fn_exists (file))
        return file;
      free (file);
    }

  return NULL;
}

/* Returns the extension part of FILE_NAME as a malloc()'d string.
   If FILE_NAME does not have an extension, returns an empty
   string. */
char *
fn_extension (const char *file_name)
{
  const char *extension = strrchr (file_name, '.');
  if (extension == NULL)
    extension = "";
  return xstrdup (extension);
}

/* Find out information about files. */

/* Returns true iff NAME specifies an absolute file name. */
bool
fn_is_absolute (const char *name)
{
  return IS_ABSOLUTE_FILE_NAME (name);
}

/* Returns true if file with name NAME exists, and that file is not a
   directory */
bool
fn_exists (const char *name)
{
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
fn_open (const char *fn, const char *mode)
{
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
    return fopen (fn, mode);
}

/* Counterpart to fn_open that closes file F with name FN; returns 0
   on success, EOF on failure.  If EOF is returned, errno is set to a
   sensible value. */
int
fn_close (const char *fn, FILE *f)
{
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


/* A file's identity:

   - For a file that exists, this is its device and inode.

   - For a file that does not exist, but which has a directory
     name that exists, this is the device and inode of the
     directory, plus the file's base name.

   - For a file that does not exist and has a nonexistent
     directory, this is the file name.

   Windows doesn't have inode numbers, so we just use the name
   there. */
struct file_identity
{
  unsigned long long device;               /* Device number. */
  unsigned long long inode;                /* Inode number. */
  char *name;                 /* File name, where needed, otherwise NULL. */
};

/* Returns a pointer to a dynamically allocated structure whose
   value can be used to tell whether two files are actually the
   same file.  Returns a null pointer if no information about the
   file is available, perhaps because it does not exist.  The
   caller is responsible for freeing the structure with
   fn_free_identity() when finished. */
struct file_identity *
fn_get_identity (const char *file_name)
{
  struct file_identity *identity = xmalloc (sizeof *identity);

#if !(defined _WIN32 || defined __WIN32__)
  struct stat s;
  if (lstat (file_name, &s) == 0)
    {
      identity->device = s.st_dev;
      identity->inode = s.st_ino;
      identity->name = NULL;
    }
  else
    {
      char *dir = dir_name (file_name);
      if (last_component (file_name) != NULL && stat (dir, &s) == 0)
        {
          identity->device = s.st_dev;
          identity->inode = s.st_ino;
          identity->name = base_name (file_name);
        }
      else
        {
          identity->device = 0;
          identity->inode = 0;
          identity->name = xstrdup (file_name);
        }
      free (dir);
    }
#else /* Windows */
  bool ok = false;
  HANDLE h = CreateFile (file_name, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL);
  if (h != INVALID_HANDLE_VALUE)
  {
    BY_HANDLE_FILE_INFORMATION fi;
    ok = GetFileInformationByHandle (h, &fi);
    if (ok)
      {
	identity->device = fi.dwVolumeSerialNumber;
	identity->inode = fi.nFileIndexHigh;
	identity->inode <<= (sizeof fi.nFileIndexLow) * CHAR_BIT;
	identity->inode |= fi.nFileIndexLow;
	identity->name = 0;
      }
    CloseHandle (h);
  }

  if (!ok)
    {
      identity->device = 0;
      identity->inode = 0;

      size_t bufsize;
      size_t pathlen = 255;
      char *cname = NULL;
      do 
      {
	bufsize = pathlen;
	cname = xrealloc (cname, bufsize);
	pathlen = GetFullPathName (file_name, bufsize, cname, NULL);
      }
      while (pathlen > bufsize);
      identity->name = xstrdup (cname);
      free (cname);
      str_lowercase (identity->name);
    }
#endif /* Windows */

  return identity;
}

/* Frees IDENTITY obtained from fn_get_identity(). */
void
fn_free_identity (struct file_identity *identity)
{
  if (identity != NULL)
    {
      free (identity->name);
      free (identity);
    }
}

/* Compares A and B, returning a strcmp()-type result. */
int
fn_compare_file_identities (const struct file_identity *a,
                            const struct file_identity *b)
{
  if (a->device != b->device)
    return a->device < b->device ? -1 : 1;
  else if (a->inode != b->inode)
    return a->inode < b->inode ? -1 : 1;
  else if (a->name != NULL)
    return b->name != NULL ? strcmp (a->name, b->name) : 1;
  else
    return b->name != NULL ? -1 : 0;
}

/* Returns a hash value for IDENTITY. */
unsigned int
fn_hash_identity (const struct file_identity *identity)
{
  unsigned int hash = hash_int (identity->device, identity->inode);
  if (identity->name != NULL)
    hash = hash_string (identity->name, hash);
  return hash;
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

