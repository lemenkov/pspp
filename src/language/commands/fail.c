/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include <stdlib.h>

#include "data/case.h"
#include "data/dataset.h"
#include "data/transformations.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "libpspp/message.h"

/* A transformation which is guaranteed to fail. */

static enum trns_result
trns_fail (void *x UNUSED, struct ccase **c UNUSED,
           casenumber n UNUSED)
{
  msg (SE, "DEBUG XFORM FAIL transformation executed");
  return TRNS_ERROR;
}

int
cmd_debug_xform_fail (struct lexer *lexer UNUSED, struct dataset *ds)
{
  static const struct trns_class fail_trns_class = {
    .name = "DEBUG XFORM FAIL",
    .execute = trns_fail
  };
  add_transformation (ds, &fail_trns_class, NULL);
  return CMD_SUCCESS;
}
