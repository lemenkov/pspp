/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

#include "output/message-item.h"

#include <stdlib.h>

#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "output/driver.h"
#include "output/output-item-provider.h"
#include "output/text-item.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct message_item *
message_item_create (const struct msg *msg)
{
  struct message_item *item = xmalloc (sizeof *msg);
  *item = (struct message_item) {
    .output_item = OUTPUT_ITEM_INITIALIZER (&message_item_class),
    .msg = msg_dup (msg)
  };
  return item;
}

const struct msg *
message_item_get_msg (const struct message_item *item)
{
  return item->msg;
}

struct text_item *
message_item_to_text_item (struct message_item *message_item)
{
  struct text_item *text_item = text_item_create_nocopy (
    TEXT_ITEM_LOG,
    msg_to_string (message_item_get_msg (message_item)),
    xstrdup (output_item_get_label (message_item_super (message_item))));
  message_item_unref (message_item);
  return text_item;
}

static const char *
message_item_get_label (const struct output_item *output_item)
{
  const struct message_item *item = to_message_item (output_item);
  return (item->msg->severity == MSG_S_ERROR ? _("Error")
          : item->msg->severity == MSG_S_WARNING ? _("Warning")
          : _("Note"));
}

static void
message_item_destroy (struct output_item *output_item)
{
  struct message_item *item = to_message_item (output_item);
  msg_destroy (item->msg);
  free (item);
}

/* Submits ITEM to the configured output drivers, and transfers ownership to
   the output subsystem. */
void
message_item_submit (struct message_item *item)
{
  output_submit (&item->output_item);
}

const struct output_item_class message_item_class =
  {
    message_item_get_label,
    message_item_destroy,
  };
