/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010,
   2011, 2013 Free Software Foundation, Inc.

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

#include "libpspp/message.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libpspp/cast.h"
#include "libpspp/intern.h"
#include "libpspp/str.h"
#include "libpspp/version.h"
#include "data/settings.h"

#include "gl/minmax.h"
#include "gl/progname.h"
#include "gl/relocatable.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Message handler as set by msg_set_handler(). */
static struct msg_handler msg_handler = { .output_msg = NULL };

/* Disables emitting messages if positive. */
static int messages_disabled;

/* Public functions. */


void
vmsg (enum msg_class class, const struct msg_location *location,
      const char *format, va_list args)
{
  struct msg *m = xmalloc (sizeof *m);
  *m = (struct msg) {
    .category = msg_class_to_category (class),
    .severity = msg_class_to_severity (class),
    .location = msg_location_dup (location),
    .text = xvasprintf (format, args),
  };
  msg_emit (m);
}

/* Writes error message in CLASS, with text FORMAT, formatted with
   printf, to the standard places. */
void
msg (enum msg_class class, const char *format, ...)
{
  va_list args;
  va_start (args, format);
  vmsg (class, NULL, format, args);
  va_end (args);
}

/* Outputs error message in CLASS, with text FORMAT, formatted with printf.
   LOCATION is the reported location for the message. */
void
msg_at (enum msg_class class, const struct msg_location *location,
        const char *format, ...)
{
  va_list args;
  va_start (args, format);
  vmsg (class, location, format, args);
  va_end (args);
}

void
msg_error (int errnum, const char *format, ...)
{
  va_list args;
  va_start (args, format);
  struct string s = DS_EMPTY_INITIALIZER;
  ds_put_vformat (&s, format, args);
  va_end (args);
  ds_put_format (&s, ": %s", strerror (errnum));

  struct msg *m = xmalloc (sizeof *m);
  *m = (struct msg) {
    .category = MSG_C_GENERAL,
    .severity = MSG_S_ERROR,
    .text = ds_steal_cstr (&s),
  };
  msg_emit (m);
}

void
msg_set_handler (const struct msg_handler *handler)
{
  msg_handler = *handler;
}

/* msg_point. */

/* Takes POINT, adds to it the syntax in SYNTAX, incrementing the line number
   for each new-line in SYNTAX and the column number for each column, and
   returns the result. */
struct msg_point
msg_point_advance (struct msg_point point, struct substring syntax)
{
  for (;;)
    {
      size_t newline = ss_find_byte (syntax, '\n');
      if (newline == SIZE_MAX)
        break;
      point.line++;
      point.column = 1;
      ss_advance (&syntax, newline + 1);
    }

  point.column += ss_utf8_count_columns (syntax);
  return point;
}

/* msg_location. */

void
msg_location_uninit (struct msg_location *loc)
{
  if (msg_handler.lex_source_unref)
    msg_handler.lex_source_unref (loc->src);
  intern_unref (loc->file_name);
}

void
msg_location_destroy (struct msg_location *loc)
{
  if (loc)
    {
      msg_location_uninit (loc);
      free (loc);
    }
}

static int
msg_point_compare_3way (const struct msg_point *a, const struct msg_point *b)
{
  return (!a->line ? 1
          : !b->line ? -1
          : a->line > b->line ? 1
          : a->line < b->line ? -1
          : !a->column ? 1
          : !b->column ? -1
          : a->column > b->column ? 1
          : a->column < b->column ? -1
          : 0);
}

void
msg_location_remove_columns (struct msg_location *location)
{
  location->start.column = 0;
  location->end.column = 0;
}

void
msg_location_merge (struct msg_location **dstp, const struct msg_location *src)
{
  struct msg_location *dst = *dstp;
  if (!dst)
    {
      *dstp = msg_location_dup (src);
      return;
    }

  if (dst->file_name != src->file_name)
    {
      /* Failure. */
      return;
    }
  if (msg_point_compare_3way (&dst->start, &src->start) > 0)
    dst->start = src->start;
  if (msg_point_compare_3way (&dst->end, &src->end) < 0)
    dst->end = src->end;
}

struct msg_location *
msg_location_merged (const struct msg_location *a,
                     const struct msg_location *b)
{
  struct msg_location *new = msg_location_dup (a);
  if (b)
    msg_location_merge (&new, b);
  return new;
}

struct msg_location *
msg_location_dup (const struct msg_location *src)
{
  if (!src)
    return NULL;

  struct msg_location *dst = xmalloc (sizeof *dst);
  *dst = *src;
  if (src->file_name)
    dst->file_name = intern_ref (src->file_name);
  if (msg_handler.lex_source_ref && src->src)
    msg_handler.lex_source_ref (dst->src);
  return dst;
}

bool
msg_location_is_empty (const struct msg_location *loc)
{
  return !loc || (!loc->file_name
                  && loc->start.line <= 0
                  && loc->start.column <= 0);
}

void
msg_location_format (const struct msg_location *loc, struct string *s)
{
  if (!loc)
    return;

  if (loc->file_name)
    ds_put_cstr (s, loc->file_name);

  int l1 = loc->start.line;
  int l2 = MAX (l1, loc->end.line);
  int c1 = loc->start.column;
  int c2 = MAX (c1, loc->end.column);

  if (l1 > 0)
    {
      if (loc->file_name)
        ds_put_byte (s, ':');

      if (l2 > l1)
        {
          if (c1 > 0)
            ds_put_format (s, "%d.%d-%d.%d", l1, c1, l2, c2);
          else
            ds_put_format (s, "%d-%d", l1, l2);
        }
      else
        {
          if (c1 > 0)
            {
              if (c2 > c1)
                {
                  /* The GNU coding standards say to use
                     LINENO-1.COLUMN-1-COLUMN-2 for this case, but GNU
                     Emacs interprets COLUMN-2 as LINENO-2 if I do that.
                     I've submitted an Emacs bug report:
                     http://debbugs.gnu.org/cgi/bugreport.cgi?bug=7725.

                     For now, let's be compatible. */
                  ds_put_format (s, "%d.%d-%d.%d", l1, c1, l1, c2);
                }
              else
                ds_put_format (s, "%d.%d", l1, c1);
            }
          else
            ds_put_format (s, "%d", l1);
        }
    }
  else if (c1 > 0)
    {
      if (c2 > c1)
        ds_put_format (s, ".%d-%d", c1, c2);
      else
        ds_put_format (s, ".%d", c1);
    }
}

/* msg_stack */

void
msg_stack_destroy (struct msg_stack *stack)
{
  if (stack)
    {
      msg_location_destroy (stack->location);
      free (stack->description);
      free (stack);
    }
}

struct msg_stack *
msg_stack_dup (const struct msg_stack *src)
{
  struct msg_stack *dst = xmalloc (sizeof *src);
  *dst = (struct msg_stack) {
    .location = msg_location_dup (src->location),
    .description = xstrdup_if_nonnull (src->description),
  };
  return dst;
}

/* Working with messages. */

const char *
msg_severity_to_string (enum msg_severity severity)
{
  switch (severity)
    {
    case MSG_S_ERROR:
      return _("error");
    case MSG_S_WARNING:
      return _("warning");
    case MSG_S_NOTE:
    default:
      return _("note");
    }
}

/* Duplicate a message */
struct msg *
msg_dup (const struct msg *src)
{
  struct msg_stack **ms = xmalloc (src->n_stack * sizeof *ms);
  for (size_t i = 0; i < src->n_stack; i++)
    ms[i] = msg_stack_dup (src->stack[i]);

  struct msg *dst = xmalloc (sizeof *dst);
  *dst = (struct msg) {
    .category = src->category,
    .severity = src->severity,
    .stack = ms,
    .n_stack = src->n_stack,
    .location = msg_location_dup (src->location),
    .command_name = xstrdup_if_nonnull (src->command_name),
    .text = xstrdup (src->text),
  };
  return dst;
}

/* Frees a message created by msg_dup().

   (Messages not created by msg_dup(), as well as their file_name
   members, are typically not dynamically allocated, so this function should
   not be used to destroy them.) */
void
msg_destroy (struct msg *m)
{
  if (m)
    {
      for (size_t i = 0; i < m->n_stack; i++)
        msg_stack_destroy (m->stack[i]);
      free (m->stack);
      msg_location_destroy (m->location);
      free (m->text);
      free (m->command_name);
      free (m);
    }
}

char *
msg_to_string (const struct msg *m)
{
  struct string s;

  ds_init_empty (&s);

  for (size_t i = 0; i < m->n_stack; i++)
    {
      const struct msg_stack *ms = m->stack[i];
      if (!msg_location_is_empty (ms->location))
        {
          msg_location_format (ms->location, &s);
          ds_put_cstr (&s, ": ");
        }
      ds_put_format (&s, "%s\n", ms->description);
    }
  if (m->category != MSG_C_GENERAL && !msg_location_is_empty (m->location))
    {
      msg_location_format (m->location, &s);
      ds_put_cstr (&s, ": ");
    }

  ds_put_format (&s, "%s: ", msg_severity_to_string (m->severity));

  if (m->category == MSG_C_SYNTAX && m->command_name != NULL)
    ds_put_format (&s, "%s: ", m->command_name);

  ds_put_cstr (&s, m->text);

  const struct msg_location *loc = m->location;
  if (m->category != MSG_C_GENERAL
      && loc->src && loc->start.line && loc->start.column
      && msg_handler.lex_source_get_line)
    {
      int l0 = loc->start.line;
      int l1 = loc->end.line;
      int nl = l1 - l0;
      for (int ln = l0; ln <= l1; ln++)
        {
          if (nl > 3 && ln == l0 + 2)
            {
              ds_put_cstr (&s, "\n  ... |");
              ln = l1;
            }

          struct substring line = msg_handler.lex_source_get_line (
            loc->src, ln);
          ss_rtrim (&line, ss_cstr ("\n\r"));

          ds_put_format (&s, "\n%5d | ", ln);
          ds_put_substring (&s, line);

          int c0 = ln == l0 ? loc->start.column : 1;
          int c1 = ln == l1 ? loc->end.column : ss_utf8_count_columns (line);
          if (c0 > 0 && c1 >= c0 && !loc->omit_underlines)
            {
              ds_put_cstr (&s, "\n      |");
              ds_put_byte_multiple (&s, ' ', c0);
              if (ln == l0)
                {
                  ds_put_byte (&s, '^');
                  if (c1 > c0)
                    ds_put_byte_multiple (&s, '~', c1 - c0);
                }
              else
                ds_put_byte_multiple (&s, '-', c1 - c0 + 1);
            }
        }
    }

  return ds_cstr (&s);
}


/* Number of messages reported, by severity level. */
static int counts[MSG_N_SEVERITIES];

/* True after the maximum number of errors or warnings has been exceeded. */
static bool too_many_errors;

/* True after the maximum number of notes has been exceeded. */
static bool too_many_notes;

/* True iff warnings have been explicitly disabled (MXWARNS = 0) */
static bool warnings_off = false;

/* Checks whether we've had so many errors that it's time to quit
   processing this syntax file. */
bool
msg_ui_too_many_errors (void)
{
  return too_many_errors;
}

void
msg_ui_disable_warnings (bool x)
{
  warnings_off = x;
}


void
msg_ui_reset_counts (void)
{
  int i;

  for (i = 0; i < MSG_N_SEVERITIES; i++)
    counts[i] = 0;
  too_many_errors = false;
  too_many_notes = false;
}

bool
msg_ui_any_errors (void)
{
  return counts[MSG_S_ERROR] > 0;
}


static void
ship_message (const struct msg *m)
{
  enum { MAX_STACK = 4 };
  static const struct msg *stack[MAX_STACK];
  static size_t n;

  /* If we're recursing on a given message, or recursing deeply, drop it. */
  if (n >= MAX_STACK)
    return;
  for (size_t i = 0; i < n; i++)
    if (stack[i] == m)
      return;

  stack[n++] = m;
  if (msg_handler.output_msg && n <= 1)
    msg_handler.output_msg (m, msg_handler.aux);
  else
    fprintf (stderr, "%s\n", m->text);
  n--;
}

static void
submit_note (char *s)
{
  struct msg m = {
    .category = MSG_C_GENERAL,
    .severity = MSG_S_NOTE,
    .text = s,
  };
  ship_message (&m);

  free (s);
}

static void
process_msg (struct msg *m)
{
  int n_msgs, max_msgs;

  if (too_many_errors
      || (too_many_notes && m->severity == MSG_S_NOTE)
      || (warnings_off && m->severity == MSG_S_WARNING))
    return;

  ship_message (m);

  counts[m->severity]++;
  max_msgs = settings_get_max_messages (m->severity);
  n_msgs = counts[m->severity];
  if (m->severity == MSG_S_WARNING)
    n_msgs += counts[MSG_S_ERROR];
  if (n_msgs > max_msgs)
    {
      if (m->severity == MSG_S_NOTE)
        {
          too_many_notes = true;
          submit_note (xasprintf (_("Notes (%d) exceed limit (%d).  "
                                    "Suppressing further notes."),
                                  n_msgs, max_msgs));
        }
      else
        {
          too_many_errors = true;
          if (m->severity == MSG_S_WARNING)
            submit_note (xasprintf (_("Warnings (%d) exceed limit (%d).  Syntax processing will be halted."),
                                    n_msgs, max_msgs));
          else
            submit_note (xasprintf (_("Errors (%d) exceed limit (%d).  Syntax processing will be halted."),
                                    n_msgs, max_msgs));
        }
    }
}


/* Emits M as an error message.  Takes ownership of M. */
void
msg_emit (struct msg *m)
{
  if (!messages_disabled)
    process_msg (m);
  msg_destroy (m);
}

/* Disables message output until the next call to msg_enable.  If
   this function is called multiple times, msg_enable must be
   called an equal number of times before messages are actually
   re-enabled. */
void
msg_disable (void)
{
  messages_disabled++;
}

/* Enables message output that was disabled by msg_disable. */
void
msg_enable (void)
{
  assert (messages_disabled > 0);
  messages_disabled--;
}

/* Private functions. */

static char fatal_error_message[1024];
static int fatal_error_message_bytes = 0;

static char diagnostic_information[1024];
static int diagnostic_information_bytes = 0;

static int
append_message (char *msg, int bytes_used, const char *fmt, ...)
{
  va_list va;
  va_start (va, fmt);
  int ret = vsnprintf (msg + bytes_used, 1024 - bytes_used, fmt, va);
  va_end (va);
  assert (ret >= 0);

  return ret;
}


/* Generate a row of asterisks held in statically allocated memory  */
static struct substring
generate_banner (void)
{
  static struct substring banner;
  if (!banner.string)
    banner = ss_cstr ("******************************************************\n");
  return banner;
}

const char *
prepare_fatal_error_message (void)
{
  fatal_error_message_bytes += append_message (fatal_error_message, fatal_error_message_bytes, generate_banner ().string);

  fatal_error_message_bytes += append_message (fatal_error_message, fatal_error_message_bytes, "You have discovered a bug in PSPP.  Please report this\n");
  fatal_error_message_bytes += append_message (fatal_error_message, fatal_error_message_bytes, "to " PACKAGE_BUGREPORT ".  Please include this entire\n");
  fatal_error_message_bytes += append_message (fatal_error_message, fatal_error_message_bytes, "message, *plus* several lines of output just above it.\n");
  fatal_error_message_bytes += append_message (fatal_error_message, fatal_error_message_bytes, "For the best chance at having the bug fixed, also\n");
  fatal_error_message_bytes += append_message (fatal_error_message, fatal_error_message_bytes, "include the syntax file that triggered it and a sample\n");
  fatal_error_message_bytes += append_message (fatal_error_message, fatal_error_message_bytes, "of any data file used for input.\n");
  return fatal_error_message;
}

const char *
prepare_diagnostic_information (void)
{
  char *allocated;

  diagnostic_information_bytes += append_message (diagnostic_information, diagnostic_information_bytes, "version:             %s\n", version);
  diagnostic_information_bytes += append_message (diagnostic_information, diagnostic_information_bytes, "host_system:         %s\n", host_system);
  diagnostic_information_bytes += append_message (diagnostic_information, diagnostic_information_bytes, "build_system:        %s\n", build_system);
  diagnostic_information_bytes += append_message (diagnostic_information, diagnostic_information_bytes, "locale_dir:          %s\n", relocate2 (locale_dir, &allocated));
  diagnostic_information_bytes += append_message (diagnostic_information, diagnostic_information_bytes, "compiler version:    %s\n",
#ifdef __VERSION__
           __VERSION__
#else
           "Unknown"
#endif
);

  free (allocated);

  return diagnostic_information;
}

void
request_bug_report (const char *msg)
{
  write (STDERR_FILENO, fatal_error_message, fatal_error_message_bytes);
  write (STDERR_FILENO, "proximate cause:     ", 21);
  write (STDERR_FILENO, msg, strlen (msg));
  write (STDERR_FILENO, "\n", 1);
  write (STDERR_FILENO, diagnostic_information, diagnostic_information_bytes);
  const struct substring banner = generate_banner ();
  write (STDERR_FILENO, banner.string, banner.length);
}
