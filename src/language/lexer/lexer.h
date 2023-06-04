/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011, 2013, 2014 Free Software Foundation, Inc.

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

#ifndef LEXER_H
#define LEXER_H 1

#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

#include "data/identifier.h"
#include "data/variable.h"
#include "language/lexer/segment.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/prompt.h"
#include "libpspp/str.h"

struct lexer;
struct lex_source;
struct macro;

/* Handling of errors. */
enum lex_error_mode
  {
    LEX_ERROR_TERMINAL,        /* Discard input line and continue reading. */
    LEX_ERROR_CONTINUE,        /* Continue to next command, except for
                                  cascading failures. */
    LEX_ERROR_IGNORE,          /* Continue, even for cascading failures. */
    LEX_ERROR_STOP,            /* Stop processing. */
  };

/* Reads a single syntax file as a stream of bytes encoded in UTF-8.

   Not opaque. */
struct lex_reader
  {
    const struct lex_reader_class *class;
    enum segmenter_mode syntax;
    enum lex_error_mode error;
    char *encoding;
    char *file_name;            /* NULL if not associated with a file. */
    int line_number;            /* 1-based initial line number, 0 if none. */
    bool eof;
  };

/* An implementation of a lex_reader. */
struct lex_reader_class
  {
    /* Reads up to N bytes of data from READER into N.  Returns the positive
       number of bytes read if successful, or zero at end of input or on
       error.

       STYLE provides a hint to interactive readers as to what kind of syntax
       is being read right now. */
    size_t (*read) (struct lex_reader *reader, char *buf, size_t n,
                    enum prompt_style style);

    /* Closes and destroys READER, releasing any allocated storage.

       The caller will free the 'file_name' member of READER, so the
       implementation should not do so. */
    void (*destroy) (struct lex_reader *reader);
  };

/* Helper functions for lex_reader. */
void lex_reader_init (struct lex_reader *, const struct lex_reader_class *);
void lex_reader_set_file_name (struct lex_reader *, const char *file_name);

/* Creating various kinds of lex_readers. */
struct lex_reader *lex_reader_for_file (const char *file_name,
                                        const char *encoding,
                                        enum segmenter_mode syntax,
                                        enum lex_error_mode error);
struct lex_reader *lex_reader_for_string (const char *, const char *encoding);
struct lex_reader *lex_reader_for_format (const char *, const char *, ...)
  PRINTF_FORMAT (1, 3);
struct lex_reader *lex_reader_for_substring_nocopy (struct substring, const char *encoding);

/* Initialization. */
struct lexer *lex_create (void);
void lex_destroy (struct lexer *);

/* Macros. */
void lex_define_macro (struct lexer *, struct macro *);
const struct macro_set *lex_get_macros (const struct lexer *);

/* Files. */
void lex_include (struct lexer *, struct lex_reader *);
void lex_append (struct lexer *, struct lex_reader *);

/* Advancing. */
void lex_get (struct lexer *);
void lex_get_n (struct lexer *, size_t n);

/* Token testing functions. */
bool lex_is_number (const struct lexer *);
double lex_number (const struct lexer *);
bool lex_is_integer (const struct lexer *);
long lex_integer (const struct lexer *);
bool lex_is_string (const struct lexer *);

/* Token testing functions with lookahead. */
bool lex_next_is_number (const struct lexer *, int n);
double lex_next_number (const struct lexer *, int n);
bool lex_next_is_integer (const struct lexer *, int n);
long lex_next_integer (const struct lexer *, int n);
bool lex_next_is_string (const struct lexer *, int n);

/* Token matching functions. */
bool lex_match (struct lexer *, enum token_type);
bool lex_match_id (struct lexer *, const char *);
bool lex_match_id_n (struct lexer *, const char *, size_t n);
bool lex_match_int (struct lexer *, int);
bool lex_at_phrase (struct lexer *, const char *s);
bool lex_match_phrase (struct lexer *, const char *s);
bool lex_force_match_phrase (struct lexer *, const char *s);

/* Forcible matching functions. */
bool lex_force_match (struct lexer *, enum token_type) WARN_UNUSED_RESULT;
bool lex_force_match_id (struct lexer *, const char *) WARN_UNUSED_RESULT;
bool lex_force_int (struct lexer *) WARN_UNUSED_RESULT;
bool lex_force_int_range (struct lexer *, const char *name,
                          long min, long max) WARN_UNUSED_RESULT;
bool lex_force_num (struct lexer *) WARN_UNUSED_RESULT;
bool lex_force_num_range_closed (struct lexer *, const char *name,
                                 double min, double max) WARN_UNUSED_RESULT;
bool lex_force_num_range_co (struct lexer *, const char *name,
                             double min, double max) WARN_UNUSED_RESULT;
bool lex_force_num_range_oc (struct lexer *, const char *name,
                             double min, double max) WARN_UNUSED_RESULT;
bool lex_force_num_range_open (struct lexer *, const char *name,
                               double min, double max) WARN_UNUSED_RESULT;
bool lex_force_id (struct lexer *) WARN_UNUSED_RESULT;
bool lex_force_string (struct lexer *) WARN_UNUSED_RESULT;
bool lex_force_string_or_id (struct lexer *) WARN_UNUSED_RESULT;

/* Token accessors. */
enum token_type lex_token (const struct lexer *);
double lex_tokval (const struct lexer *);
const char *lex_tokcstr (const struct lexer *);
struct substring lex_tokss (const struct lexer *);

/* Looking ahead. */
const struct token *lex_next (const struct lexer *, int n);
enum token_type lex_next_token (const struct lexer *, int n);
const char *lex_next_tokcstr (const struct lexer *, int n);
double lex_next_tokval (const struct lexer *, int n);
struct substring lex_next_tokss (const struct lexer *, int n);

/* Looking at the current command, including lookahead and lookbehind. */
int lex_ofs (const struct lexer *);
int lex_max_ofs (const struct lexer *);
const struct token *lex_ofs_token (const struct lexer *, int ofs);
struct msg_location *lex_ofs_location (const struct lexer *, int ofs0, int ofs1);
struct msg_point lex_ofs_start_point (const struct lexer *, int ofs);
struct msg_point lex_ofs_end_point (const struct lexer *, int ofs);

/* Token representation. */
char *lex_next_representation (const struct lexer *, int n0, int n1);
char *lex_ofs_representation (const struct lexer *, int ofs0, int ofs1);
bool lex_next_is_from_macro (const struct lexer *, int n);

/* Current position. */
const char *lex_get_file_name (const struct lexer *);
struct msg_location *lex_get_location (const struct lexer *, int n0, int n1);
const char *lex_get_encoding (const struct lexer *);
const struct lex_source *lex_source (const struct lexer *);

/* Issuing errors and warnings. */
void lex_error (struct lexer *, const char *, ...) PRINTF_FORMAT (2, 3);
void lex_next_error (struct lexer *, int n0, int n1, const char *, ...)
  PRINTF_FORMAT (4, 5);
void lex_ofs_error (struct lexer *, int ofs0, int ofs1, const char *, ...)
  PRINTF_FORMAT (4, 5);

void lex_msg (struct lexer *, enum msg_class, const char *, ...)
  PRINTF_FORMAT (3, 4);
void lex_next_msg (struct lexer *, enum msg_class, int n0, int n1,
                   const char *, ...)
  PRINTF_FORMAT (5, 6);
void lex_ofs_msg (struct lexer *, enum msg_class, int ofs0, int ofs1,
                  const char *, ...)
  PRINTF_FORMAT (5, 6);
void lex_ofs_msg_valist (struct lexer *lexer, enum msg_class,
                         int ofs0, int ofs1, const char *format, va_list)
  PRINTF_FORMAT (5, 0);

int lex_end_of_command (struct lexer *);

void lex_error_expecting (struct lexer *, ...) SENTINEL(0);
#define lex_error_expecting(...) \
  lex_error_expecting(__VA_ARGS__, NULL_SENTINEL)
void lex_error_expecting_valist (struct lexer *, va_list);
void lex_error_expecting_array (struct lexer *, const char **, size_t n);

void lex_sbc_only_once (struct lexer *, const char *);
void lex_sbc_missing (struct lexer *, const char *);

void lex_spec_only_once (struct lexer *, const char *subcommand,
                         const char *specification);
void lex_spec_missing (struct lexer *, const char *subcommand,
                       const char *specification);

/* Error handling. */
enum segmenter_mode lex_get_syntax_mode (const struct lexer *);
enum lex_error_mode lex_get_error_mode (const struct lexer *);
void lex_discard_rest_of_command (struct lexer *);
void lex_interactive_reset (struct lexer *);
void lex_discard_noninteractive (struct lexer *);

/* Source code access. */
void lex_set_message_handler (struct lexer *,
                              void (*output_msg) (const struct msg *,
                                                  struct lexer *));
struct lex_source *lex_source_ref (const struct lex_source *);
void lex_source_unref (struct lex_source *);
struct substring lex_source_get_line (const struct lex_source *, int line);

#endif /* lexer.h */
