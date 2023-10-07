/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2023 Free Software Foundation, Inc.

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

#include "data/casereader.h"
#include "data/data-in.h"
#include "data/data-out.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/format.h"
#include "data/identifier.h"
#include "data/settings.h"
#include "data/value.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/token.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/copyleft.h"
#include "libpspp/temp-file.h"
#include "libpspp/version.h"
#include "libpspp/float-format.h"
#include "libpspp/i18n.h"
#include "libpspp/integer-format.h"
#include "libpspp/message.h"
#include "libpspp/string-array.h"
#include "math/random.h"
#include "output/driver.h"
#include "output/journal.h"
#include "output/pivot-table.h"

#include "gl/ftoastr.h"
#include "gl/minmax.h"
#include "gl/relocatable.h"
#include "gl/vasnprintf.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) (msgid)

struct setting
  {
    const char *name;
    bool (*set) (struct lexer *);
    char *(*show) (const struct dataset *);
  };

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
subcommand_start_ofs (struct lexer *lexer)
{
  int ofs = lex_ofs (lexer) - 1;
  return lex_ofs_token (lexer, ofs)->type == T_EQUALS ? ofs - 1 : ofs;
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
      lex_error_expecting (lexer, "ON", "BOTH", "TERMINAL", "LISTING",
                           "OFF", "NONE");
      return false;
    }

  settings_set_output_routing (type, devices);

  return true;
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

/* Returns a name for the given FLOAT_FORMAT value. */
static char *
show_real_format (enum float_format float_format)
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

static bool
parse_unimplemented (struct lexer *lexer, const char *name)
{
  int start = subcommand_start_ofs (lexer);
  if (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD)
    lex_get (lexer);
  int end = lex_ofs (lexer) - 1;

  lex_ofs_msg (lexer, SW, start, end, _("%s is not yet implemented."), name);
  return true;
}

static bool
parse_ccx (struct lexer *lexer, enum fmt_type ccx)
{
  if (!lex_force_string (lexer))
    return false;

  char *error = settings_set_cc (lex_tokcstr (lexer), ccx);
  if (error)
    {
      lex_error (lexer, "%s", error);
      free (error);
      return false;
    }

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

static char *
show_BLANKS (const struct dataset *ds UNUSED)
{
  return (settings_get_blanks () == SYSMIS
          ? xstrdup ("SYSMIS")
          : xasprintf ("%.*g", DBL_DIG + 1, settings_get_blanks ()));
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

static char *
show_cc (enum fmt_type type)
{
  return fmt_number_style_to_string (fmt_settings_get_style (
                                       settings_get_fmt_settings (), type));
}

static char *
show_CCA (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCA);
}

static char *
show_CCB (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCB);
}

static char *
show_CCC (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCC);
}

static char *
show_CCD (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCD);
}

static char *
show_CCE (const struct dataset *ds UNUSED)
{
  return show_cc (FMT_CCE);
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

static char *
show_DECIMAL (const struct dataset *ds UNUSED)
{
  return xasprintf ("`%c'", settings_get_fmt_settings ()->decimal);
}

static bool
parse_EPOCH (struct lexer *lexer)
{
  if (lex_match_id (lexer, "AUTOMATIC"))
    settings_set_epoch (-1);
  else if (lex_is_integer (lexer))
    {
      if (!lex_force_int_range (lexer, "EPOCH", 1500, INT_MAX))
        return false;
      settings_set_epoch (lex_integer (lexer));
      lex_get (lexer);
    }
  else
    {
      lex_error (lexer, _("Syntax error expecting %s or year."), "AUTOMATIC");
      return false;
    }

  return true;
}

static char *
show_EPOCH (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_epoch ());
}

static bool
parse_ERRORS (struct lexer *lexer)
{
  return parse_output_routing (lexer, SETTINGS_OUTPUT_ERROR);
}

static char *
show_ERRORS (const struct dataset *ds UNUSED)
{
  return show_output_routing (SETTINGS_OUTPUT_ERROR);
}

static bool
parse_FORMAT (struct lexer *lexer)
{
  int start = subcommand_start_ofs (lexer);
  struct fmt_spec fmt;

  if (!parse_format_specifier (lexer, &fmt))
    return false;

  char *error = fmt_check_output__ (fmt);
  if (error)
    {
      lex_next_error (lexer, -1, -1, "%s", error);
      free (error);
      return false;
    }

  int end = lex_ofs (lexer) - 1;
  if (fmt_is_string (fmt.type))
    {
      char str[FMT_STRING_LEN_MAX + 1];
      lex_ofs_error (lexer, start, end,
                     _("%s requires numeric output format as an argument.  "
                       "Specified format %s is of type string."),
                     "FORMAT", fmt_to_string (fmt, str));
      return false;
    }

  settings_set_format (fmt);
  return true;
}

static char *
show_FORMAT (const struct dataset *ds UNUSED)
{
  char str[FMT_STRING_LEN_MAX + 1];
  return xstrdup (fmt_to_string (settings_get_format (), str));
}

static bool
parse_FUZZBITS (struct lexer *lexer)
{
  if (!lex_force_int_range (lexer, "FUZZBITS", 0, 20))
    return false;
  settings_set_fuzzbits (lex_integer (lexer));
  lex_get (lexer);
  return true;
}

static char *
show_FUZZBITS (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_fuzzbits ());
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

static char *
show_INCLUDE (const struct dataset *ds UNUSED)
{
  return xstrdup (settings_get_include () ? "ON" : "OFF");
}

static bool
parse_JOURNAL (struct lexer *lexer)
{
  do
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
          lex_error (lexer, _("Syntax error expecting ON or OFF or a file name."));
          return false;
        }
    }
  while (lex_token (lexer) != T_SLASH && lex_token (lexer) != T_ENDCMD);
  return true;
}

static char *
show_JOURNAL (const struct dataset *ds UNUSED)
{
  const char *enabled = journal_is_enabled () ? "ON" : "OFF";
  const char *file_name = journal_get_file_name ();
  return (file_name
          ? xasprintf ("%s (%s)", enabled, file_name)
          : xstrdup (enabled));
}

static bool
parse_LEADZERO (struct lexer *lexer)
{
  int leadzero = force_parse_bool (lexer);
  if (leadzero != -1)
    settings_set_include_leading_zero (leadzero);
  return leadzero != -1;
}

static char *
show_LEADZERO (const struct dataset *ds UNUSED)
{
  bool leadzero = settings_get_fmt_settings ()->include_leading_zero;
  return xstrdup (leadzero ? "ON" : "OFF");
}

static bool
parse_LENGTH (struct lexer *lexer)
{
  int page_length;

  if (lex_match_id (lexer, "NONE"))
    page_length = -1;
  else
    {
      if (!lex_force_int_range (lexer, "LENGTH", 1, INT_MAX))
        return false;
      page_length = lex_integer (lexer);
      lex_get (lexer);
    }

  if (page_length != -1)
    settings_set_viewlength (page_length);

  return true;
}

static char *
show_LENGTH (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_viewlength ());
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
      lex_error (lexer, _("%s is not a recognized encoding or locale name"), s);
      return false;
    }

  lex_get (lexer);
  return true;
}

static char *
show_LOCALE (const struct dataset *ds UNUSED)
{
  return xstrdup (get_default_encoding ());
}

static bool
parse_MDISPLAY (struct lexer *lexer)
{
  int mdisplay = force_parse_enum (lexer,
                                   "TEXT", SETTINGS_MDISPLAY_TEXT,
                                   "TABLES", SETTINGS_MDISPLAY_TABLES);
  if (mdisplay >= 0)
    settings_set_mdisplay (mdisplay);
  return mdisplay >= 0;
}

static char *
show_MDISPLAY (const struct dataset *ds UNUSED)
{
  return xstrdup (settings_get_mdisplay () == SETTINGS_MDISPLAY_TEXT
                  ? "TEXT" : "TABLES");
}

static bool
parse_MESSAGES (struct lexer *lexer)
{
  return parse_output_routing (lexer, SETTINGS_OUTPUT_NOTE);
}

static char *
show_MESSAGES (const struct dataset *ds UNUSED)
{
  return show_output_routing (SETTINGS_OUTPUT_NOTE);
}

static bool
parse_MEXPAND (struct lexer *lexer)
{
  int mexpand = force_parse_bool (lexer);
  if (mexpand != -1)
    settings_set_mexpand (mexpand);
  return mexpand != -1;
}

static char *
show_MEXPAND (const struct dataset *ds UNUSED)
{
  return xstrdup (settings_get_mexpand () ? "ON" : "OFF");
}

static bool
parse_MITERATE (struct lexer *lexer)
{
  if (!lex_force_int_range (lexer, "MITERATE", 1, INT_MAX))
    return false;
  settings_set_miterate (lex_integer (lexer));
  lex_get (lexer);
  return true;
}

static char *
show_MITERATE (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_miterate ());
}

static bool
parse_MNEST (struct lexer *lexer)
{
  if (!lex_force_int_range (lexer, "MNEST", 1, INT_MAX))
    return false;
  settings_set_mnest (lex_integer (lexer));
  lex_get (lexer);
  return true;
}

static char *
show_MNEST (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_mnest ());
}

static bool
parse_MPRINT (struct lexer *lexer)
{
  int mprint = force_parse_bool (lexer);
  if (mprint != -1)
    settings_set_mprint (mprint);
  return mprint != -1;
}

static char *
show_MPRINT (const struct dataset *ds UNUSED)
{
  return xstrdup (settings_get_mprint () ? "ON" : "OFF");
}

static bool
parse_MXERRS (struct lexer *lexer)
{
  if (!lex_force_int_range (lexer, "MXERRS", 1, INT_MAX))
    return false;
  settings_set_max_messages (MSG_S_ERROR, lex_integer (lexer));
  lex_get (lexer);
  return true;
}

static char *
show_MXERRS (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_max_messages (MSG_S_ERROR));
}

static bool
parse_MXLOOPS (struct lexer *lexer)
{
  if (!lex_force_int_range (lexer, "MXLOOPS", 1, INT_MAX))
    return false;
  settings_set_mxloops (lex_integer (lexer));
  lex_get (lexer);
  return true;
}

static char *
show_MXLOOPS (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_mxloops ());
}

static bool
parse_MXWARNS (struct lexer *lexer)
{
  if (!lex_force_int_range (lexer, "MXWARNS", 0, INT_MAX))
    return false;
  settings_set_max_messages (MSG_S_WARNING, lex_integer (lexer));
  lex_get (lexer);
  return true;
}

static char *
show_MXWARNS (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_max_messages (MSG_S_WARNING));
}

static bool
parse_PRINTBACK (struct lexer *lexer)
{
  return parse_output_routing (lexer, SETTINGS_OUTPUT_SYNTAX);
}

static char *
show_PRINTBACK (const struct dataset *ds UNUSED)
{
  return show_output_routing (SETTINGS_OUTPUT_SYNTAX);
}

static bool
parse_RESULTS (struct lexer *lexer)
{
  return parse_output_routing (lexer, SETTINGS_OUTPUT_RESULT);
}

static char *
show_RESULTS (const struct dataset *ds UNUSED)
{
  return show_output_routing (SETTINGS_OUTPUT_RESULT);
}

static bool
parse_RIB (struct lexer *lexer)
{
  return parse_integer_format (lexer, settings_set_input_integer_format);
}

static char *
show_RIB (const struct dataset *ds UNUSED)
{
  return show_integer_format (settings_get_input_integer_format ());
}

static bool
parse_RRB (struct lexer *lexer)
{
  return parse_real_format (lexer, settings_set_input_float_format);
}

static char *
show_RRB (const struct dataset *ds UNUSED)
{
  return show_real_format (settings_get_input_float_format ());
}

static bool
parse_SAFER (struct lexer *lexer)
{
  bool ok = force_parse_enum (lexer, "ON", true, "YES", true) != -1;
  if (ok)
    settings_set_safer_mode ();
  return ok;
}

static char *
show_SAFER (const struct dataset *ds UNUSED)
{
  return xstrdup (settings_get_safer_mode () ? "ON" : "OFF");
}

static bool
parse_SCOMPRESSION (struct lexer *lexer)
{
  int value = force_parse_bool (lexer);
  if (value >= 0)
    settings_set_scompression (value);
  return value >= 0;
}

static char *
show_SCOMPRESSION (const struct dataset *ds UNUSED)
{
  return xstrdup (settings_get_scompression () ? "ON" : "OFF");
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
parse_SUMMARY (struct lexer *lexer)
{
  lex_match (lexer, T_EQUALS);

  if (lex_match_id (lexer, "NONE"))
    {
      settings_set_summary (NULL);
      return true;
    }

  if (!lex_force_string (lexer))
    return false;

  const char *s = lex_tokcstr (lexer);
  settings_set_summary (s);
  lex_get (lexer);

  return true;
}

static char *
show_SUMMARY (const struct dataset *ds UNUSED)
{
  return settings_get_summary ();
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

static char *
show_SMALL (const struct dataset *ds UNUSED)
{
  char buf[DBL_BUFSIZE_BOUND];
  if (dtoastr (buf, sizeof buf, 0, 0, settings_get_small ()) < 0)
    abort ();
  return xstrdup (buf);
}

static char *
show_SPLIT (const struct dataset *ds)
{
  const struct dictionary *dict = dataset_dict (ds);

  const char *type;
  switch (dict_get_split_type (dict))
    {
    case SPLIT_NONE:
      return xstrdup ("none");

    case SPLIT_SEPARATE:
      type = "SEPARATE";
      break;

    case SPLIT_LAYERED:
      type = "LAYERED";
      break;

    default:
      NOT_REACHED ();
    }

  struct string s = DS_EMPTY_INITIALIZER;

  size_t n = dict_get_n_splits (dict);
  const struct variable *const *vars = dict_get_split_vars (dict);
  for (size_t i = 0; i < n; i++)
    {
      if (i > 0)
        ds_put_cstr (&s, ", ");
      ds_put_cstr (&s, var_get_name (vars[i]));
    }
  ds_put_format (&s, " (%s)", type);
  return ds_steal_cstr (&s);
}

static char *
show_SUBTITLE (const struct dataset *ds UNUSED)
{
  return xstrdup (output_get_subtitle ());
}

static char *
show_TEMPDIR (const struct dataset *ds UNUSED)
{
  return xstrdup (temp_dir_name ());
}

static char *
show_TITLE (const struct dataset *ds UNUSED)
{
  return xstrdup (output_get_title ());
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

static char *
show_TNUMBERS (const struct dataset *ds UNUSED)
{
  enum settings_value_show tnumbers = settings_get_show_values ();
  return xstrdup (tnumbers == SETTINGS_VALUE_SHOW_LABEL ? "LABELS"
                  : tnumbers == SETTINGS_VALUE_SHOW_VALUE ? "VALUES"
                  : "BOTH");
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

static char *
show_TVARS (const struct dataset *ds UNUSED)
{
  enum settings_value_show tvars = settings_get_show_variables ();
  return xstrdup (tvars == SETTINGS_VALUE_SHOW_LABEL ? "LABELS"
                  : tvars == SETTINGS_VALUE_SHOW_VALUE ? "NAMES"
                  : "BOTH");
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

static char *
show_TLOOK (const struct dataset *ds UNUSED)
{
  const struct pivot_table_look *look = pivot_table_look_get_default ();
  return xstrdup (look->file_name ? look->file_name : "NONE");
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

static char *
show_UNDEFINED (const struct dataset *ds UNUSED)
{
  return xstrdup (settings_get_undefined () ? "WARN" : "NOWARN");
}

static char *
show_VERSION (const struct dataset *ds UNUSED)
{
  return strdup (announced_version);
}

static char *
show_WEIGHT (const struct dataset *ds)
{
  const struct variable *var = dict_get_weight (dataset_dict (ds));
  return xstrdup (var != NULL ? var_get_name (var) : "OFF");
}

static bool
parse_WIB (struct lexer *lexer)
{
  return parse_integer_format (lexer, settings_set_output_integer_format);
}

static char *
show_WIB (const struct dataset *ds UNUSED)
{
  return show_integer_format (settings_get_output_integer_format ());
}

static bool
parse_WRB (struct lexer *lexer)
{
  return parse_real_format (lexer, settings_set_output_float_format);
}

static char *
show_WRB (const struct dataset *ds UNUSED)
{
  return show_real_format (settings_get_output_float_format ());
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
      if (!lex_force_int_range (lexer, "WIDTH", 40, INT_MAX))
        return false;
      settings_set_viewwidth (lex_integer (lexer));
      lex_get (lexer);
    }

  return true;
}

static char *
show_WIDTH (const struct dataset *ds UNUSED)
{
  return xasprintf ("%d", settings_get_viewwidth ());
}

static bool
parse_WORKSPACE (struct lexer *lexer)
{
  if (!lex_force_int_range (lexer, "WORKSPACE",
                            settings_get_testing_mode () ? 1 : 1024,
                            INT_MAX))
    return false;
  int workspace = lex_integer (lexer);
  lex_get (lexer);
  settings_set_workspace (MIN (workspace, INT_MAX / 1024) * 1024);
  return true;
}

static char *
show_WORKSPACE (const struct dataset *ds UNUSED)
{
  size_t ws = settings_get_workspace () / 1024L;
  return xasprintf ("%zu", ws);
}

static char *
show_DIRECTORY (const struct dataset *ds UNUSED)
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
show_N (const struct dataset *ds)
{
  const struct casereader *reader = dataset_source (ds);
  return (reader
          ? xasprintf ("%lld", (long long int) casereader_count_cases (reader))
          : xstrdup (_("Unknown")));
}

static void
do_show (const struct dataset *ds, const struct setting *s,
         struct pivot_table **ptp)
{
  struct pivot_table *pt = *ptp;
  if (!pt)
    {
      pt = *ptp = pivot_table_create (N_("Settings"));
      pivot_dimension_create (pt, PIVOT_AXIS_ROW, N_("Setting"));
    }

  struct pivot_value *name = pivot_value_new_user_text (s->name, SIZE_MAX);
  char *text = s->show (ds);
  if (!text)
    text = xstrdup("empty");
  struct pivot_value *value = pivot_value_new_user_text_nocopy (text);

  int row = pivot_category_create_leaf (pt->dimensions[0]->root, name);
  pivot_table_put1 (pt, row, value);
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

static void
add_row (struct pivot_table *table, const char *attribute,
         const char *value)
{
  int row = pivot_category_create_leaf (table->dimensions[0]->root,
                                        pivot_value_new_text (attribute));
  if (value)
    pivot_table_put1 (table, row, pivot_value_new_user_text (value, -1));
}

static void
show_system (const struct dataset *ds UNUSED)
{
  struct pivot_table *table = pivot_table_create (N_("System Information"));
  pivot_dimension_create (table, PIVOT_AXIS_ROW, N_("Attribute"));

  add_row (table, N_("Version"), version);
  add_row (table, N_("Host System"), host_system);
  add_row (table, N_("Build System"), build_system);

  char *allocated;
  add_row (table, N_("Locale Directory"), relocate2 (locale_dir, &allocated));
  free (allocated);

  add_row (table, N_("Journal File"), journal_get_file_name ());

  add_row (table, N_("Compiler Version"),
#ifdef __VERSION__
           __VERSION__
#else
           "Unknown"
#endif
           );
  pivot_table_submit (table);
}

static const struct setting settings[] = {
  { "BASETEXTDIRECTION", parse_BASETEXTDIRECTION, NULL },
  { "BLANKS", parse_BLANKS, show_BLANKS },
  { "BLOCK", parse_BLOCK, NULL },
  { "BOX", parse_BOX, NULL },
  { "CACHE", parse_CACHE, NULL },
  { "CCA", parse_CCA, show_CCA },
  { "CCB", parse_CCB, show_CCB },
  { "CCC", parse_CCC, show_CCC },
  { "CCD", parse_CCD, show_CCD },
  { "CCE", parse_CCE, show_CCE },
  { "CELLSBREAK", parse_CELLSBREAK, NULL },
  { "CMPTRANS", parse_CMPTRANS, NULL },
  { "COMPRESSION", parse_COMPRESSION, NULL },
  { "CTEMPLATE", parse_CTEMPLATE, NULL },
  { "DECIMAL", parse_DECIMAL, show_DECIMAL },
  { "DIRECTORY", NULL, show_DIRECTORY },
  { "EPOCH", parse_EPOCH, show_EPOCH },
  { "ERRORS", parse_ERRORS, show_ERRORS },
  { "FORMAT", parse_FORMAT, show_FORMAT },
  { "FUZZBITS", parse_FUZZBITS, show_FUZZBITS },
  { "HEADER", parse_HEADER, NULL },
  { "INCLUDE", parse_INCLUDE, show_INCLUDE },
  { "JOURNAL", parse_JOURNAL, show_JOURNAL },
  { "LEADZERO", parse_LEADZERO, show_LEADZERO },
  { "LENGTH", parse_LENGTH, show_LENGTH },
  { "LOCALE", parse_LOCALE, show_LOCALE },
  { "MDISPLAY", parse_MDISPLAY, show_MDISPLAY },
  { "MESSAGES", parse_MESSAGES, show_MESSAGES },
  { "MEXPAND", parse_MEXPAND, show_MEXPAND },
  { "MITERATE", parse_MITERATE, show_MITERATE },
  { "MNEST", parse_MNEST, show_MNEST },
  { "MPRINT", parse_MPRINT, show_MPRINT },
  { "MXERRS", parse_MXERRS, show_MXERRS },
  { "MXLOOPS", parse_MXLOOPS, show_MXLOOPS },
  { "MXWARNS", parse_MXWARNS, show_MXWARNS },
  { "N", NULL, show_N },
  { "PRINTBACK", parse_PRINTBACK, show_PRINTBACK },
  { "RESULTS", parse_RESULTS, show_RESULTS },
  { "RIB", parse_RIB, show_RIB },
  { "RRB", parse_RRB, show_RRB },
  { "SAFER", parse_SAFER, show_SAFER },
  { "SCOMPRESSION", parse_SCOMPRESSION, show_SCOMPRESSION },
  { "SEED", parse_SEED, NULL },
  { "SMALL", parse_SMALL, show_SMALL },
  { "SPLIT", NULL, show_SPLIT },
  { "SUMMARY", parse_SUMMARY, show_SUMMARY },
  { "TEMPDIR", NULL, show_TEMPDIR },
  { "TNUMBERS", parse_TNUMBERS, show_TNUMBERS },
  { "TVARS", parse_TVARS, show_TVARS },
  { "TLOOK", parse_TLOOK, show_TLOOK },
  { "UNDEFINED", parse_UNDEFINED, show_UNDEFINED },
  { "VERSION", NULL, show_VERSION },
  { "WEIGHT", NULL, show_WEIGHT },
  { "WIB", parse_WIB, show_WIB },
  { "WRB", parse_WRB, show_WRB },
  { "WIDTH", parse_WIDTH, show_WIDTH },
  { "WORKSPACE", parse_WORKSPACE, show_WORKSPACE },
};
enum { N_SETTINGS = sizeof settings / sizeof *settings };

static bool
parse_setting (struct lexer *lexer)
{
  for (size_t i = 0; i < N_SETTINGS; i++)
    if (settings[i].set && match_subcommand (lexer, settings[i].name))
      return settings[i].set (lexer);

  lex_error (lexer, _("Syntax error expecting the name of a setting."));
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

static void
show_all (const struct dataset *ds, struct pivot_table **ptp)
{
  for (size_t i = 0; i < sizeof settings / sizeof *settings; i++)
    if (settings[i].show)
      do_show (ds, &settings[i], ptp);
}

static void
show_all_cc (const struct dataset *ds, struct pivot_table **ptp)
{
  for (size_t i = 0; i < sizeof settings / sizeof *settings; i++)
    {
      const struct setting *s = &settings[i];
      if (s->show && !strncmp (s->name, "CC", 2))
        do_show (ds, s, ptp);
    }
}

static void
show_environment (void)
{
  struct pivot_table *pt = pivot_table_create (N_("Environment Variables"));
  pivot_dimension_create (pt, PIVOT_AXIS_ROW, N_("Variable"));

  struct string_array sa = STRING_ARRAY_INITIALIZER;
  for (char **env = environ; *env; env++)
    string_array_append (&sa, *env);
  string_array_sort (&sa);

  for (size_t i = 0; i < sa.n; i++)
    {
      struct substring value = ss_cstr (sa.strings[i]);
      struct substring variable;
      ss_get_until (&value, '=', &variable);

      char *variable_s = ss_xstrdup (variable);
      char *value_s = ss_xstrdup (value);
      add_row (pt, variable_s, value_s);
      free (variable_s);
      free (value_s);
    }
  string_array_destroy (&sa);
  pivot_table_submit (pt);
}

int
cmd_show (struct lexer *lexer, struct dataset *ds)
{
  struct pivot_table *pt = NULL;
  if (lex_token (lexer) == T_ENDCMD)
    {
      show_all (ds, &pt);
      pivot_table_submit (pt);
      return CMD_SUCCESS;
    }

  do
    {
      if (lex_match (lexer, T_ALL))
        show_all (ds, &pt);
      else if (lex_match_id (lexer, "CC"))
        show_all_cc (ds, &pt);
      else if (lex_match_id (lexer, "WARRANTY"))
        show_warranty (ds);
      else if (lex_match_id (lexer, "COPYING") || lex_match_id (lexer, "LICENSE"))
        show_copying (ds);
      else if (lex_match_id (lexer, "SYSTEM"))
        show_system (ds);
      else if (lex_match_id (lexer, "ENVIRONMENT"))
        show_environment ();
      else if (lex_match_id (lexer, "TITLE"))
        {
          struct setting s = { .name = "TITLE", .show = show_TITLE };
          do_show (ds, &s, &pt);
        }
      else if (lex_match_id (lexer, "SUBTITLE"))
        {
          struct setting s = { .name = "SUBTITLE", .show = show_SUBTITLE };
          do_show (ds, &s, &pt);
        }
      else if (lex_token (lexer) == T_ID)
        {
          for (size_t i = 0; i < sizeof settings / sizeof *settings; i++)
            {
              const struct setting *s = &settings[i];
              if (s->show && lex_match_id (lexer, s->name))
                {
                  do_show (ds, s, &pt);
                  goto found;
                }
              }
          lex_error (lexer, _("Syntax error expecting the name of a setting."));
          return CMD_FAILURE;

        found: ;
        }
      else
        {
          lex_error (lexer, _("Syntax error expecting the name of a setting."));
          return CMD_FAILURE;
        }

      lex_match (lexer, T_SLASH);
    }
  while (lex_token (lexer) != T_ENDCMD);

  if (pt)
    pivot_table_submit (pt);

  return CMD_SUCCESS;
}

#define MAX_SAVED_SETTINGS 5

struct preserved_settings
  {
    struct settings *settings;
    struct pivot_table_look *look;
  };

static struct preserved_settings saved_settings[MAX_SAVED_SETTINGS];
static int n_saved_settings;

int
cmd_preserve (struct lexer *lexer, struct dataset *ds UNUSED)
{
  if (n_saved_settings < MAX_SAVED_SETTINGS)
    {
      struct preserved_settings *ps = &saved_settings[n_saved_settings++];
      ps->settings = settings_get ();
      ps->look = pivot_table_look_ref (pivot_table_look_get_default ());
      return CMD_SUCCESS;
    }
  else
    {
      lex_next_error (lexer, -1, -1,
                      _("Too many %s commands without a %s: at most "
                        "%d levels of saved settings are allowed."),
                      "PRESERVE", "RESTORE",
                      MAX_SAVED_SETTINGS);
      return CMD_CASCADING_FAILURE;
    }
}

int
cmd_restore (struct lexer *lexer, struct dataset *ds UNUSED)
{
  if (n_saved_settings > 0)
    {
      struct preserved_settings *ps = &saved_settings[--n_saved_settings];
      settings_set (ps->settings);
      settings_destroy (ps->settings);
      pivot_table_look_set_default (ps->look);
      pivot_table_look_unref (ps->look);
      return CMD_SUCCESS;
    }
  else
    {
      lex_next_error (lexer, -1, -1,
                      _("%s without matching %s."), "RESTORE", "PRESERVE");
      return CMD_FAILURE;
    }
}
