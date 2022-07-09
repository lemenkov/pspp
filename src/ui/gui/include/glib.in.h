/* Wrapper for <glib.h>.
   Copyright (C) 2022 Free Software Foundation, Inc.

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


#if !GLIB_CHECK_VERSION(2,68,0)
#define g_memdup2 g_memdup
#endif

#endif /* PSPP_GLIB_H */
