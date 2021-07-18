/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2013 Free Software Foundation, Inc.

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

#ifndef SCAN_H
#define SCAN_H 1

#include "language/lexer/segment.h"
#include "libpspp/str.h"

struct token;

/* PSPP syntax scanning.

   PSPP divides traditional "lexical analysis" or "tokenization" into two
   phases: a lower-level phase called "segmentation" and a higher-level phase
   called "scanning".  segment.h provides declarations for the segmentation
   phase.  This header file contains declarations for the scanning phase.

   Scanning accepts as input a stream of segments, which are UTF-8 strings each
   labeled with a segment type.  It outputs a stream of "scan tokens", which
   are the same as the tokens used by the PSPP parser with a few additional
   types.
*/

enum tokenize_result
  {
    TOKENIZE_EMPTY,
    TOKENIZE_TOKEN,
    TOKENIZE_ERROR
  };

enum tokenize_result token_from_segment (enum segment_type, struct substring,
                                         struct token *);

struct merger
  {
    unsigned int state;
  };
#define MERGER_INIT { 0 }

int merger_add (struct merger *m, const struct token *in, struct token *out);

/* A simplified lexer for handling syntax in a string. */

struct string_lexer
  {
    const char *input;
    size_t length;
    size_t offset;
    struct segmenter segmenter;
  };

enum string_lexer_result
  {
    SLR_END,
    SLR_TOKEN,
    SLR_ERROR
  };

void string_lexer_init (struct string_lexer *, const char *input,
                        size_t length, enum segmenter_mode, bool is_snippet);
enum string_lexer_result string_lexer_next (struct string_lexer *,
                                            struct token *);

#endif /* scan.h */
