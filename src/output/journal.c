/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2010, 2012, 2013 Free Software Foundation, Inc.

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

#include "output/journal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "data/file-name.h"
#include "libpspp/cast.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "output/driver-provider.h"
#include "output/output-item.h"

#include "gl/fwriteerror.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct journal_driver
  {
    struct output_driver driver;
    FILE *file;
    char *file_name;
    bool newly_opened;
};

static const struct output_driver_class journal_class;

/* This persists even if the driver is destroyed and recreated. */
static char *journal_file_name;

static struct journal_driver *
journal_driver_cast (struct output_driver *driver)
{
  assert (driver->class == &journal_class);
  return UP_CAST (driver, struct journal_driver, driver);
}

static void
journal_destroy (struct output_driver *driver)
{
  struct journal_driver *j = journal_driver_cast (driver);

  if (fwriteerror (j->file))
    msg_error(errno, _("error writing output file `%s'"), j->file_name);
  free (j->file_name);
  free (j);
}

static void
journal_output (struct journal_driver *j, char *s, const char *prefix)
{
  if (j->file)
    {
      if (j->newly_opened)
        {
          j->newly_opened = false;

          /* Unless this file is empty, start off with a blank line. */
          struct stat s;
          if (!fstat (fileno (j->file), &s) && s.st_size != 0)
            putc ('\n', j->file);

          /* Write the date and time. */
          char buf[64];
          time_t t = time (NULL);
          struct tm *tm = localtime (&t);
          strftime (buf, sizeof buf, "%Y-%m-%d %H:%M:%S", tm);
          fprintf (j->file, "* New session at %s.\n", buf);
        }

      const char *p = s;
      do
        {
          size_t len = strcspn (p, "\n");
          fputs (prefix, j->file);
          fwrite (p, len, 1, j->file);
          putc ('\n', j->file);
          p += len;
          if (*p == '\n')
            p++;
        }
      while (*p);

      /* Flush the journal in case the syntax we're about to write
         causes a crash.  Having the syntax already written to disk
         makes postmortem analysis of the problem possible. */
      fflush (j->file);
    }

  free (s);
}

static void
journal_submit (struct output_driver *driver, const struct output_item *item)
{
  struct journal_driver *j = journal_driver_cast (driver);

  switch (item->type)
    {
    case OUTPUT_ITEM_MESSAGE:
      journal_output (j, msg_to_string (item->message), "> ");
      break;

    case OUTPUT_ITEM_TEXT:
      if (item->text.subtype == TEXT_ITEM_SYNTAX)
        journal_output (j, text_item_get_plain_text (item), "");
      break;

    case OUTPUT_ITEM_GROUP:
      for (size_t i = 0; i < item->group.n_children; i++)
        journal_submit (driver, item->group.children[i]);
      break;

    case OUTPUT_ITEM_CHART:
    case OUTPUT_ITEM_IMAGE:
    case OUTPUT_ITEM_PAGE_BREAK:
    case OUTPUT_ITEM_TABLE:
      break;
    }
}

static const struct output_driver_class journal_class =
  {
    .name = "journal",
    .destroy = journal_destroy,
    .submit = journal_submit,
  };

static struct journal_driver *
get_journal_driver (void)
{
  struct output_driver *d = output_driver_find (&journal_class);
  return d ? journal_driver_cast (d) : NULL;
}

/* Disables journaling. */
void
journal_disable (void)
{
  struct journal_driver *j = get_journal_driver ();
  if (j)
    output_driver_destroy (&j->driver);
}

/* Enable journaling. */
void
journal_enable (void)
{
  if (get_journal_driver ())
    return;

  const char *file_name = journal_get_file_name ();
  FILE *file = fopen (file_name, "a");
  if (file == NULL)
    {
      msg_error (errno, _("error opening output file `%s'"), file_name);
      return;
    }

  struct journal_driver *j = xmalloc (sizeof *j);
  *j = (struct journal_driver) {
    .driver = {
      .class = &journal_class,
      .name = xstrdup ("journal"),
      .device_type = SETTINGS_DEVICE_UNFILTERED,
    },
    .file = file,
    .file_name = xstrdup (file_name),
    .newly_opened = true,
  };
  output_driver_register (&j->driver);
}


/* Returns true if journaling is enabled, false otherwise. */
bool
journal_is_enabled (void)
{
  return get_journal_driver () != NULL;
}

/* Sets the name of the journal file to FILE_NAME. */
void
journal_set_file_name (const char *file_name)
{
  if (!strcmp (file_name, journal_get_file_name ()))
    return;

  bool enabled = journal_is_enabled ();
  if (enabled)
    journal_disable ();

  free (journal_file_name);
  journal_file_name = xstrdup (file_name);

  if (enabled)
    journal_enable ();
}

/* Returns the name of the journal file.  The caller must not modify or free
   the returned string. */
const char *
journal_get_file_name (void)
{
  if (!journal_file_name)
    journal_file_name = xstrdup (journal_get_default_file_name ());
  return journal_file_name;
}

/* Returns the name of the default journal file.  The caller must not modify or
   free the returned string. */
const char *
journal_get_default_file_name (void)
{
  static char *default_file_name;

  if (!default_file_name)
    default_file_name = xasprintf ("%s%s", default_log_path (), "pspp.jnl");

  return default_file_name;
}
