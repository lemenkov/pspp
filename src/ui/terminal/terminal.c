/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2010, 2017 Free Software Foundation, Inc.

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

#include "ui/terminal/terminal.h"

#include <stdbool.h>
#include <stdlib.h>

#include "data/settings.h"
#include "libpspp/compiler.h"

#include "gl/error.h"

#include "gettext.h"


#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif

#ifdef GWINSZ_IN_SYS_IOCTL
# include <sys/ioctl.h>
#endif

#define _(msgid) gettext (msgid)


/* Determines the size of the terminal, if possible, or at least
   takes an educated guess. */
void
terminal_check_size (void)
{
  int view_width = 0;
  int view_length = 0;

  struct winsize ws;
  if (0 == ioctl (0, TIOCGWINSZ, &ws))
    {
      view_width = ws.ws_col;
      view_length = ws.ws_row;
    }
  else
    {
      if (view_width <= 0 && getenv ("COLUMNS") != NULL)
	view_width = atoi (getenv ("COLUMNS"));

      if (view_length <= 0 && getenv ("LINES") != NULL)
	view_length = atoi (getenv ("LINES"));
    }

  if (view_width > 0)
    settings_set_viewwidth (view_width);

  if (view_length > 0)
    settings_set_viewlength (view_length);
}
