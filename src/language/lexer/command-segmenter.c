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

#include <config.h>

#include "language/lexer/command-segmenter.h"

#include "language/lexer/segment.h"
#include "libpspp/deque.h"
#include "libpspp/str.h"

struct lines
  {
    int first;
    int last;
  };

struct command_segmenter
  {
    struct segmenter segmenter;
    struct string input;

    int command_first_line;
    int line;
    enum segment_type prev_segment;

    struct deque deque;
    struct lines *lines;
  };

/* Creates and returns a new command segmenter for the given syntax MODE. */
struct command_segmenter *
command_segmenter_create (enum segmenter_mode mode)
{
  struct command_segmenter *cs = xmalloc (sizeof *cs);
  *cs = (struct command_segmenter) {
    .segmenter = segmenter_init (mode, false),
    .input = DS_EMPTY_INITIALIZER,
    .prev_segment = SEG_NEWLINE,
    .deque = DEQUE_EMPTY_INITIALIZER,
  };
  return cs;
}

/* Destroys CS. */
void
command_segmenter_destroy (struct command_segmenter *cs)
{
  if (cs)
    {
      ds_destroy (&cs->input);
      free (cs->lines);
      free (cs);
    }
}

static void
emit (struct command_segmenter *cs, int first, int last)
{
  if (first < last)
    {
      if (deque_is_full (&cs->deque))
        cs->lines = deque_expand (&cs->deque, cs->lines, sizeof *cs->lines);
      cs->lines[deque_push_back (&cs->deque)] = (struct lines) {
        .first = first,
        .last = last,
      };
    }
}

static void
command_segmenter_push__ (struct command_segmenter *cs,
                          const char *input, size_t n, bool eof)
{
  if (!ds_is_empty (&cs->input))
    {
      ds_put_substring (&cs->input, ss_buffer (input, n));
      input = ds_cstr (&cs->input);
      n = ds_length (&cs->input);
    }

  for (;;)
    {
      enum segment_type type;
      int retval = segmenter_push (&cs->segmenter, input, n, eof, &type);
      if (retval < 0)
        break;

      switch (type)
        {
        case SEG_NUMBER:
        case SEG_QUOTED_STRING:
        case SEG_HEX_STRING:
        case SEG_UNICODE_STRING:
        case SEG_UNQUOTED_STRING:
        case SEG_RESERVED_WORD:
        case SEG_IDENTIFIER:
        case SEG_PUNCT:
        case SEG_SHBANG:
        case SEG_SPACES:
        case SEG_COMMENT:
        case SEG_COMMENT_COMMAND:
        case SEG_DO_REPEAT_COMMAND:
        case SEG_INLINE_DATA:
        case SEG_INNER_START_COMMAND:
        case SEG_INNER_SEPARATE_COMMANDS:
        case SEG_INNER_END_COMMAND:
        case SEG_MACRO_ID:
        case SEG_MACRO_NAME:
        case SEG_MACRO_BODY:
        case SEG_START_DOCUMENT:
        case SEG_DOCUMENT:
        case SEG_EXPECTED_QUOTE:
        case SEG_EXPECTED_EXPONENT:
        case SEG_UNEXPECTED_CHAR:
          break;

        case SEG_NEWLINE:
          cs->line++;
          break;

        case SEG_START_COMMAND:
          if (cs->line > cs->command_first_line)
            emit (cs, cs->command_first_line, cs->line);
          cs->command_first_line = cs->line;
          break;

        case SEG_SEPARATE_COMMANDS:
          if (cs->line > cs->command_first_line)
            emit (cs, cs->command_first_line, cs->line);
          cs->command_first_line = cs->line + 1;
          break;

        case SEG_END_COMMAND:
          emit (cs, cs->command_first_line, cs->line + 1);
          cs->command_first_line = cs->line + 1;
          break;

        case SEG_END:
          emit (cs, cs->command_first_line, cs->line + (cs->prev_segment != SEG_NEWLINE));
          break;
        }

      cs->prev_segment = type;
      input += retval;
      n -= retval;
      if (type == SEG_END)
        break;
    }

  ds_assign_substring (&cs->input, ss_buffer (input, n));
}

/* Adds the N bytes of UTF-8 encoded syntax INPUT to CS. */
void
command_segmenter_push (struct command_segmenter *cs,
                        const char *input, size_t n)
{
  command_segmenter_push__ (cs, input, n, false);
}

/* Tells CS that no more input is coming.  The caller shouldn't call
   command_segmenter_push() again. */
void
command_segmenter_eof (struct command_segmenter *cs)
{
  command_segmenter_push__ (cs, "", 0, true);
}

/* Attempts to get a pair of line numbers bounding a command in the input from
   CS.  If successful, returns true and stores the first line in LINES[0] and
   one past the last line in LINES[1].  On failure, returns false.

   Command bounds can start becoming available as soon as after the first call
   to command_segmenter_push().  Often the output lags behind the input a
   little because some lookahead is needed.  After calling
   command_segmenter_eof(), all the output is available.

   Command bounds are always in order and commands never overlap.  Some lines,
   such as blank lines, might not be part of any command.  An empty input or
   input consisting of just blank lines contains no commands. */
bool
command_segmenter_get (struct command_segmenter *cs, int lines[2])
{
  if (deque_is_empty (&cs->deque))
    return false;

  struct lines *r = &cs->lines[deque_pop_front (&cs->deque)];
  lines[0] = r->first;
  lines[1] = r->last;
  return true;
}
