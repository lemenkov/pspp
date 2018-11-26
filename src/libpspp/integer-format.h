/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2011 Free Software Foundation, Inc.

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

#ifndef LIBPSPP_INTEGER_FORMAT_H
#define LIBPSPP_INTEGER_FORMAT_H 1

#include <byteswap.h>
#include <stdint.h>
#include <string.h>

#include "libpspp/str.h"

/* An integer format. */
enum integer_format
  {
    INTEGER_MSB_FIRST,          /* Big-endian: MSB at lowest address. */
    INTEGER_LSB_FIRST,          /* Little-endian: LSB at lowest address. */
    INTEGER_VAX,                /* VAX-endian: little-endian 16-bit words
                                   in big-endian order. */

    /* Native endianness. */
#ifdef WORDS_BIGENDIAN
    INTEGER_NATIVE = INTEGER_MSB_FIRST
#else
    INTEGER_NATIVE = INTEGER_LSB_FIRST
#endif
  };

void integer_convert (enum integer_format, const void *,
                      enum integer_format, void *,
                      size_t);
uint64_t integer_get (enum integer_format, const void *, size_t);
void integer_put (uint64_t, enum integer_format, void *, size_t);

bool integer_identify (uint64_t expected_value, const void *, size_t,
                       enum integer_format *);

/* Returns the 16-bit unsigned integer at P, which need not be aligned. */
static inline uint16_t
get_uint16 (const void *p)
{
  uint16_t x;
  memcpy (&x, p, sizeof x);
  return x;
}

/* Returns the 32-bit unsigned integer at P, which need not be aligned. */
static inline uint32_t
get_uint32 (const void *p)
{
  uint32_t x;
  memcpy (&x, p, sizeof x);
  return x;
}

/* Returns the 64-bit unsigned integer at P, which need not be aligned. */
static inline uint64_t
get_uint64 (const void *p)
{
  uint64_t x;
  memcpy (&x, p, sizeof x);
  return x;
}

/* Stores 16-bit unsigned integer X at P, which need not be aligned. */
static inline void
put_uint16 (uint16_t x, void *p)
{
  memcpy (p, &x, sizeof x);
}

/* Stores 32-bit unsigned integer X at P, which need not be aligned. */
static inline void
put_uint32 (uint32_t x, void *p)
{
  memcpy (p, &x, sizeof x);
}

/* Stores 64-bit unsigned integer X at P, which need not be aligned. */
static inline void
put_uint64 (uint64_t x, void *p)
{
  memcpy (p, &x, sizeof x);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in little-endian byte order. */
static inline uint16_t
native_to_le16 (uint16_t native)
{
  return INTEGER_NATIVE == INTEGER_LSB_FIRST ? native : bswap_16 (native);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in big-endian byte order. */
static inline uint16_t
native_to_be16 (uint16_t native)
{
  return INTEGER_NATIVE == INTEGER_MSB_FIRST ? native : bswap_16 (native);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in little-endian byte order. */
static inline uint32_t
native_to_le32 (uint32_t native)
{
  return INTEGER_NATIVE == INTEGER_LSB_FIRST ? native : bswap_32 (native);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in big-endian byte order. */
static inline uint32_t
native_to_be32 (uint32_t native)
{
  return INTEGER_NATIVE == INTEGER_MSB_FIRST ? native : bswap_32 (native);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in little-endian byte order. */
static inline uint64_t
native_to_le64 (uint64_t native)
{
  return INTEGER_NATIVE == INTEGER_LSB_FIRST ? native : bswap_64 (native);
}

/* Returns NATIVE converted to a form that, when stored in
   memory, will be in big-endian byte order. */
static inline uint64_t
native_to_be64 (uint64_t native)
{
  return INTEGER_NATIVE == INTEGER_MSB_FIRST ? native : bswap_64 (native);
}

/* Given LE, obtained from memory in little-endian format,
   returns its value. */
static inline uint16_t
le_to_native16 (uint16_t le)
{
  return INTEGER_NATIVE == INTEGER_LSB_FIRST ? le : bswap_16 (le);
}

/* Given BE, obtained from memory in big-endian format, returns
   its value. */
static inline uint16_t
be_to_native16 (uint16_t be)
{
  return INTEGER_NATIVE == INTEGER_MSB_FIRST ? be : bswap_16 (be);
}

/* Given LE, obtained from memory in little-endian format,
   returns its value. */
static inline uint32_t
le_to_native32 (uint32_t le)
{
  return INTEGER_NATIVE == INTEGER_LSB_FIRST ? le : bswap_32 (le);
}

/* Given BE, obtained from memory in big-endian format, returns
   its value. */
static inline uint32_t
be_to_native32 (uint32_t be)
{
  return INTEGER_NATIVE == INTEGER_MSB_FIRST ? be : bswap_32 (be);
}

/* Given LE, obtained from memory in little-endian format,
   returns its value. */
static inline uint64_t
le_to_native64 (uint64_t le)
{
  return INTEGER_NATIVE == INTEGER_LSB_FIRST ? le : bswap_64 (le);
}

/* Given BE, obtained from memory in big-endian format, returns
   its value. */
static inline uint64_t
be_to_native64 (uint64_t be)
{
  return INTEGER_NATIVE == INTEGER_MSB_FIRST ? be : bswap_64 (be);
}


#endif /* libpspp/integer-format.h */
