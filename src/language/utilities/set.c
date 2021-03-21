/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012, 2013, 2014, 2015 Free Software Foundation, Inc.

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

#include <float.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "gl/vasnprintf.h"

#include "data/casereader.h"
#include "data/data-in.h"
#include "data/data-out.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/settings.h"
#include "data/value.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/copyleft.h"
#include "libpspp/temp-file.h"
#include "libpspp/version.h"
#include "libpspp/float-format.h"
#include "libpspp/i18n.h"
#include "libpspp/integer-format.h"
#include "libpspp/message.h"
#include "math/random.h"
#include "output/driver.h"
#include "output/journal.h"
#include "output/pivot-table.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static bool
match_subcommand (struct lexer *lexer, const char *name)
{
  if (lex_match_id (lexer, name))
    {
      lex_match (lexer, T_EQUALS);
      return true;
    }
  else
    return false;
}

static int
parse_enum_valist (struct lexer *lexer, va_list args)
{
  for (;;)
    {
      const char *name = va_arg (args, char *);
      if (!name)
        return -1;
      int value = va_arg (args, int);

      if (lex_match_id (lexer, name))
        return value;
    }
}

#define parse_enum(...) parse_enum (__VA_ARGS__, NULL_SENTINEL)
static int SENTINEL(0)
(parse_enum) (struct lexer *lexer, ...)
{
  va_list args;

  va_start (args, lexer);
  int retval = parse_enum_valist (lexer, args);
  va_end (args);

  return retval;
}

#define force_parse_enum(...) force_parse_enum (__VA_ARGS__, NULL_SENTINEL)
static int SENTINEL(0)
(force_parse_enum) (struct lexer *lexer, ...)
{
  va_list args;

  va_start (args, lexer);
  int retval = parse_enum_valist (lexer, args);
  va_end (args);

  if (retval == -1)
    {
      enum { MAX_OPTIONS = 9 };
      const char *options[MAX_OPTIONS];
      int n = 0;

      va_start (args, lexer);
      while (n < MAX_OPTIONS)
        {
          const char *name = va_arg (args, char *);
          if (!name)
            break;
          va_arg (args, int);

          options[n++] = name;
        }
      va_end (args);

      lex_error_expecting_array (lexer, options, n);
    }

  return retval;
}

static int
parse_bool (struct lexer *lexer)
{
  return parse_enum (lexer,
                     "ON", true, "YES", true,
                     "OFF", false, "NO", false);
}

static int
force_parse_bool (struct lexer *lexer)
{
  return force_parse_enum (lexer,
                           "ON", true, "YES", true,
                           "OFF", false, "NO", false);
}

static bool
force_parse_int (struct lexer *lexer, int *integerp)
{
  if (!lex_force_int (lexer))
    return false;
  *integerp = lex_integer (lexer);
  lex_get (lexer);
  return true;
}

static bool
parse_output_routing (struct lexer *lexer, enum settings_output_type type)
{
  enum settings_output_devices devices;
  if (lex_match_id (lexer, "ON") || lex_match_id (lexer, "BOTH"))
    devices = SETTINGS_DEVICE_LISTING | SETTINGS_DEVICE_TERMINAL;
  else if (lex_match_id (lexer, "TERMINAL"))
    devices = SETTINGS_DEVICE_TERMINAL;
  else if (lex_match_id (lexer, "LISTING"))
    devices = SETTINGS_DEVICE_LISTING;
  else if (lex_match_id (lexer, "OFF") || lex_match_id (lexer, "NONE"))
    devices = 0;
  else
    {
      lex_error (lexer, NULL);
      return false;
    }

  settings_set_output_routing (type, devices);

  return true;
}

static bool
parse_integer_format (struct lexer *lexer,
                      void (*set_format) (enum integer_format))
{
  int value = force_parse_enum (lexer,
                                "MSBFIRST", INTEGER_MSB_FIRST,
                                "LSBFIRST", INTEGER_LSB_FIRST,
                                "VAX", INTEGER_VAX,
                                "NATIVE", INTEGER_NATIVE);
  if (value >= 0)
    set_format (value);
  return value >= 0;
}

static bool
parse_real_format (struct lexer *lexer,
                   void (*set_format) (enum float_format))
{
  int value = force_parse_enum (lexer,
                                "NATIVE", FLOAT_NATIVE_DOUBLE,
                                "ISL", FLOAT_IEEE_SINGLE_LE,
                                "ISB", FLOAT_IEEE_SINGLE_BE,
                                "IDL", FLOAT_IEEE_DOUBLE_LE,
                                "IDB", FLOAT_IEEE_DOUBLE_BE,
                                "VF", FLOAT_VAX_F,
                                "VD", FLOAT_VAX_D,
                                "VG", FLOAT_VAX_G,
                                "ZS", FLOAT_Z_SHORT,
                                "ZL", FLOAT_Z_LONG);
  if (value >= 0)
    set_format (value);
  return value >= 0;
}

static bool
parse_unimplemented (struct lexer *lexer, const char *name)
{
  msg (SW, _("%s is not yet implemented."), name);
  if (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
    lex_get (lexer);
  return true;
}

static bool
parse_ccx (struct lexer *lexer, enum fmt_type ccx)
{
  if (!lex_force_string (lexer))
    return false;

  settings_set_cc (lex_tokcstr (lexer), ccx);
  lex_get (lexer);
  return true;
}

static bool
parse_BASETEXTDIRECTION (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "BASETEXTDIRECTION");
}

static bool
parse_BLANKS (struct lexer *lexer)
{
  if (lex_match_id (lexer, "SYSMIS"))
    settings_set_blanks (SYSMIS);
  else
    {
      if (!lex_force_num (lexer))
        return false;
      settings_set_blanks (lex_number (lexer));
      lex_get (lexer);
    }
  return true;
}

static bool
parse_BLOCK (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "BLOCK");
}

static bool
parse_BOX (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "BOX");
}

static bool
parse_CACHE (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "CACHE");
}

static bool
parse_CCA (struct lexer *lexer)
{
  return parse_ccx (lexer, FMT_CCA);
}

static bool
parse_CCB (struct lexer *lexer)
{
  return parse_ccx (lexer, FMT_CCB);
}

static bool
parse_CCC (struct lexer *lexer)
{
  return parse_ccx (lexer, FMT_CCC);
}

static bool
parse_CCD (struct lexer *lexer)
{
  return parse_ccx (lexer, FMT_CCD);
}

static bool
parse_CCE (struct lexer *lexer)
{
  return parse_ccx (lexer, FMT_CCE);
}

static bool
parse_CELLSBREAK (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "CELLSBREAK");
}

static bool
parse_CMPTRANS (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "CMPTRANS");
}

static bool
parse_COMPRESSION (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "COMPRESSION");
}

static bool
parse_CTEMPLATE (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "CTEMPLATE");
}

static bool
parse_DECIMAL (struct lexer *lexer)
{
  int decimal_char = force_parse_enum (lexer,
                                       "DOT", '.',
                                       "COMMA", ',');
  if (decimal_char != -1)
    settings_set_decimal_char (decimal_char);
  return decimal_char != -1;
}

static bool
parse_EPOCH (struct lexer *lexer)
{
  if (lex_match_id (lexer, "AUTOMATIC"))
    settings_set_epoch (-1);
  else if (lex_is_integer (lexer))
    {
      int new_epoch = lex_integer (lexer);
      lex_get (lexer);
      if (new_epoch < 1500)
        {
          msg (SE, _("%s must be 1500 or later."), "EPOCH");
          return false;
        }
      settings_set_epoch (new_epoch);
    }
  else
    {
      lex_error (lexer, _("expecting %s or year"), "AUTOMATIC");
      return false;
    }

  return true;
}

static bool
parse_ERRORS (struct lexer *lexer)
{
  return parse_output_routing (lexer, SETTINGS_OUTPUT_ERROR);
}

static bool
parse_FORMAT (struct lexer *lexer)
{
  struct fmt_spec fmt;

  lex_match (lexer, T_EQUALS);
  if (!parse_format_specifier (lexer, &fmt))
    return false;

  if (!fmt_check_output (&fmt))
    return false;

  if (fmt_is_string (fmt.type))
    {
      char str[FMT_STRING_LEN_MAX + 1];
      msg (SE, _("%s requires numeric output format as an argument.  "
		 "Specified format %s is of type string."),
	   "FORMAT",
	   fmt_to_string (&fmt, str));
      return false;
    }

  settings_set_format (&fmt);
  return true;
}

static bool
parse_FUZZBITS (struct lexer *lexer)
{
  if (!lex_force_int (lexer))
    return false;
  int fuzzbits = lex_integer (lexer);
  lex_get (lexer);

  if (fuzzbits >= 0 && fuzzbits <= 20)
    settings_set_fuzzbits (fuzzbits);
  else
    msg (SE, _("%s must be between 0 and 20."), "FUZZBITS");
  return true;
}

static bool
parse_HEADER (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "HEADER");
}

static bool
parse_INCLUDE (struct lexer *lexer)
{
  int include = force_parse_bool (lexer);
  if (include != -1)
    settings_set_include (include);
  return include != -1;
}

static bool
parse_JOURNAL (struct lexer *lexer)
{
  int b = parse_bool (lexer);
  if (b == true)
    journal_enable ();
  else if (b == false)
    journal_disable ();
  else if (lex_is_string (lexer) || lex_token (lexer) == T_ID)
    {
      char *filename = utf8_to_filename (lex_tokcstr (lexer));
      journal_set_file_name (filename);
      free (filename);

      lex_get (lexer);
    }
  else
    {
      lex_error (lexer, NULL);
      return false;
    }
  return true;
}

static bool
parse_LENGTH (struct lexer *lexer)
{
  int page_length;

  if (lex_match_id (lexer, "NONE"))
    page_length = -1;
  else
    {
      if (!lex_force_int (lexer))
	return false;
      if (lex_integer (lexer) < 1)
	{
	  msg (SE, _("%s must be at least %d."), "LENGTH", 1);
	  return false;
	}
      page_length = lex_integer (lexer);
      lex_get (lexer);
    }

  if (page_length != -1)
    settings_set_viewlength (page_length);

  return true;
}

static bool
parse_LOCALE (struct lexer *lexer)
{
  if (!lex_force_string (lexer))
    return false;

  /* Try the argument as an encoding name, then as a locale name or alias. */
  const char *s = lex_tokcstr (lexer);
  if (valid_encoding (s))
    set_default_encoding (s);
  else if (!set_encoding_from_locale (s))
    {
      msg (ME, _("%s is not a recognized encoding or locale name"), s);
      return false;
    }

  lex_get (lexer);
  return true;
}

static bool
parse_MESSAGES (struct lexer *lexer)
{
  return parse_output_routing (lexer, SETTINGS_OUTPUT_NOTE);
}

static bool
parse_MEXPAND (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "MEXPAND");
}

static bool
parse_MITERATE (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "MITERATE");
}

static bool
parse_MNEST (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "MNEST");
}

static bool
parse_MPRINT (struct lexer *lexer)
{
  return parse_unimplemented (lexer, "MPRINT");
}

static bool
parse_MXERRS (struct lexer *lexer)
{
  int n;
  if (!force_parse_int (lexer, &n))
    return false;

  if (n >= 1)
    settings_set_max_messages (MSG_S_ERROR, n);
  else
    msg (SE, _("%s must be at least 1."), "MXERRS");
  return true;
}

static bool
parse_MXLOOPS (struct lexer *lexer)
{
  int n;
  if (!force_parse_int (lexer, &n))
    return false;

  if (n >= 1)
    settings_set_mxloops (n);
  else
    msg (SE, _("%s must be at least 1."), "MXLOOPS");
  return true;
}

static bool
parse_MXWARNS (struct lexer *lexer)
{
  int n;
  if (!force_parse_int (lexer, &n))
    return false;

  if (n >= 0)
    settings_set_max_messages (MSG_S_WARNING, n);
  else
    msg (SE, _("%s must not be negative."), "MXWARNS");
  return true;
}

static bool
parse_PRINTBACK (struct lexer *lexer)
{
  return parse_output_routing (lexer, SETTINGS_OUTPUT_SYNTAX);
}

static bool
parse_RESULTS (struct lexer *lexer)
{
  return parse_output_routing (lexer, SETTINGS_OUTPUT_RESULT);
}

static bool
parse_RIB (struct lexer *lexer)
{
  return parse_integer_format (lexer, settings_set_input_integer_format);
}

static bool
parse_RRB (struct lexer *lexer)
{
  return parse_real_format (lexer, settings_set_input_float_format);
}

static bool
parse_SAFER (struct lexer *lexer)
{
  bool ok = force_parse_enum (lexer, "ON", true, "YES", true) != -1;
  if (ok)
    settings_set_safer_mode ();
  return ok;
}

static bool
parse_SCOMPRESSION (struct lexer *lexer)
{
  int value = force_parse_bool (lexer);
  if (value >= 0)
    settings_set_scompression (value);
  return value >= 0;
}

static bool
parse_SEED (struct lexer *lexer)
{
  if (lex_match_id (lexer, "RANDOM"))
    set_rng (time (0));
  else
    {
      if (!lex_force_num (lexer))
	return false;
      set_rng (lex_number (lexer));
      lex_get (lexer);
    }

  return true;
}

static bool
parse_SMALL (struct lexer *lexer)
{
  if (!lex_force_num (lexer))
    return false;
  settings_set_small (lex_number (lexer));
  lex_get (lexer);
  return true;
}

static bool
parse_TNUMBERS (struct lexer *lexer)
{
  int value = force_parse_enum (lexer,
                                "LABELS", SETTINGS_VALUE_SHOW_LABEL,
                                "VALUES", SETTINGS_VALUE_SHOW_VALUE,
                                "BOTH", SETTINGS_VALUE_SHOW_BOTH);
  if (value >= 0)
    settings_set_show_values (value);
  return value >= 0;
}

static bool
parse_TVARS (struct lexer *lexer)
{
  int value = force_parse_enum (lexer,
                                "LABELS", SETTINGS_VALUE_SHOW_LABEL,
                                "NAMES", SETTINGS_VALUE_SHOW_VALUE,
                                "BOTH", SETTINGS_VALUE_SHOW_BOTH);
  if (value >= 0)
    settings_set_show_variables (value);
  return value >= 0;
}

static bool
parse_TLOOK (struct lexer *lexer)
{
  if (lex_match_id (lexer, "NONE"))
    pivot_table_look_set_default (pivot_table_look_builtin_default ());
  else if (lex_is_string (lexer))
    {
      struct pivot_table_look *look;
      char *error = pivot_table_look_read (lex_tokcstr (lexer), &look);
      lex_get (lexer);

      if (error)
        {
          msg (SE, "%s", error);
          free (error);
          return false;
        }

      pivot_table_look_set_default (look);
      pivot_table_look_unref (look);
    }

  return true;
}

static bool
parse_UNDEFINED (struct lexer *lexer)
{
  int value = force_parse_enum (lexer,
                                "WARN", true,
                                "NOWARN", false);
  if (value >= 0)
    settings_set_undefined (value);
  return value >= 0;
}

static bool
parse_WIB (struct lexer *lexer)
{
  return parse_integer_format (lexer, settings_set_output_integer_format);
}

static bool
parse_WRB (struct lexer *lexer)
{
  return parse_real_format (lexer, settings_set_output_float_format);
}

static bool
parse_WIDTH (struct lexer *lexer)
{
  if (lex_match_id (lexer, "NARROW"))
    settings_set_viewwidth (79);
  else if (lex_match_id (lexer, "WIDE"))
    settings_set_viewwidth (131);
  else
    {
      if (!lex_force_int (lexer))
	return 0;
      if (lex_integer (lexer) < 40)
	{
	  msg (SE, _("%s must be at least %d."), "WIDTH", 40);
	  return 0;
	}
      settings_set_viewwidth (lex_integer (lexer));
      lex_get (lexer);
    }

  return 1;
}

static bool
parse_WORKSPACE (struct lexer *lexer)
{
  if (!lex_force_int (lexer))
    return false;
  int workspace = lex_integer (lexer);
  lex_get (lexer);

  if (workspace < 1024 && !settings_get_testing_mode ())
    msg (SE, _("%s must be at least 1MB"), "WORKSPACE");
  else if (workspace <= 0)
    msg (SE, _("%s must be positive"), "WORKSPACE");
  else
    settings_set_workspace (workspace * 1024L);
  return true;
}

static bool
parse_setting (struct lexer *lexer)
{
  struct setting
    {
      const char *name;
      bool (*function) (struct lexer *);
    };
  const struct setting settings[] = {
    { "BASETEXTDIRECTION", parse_BASETEXTDIRECTION },
    { "BLANKS", parse_BLANKS },
    { "BLOCK", parse_BLOCK },
    { "BOX", parse_BOX },
    { "CACHE", parse_CACHE },
    { "CCA", parse_CCA },
    { "CCB", parse_CCB },
    { "CCC", parse_CCC },
    { "CCD", parse_CCD },
    { "CCE", parse_CCE },
    { "CELLSBREAK", parse_CELLSBREAK },
    { "CMPTRANS", parse_CMPTRANS },
    { "COMPRESSION", parse_COMPRESSION },
    { "CTEMPLATE", parse_CTEMPLATE },
    { "DECIMAL", parse_DECIMAL },
    { "EPOCH", parse_EPOCH },
    { "ERRORS", parse_ERRORS },
    { "FORMAT", parse_FORMAT },
    { "FUZZBITS", parse_FUZZBITS },
    { "HEADER", parse_HEADER },
    { "INCLUDE", parse_INCLUDE },
    { "JOURNAL", parse_JOURNAL },
    { "LENGTH", parse_LENGTH },
    { "LOCALE", parse_LOCALE },
    { "MESSAGES", parse_MESSAGES },
    { "MEXPAND", parse_MEXPAND },
    { "MITERATE", parse_MITERATE },
    { "MNEST", parse_MNEST },
    { "MPRINT", parse_MPRINT },
    { "MXERRS", parse_MXERRS },
    { "MXLOOPS", parse_MXLOOPS },
    { "MXWARNS", parse_MXWARNS },
    { "PRINTBACK", parse_PRINTBACK },
    { "RESULTS", parse_RESULTS },
    { "RIB", parse_RIB },
    { "RRB", parse_RRB },
    { "SAFER", parse_SAFER },
    { "SCOMPRESSION", parse_SCOMPRESSION },
    { "SEED", parse_SEED },
    { "SMALL", parse_SMALL },
    { "TNUMBERS", parse_TNUMBERS },
    { "TVARS", parse_TVARS },
    { "TLOOK", parse_TLOOK },
    { "UNDEFINED", parse_UNDEFINED },
    { "WIB", parse_WIB },
    { "WRB", parse_WRB },
    { "WIDTH", parse_WIDTH },
    { "WORKSPACE", parse_WORKSPACE },
  };
  enum { N_SETTINGS = sizeof settings / sizeof *settings };

  for (size_t i = 0; i < N_SETTINGS; i++)
    if (match_subcommand (lexer, settings[i].name))
        return settings[i].function (lexer);

  lex_error (lexer, NULL);
  return false;
}

int
cmd_set (struct lexer *lexer, struct dataset *ds UNUSED)
{
  for (;;)
    {
      lex_match (lexer, T_SLASH);
      if (lex_token (lexer) == T_ENDCMD)
        break;

      if (!parse_setting (lexer))
        return CMD_FAILURE;
    }

  return CMD_SUCCESS;
}

static char *
show_output_routing (enum settings_output_type type)
{
  enum settings_output_devices devices;
  const char *s;

  devices = settings_get_output_routing (type);
  if (devices & SETTINGS_DEVICE_LISTING)
    s = devices & SETTINGS_DEVICE_TERMINAL ? "BOTH" : "LISTING";
  else if (devices & SETTINGS_DEVICE_TERMINAL)
    s = "TERMINAL";
  else
    s = "NONE";

  return xstrdup (s);
}

static char *
show_blanks (const struct dataset *ds UNUSED)
{
  return (settings_get_blanks () == SYSMIS
          ? xstrdup ("SYSMIS")
          : xasprintf ("%.*g", DBL_DIG + 1, settings_get_blanks ()));
}

static char *
show_cc (enum fmt_type type)
{
  return fmt_number_style_to_string (fmt_settings_get_style (
                                       settings_get_fmt_settings (), type));
}

static char *
show_cca (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCA);
}

static char *
show_ccb (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCB);
}

static char *
show_ccc (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCC);
}

static char *
show_ccd (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCD);
}

static char *
show_cce (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCE);
}

static char *
show_decimals (const struct dataset *ds UNUSED)
{
  return xasprintf ("`%c'", settings_get_fmt_settings ()->decimal);
}

static char *
show_errors (const struct dataset *ds UNUSED)
{
  return show_output_routing (SETTINGS_OUTPUT_ERROR);
}

static char *
show_format (const struct dataset *ds UNUSED)
{
  char str[FMT_STRING_LEN_MAX + 1];
  return xstrdup (fmt_to_string (settings_get_format (), str));
}

static char *
show_fuzzbits (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_fuzzbits ());
}

static char *
show_journal (const struct dataset *ds UNUSED)
{
  return (journal_is_enabled ()
          ? xasprintf ("\"%s\"", journal_get_file_name ())
          : xstrdup ("disabled"));
}

static char *
show_length (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_viewlength ());
}

static char *
show_locale (const struct dataset *ds UNUSED)
{
  return xstrdup (get_default_encoding ());
}

static char *
show_messages (const struct dataset *ds UNUSED)
{
  return show_output_routing (SETTINGS_OUTPUT_NOTE);
}

static char *
show_printback (const struct dataset *ds UNUSED)
{
  return show_output_routing (SETTINGS_OUTPUT_SYNTAX);
}

static char *
show_results (const struct dataset *ds UNUSED)
{
  return show_output_routing (SETTINGS_OUTPUT_RESULT);
}

static char *
show_mxerrs (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_max_messages (MSG_S_ERROR));
}

static char *
show_mxloops (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_mxloops ());
}

static char *
show_mxwarns (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_max_messages (MSG_S_WARNING));
}

/* Returns a name for the given INTEGER_FORMAT value. */
static char *
show_integer_format (enum integer_format integer_format)
{
  return xasprintf ("%s (%s)",
                    (integer_format == INTEGER_MSB_FIRST ? "MSBFIRST"
                     : integer_format == INTEGER_LSB_FIRST ? "LSBFIRST"
                     : "VAX"),
                    integer_format == INTEGER_NATIVE ? "NATIVE" : "nonnative");
}

/* Returns a name for the given FLOAT_FORMAT value. */
static char *
show_float_format (enum float_format float_format)
{
  const char *format_name = "";

  switch (float_format)
    {
    case FLOAT_IEEE_SINGLE_LE:
      format_name = _("ISL (32-bit IEEE 754 single, little-endian)");
      break;
    case FLOAT_IEEE_SINGLE_BE:
      format_name = _("ISB (32-bit IEEE 754 single, big-endian)");
      break;
    case FLOAT_IEEE_DOUBLE_LE:
      format_name = _("IDL (64-bit IEEE 754 double, little-endian)");
      break;
    case FLOAT_IEEE_DOUBLE_BE:
      format_name = _("IDB (64-bit IEEE 754 double, big-endian)");
      break;

    case FLOAT_VAX_F:
      format_name = _("VF (32-bit VAX F, VAX-endian)");
      break;
    case FLOAT_VAX_D:
      format_name = _("VD (64-bit VAX D, VAX-endian)");
      break;
    case FLOAT_VAX_G:
      format_name = _("VG (64-bit VAX G, VAX-endian)");
      break;

    case FLOAT_Z_SHORT:
      format_name = _("ZS (32-bit IBM Z hexadecimal short, big-endian)");
      break;
    case FLOAT_Z_LONG:
      format_name = _("ZL (64-bit IBM Z hexadecimal long, big-endian)");
      break;

    case FLOAT_FP:
    case FLOAT_HEX:
      NOT_REACHED ();
    }

  return xasprintf ("%s (%s)", format_name,
                    (float_format == FLOAT_NATIVE_DOUBLE
                     ? "NATIVE" : "nonnative"));
}

static char *
show_rib (const struct dataset *ds UNUSED)
{
  return show_integer_format (settings_get_input_integer_format ());
}

static char *
show_rrb (const struct dataset *ds UNUSED)
{
  return show_float_format (settings_get_input_float_format ());
}

static char *
show_scompression (const struct dataset *ds UNUSED)
{
  return xstrdup (settings_get_scompression () ? "ON" : "OFF");
}

static char *
show_undefined (const struct dataset *ds UNUSED)
{
  return xstrdup (settings_get_undefined () ? "WARN" : "NOWARN");
}

static char *
show_weight (const struct dataset *ds)
{
  const struct variable *var = dict_get_weight (dataset_dict (ds));
  return xstrdup (var != NULL ? var_get_name (var) : "OFF");
}

static char *
show_wib (const struct dataset *ds UNUSED)
{
  return show_integer_format (settings_get_output_integer_format ());
}

static char *
show_wrb (const struct dataset *ds UNUSED)
{
  return show_float_format (settings_get_output_float_format ());
}

static char *
show_width (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_viewwidth ());
}

static char *
show_workspace (const struct dataset *ds UNUSED)
{
  size_t ws = settings_get_workspace () / 1024L;
  return xasprintf ("%zu", ws);
}

static char *
show_current_directory (const struct dataset *ds UNUSED)
{
  char *buf = NULL;
  char *wd = NULL;
  size_t len = 256;

  do
    {
      len <<= 1;
      buf = xrealloc (buf, len);
    }
  while (NULL == (wd = getcwd (buf, len)));

  return wd;
}

static char *
show_tempdir (const struct dataset *ds UNUSED)
{
  return strdup (temp_dir_name ());
}

static char *
show_version (const struct dataset *ds UNUSED)
{
  return strdup (announced_version);
}

static char *
show_system (const struct dataset *ds UNUSED)
{
  return strdup (host_system);
}

static char *
show_n (const struct dataset *ds)
{
  casenumber n;
  size_t l;

  const struct casereader *reader = dataset_source (ds);

  if (reader == NULL)
    return strdup (_("Unknown"));

  n =  casereader_count_cases (reader);

  return  asnprintf (NULL, &l, "%ld", n);
}


struct show_sbc
  {
    const char *name;
    char *(*function) (const struct dataset *);
  };

const struct show_sbc show_table[] =
  {
    {"BLANKS", show_blanks},
    {"CCA", show_cca},
    {"CCB", show_ccb},
    {"CCC", show_ccc},
    {"CCD", show_ccd},
    {"CCE", show_cce},
    {"DECIMALS", show_decimals},
    {"DIRECTORY", show_current_directory},
    {"ENVIRONMENT", show_system},
    {"ERRORS", show_errors},
    {"FORMAT", show_format},
    {"FUZZBITS", show_fuzzbits},
    {"JOURNAL", show_journal},
    {"LENGTH", show_length},
    {"LOCALE", show_locale},
    {"MESSAGES", show_messages},
    {"MXERRS", show_mxerrs},
    {"MXLOOPS", show_mxloops},
    {"MXWARNS", show_mxwarns},
    {"N", show_n},
    {"PRINTBACk", show_printback},
    {"RESULTS", show_results},
    {"RIB", show_rib},
    {"RRB", show_rrb},
    {"SCOMPRESSION", show_scompression},
    {"TEMPDIR", show_tempdir},
    {"UNDEFINED", show_undefined},
    {"VERSION", show_version},
    {"WEIGHT", show_weight},
    {"WIB", show_wib},
    {"WRB", show_wrb},
    {"WIDTH", show_width},
    {"WORKSPACE", show_workspace},
  };

static void
do_show (const struct dataset *ds, const struct show_sbc *sbc)
{
  char *value = sbc->function (ds);
  msg (SN, _("%s is %s."), sbc->name, value);
  free (value);
}

static void
show_all (const struct dataset *ds)
{
  size_t i;

  for (i = 0; i < sizeof show_table / sizeof *show_table; i++)
    do_show (ds, &show_table[i]);
}

static void
show_all_cc (const struct dataset *ds)
{
  int i;

  for (i = 0; i < sizeof show_table / sizeof *show_table; i++)
    {
      const struct show_sbc *sbc = &show_table[i];
      if (!strncmp (sbc->name, "CC", 2))
        do_show (ds, sbc);
    }
}

static void
show_warranty (const struct dataset *ds UNUSED)
{
  fputs (lack_of_warranty, stdout);
}

static void
show_copying (const struct dataset *ds UNUSED)
{
  fputs (copyleft, stdout);
}


int
cmd_show (struct lexer *lexer, struct dataset *ds)
{
  if (lex_token (lexer) == T_ENDCMD)
    {
      show_all (ds);
      return CMD_SUCCESS;
    }

  do
    {
      if (lex_match (lexer, T_ALL))
        show_all (ds);
      else if (lex_match_id (lexer, "CC"))
        show_all_cc (ds);
      else if (lex_match_id (lexer, "WARRANTY"))
        show_warranty (ds);
      else if (lex_match_id (lexer, "COPYING") || lex_match_id (lexer, "LICENSE"))
        show_copying (ds);
      else if (lex_token (lexer) == T_ID)
        {
          int i;

          for (i = 0; i < sizeof show_table / sizeof *show_table; i++)
            {
              const struct show_sbc *sbc = &show_table[i];
              if (lex_match_id (lexer, sbc->name))
                {
                  do_show (ds, sbc);
                  goto found;
                }
              }
          lex_error (lexer, NULL);
          return CMD_FAILURE;

        found: ;
        }
      else
        {
          lex_error (lexer, NULL);
          return CMD_FAILURE;
        }

      lex_match (lexer, T_SLASH);
    }
  while (lex_token (lexer) != T_ENDCMD);

  return CMD_SUCCESS;
}

#define MAX_SAVED_SETTINGS 5

static struct settings *saved_settings[MAX_SAVED_SETTINGS];
static int n_saved_settings;

int
cmd_preserve (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  if (n_saved_settings < MAX_SAVED_SETTINGS)
    {
      saved_settings[n_saved_settings++] = settings_get ();
      return CMD_SUCCESS;
    }
  else
    {
      msg (SE, _("Too many %s commands without a %s: at most "
                 "%d levels of saved settings are allowed."),
	   "PRESERVE", "RESTORE",
           MAX_SAVED_SETTINGS);
      return CMD_CASCADING_FAILURE;
    }
}

int
cmd_restore (struct lexer *lexer UNUSED, struct dataset *ds UNUSED)
{
  if (n_saved_settings > 0)
    {
      struct settings *s = saved_settings[--n_saved_settings];
      settings_set (s);
      settings_destroy (s);
      return CMD_SUCCESS;
    }
  else
    {
      msg (SE, _("%s without matching %s."), "RESTORE", "PRESERVE");
      return CMD_FAILURE;
    }
}

/*
   Local Variables:
   mode: c
   End:
*/
