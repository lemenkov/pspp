/* PSPP - a program for statistical analysis.
   Copyright (C) 2020 Free Software Foundation, Inc.

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

#ifndef TEX_PARSING_H
#define TEX_PARSING_H


#include "libpspp/str.h"
#include "libpspp/ll.h"

/* These are the default TeX categories as defined in Chapter 7 of
   The TeXbook ,  plus a new one: CAT_CONTROL_SEQUENCE.  */
enum tex_cat
  {
   CAT_ESCAPE = 0,
   CAT_BEGIN_GROUP,  // 1
   CAT_END_GROUP,    // 2
   CAT_MATH_MODE,    // 3
   CAT_ALIGNMENT,    // 4
   CAT_PARAMETER,    // 5
   CAT_SUPERSCRIPT,  // 6
   CAT_SUBSCRIPT,    // 7
   CAT_IGNORED,      // 8
   CAT_EOL,          // 9
   CAT_SPACE,        // 10
   CAT_LETTER,       // 11
   CAT_OTHER,        // 12
   CAT_ACTIVE,       // 13
   CAT_COMMENT,      // 14
   CAT_INVALID,       // 15
   CAT_CONTROL_SEQ,
  };


struct tex_token
{
  struct ll ll;
  struct string str;
  enum tex_cat cat;
};


/* Parse the TeX fragment STR into TeX tokens and push them
   on to LIST. */
void tex_parse (const char *str, struct ll_list *list);


#endif //TEX_PARSING_H
