/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2016 Free Software Foundation, Inc.

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

#ifndef VALUE_VARIANT_H
#define VALUE_VARIANT_H

union value;

GVariant *value_variant_new (const union value *in, int width);

void value_variant_get (union value *val, GVariant *v);

void value_destroy_from_variant (union value *val, GVariant *v);

#endif
