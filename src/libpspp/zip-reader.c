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

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <xalloc.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>

#include <byteswap.h>

#include "str.h"

#include "zip-reader.h"
#include "zip-private.h"

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
  struct string *errmsgs;    /* A string to hold error messages.
				This string is NOT owned by this object. */
  void *aux;
};

struct decompressor
{
  bool (*init) (struct zip_member *);
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
  char *file_name;                  /* The name of the file from which the data is read */
  uint16_t n_entries;              /* Number of directory entries. */
  struct zip_entry *entries;       /* Directory entries. */
  struct string *errs;             /* A string to hold error messages.  This
                                      string is NOT owned by this object. */
};

struct zip_entry
{
  uint32_t offset;            /* Starting offset in file. */
  uint32_t comp_size;         /* Length of member file data, in bytes. */
  uint32_t ucomp_size;        /* Uncompressed length of member file data, in bytes. */
  char *name;                 /* Name of member file. */
};

void
zip_member_finish (struct zip_member *zm)
{
  if (zm)
    {
      free (zm->file_name);
      free (zm->member_name);
      ds_clear (zm->errmsgs);
      zm->decompressor->finish (zm);
      fclose (zm->fp);
      free (zm);
    }
}

/* Destroy the zip reader */
void
zip_reader_destroy (struct zip_reader *zr)
{
  int i;
  if (zr == NULL)
    return;

  free (zr->file_name);

  for (i = 0; i < zr->n_entries; ++i)
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

static bool get_bytes (FILE *f, void *x, size_t n) WARN_UNUSED_RESULT;


/* Read N bytes from F, storing the result in X */
static bool
get_bytes (FILE *f, void *x, size_t n)
{
  return (n == fread (x, 1, n, f));
}

static bool get_u32 (FILE *f, uint32_t *v) WARN_UNUSED_RESULT;


/* Read a 32 bit value from F */
static bool
get_u32 (FILE *f, uint32_t *v)
{
  uint32_t x;
  if (!get_bytes (f, &x, sizeof x))
    return false;
#ifdef WORDS_BIGENDIAN
  *v = bswap_32 (x);
#else
  *v = x;
#endif
  return true;
}

static bool get_u16 (FILE *f, uint16_t *v) WARN_UNUSED_RESULT;


/* Read a 16 bit value from F */
static bool
get_u16 (FILE *f, uint16_t *v)
{
  uint16_t x;
  if (!get_bytes (f, &x, sizeof x))
    return false;
#ifdef WORDS_BIGENDIAN
  *v = bswap_16 (x);
#else
  *v = x;
#endif
  return true;
}


/* Read 32 bit integer and compare it with EXPECTED.
   place an error string in ERR if necessary. */
static bool
check_magic (FILE *f, const char *file_name,
             uint32_t expected, struct string *err)
{
  uint32_t magic;

  if (! get_u32 (f, &magic)) return false;

  if ((expected != magic))
    {
      ds_put_format (err,
		     _("%s: corrupt archive at 0x%llx: "
                       "expected %#"PRIx32" but got %#"PRIx32),
                     file_name,
		     (long long int) ftello (f) - sizeof (uint32_t),
                     expected, magic);

      return false;
    }
  return true;
}


/* Reads upto BYTES bytes from ZM and puts them in BUF.
   Returns the number of bytes read, or -1 on error */
int
zip_member_read (struct zip_member *zm, void *buf, size_t bytes)
{
  int bytes_read = 0;

  ds_clear (zm->errmsgs);

  if (bytes > zm->bytes_unread)
    bytes = zm->bytes_unread;

  bytes_read  = zm->decompressor->read (zm, buf, bytes);
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
  struct zip_member *zm = zip_member_open (zr, member_name);
  if (!zm)
    {
      *datap = NULL;
      *np = 0;
      return ds_steal_cstr (zr->errs);
    }

  *datap = xmalloc (zm->ucomp_size);
  *np = zm->ucomp_size;

  uint8_t *data = *datap;
  while (zm->bytes_unread)
    if (zip_member_read (zm, data + (zm->ucomp_size - zm->bytes_unread),
                         zm->bytes_unread) == -1)
      {
        zip_member_finish (zm);
        free (*datap);
        *datap = NULL;
        *np = 0;
        return ds_steal_cstr (zr->errs);
      }

  zip_member_finish (zm);
  return NULL;
}

/* Read a central directory header from FILE and initializes ZE with it.
   Returns true if successful, false otherwise.  On error, appends error
   messages to ERRS. */
static bool
zip_header_read_next (FILE *file, const char *file_name,
                      struct zip_entry *ze, struct string *errs)
{
  uint16_t v, nlen, extralen;
  uint16_t gp, time, date;
  uint32_t expected_crc;

  uint16_t clen, diskstart, iattr;
  uint32_t eattr;
  uint16_t comp_type;

  if (! check_magic (file, file_name, MAGIC_SOCD, errs))
    return false;

  if (! get_u16 (file, &v)) return false;
  if (! get_u16 (file, &v)) return false;
  if (! get_u16 (file, &gp)) return false;
  if (! get_u16 (file, &comp_type)) return false;
  if (! get_u16 (file, &time)) return false;
  if (! get_u16 (file, &date)) return false;
  if (! get_u32 (file, &expected_crc)) return false;
  if (! get_u32 (file, &ze->comp_size)) return false;
  if (! get_u32 (file, &ze->ucomp_size)) return false;
  if (! get_u16 (file, &nlen)) return false;
  if (! get_u16 (file, &extralen)) return false;
  if (! get_u16 (file, &clen)) return false;
  if (! get_u16 (file, &diskstart)) return false;
  if (! get_u16 (file, &iattr)) return false;
  if (! get_u32 (file, &eattr)) return false;
  if (! get_u32 (file, &ze->offset)) return false;

  ze->name = xzalloc (nlen + 1);
  if (! get_bytes (file, ze->name, nlen)) return false;

  skip_bytes (file, extralen);

  return true;
}


/* Create a reader from the zip called FILE_NAME */
struct zip_reader *
zip_reader_create (const char *file_name, struct string *errs)
{
  uint16_t disknum, n_members, total_members;
  off_t offset = 0;
  uint32_t central_dir_start, central_dir_length;

  struct zip_reader *zr = xzalloc (sizeof *zr);
  zr->errs = errs;
  if (zr->errs)
    ds_init_empty (zr->errs);

  FILE *file = fopen (file_name, "rb");
  if (!file)
    {
      ds_put_format (zr->errs, _("%s: open failed (%s)"),
                     file_name, strerror (errno));
      free (zr);
      return NULL;
    }

  if (! check_magic (file, file_name, MAGIC_LHDR, zr->errs))
    {
      fclose (file);
      free (zr);
      return NULL;
    }

  if (! find_eocd (file, &offset))
    {
      ds_put_format (zr->errs, _("%s: cannot find central directory"),
                     file_name);
      fclose (file);
      free (zr);
      return NULL;
    }

  if (0 != fseeko (file, offset, SEEK_SET))
    {
      ds_put_format (zr->errs, _("%s: seek failed (%s)"),
                     file_name, strerror (errno));
      fclose (file);
      free (zr);
      return NULL;
    }


  if (! check_magic (file, file_name, MAGIC_EOCD, zr->errs))
    {
      fclose (file);
      free (zr);
      return NULL;
    }

  if (! get_u16 (file, &disknum)
      || ! get_u16 (file, &disknum)

      || ! get_u16 (file, &n_members)
      || ! get_u16 (file, &total_members)

      || ! get_u32 (file, &central_dir_length)
      || ! get_u32 (file, &central_dir_start))
    {
      fclose (file);
      free (zr);
      return NULL;
    }

  if (0 != fseeko (file, central_dir_start, SEEK_SET))
    {
      ds_put_format (zr->errs, _("%s: seek failed (%s)"),
                     file_name, strerror (errno));
      fclose (file);
      free (zr);
      return NULL;
    }

  zr->file_name = xstrdup (file_name);

  zr->entries = xcalloc (n_members, sizeof *zr->entries);
  for (int i = 0; i < n_members; i++)
    {
      if (!zip_header_read_next (file, file_name,
                                 &zr->entries[zr->n_entries], errs))
        {
          fclose (file);
          zip_reader_destroy (zr);
          return NULL;
        }
      zr->n_entries++;
    }

  return zr;
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
struct zip_member *
zip_member_open (struct zip_reader *zr, const char *member)
{
  struct zip_entry *ze = zip_entry_find (zr, member);
  if (ze == NULL)
    {
      ds_put_format (zr->errs, _("%s: unknown member \"%s\""),
                     zr->file_name, member);
      return NULL;
    }

  FILE *fp = fopen (zr->file_name, "rb");
  if (!fp)
    {
      ds_put_format (zr->errs, _("%s: open failed (%s)"),
                     zr->file_name, strerror (errno));
      return NULL;
    }

  struct zip_member *zm = xmalloc (sizeof *zm);
  zm->file_name = xstrdup (zr->file_name);
  zm->member_name = xstrdup (member);
  zm->fp = fp;
  zm->offset = ze->offset;
  zm->comp_size = ze->comp_size;
  zm->ucomp_size = ze->ucomp_size;
  zm->decompressor = NULL;
  zm->bytes_unread = ze->ucomp_size;
  zm->errmsgs = zr->errs;
  zm->aux = NULL;

  if (0 != fseeko (zm->fp, zm->offset, SEEK_SET))
    {
      ds_put_format (zr->errs, _("%s: seek failed (%s)"),
                     ze->name, strerror (errno));
      goto error;
    }

  if (! check_magic (zm->fp, zr->file_name, MAGIC_LHDR, zr->errs))
    goto error;

  uint16_t v, nlen, extra_len;
  uint16_t gp, comp_type, time, date;
  uint32_t ucomp_size, comp_size;
  uint32_t crc;
  if (! get_u16 (zm->fp, &v)) goto error;
  if (! get_u16 (zm->fp, &gp)) goto error;
  if (! get_u16 (zm->fp, &comp_type)) goto error;
  zm->decompressor = get_decompressor (comp_type);
  if (! zm->decompressor) goto error;
  if (! get_u16 (zm->fp, &time)) goto error;
  if (! get_u16 (zm->fp, &date)) goto error;
  if (! get_u32 (zm->fp, &crc)) goto error;
  if (! get_u32 (zm->fp, &comp_size)) goto error;

  if (! get_u32 (zm->fp, &ucomp_size)) goto error;
  if (! get_u16 (zm->fp, &nlen)) goto error;
  if (! get_u16 (zm->fp, &extra_len)) goto error;

  char *name = xzalloc (nlen + 1);
  if (! get_bytes (zm->fp, name, nlen))
    {
      free (name);
      goto error;
    }
  if (strcmp (name, ze->name) != 0)
    {
      ds_put_format (zm->errmsgs,
		     _("%s: name mismatch between central directory (%s) "
                       "and local file header (%s)"),
                     zm->file_name, ze->name, name);
      free (name);
      goto error;
    }
  free (name);

  skip_bytes (zm->fp, extra_len);

  if (!zm->decompressor->init (zm))
    goto error;

  return zm;

error:
  fclose (zm->fp);
  free (zm->file_name);
  free (zm->member_name);
  free (zm);
  return NULL;
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
  return fread (buf, 1, n, zm->fp);
}

static bool
stored_init (struct zip_member *zm UNUSED)
{
  return true;
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

static bool
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
    {
      ds_put_format (zm->errmsgs,
                     _("%s: cannot initialize inflator (%s)"),
                     zm->file_name, zError (r));
      return false;
    }

  zm->aux = inf;

  return true;
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

  ds_put_format (zm->errmsgs, _("%s: error inflating \"%s\" (%s)"),
                 zm->file_name, zm->member_name, zError (r));

  return -1;
}

static const struct decompressor inflate_decompressor =
  {inflate_init, inflate_read, inflate_finish};

