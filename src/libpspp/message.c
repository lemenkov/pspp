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
#include "libpspp/str.h"
#include "libpspp/version.h"
#include "data/settings.h"

#include "gl/minmax.h"
#include "gl/progname.h"
#include "gl/xalloc.h"
#include "gl/xvasprintf.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Message handler as set by msg_set_handler(). */
static void (*msg_handler)  (const struct msg *, void *aux);
static void *msg_aux;

/* Disables emitting messages if positive. */
static int messages_disabled;

/* Public functions. */


void
vmsg (enum msg_class class, const char *format, va_list args)
{
  struct msg m = {
    .category = msg_class_to_category (class),
    .severity = msg_class_to_severity (class),
    .text = xvasprintf (format, args),
  };

  msg_emit (&m);
}

/* Writes error message in CLASS, with text FORMAT, formatted with
   printf, to the standard places. */
void
msg (enum msg_class class, const char *format, ...)
{
  va_list args;
  va_start (args, format);
  vmsg (class, format, args);
  va_end (args);
}



void
msg_error (int errnum, const char *format, ...)
{
  va_list args;
  va_start (args, format);
  char *e = xvasprintf (format, args);
  va_end (args);

  struct msg m = {
    .category = MSG_C_GENERAL,
    .severity = MSG_S_ERROR,
    .file_name = NULL,
    .text = xasprintf (_("%s: %s"), e, strerror (errnum)),
  };
  msg_emit (&m);

  free (e);
}



void
msg_set_handler (void (*handler) (const struct msg *, void *aux), void *aux)
{
  msg_handler = handler;
  msg_aux = aux;
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
msg_dup (const struct msg *m)
{
  struct msg *new_msg;

  new_msg = xmemdup (m, sizeof *m);
  if (m->file_name != NULL)
    new_msg->file_name = xstrdup (m->file_name);
  if (m->command_name != NULL)
    new_msg->command_name = xstrdup (m->command_name);
  new_msg->text = xstrdup (m->text);

  return new_msg;
}

/* Frees a message created by msg_dup().

   (Messages not created by msg_dup(), as well as their file_name
   members, are typically not dynamically allocated, so this function should
   not be used to destroy them.) */
void
msg_destroy (struct msg *m)
{
  free (m->file_name);
  free (m->text);
  free (m->command_name);
  free (m);
}

char *
msg_to_string (const struct msg *m)
{
  struct string s;

  ds_init_empty (&s);

  if (m->category != MSG_C_GENERAL
      && (m->file_name || m->first_line > 0 || m->first_column > 0))
    {
      int l1 = m->first_line;
      int l2 = MAX (m->first_line, m->last_line - 1);
      int c1 = m->first_column;
      int c2 = MAX (m->first_column, m->last_column - 1);

      if (m->file_name)
        ds_put_format (&s, "%s", m->file_name);

      if (l1 > 0)
        {
          if (!ds_is_empty (&s))
            ds_put_byte (&s, ':');

          if (l2 > l1)
            {
              if (c1 > 0)
                ds_put_format (&s, "%d.%d-%d.%d", l1, c1, l2, c2);
              else
                ds_put_format (&s, "%d-%d", l1, l2);
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
                      ds_put_format (&s, "%d.%d-%d.%d", l1, c1, l1, c2);
                    }
                  else
                    ds_put_format (&s, "%d.%d", l1, c1);
                }
              else
                ds_put_format (&s, "%d", l1);
            }
        }
      else if (c1 > 0)
        {
          if (c2 > c1)
            ds_put_format (&s, ".%d-%d", c1, c2);
          else
            ds_put_format (&s, ".%d", c1);
        }
      ds_put_cstr (&s, ": ");
    }

  ds_put_format (&s, "%s: ", msg_severity_to_string (m->severity));

  if (m->category == MSG_C_SYNTAX && m->command_name != NULL)
    ds_put_format (&s, "%s: ", m->command_name);

  ds_put_cstr (&s, m->text);

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


static int entrances = 0;

static void
ship_message (struct msg *m)
{
  entrances++;
  if (! m->shipped)
    {
      if (msg_handler && entrances <= 1)
	msg_handler (m, msg_aux);
      else
	{
	  fwrite (m->text, 1, strlen (m->text), stderr);
	  fwrite ("\n", 1, 1, stderr);
	}
    }
  m->shipped = true;
  entrances--;
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


/* Emits M as an error message.
   Frees allocated data in M. */
void
msg_emit (struct msg *m)
{
  m->shipped = false;
  if (!messages_disabled)
     process_msg (m);

  free (m->text);
  free (m->command_name);
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
  diagnostic_information_bytes += append_message (diagnostic_information, diagnostic_information_bytes, "version:             %s\n", version);
  diagnostic_information_bytes += append_message (diagnostic_information, diagnostic_information_bytes, "host_system:         %s\n", host_system);
  diagnostic_information_bytes += append_message (diagnostic_information, diagnostic_information_bytes, "build_system:        %s\n", build_system);
  diagnostic_information_bytes += append_message (diagnostic_information, diagnostic_information_bytes, "locale_dir:          %s\n", locale_dir);
  diagnostic_information_bytes += append_message (diagnostic_information, diagnostic_information_bytes, "compiler version:    %s\n",
#ifdef __VERSION__
           __VERSION__
#else
           "Unknown"
#endif
);

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
