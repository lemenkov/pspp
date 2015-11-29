/* PSPP - a program for statistical analysis.
   Copyright (C) 2004, 2010, 2015 Free Software Foundation, Inc.

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

#include "data/make-file.h"
#include "libpspp/i18n.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "data/file-name.h"
#include "data/file-handle-def.h"
#include "libpspp/ll.h"
#include "libpspp/message.h"

#include "gl/fatal-signal.h"
#include "gl/tempname.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"
#include "gl/localcharset.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)


#if defined _WIN32 || defined __WIN32__
#define WIN32_LEAN_AND_MEAN  /* avoid including junk */
#include <windows.h>
#define TS_stat _stat
#define Tunlink _wunlink
#define Topen _wopen
#define Tstat _wstat

/* Shamelessly lifted and modified from the rpl_rename function in Gnulib */
static int
Trename (TCHAR const *src, TCHAR const *dst)
{
  int error;

  /* MoveFileExW works if SRC is a directory without any flags, but
     fails with MOVEFILE_REPLACE_EXISTING, so try without flags first.
     Thankfully, MoveFileExW handles hard links correctly, even though
     rename() does not.  */
  if (MoveFileExW (src, dst, 0))
    return 0;

  /* Retry with MOVEFILE_REPLACE_EXISTING if the move failed
     due to the destination already existing.  */
  error = GetLastError ();
  if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
    {
      if (MoveFileExW (src, dst, MOVEFILE_REPLACE_EXISTING))
        return 0;

      error = GetLastError ();
    }

  switch (error)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_BAD_PATHNAME:
    case ERROR_DIRECTORY:
      errno = ENOENT;
      break;

    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
      errno = EACCES;
      break;

    case ERROR_OUTOFMEMORY:
      errno = ENOMEM;
      break;

    case ERROR_CURRENT_DIRECTORY:
      errno = EBUSY;
      break;

    case ERROR_NOT_SAME_DEVICE:
      errno = EXDEV;
      break;

    case ERROR_WRITE_PROTECT:
      errno = EROFS;
      break;

    case ERROR_WRITE_FAULT:
    case ERROR_READ_FAULT:
    case ERROR_GEN_FAILURE:
      errno = EIO;
      break;

    case ERROR_HANDLE_DISK_FULL:
    case ERROR_DISK_FULL:
    case ERROR_DISK_TOO_FRAGMENTED:
      errno = ENOSPC;
      break;

    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
      errno = EEXIST;
      break;

    case ERROR_BUFFER_OVERFLOW:
    case ERROR_FILENAME_EXCED_RANGE:
      errno = ENAMETOOLONG;
      break;

    case ERROR_INVALID_NAME:
    case ERROR_DELETE_PENDING:
      errno = EPERM;        /* ? */
      break;

# ifndef ERROR_FILE_TOO_LARGE
/* This value is documented but not defined in all versions of windows.h.  */
#  define ERROR_FILE_TOO_LARGE 223
# endif
    case ERROR_FILE_TOO_LARGE:
      errno = EFBIG;
      break;

    default:
      errno = EINVAL;
      break;
    }

  return -1;
}

TCHAR * 
convert_to_filename_encoding (const char *s, size_t len, const char *current_encoding)
{
  const char *enc = current_encoding;
  if (NULL == enc || 0 == strcmp (enc, "Auto"))
    enc = locale_charset ();

  return (TCHAR *) recode_string ("UTF-16LE", enc, s, len);
}


#else
#define TS_stat stat
#define Trename rename
#define Tunlink unlink
#define Topen open
#define Tstat stat

TCHAR * 
convert_to_filename_encoding (const char *s, size_t len UNUSED, const char *current_encoding UNUSED)
{
  /* Non-windows systems don't care about the encoding.  
     The string is copied here, to be consistent with the w32 case.  */
  return xstrdup (s);
}

#endif


struct replace_file
{
  struct ll ll;
  TCHAR *file_name;
  TCHAR *tmp_name;

  char *tmp_name_verbatim;
  const char *file_name_verbatim;
};
 
static struct ll_list all_files = LL_INITIALIZER (all_files);

static void free_replace_file (struct replace_file *);
static void unlink_replace_files (void);

struct replace_file *
replace_file_start (const struct file_handle *fh, const char *mode,
                    mode_t permissions, FILE **fp)
{
  static bool registered;
  struct TS_stat s;
  struct replace_file *rf;
  int fd;
  int saved_errno = errno;

  const char *file_name = fh_get_file_name (fh);

  TCHAR * Tfile_name = convert_to_filename_encoding (file_name, strlen (file_name), fh_get_file_name_encoding (fh));

  /* If FILE_NAME represents a special file, write to it directly
     instead of trying to replace it. */
  if (Tstat (Tfile_name, &s) == 0 && !S_ISREG (s.st_mode))
    {
      /* Open file descriptor. */
      fd = Topen (Tfile_name, O_WRONLY);
      if (fd < 0)
        {
	  saved_errno = errno;     
          msg (ME, _("Opening %s for writing: %s."),
               file_name, strerror (saved_errno));
	  free (Tfile_name);
          return NULL;
        }

      /* Open file as stream. */
      *fp = fdopen (fd, mode);
      if (*fp == NULL)
        {
	  saved_errno = errno;     
	  msg (ME, _("Opening stream for %s: %s."),
               file_name, strerror (saved_errno));
          close (fd);
	  free (Tfile_name);
          return NULL;
        }

      rf = xzalloc (sizeof *rf);
      rf->file_name = NULL;
      rf->tmp_name = Tfile_name;
      return rf;
    }

  if (!registered)
    {
      at_fatal_signal (unlink_replace_files);
      registered = true;
    }
  block_fatal_signals ();

  rf = xzalloc (sizeof *rf);
  rf->file_name = Tfile_name;
  rf->file_name_verbatim = file_name;

  for (;;)
    {
      /* Generate unique temporary file name. */
      free (rf->tmp_name_verbatim);
      rf->tmp_name_verbatim = xasprintf ("%stmpXXXXXX", file_name);
      if (gen_tempname (rf->tmp_name_verbatim, 0, 0600, GT_NOCREATE) < 0)
        {
	  saved_errno = errno;
          msg (ME, _("Creating temporary file to replace %s: %s."),
               file_name, strerror (saved_errno));
          goto error;
        }

      rf->tmp_name = convert_to_filename_encoding (rf->tmp_name_verbatim, strlen (rf->tmp_name_verbatim), fh_get_file_name_encoding (fh));

      /* Create file by that name. */
      fd = Topen (rf->tmp_name, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, permissions);
      if (fd >= 0)
        break;
      if (errno != EEXIST)
        {
	  saved_errno = errno;
          msg (ME, _("Creating temporary file %s: %s."),
               rf->tmp_name_verbatim, strerror (saved_errno));
          goto error;
        }
    }


  /* Open file as stream. */
  *fp = fdopen (fd, mode);
  if (*fp == NULL)
    {
      saved_errno = errno;
      msg (ME, _("Opening stream for temporary file %s: %s."),
           rf->tmp_name_verbatim, strerror (saved_errno));
      close (fd);
      Tunlink (rf->tmp_name);
      goto error;
    }

  /* Register file for deletion. */
  ll_push_head (&all_files, &rf->ll);
  unblock_fatal_signals ();

  return rf;

 error:
  unblock_fatal_signals ();
  free_replace_file (rf);
  *fp = NULL;
  errno = saved_errno;
  return NULL;
}

bool
replace_file_commit (struct replace_file *rf)
{
  bool ok = true;

  if (rf->file_name != NULL)
    {
      int save_errno;

      block_fatal_signals ();
      ok = Trename (rf->tmp_name, rf->file_name) == 0;
      save_errno = errno;
      ll_remove (&rf->ll);
      unblock_fatal_signals ();

      if (!ok)
        msg (ME, _("Replacing %s by %s: %s."),
             rf->file_name_verbatim, rf->tmp_name_verbatim, strerror (save_errno));
    }
  else
    {
      /* Special file: no temporary file to rename. */
    }
  free_replace_file (rf);

  return ok;
}

bool
replace_file_abort (struct replace_file *rf)
{
  bool ok = true;

  if (rf->file_name != NULL)
    {
      int save_errno;

      block_fatal_signals ();
      ok = Tunlink (rf->tmp_name) == 0;
      save_errno = errno;
      ll_remove (&rf->ll);
      unblock_fatal_signals ();

      if (!ok)
        msg (ME, _("Removing %s: %s."), rf->tmp_name_verbatim, strerror (save_errno));
    }
  else
    {
      /* Special file: no temporary file to unlink. */
    }
  free_replace_file (rf);

  return ok;
}

static void
free_replace_file (struct replace_file *rf)
{
  free (rf->file_name);
  free (rf->tmp_name);
  free (rf->tmp_name_verbatim);
  free (rf);
}

static void
unlink_replace_files (void)
{
  struct replace_file *rf;

  block_fatal_signals ();
  ll_for_each (rf, struct replace_file, ll, &all_files)
    {
      /* We don't free_replace_file(RF) because calling free is unsafe
         from an asynchronous signal handler. */
      Tunlink (rf->tmp_name);
    }
  unblock_fatal_signals ();
}
