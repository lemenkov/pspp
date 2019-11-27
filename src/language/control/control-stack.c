/*
PSPP - a program for statistical analysis.
Copyright (C) 2017 Free Software Foundation, Inc.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#include "language/control/control-stack.h"

#include <assert.h>
#include <stdlib.h>

#include "libpspp/compiler.h"
#include "libpspp/message.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct ctl_struct
  {
    const struct ctl_class *class;    /* Class of control structure. */
    struct ctl_struct *down;	/* Points toward the bottom of ctl_stack. */
    void *private;              /* Private data. */
  };

static struct ctl_struct *ctl_stack;

void
ctl_stack_clear (void)
{
  while (ctl_stack != NULL)
    {
      struct ctl_struct *top = ctl_stack;
      msg (SE, _("%s without %s."),
           top->class->start_name, top->class->end_name);
      ctl_stack_pop (top->private);
    }
}

void
ctl_stack_push (const struct ctl_class *class, void *private)
{
  struct ctl_struct *ctl;

  assert (private != NULL);
  ctl = xmalloc (sizeof *ctl);
  ctl->class = class;
  ctl->down = ctl_stack;
  ctl->private = private;
  ctl_stack = ctl;
}

void *
ctl_stack_top (const struct ctl_class *class)
{
  struct ctl_struct *top = ctl_stack;
  if (top != NULL && top->class == class)
    return top->private;
  else
    {
      if (ctl_stack_search (class) != NULL)
        msg (SE, _("This command must appear inside %s...%s, "
                   "without intermediate %s...%s."),
             class->start_name, class->end_name,
             top->class->start_name, top->class->end_name);
      return NULL;
    }
}

void *
ctl_stack_search (const struct ctl_class *class)
{
  struct ctl_struct *ctl;

  for (ctl = ctl_stack; ctl != NULL; ctl = ctl->down)
    if (ctl->class == class)
      return ctl->private;

  msg (SE, _("This command cannot appear outside %s...%s."),
       class->start_name, class->end_name);
  return NULL;
}

void
ctl_stack_pop (void *private)
{
  struct ctl_struct *top = ctl_stack;

  assert (top != NULL);
  assert (top->private == private);

  top->class->close (top->private);
  ctl_stack = top->down;
  free (top);
}

bool
ctl_stack_is_empty (void)
{
  return ctl_stack == NULL;
}
