/* PSPP - a program for statistical analysis.
   Copyright (C) 2023 Free Software Foundation, Inc.

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

#ifndef COMMAND_SEGMENTER_H
#define COMMAND_SEGMENTER_H 1

#include "language/lexer/segment.h"

/* Divides syntax lines into individual commands.

   This is for use by the GUI, which has a feature to run an individual command
   in a syntax window.

   This groups together some kinds of commands that the PSPP tokenizer would
   put T_ENDCMD inside.  For example, it always considers BEGIN DATA...END DATA
   as a single command, even though the tokenizer will emit T_ENDCMD after
   BEGIN DATA if it has a command terminator.  That's because it's the behavior
   most useful for the GUI feature.
*/

struct command_segmenter;

struct command_segmenter *command_segmenter_create (enum segmenter_mode);
void command_segmenter_destroy (struct command_segmenter *);

void command_segmenter_push (struct command_segmenter *,
                             const char *input, size_t n);
void command_segmenter_eof (struct command_segmenter *);
bool command_segmenter_get (struct command_segmenter *, int lines[2]);

#endif /* command-segmenter.h */
