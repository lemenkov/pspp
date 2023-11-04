/* Wrapper for <glib.h>.
   Copyright (C) 2023 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef PSPP_GLIB_H
#define PSPP_GLIB_H

#if __GNUC__ >= 3
%PRAGMA_SYSTEM_HEADER%
#endif
%PRAGMA_COLUMNS%

#%INCLUDE_NEXT% %NEXT_GLIB_H%

/* Workaround for g_memdup2 which is introduced in glib 2.67.3
   for earlier versions of glib
   Taken from:
   https://gitlab.gnome.org/GNOME/glib/-/merge_requests/1927
*/

#ifndef HAVE_G_MEMDUP2
static inline gpointer
g_memdup2 (gconstpointer mem,
           gsize         byte_size)
{
  gpointer new_mem;

  if (mem && byte_size != 0)
    {
      new_mem = g_malloc (byte_size);
      memcpy (new_mem, mem, byte_size);
    }
  else
    new_mem = NULL;

  return new_mem;
}
#endif

/* g_string_free_and_steal() was introduced in glib 2.76 but it is a persistent
   problem.  I don't understand why.  It's easiest to just unconditionally
   replace it. */
#define g_string_free_and_steal rpl_g_string_free_and_steal
static inline gchar *
rpl_g_string_free_and_steal (GString *string)
{
  return (g_string_free) (string, FALSE);
}

#endif /* PSPP_GLIB_H */
