/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011, 2014 Free Software Foundation, Inc.

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

#ifndef MESSAGE_H
#define MESSAGE_H 1

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include "libpspp/compiler.h"

struct string;
struct substring;

/* What kind of message is this? */
enum msg_category
  {
    MSG_C_GENERAL,              /* General info. */
    MSG_C_SYNTAX,               /* Messages that relate to syntax files. */
    MSG_C_DATA,                 /* Messages that relate to data files. */
    MSG_N_CATEGORIES
  };

/* How important a condition is it? */
enum msg_severity
  {
    MSG_S_ERROR,
    MSG_S_WARNING,
    MSG_S_NOTE,
    MSG_N_SEVERITIES
  };

const char *msg_severity_to_string (enum msg_severity);

/* Combination of a category and a severity for convenience. */
enum msg_class
  {
    ME, MW, MN,                        /* General error/warning/note. */
    SE, SW, SN,                        /* Script error/warning/note. */
    DE, DW, DN,                        /* Data-file error/note. */
    MSG_CLASS_CNT,
  };

static inline enum msg_category
msg_class_to_category (enum msg_class class)
{
  return class / 3;
}

static inline enum msg_severity
msg_class_to_severity (enum msg_class class)
{
  return class % 3;
}

static inline enum msg_class
msg_class_from_category_and_severity (enum msg_category category,
                                      enum msg_severity severity)
{
  return category * 3 + severity;
}

/* A line number and column number within a source file.  Both are 1-based.  If
   only a line number is available, 'column' is zero.  If neither is available,
   'line' and 'column' are zero.

   Column numbers are measured according to the width of characters as shown in
   a typical fixed-width font, in which CJK characters have width 2 and
   combining characters have width 0.  */
struct msg_point
  {
    int line;
    int column;
  };

struct msg_point msg_point_advance (struct msg_point, struct substring);

/* Location of the cause of an error. */
struct msg_location
  {
    /* Interned file name, or NULL. */
    const char *file_name;

    /* Nonnull if this came from a source file. */
    struct lex_source *src;

    /* The starting and ending point of the cause.  One of:

       - Both empty, with all their members zero.

       - A range of lines, with 0 < start.line <= end.line and start.column =
         end.column = 0.

       - A range of columns spanning one or more lines.  If it's on a single
         line, then start.line = end.line and 0 < start.column <= end.column.
         If it's across multiple lines, then 0 < start.line < end.line and the
         column members are both positive.

       Both 'start' and 'end' are inclusive, line-wise and column-wise.
    */
    struct msg_point start, end;

    /* Normally, 'start' and 'end' contain column information, then displaying
       the message will underline the location.  Setting this to true disables
       displaying underlines. */
    bool omit_underlines;
  };

void msg_location_uninit (struct msg_location *);
void msg_location_destroy (struct msg_location *);
struct msg_location *msg_location_dup (const struct msg_location *);

void msg_location_remove_columns (struct msg_location *);

void msg_location_merge (struct msg_location **, const struct msg_location *);
struct msg_location *msg_location_merged (const struct msg_location *,
                                          const struct msg_location *);

bool msg_location_is_empty (const struct msg_location *);
void msg_location_format (const struct msg_location *, struct string *);

struct msg_stack
  {
    struct msg_location *location;
    char *description;
  };

void msg_stack_destroy (struct msg_stack *);
struct msg_stack *msg_stack_dup (const struct msg_stack *);

/* A message. */
struct msg
  {
    enum msg_category category; /* Message category. */
    enum msg_severity severity; /* Message severity. */
    struct msg_location *location; /* Code location. */
    struct msg_stack **stack;
    size_t n_stack;
    char *command_name;         /* Name of erroneous command, or NULL.  */
    char *text;                 /* Error text. */
  };

/* Initialization. */
struct msg_handler
  {
    void (*output_msg) (const struct msg *, void *aux);
    void *aux;

    struct lex_source *(*lex_source_ref) (const struct lex_source *);
    void (*lex_source_unref) (struct lex_source *);
    struct substring (*lex_source_get_line) (const struct lex_source *,
                                             int line);
  };
void msg_set_handler (const struct msg_handler *);

/* Working with messages. */
struct msg *msg_dup (const struct msg *);
void msg_destroy(struct msg *);
char *msg_to_string (const struct msg *);

/* Emitting messages. */
void vmsg (enum msg_class, const struct msg_location *,
           const char *format, va_list args)
     PRINTF_FORMAT (3, 0);
void msg (enum msg_class, const char *format, ...)
     PRINTF_FORMAT (2, 3);
void msg_at (enum msg_class, const struct msg_location *,
             const char *format, ...)
     PRINTF_FORMAT (3, 4);
void msg_emit (struct msg *);

void msg_error (int errnum, const char *format, ...)
  PRINTF_FORMAT (2, 3);


/* Enable and disable messages. */
void msg_enable (void);
void msg_disable (void);

/* Error context. */
bool msg_ui_too_many_errors (void);
void msg_ui_reset_counts (void);
bool msg_ui_any_errors (void);
void msg_ui_disable_warnings (bool);


/* Used in panic situations only. */
const char * prepare_diagnostic_information (void);
const char * prepare_fatal_error_message (void);
void request_bug_report (const char *msg);


#endif /* message.h */
