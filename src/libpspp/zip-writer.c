/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2012, 2014 Free Software Foundation, Inc.

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

#include "libpspp/zip-writer.h"
#include "libpspp/zip-private.h"

#include <assert.h>
#include <byteswap.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "gl/crc.h"
#include "gl/fwriteerror.h"
#include "gl/xalloc.h"

#include "libpspp/message.h"
#include "libpspp/temp-file.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct zip_writer
  {
    char *file_name;            /* File name, for use in error mesages. */
    FILE *file;                 /* Output stream. */
    uint32_t offset;            /* Offset in output stream. */

    uint16_t date, time;        /* Date and time in MS-DOS format. */

    bool ok;

    /* Member being added to the file. */
    char *m_name;
    uint32_t m_start;
    uint32_t m_size;
    uint32_t m_crc;

    /* Members already added to the file, so that we can summarize them to the
       central directory at the end of the ZIP file. */
    struct zip_member *members;
    size_t n_members, allocated_members;
  };

struct zip_member
  {
    uint32_t offset;            /* Starting offset in file. */
    uint32_t size;              /* Length of member file data, in bytes. */
    uint32_t crc;               /* CRC-32 of member file data.. */
    char *name;                 /* Name of member file. */
  };

static void
put_bytes (struct zip_writer *zw, const void *p, size_t n)
{
  fwrite (p, 1, n, zw->file);
  zw->offset += n;
}

static void
put_u16 (struct zip_writer *zw, uint16_t x)
{
#ifdef WORDS_BIGENDIAN
  x = bswap_16 (x);
#endif
  put_bytes (zw, &x, sizeof x);
}

static void
put_u32 (struct zip_writer *zw, uint32_t x)
{
#ifdef WORDS_BIGENDIAN
  x = bswap_32 (x);
#endif
  put_bytes (zw, &x, sizeof x);
}

/* Starts writing a new ZIP file named FILE_NAME.  Returns a new zip_writer if
   successful, otherwise a null pointer. */
struct zip_writer *
zip_writer_create (const char *file_name)
{
  FILE *file;
  if (strcmp (file_name, "-"))
    {
      file = fopen (file_name, "wb");
      if (file == NULL)
        {
          msg_error (errno, _("%s: error opening output file"), file_name);
          return NULL;
        }
    }
  else
    {
      if (isatty (STDOUT_FILENO))
        {
          msg (ME, _("%s: not writing ZIP file to terminal"), file_name);
          return NULL;
        }

      file = stdout;
    }

  time_t now = time (NULL);
  struct tm *tm = localtime (&now);

  struct zip_writer *zw = xmalloc (sizeof *zw);
  *zw = (struct zip_writer) {
    .file_name = xstrdup (file_name),
    .file = file,
    .ok = true,
    .date = tm->tm_mday + ((tm->tm_mon + 1) << 5) + ((tm->tm_year - 80) << 9),
    .time = tm->tm_sec / 2 + (tm->tm_min << 5) + (tm->tm_hour << 11),
  };
  return zw;
}

static void
put_local_header (struct zip_writer *zw, const char *member_name, uint32_t crc,
                  uint32_t size, int flag)
{
  put_u32 (zw, MAGIC_LHDR);     /* local file header signature */
  put_u16 (zw, 10);             /* version needed to extract */
  put_u16 (zw, flag);           /* general purpose bit flag */
  put_u16 (zw, 0);              /* compression method */
  put_u16 (zw, zw->time);       /* last mod file time */
  put_u16 (zw, zw->date);       /* last mod file date */
  put_u32 (zw, crc);            /* crc-32 */
  put_u32 (zw, size);           /* compressed size */
  put_u32 (zw, size);           /* uncompressed size */
  put_u16 (zw, strlen (member_name)); /* file name length */
  put_u16 (zw, 0);                    /* extra field length */
  put_bytes (zw, member_name, strlen (member_name));
}

/* Start adding a new member, named MEMBER_NAME, to ZW.  Add content to the
   member by calling zip_writer_add_write() possibly multiple times, then
   finish it off with zip_writer_add_finish().

   Only one member may be open at a time. */
void
zip_writer_add_start (struct zip_writer *zw, const char *member_name)
{
  assert (!zw->m_name);
  zw->m_name = xstrdup (member_name);
  zw->m_start = zw->offset;
  zw->m_size = 0;
  zw->m_crc = 0;
  put_local_header (zw, member_name, 0, 0, 1 << 3);
}

/* Adds the N bytes in BUF to the currently open member in ZW. */
void
zip_writer_add_write (struct zip_writer *zw, const void *buf, size_t n)
{
  assert (zw->m_name);
  put_bytes (zw, buf, n);
  zw->m_size += n;
  zw->m_crc = crc32_update (zw->m_crc, buf, n);
}

/* Finishes writing the currently open member in ZW. */
void
zip_writer_add_finish (struct zip_writer *zw)
{
  assert (zw->m_name);

  /* Try to seek back to the local file header.  If successful, overwrite it
     with the correct file size and CRC.  Otherwise, write data descriptor. */
  if (fseeko (zw->file, zw->m_start, SEEK_SET) == 0)
    {
      uint32_t save_offset = zw->offset;
      put_local_header (zw, zw->m_name, zw->m_crc, zw->m_size, 0);
      if (fseeko (zw->file, zw->m_size, SEEK_CUR)
          && zw->ok)
        {
          msg_error (errno, _("%s: error seeking in output file"), zw->file_name);
          zw->ok = false;
        }
      zw->offset = save_offset;
    }
  else
    {
      put_u32 (zw, MAGIC_DDHD);
      put_u32 (zw, zw->m_crc);
      put_u32 (zw, zw->m_size);
      put_u32 (zw, zw->m_size);
    }

  /* Add to set of members. */
  if (zw->n_members >= zw->allocated_members)
    zw->members = x2nrealloc (zw->members, &zw->allocated_members,
                              sizeof *zw->members);
  struct zip_member *member = &zw->members[zw->n_members++];
  member->offset = zw->m_start;
  member->size = zw->m_size;
  member->crc = zw->m_crc;
  member->name = zw->m_name;

  zw->m_name = NULL;
  zw->m_start = zw->m_size = zw->m_crc = 0;
}

/* Adds the contents of FILE, with name MEMBER_NAME, to ZW. */
void
zip_writer_add (struct zip_writer *zw, FILE *file, const char *member_name)
{
  zip_writer_add_start (zw, member_name);

  fseeko (file, 0, SEEK_SET);
  for (;;)
    {
      char buf[4096];
      size_t n = fread (buf, 1, sizeof buf, file);
      if (!n)
        break;
      zip_writer_add_write (zw, buf, n);
    }
  zip_writer_add_finish (zw);
}

/* Adds a member named MEMBER_NAME whose contents is the null-terminated string
   CONTENT. */
void
zip_writer_add_string (struct zip_writer *zw, const char *member_name,
                       const char *content)
{
  zip_writer_add_memory (zw, member_name, content, strlen (content));
}

/* Adds a member named MEMBER_NAME whose contents is the SIZE bytes of
   CONTENT. */
void
zip_writer_add_memory (struct zip_writer *zw, const char *member_name,
                       const void *content, size_t size)
{
  zip_writer_add_start (zw, member_name);
  zip_writer_add_write (zw, content, size);
  zip_writer_add_finish (zw);
}

/* Finalizes the contents of ZW and closes it.  Returns true if successful,
   false if a write error occurred while finalizing the file or at any earlier
   time. */
bool
zip_writer_close (struct zip_writer *zw)
{
  uint32_t dir_start, dir_end;
  size_t i;
  bool ok;

  if (zw == NULL)
    return true;

  assert (!zw->m_name);

  dir_start = zw->offset;
  for (i = 0; i < zw->n_members; i++)
    {
      struct zip_member *m = &zw->members[i];

      /* Central directory file header. */
      put_u32 (zw, MAGIC_SOCD);       /* central file header signature */
      put_u16 (zw, 63);               /* version made by */
      put_u16 (zw, 10);               /* version needed to extract */
      put_u16 (zw, 1 << 3);           /* general purpose bit flag */
      put_u16 (zw, 0);                /* compression method */
      put_u16 (zw, zw->time);         /* last mod file time */
      put_u16 (zw, zw->date);         /* last mod file date */
      put_u32 (zw, m->crc);           /* crc-32 */
      put_u32 (zw, m->size);          /* compressed size */
      put_u32 (zw, m->size);          /* uncompressed size */
      put_u16 (zw, strlen (m->name)); /* file name length */
      put_u16 (zw, 0);                /* extra field length */
      put_u16 (zw, 0);                /* file comment length */
      put_u16 (zw, 0);                /* disk number start */
      put_u16 (zw, 0);                /* internal file attributes */
      put_u32 (zw, 0);                /* external file attributes */
      put_u32 (zw, m->offset);        /* relative offset of local header */
      put_bytes (zw, m->name, strlen (m->name));
      free (m->name);
    }
  free (zw->members);
  dir_end = zw->offset;

  /* End of central directory record. */
  put_u32 (zw, MAGIC_EOCD);     /* end of central dir signature */
  put_u16 (zw, 0);              /* number of this disk */
  put_u16 (zw, 0);              /* number of the disk with the
                                   start of the central directory */
  put_u16 (zw, zw->n_members);  /* total number of entries in the
                                   central directory on this disk */
  put_u16 (zw, zw->n_members);  /* total number of entries in
                                   the central directory */
  put_u32 (zw, dir_end - dir_start); /* size of the central directory */
  put_u32 (zw, dir_start);      /* offset of start of central
                                   directory with respect to
                                   the starting disk number */
  put_u16 (zw, 0);              /* .ZIP file comment length */

  ok = zw->ok;
  if (ok && zw->file != stdout && fwriteerror (zw->file))
    {
      msg_error (errno, _("%s: write failed"), zw->file_name);
      ok = false;
    }

  free (zw->file_name);
  free (zw);

  return ok;
}
