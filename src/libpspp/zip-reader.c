/* PSPP - a program for statistical analysis.
   Copyright (C) 2011, 2013, 2014 Free Software Foundation, Inc.

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

#include "libpspp/zip-reader.h"
#include "libpspp/zip-private.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/integer-format.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

struct zip_member
{
  char *file_name;             /* File name. */
  char *member_name;          /* Member name. */
  FILE *fp;                   /* The stream from which the data is read */
  uint32_t offset;            /* Starting offset in file. */
  uint32_t comp_size;         /* Length of member file data, in bytes. */
  uint32_t ucomp_size;        /* Uncompressed length of member file data, in bytes. */
  const struct decompressor *decompressor;

  size_t bytes_unread;       /* Number of bytes left in the member available for reading */
  char *error;               /* Error message, if any. */
  void *aux;
};

struct decompressor
{
  char *(*init) (struct zip_member *);
  int  (*read) (struct zip_member *, void *, size_t);
  void (*finish) (struct zip_member *);
};
static const struct decompressor stored_decompressor;
static const struct decompressor inflate_decompressor;

static bool find_eocd (FILE *fp, off_t *off);

static const struct decompressor *
get_decompressor (uint16_t c)
{
  switch (c)
    {
    case 0:
      return &stored_decompressor;

    case 8:
      return &inflate_decompressor;

    default:
      return NULL;
    }
}

struct zip_reader
{
  int ref_cnt;
  char *file_name;                  /* The name of the file from which the data is read */
  uint16_t n_entries;              /* Number of directory entries. */
  struct zip_entry *entries;       /* Directory entries. */
};

struct zip_entry
{
  uint32_t offset;            /* Starting offset in file. */
  uint32_t comp_size;         /* Length of member file data, in bytes. */
  uint32_t ucomp_size;        /* Uncompressed length of member file data, in bytes. */
  char *name;                 /* Name of member file. */
};

char * WARN_UNUSED_RESULT
zip_member_steal_error (struct zip_member *zm)
{
  char *retval = zm->error;
  zm->error = NULL;
  return retval;
}

void
zip_member_finish (struct zip_member *zm)
{
  if (zm)
    {
      free (zm->file_name);
      free (zm->member_name);
      zm->decompressor->finish (zm);
      fclose (zm->fp);
      free (zm->error);
      free (zm);
    }
}

struct zip_reader *
zip_reader_ref (const struct zip_reader *zr_)
{
  struct zip_reader *zr = CONST_CAST (struct zip_reader *, zr_);
  assert (zr->ref_cnt > 0);
  zr->ref_cnt++;
  return zr;
}

/* Destroy the zip reader */
void
zip_reader_unref (struct zip_reader *zr)
{
  if (zr == NULL)
    return;
  assert (zr->ref_cnt > 0);
  if (--zr->ref_cnt)
    return;

  free (zr->file_name);

  for (int i = 0; i < zr->n_entries; ++i)
    {
      struct zip_entry *ze = &zr->entries[i];
      free (ze->name);
    }
  free (zr->entries);
  free (zr);
}


/* Skip N bytes in F */
static void
skip_bytes (FILE *f, size_t n)
{
  fseeko (f, n, SEEK_CUR);
}

static void
get_bytes (FILE *f, void *x, size_t n)
{
  if (!fread (x, n, 1, f))
    memset (x, 0, n);
}

static uint32_t
get_u32 (FILE *f)
{
  uint32_t x;
  get_bytes (f, &x, sizeof x);
  return le_to_native32 (x);
}

static uint16_t
get_u16 (FILE *f)
{
  uint16_t x;
  get_bytes (f, &x, sizeof x);
  return le_to_native16 (x);
}

static char * WARN_UNUSED_RESULT
get_stream_error (FILE *f, const char *file_name)
{
  if (feof (f))
    return xasprintf (_("%s: unexpected end of file"), file_name);
  else if (ferror (f))
    {
      /* The particular error might not be in errno anymore.  Try to find out
         what the error was. */
      errno = 0;
      char x;
      return (!fread (&x, 1, sizeof x, f) && errno
              ? xasprintf (_("%s: I/O error reading Zip archive (%s)"),
                           file_name, strerror (errno))
              : xasprintf (_("%s: I/O error reading Zip archive"), file_name));
    }
  else
    return NULL;
}

/* Read 32 bit integer and compare it with EXPECTED.
   place an error string in ERR if necessary. */
static char * WARN_UNUSED_RESULT
check_magic (FILE *f, const char *file_name, uint32_t expected)
{
  uint32_t magic = get_u32 (f);
  char *error = get_stream_error (f, file_name);
  if (error)
    return error;
  else if (expected != magic)
    return xasprintf (_("%s: corrupt archive at 0x%llx: "
                        "expected %#"PRIx32" but got %#"PRIx32),
                      file_name,
                      (long long int) ftello (f) - sizeof (uint32_t),
                      expected, magic);
  else
    return NULL;
}


/* Reads upto BYTES bytes from ZM and puts them in BUF.
   Returns the number of bytes read, or -1 on error */
int
zip_member_read (struct zip_member *zm, void *buf, size_t bytes)
{
  if (bytes > zm->bytes_unread)
    bytes = zm->bytes_unread;
  if (!bytes)
    return 0;

  int bytes_read = zm->decompressor->read (zm, buf, bytes);
  if (bytes_read < 0)
    return bytes_read;

  zm->bytes_unread -= bytes_read;

  return bytes_read;
}

/* Read all of ZM into memory, storing the data in *DATAP and its size in *NP.
   Returns NULL if successful, otherwise an error string that the caller
   must eventually free(). */
char * WARN_UNUSED_RESULT
zip_member_read_all (struct zip_reader *zr, const char *member_name,
                     void **datap, size_t *np)
{
  struct zip_member *zm;
  char *error = zip_member_open (zr, member_name, &zm);
  if (error)
    {
      *datap = NULL;
      *np = 0;
      return error;
    }

  *datap = xmalloc (zm->ucomp_size);
  *np = zm->ucomp_size;

  uint8_t *data = *datap;
  while (zm->bytes_unread)
    if (zip_member_read (zm, data + (zm->ucomp_size - zm->bytes_unread),
                         zm->bytes_unread) == -1)
      {
        char *error = zip_member_steal_error (zm);
        zip_member_finish (zm);
        free (*datap);
        *datap = NULL;
        *np = 0;
        return error;
      }

  zip_member_finish (zm);
  return NULL;
}

/* Read a central directory header from FILE and initializes ZE with it.
   Returns true if successful, false otherwise.  On error, appends error
   messages to ERRS. */
static char * WARN_UNUSED_RESULT
zip_header_read_next (FILE *file, const char *file_name,
                      struct zip_entry *ze)
{
  char *error = check_magic (file, file_name, MAGIC_SOCD);
  if (error)
    return error;

  get_u16 (file);       /* v */
  get_u16 (file);       /* v */
  get_u16 (file);       /* gp */
  get_u16 (file);       /* comp_type */
  get_u16 (file);       /* time */
  get_u16 (file);       /* date */
  get_u32 (file);       /* expected_crc */
  ze->comp_size = get_u32 (file);
  ze->ucomp_size = get_u32 (file);
  uint16_t nlen = get_u16 (file);
  uint16_t extralen = get_u16 (file);
  get_u16 (file);       /* clen */
  get_u16 (file);       /* diskstart */
  get_u16 (file);       /* iattr */
  get_u32 (file);       /* eattr */
  ze->offset = get_u32 (file);

  error = get_stream_error (file, file_name);
  if (error)
    return error;

  ze->name = xzalloc (nlen + 1);
  get_bytes (file, ze->name, nlen);
  error = get_stream_error (file, file_name);
  if (error)
    return error;

  skip_bytes (file, extralen);

  return NULL;
}


/* Create a reader from the zip called FILE_NAME */
char * WARN_UNUSED_RESULT
zip_reader_create (const char *file_name, struct zip_reader **zrp)
{
  *zrp = NULL;

  FILE *file = fopen (file_name, "rb");
  if (!file)
    return xasprintf (_("%s: open failed (%s)"), file_name, strerror (errno));

  /* Check the Zip file magic. */
  char *error = check_magic (file, file_name, MAGIC_LHDR);
  if (error)
    {
      fclose (file);
      return error;
    }

  /* Find end of central directory record and read it. */
  off_t offset = 0;
  if (! find_eocd (file, &offset))
    {
      fclose (file);
      return xasprintf (_("%s: cannot find central directory"), file_name);
    }
  if (0 != fseeko (file, offset, SEEK_SET))
    {
      error = xasprintf (_("%s: seek failed (%s)"),
                         file_name, strerror (errno));
      fclose (file);
      return error;
    }
  error = check_magic (file, file_name, MAGIC_EOCD);
  if (error)
    {
      fclose (file);
      return error;
    }
  get_u16 (file);               /* disknum */
  get_u16 (file);               /* disknum */
  uint16_t n_members = get_u16 (file);
  get_u16 (file);               /* total_members */
  get_u32 (file);               /* central_dir_length */
  uint32_t central_dir_start = get_u32 (file);
  error = get_stream_error (file, file_name);
  if (error)
    {
      fclose (file);
      return error;
    }

  /* Read central directory. */
  if (0 != fseeko (file, central_dir_start, SEEK_SET))
    {
      error = xasprintf (_("%s: seek failed (%s)"),
                         file_name, strerror (errno));
      fclose (file);
      return NULL;
    }

  struct zip_reader *zr = xzalloc (sizeof *zr);
  zr->ref_cnt = 1;
  zr->file_name = xstrdup (file_name);
  zr->entries = xcalloc (n_members, sizeof *zr->entries);
  for (int i = 0; i < n_members; i++)
    {
      error = zip_header_read_next (file, file_name,
                                    &zr->entries[zr->n_entries]);
      if (error)
        {
          fclose (file);
          zip_reader_unref (zr);
          return error;
        }
      zr->n_entries++;
    }

  fclose (file);

  *zrp = zr;
  return NULL;
}

static struct zip_entry *
zip_entry_find (const struct zip_reader *zr, const char *member)
{
  for (int i = 0; i < zr->n_entries; ++i)
    {
      struct zip_entry *ze = &zr->entries[i];
      if (0 == strcmp (ze->name, member))
        return ze;
    }
  return NULL;
}

const char *
zip_reader_get_member_name(const struct zip_reader *zr, size_t idx)
{
  return idx < zr->n_entries ? zr->entries[idx].name : NULL;
}

/* Returns true if ZR contains a member named MEMBER, false otherwise. */
bool
zip_reader_contains_member (const struct zip_reader *zr, const char *member)
{
  return zip_entry_find (zr, member) != NULL;
}

/* Return the member called MEMBER from the reader ZR  */
char * WARN_UNUSED_RESULT
zip_member_open (struct zip_reader *zr, const char *member,
                 struct zip_member **zmp)
{
  *zmp = NULL;

  struct zip_entry *ze = zip_entry_find (zr, member);
  if (ze == NULL)
    return xasprintf (_("%s: unknown member \"%s\""),
                        zr->file_name, member);

  FILE *fp = fopen (zr->file_name, "rb");
  if (!fp)
    return xasprintf ( _("%s: open failed (%s)"),
                       zr->file_name, strerror (errno));

  struct zip_member *zm = xmalloc (sizeof *zm);
  zm->file_name = xstrdup (zr->file_name);
  zm->member_name = xstrdup (member);
  zm->fp = fp;
  zm->offset = ze->offset;
  zm->comp_size = ze->comp_size;
  zm->ucomp_size = ze->ucomp_size;
  zm->decompressor = NULL;
  zm->bytes_unread = ze->ucomp_size;
  zm->aux = NULL;
  zm->error = NULL;

  char *error;
  if (0 != fseeko (zm->fp, zm->offset, SEEK_SET))
    {
      error = xasprintf (_("%s: seek failed (%s)"),
                         ze->name, strerror (errno));
      goto error;
    }

  error = check_magic (zm->fp, zr->file_name, MAGIC_LHDR);
  if (error)
    goto error;

  get_u16 (zm->fp);             /* v */
  get_u16 (zm->fp);             /* gp */
  uint16_t comp_type = get_u16 (zm->fp);
  zm->decompressor = get_decompressor (comp_type);
  if (!zm->decompressor)
    {
      error = xasprintf (_("%s: member \"%s\" has unknown compression "
                           "type %"PRIu16),
                         zr->file_name, zm->member_name, comp_type);
      goto error;
    }
  get_u16 (zm->fp);             /* time */
  get_u16 (zm->fp);             /* date */
  get_u32 (zm->fp);             /* crc */
  get_u32 (zm->fp);             /* comp_size */
  get_u32 (zm->fp);             /* ucomp_size */
  uint16_t nlen = get_u16 (zm->fp);
  uint16_t extra_len = get_u16 (zm->fp);
  error = get_stream_error (zm->fp, zr->file_name);
  if (error)
    goto error;

  char *name = xzalloc (nlen + 1);
  get_bytes (zm->fp, name, nlen);
  error = get_stream_error (zm->fp, zr->file_name);
  if (error)
    goto error;
  if (strcmp (name, ze->name) != 0)
    {
      error = xasprintf (_("%s: name mismatch between central directory (%s) "
                           "and local file header (%s)"),
                         zm->file_name, ze->name, name);
      free (name);
      goto error;
    }
  free (name);

  skip_bytes (zm->fp, extra_len);

  error = zm->decompressor->init (zm);
  if (error)
    goto error;

  *zmp = zm;
  return NULL;

error:
  fclose (zm->fp);
  free (zm->file_name);
  free (zm->member_name);
  free (zm);
  return error;
}



static bool probe_magic (FILE *fp, uint32_t magic, off_t start, off_t stop, off_t *off);


/* Search for something that looks like the End Of Central Directory in FP.
   If found, the offset of the record will be placed in OFF.
   Returns true if found false otherwise.
*/
static bool
find_eocd (FILE *fp, off_t *off)
{
  off_t start, stop;
  const uint32_t magic = MAGIC_EOCD;
  bool found = false;

  /* The magic cannot be more than 22 bytes from the end of the file,
     because that is the minimum length of the EndOfCentralDirectory
     record.
   */
  if (0 > fseeko (fp, -22, SEEK_END))
    {
      return false;
    }
  start = ftello (fp);
  stop = start + sizeof (magic);
  do
    {
      found = probe_magic (fp, magic, start, stop, off);
      /* FIXME: For extra confidence lookup the directory start record here*/
      if (start == 0)
	break;
      stop = start + sizeof (magic);
      start >>= 1;
    }
  while (!found);

  return found;
}


/*
  Search FP for MAGIC starting at START and reaching until STOP.
  Returns true iff MAGIC is found.  False otherwise.
  OFF receives the location of the magic.
*/
static bool
probe_magic (FILE *fp, uint32_t magic, off_t start, off_t stop, off_t *off)
{
  int i;
  int state = 0;
  unsigned char seq[4];
  unsigned char byte;

  if (0 > fseeko (fp, start, SEEK_SET))
    {
      return -1;
    }

  for (i = 0; i < 4 ; ++i)
    {
      seq[i] = (magic >> i * 8) & 0xFF;
    }

  do
    {
      if (1 != fread (&byte, 1, 1, fp))
	break;

      if (byte == seq[state])
	state++;
      else
	state = 0;

      if (state == 4)
	{
	  *off = ftello (fp) - 4;
	  return true;
	}
      start++;
      if (start >= stop)
	break;
    }
  while (!feof (fp));

  return false;
}

/* Null decompressor. */

static int
stored_read (struct zip_member *zm, void *buf, size_t n)
{
  size_t bytes_read = fread (buf, 1, n, zm->fp);
  if (!bytes_read && !zm->error)
    zm->error = get_stream_error (zm->fp, zm->file_name);
  return bytes_read;
}

static char *
stored_init (struct zip_member *zm UNUSED)
{
  return NULL;
}

static void
stored_finish (struct zip_member *zm UNUSED)
{
  /* Nothing required */
}

static const struct decompressor stored_decompressor =
  {stored_init, stored_read, stored_finish};

/* Inflate decompressor. */

#undef crc32
#include <zlib.h>

#define UCOMPSIZE 4096

struct inflator
{
  z_stream zss;
  int state;
  unsigned char ucomp[UCOMPSIZE];
  size_t bytes_uncomp;
  size_t ucomp_bytes_read;

  /* Two bitfields as defined by RFC1950 */
  uint16_t cmf_flg ;
};

static void
inflate_finish (struct zip_member *zm)
{
  struct inflator *inf = zm->aux;

  inflateEnd (&inf->zss);

  free (inf);
}

static char *
inflate_init (struct zip_member *zm)
{
  int r;
  struct inflator *inf = xzalloc (sizeof *inf);

  uint16_t flg = 0 ;
  uint16_t cmf = 0x8; /* Always 8 for inflate */

  const uint16_t cinfo = 7;  /* log_2(Window size) - 8 */

  cmf |= cinfo << 4;     /* Put cinfo into the high nibble */

  /* make these into a 16 bit word */
  inf->cmf_flg = (cmf << 8) | flg;

  /* Set the check bits */
  inf->cmf_flg += 31 - (inf->cmf_flg % 31);
  assert (inf->cmf_flg % 31 == 0);

  inf->zss.next_in = Z_NULL;
  inf->zss.avail_in = 0;
  inf->zss.zalloc = Z_NULL;
  inf->zss.zfree  = Z_NULL;
  inf->zss.opaque = Z_NULL;
  r = inflateInit (&inf->zss);

  if (Z_OK != r)
    return xasprintf (_("%s: cannot initialize inflator (%s)"),
                      zm->file_name, zError (r));

  zm->aux = inf;

  return NULL;
}

static int
inflate_read (struct zip_member *zm, void *buf, size_t n)
{
  int r;
  struct inflator *inf = zm->aux;

  if (inf->zss.avail_in == 0)
    {
      int bytes_read;
      int bytes_to_read;
      int pad = 0;

      if (inf->state == 0)
	{
	  inf->ucomp[1] = inf->cmf_flg ;
      	  inf->ucomp[0] = inf->cmf_flg >> 8 ;

	  pad = 2;
	  inf->state++;
	}

      bytes_to_read = zm->comp_size - inf->ucomp_bytes_read;

      if (bytes_to_read == 0)
	return 0;

      if (bytes_to_read > UCOMPSIZE)
	bytes_to_read = UCOMPSIZE;

      bytes_read = fread (inf->ucomp + pad, 1, bytes_to_read - pad, zm->fp);
      if (!bytes_read && !zm->error)
        {
          zm->error = get_stream_error (zm->fp, zm->file_name);
          return -1;
        }

      inf->ucomp_bytes_read += bytes_read;

      inf->zss.avail_in = bytes_read + pad;
      inf->zss.next_in = inf->ucomp;
    }
  inf->zss.avail_out = n;
  inf->zss.next_out = buf;

  r = inflate (&inf->zss, Z_NO_FLUSH);
  if (Z_OK == r)
    {
      return n - inf->zss.avail_out;
    }

  if (!zm->error)
    zm->error = xasprintf (_("%s: error inflating \"%s\" (%s)"),
                           zm->file_name, zm->member_name, zError (r));

  return -1;
}

static const struct decompressor inflate_decompressor =
  {inflate_init, inflate_read, inflate_finish};

