/* PSPP - a program for statistical analysis.
   Copyright (C) 2007 Free Software Foundation, Inc.

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

/* Dictionary classes.

   Occasionally it is useful to classify variables into three
   groups: system variables (those whose names begin with $),
   scratch variables (those whose names begin with #), and
   ordinary variables (all others).  This header provides a
   little bit of support for this. */

#ifndef DATA_DICT_CLASS_H
#define DATA_DICT_CLASS_H 1

/* Classes of variables.
   These values are bitwise disjoint so that they can be used in
   masks. */
enum dict_class
  {
    DC_ORDINARY = 1 << 0,       /* Ordinary identifier. */
    DC_SYSTEM = 1 << 1,         /* System variable. */
    DC_SCRATCH = 1 << 2,        /* Scratch variable. */
#define DC_ALL (DC_ORDINARY | DC_SYSTEM | DC_SCRATCH)
  };

enum dict_class dict_class_from_id (const char *name);
const char *dict_class_to_name (enum dict_class);

#endif /* data/dict-class.h */
