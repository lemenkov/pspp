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

#include "ui/syntax-gen.h"

#include <stdio.h>

static void
test_runner (const char *format, ...)
{
  struct string syntax;
  va_list args;
  va_start (args, format);

  ds_init_empty (&syntax);

  syntax_gen_pspp_valist (&syntax, format, args);

  va_end (args);

  puts (ds_cstr (&syntax));

  ds_destroy (&syntax);
}

int
main (void)
{
  test_runner ("A simple string: %ssEND", "Hello world");
  test_runner ("A syntax string: %sqEND", "Hello world");
  test_runner ("A syntax string containing \": %sqEND", "here\"is the quote");
  test_runner ("A syntax string containing non-printables: %sqEND", "A CtrlLchar");
  test_runner ("An integer: %dEND", 98765);
  test_runner ("A floating point number: %gEND", 3.142);
  test_runner ("A floating point number with default precision: %fEND", 1.234);
  test_runner ("A floating point number with given precision: %.20fEND", 1.234);
  test_runner ("A literal %%");

  test_runner ("and %ss a %sq of %d different %f examples %g of 100%% conversions.",
               "finally", "concatination", 6, 20.309, 23.09);

  return 0;
}
