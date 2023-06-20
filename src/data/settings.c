/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009, 2010, 2011, 2015, 2023 Free Software Foundation, Inc.

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

#include "data/settings.h"

#include <assert.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/format.h"
#include "data/value.h"
#include "libpspp/i18n.h"
#include "libpspp/integer-format.h"
#include "libpspp/message.h"

#include "gl/minmax.h"
#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct settings
{
  /* Integer format used for IB and PIB input. */
  enum integer_format input_integer_format;

  /* Floating-point format used for RB and RBHEX input. */
  enum float_format input_float_format;

  /* Format of integers in output (SET WIB). */
  enum integer_format output_integer_format;

  /* Format of reals in output (SET WRB). */
  enum float_format output_float_format;

  /* MATRIX...END MATRIX settings. */
  enum settings_mdisplay mdisplay;

  int viewlength;
  int viewwidth;
  bool safer_mode;
  bool include;
  bool route_errors_to_terminal;
  bool route_errors_to_listing;
  bool scompress;
  bool undefined;
  double blanks;
  int max_messages[MSG_N_SEVERITIES];
  bool printback;

  /* Macro settings. */
  bool mexpand;                 /* Expand macros? */
  bool mprint;                  /* Print macro expansions? */
  int miterate;                 /* Maximum iterations of !FOR. */
  int mnest;                    /* Maximum nested macro expansion levels. */

  int mxloops;
  size_t workspace;
  struct fmt_spec default_format;
  bool testing_mode;
  int fuzzbits;
  int scalemin;

  int cmd_algorithm;
  int global_algorithm;
  int syntax;

  struct fmt_settings styles;
  double small;

  enum settings_output_devices output_routing[SETTINGS_N_OUTPUT_TYPES];

  enum settings_value_show show_values;
  enum settings_value_show show_variables;

  char *table_summary;
};

static struct settings the_settings = {
  .input_integer_format = INTEGER_NATIVE,
  .input_float_format = FLOAT_NATIVE_DOUBLE,
  .output_integer_format = INTEGER_NATIVE,
  .output_float_format = FLOAT_NATIVE_DOUBLE,
  .mdisplay = SETTINGS_MDISPLAY_TEXT,
  .viewlength = 24,
  .viewwidth = 79,
  .safer_mode = false,
  .include = true,
  .route_errors_to_terminal = true,
  .route_errors_to_listing = true,
  .scompress = true,
  .undefined = true,
  .blanks = SYSMIS,

  .max_messages = {
    [MSG_S_ERROR] = 100,
    [MSG_S_WARNING] = 100,
    [MSG_S_NOTE] = 100
  },

  .printback = true,

  .mexpand = true,
  .mprint = false,
  .miterate = 1000,
  .mnest = 50,

  .mxloops = 40,
  .workspace = 64L * 1024 * 1024,
  .default_format = { .type = FMT_F, .w = 8, .d = 2 },
  .testing_mode = false,
  .fuzzbits = 6,
  .scalemin = 24,
  .cmd_algorithm = ENHANCED,
  .global_algorithm = ENHANCED,
  .syntax = ENHANCED,
  .styles = FMT_SETTINGS_INIT,
  .small = .0001,
  .table_summary = 0,

  /* output_routing */
  .output_routing = {
#define LT (SETTINGS_DEVICE_LISTING | SETTINGS_DEVICE_TERMINAL)
    [SETTINGS_OUTPUT_ERROR] = LT,
    [SETTINGS_OUTPUT_NOTE] = LT,
    [SETTINGS_OUTPUT_SYNTAX] = 0,
    [SETTINGS_OUTPUT_RESULT] = LT
#undef LT
  },

  .show_values = SETTINGS_VALUE_SHOW_LABEL,
  .show_variables = SETTINGS_VALUE_SHOW_LABEL,
};

/* Initializes the settings module. */
void
settings_init (void)
{
  settings_set_decimal_char (get_system_decimal ());
}

/* Cleans up the settings module. */
void
settings_done (void)
{
  settings_destroy (&the_settings);
}

static void
settings_copy (struct settings *dst, const struct settings *src)
{
  *dst = *src;
  dst->styles = fmt_settings_copy (&src->styles);
}

/* Returns a copy of the current settings. */
struct settings *
settings_get (void)
{
  struct settings *s = xmalloc (sizeof *s);
  settings_copy (s, &the_settings);
  return s;
}

/* Replaces the current settings by those in S.  The caller retains ownership
   of S. */
void
settings_set (const struct settings *s)
{
  settings_destroy (&the_settings);
  settings_copy (&the_settings, s);
}

/* Destroys S. */
void
settings_destroy (struct settings *s)
{
  if (s != NULL)
    {
      fmt_settings_uninit (&s->styles);
      free (s->table_summary);
      if (s != &the_settings)
        free (s);
    }
}

/* Returns the floating-point format used for RB and RBHEX
   input. */
enum float_format
settings_get_input_float_format (void)
{
  return the_settings.input_float_format;
}

/* Sets the floating-point format used for RB and RBHEX input to
   FORMAT. */
void
settings_set_input_float_format (enum float_format format)
{
  the_settings.input_float_format = format;
}

/* Returns the integer format used for IB and PIB input. */
enum integer_format
settings_get_input_integer_format (void)
{
  return the_settings.input_integer_format;
}

/* Sets the integer format used for IB and PIB input to
   FORMAT. */
void
settings_set_input_integer_format (enum integer_format format)
{
  the_settings.input_integer_format = format;
}

/* Returns the current output integer format. */
enum integer_format
settings_get_output_integer_format (void)
{
  return the_settings.output_integer_format;
}

/* Sets the output integer format to INTEGER_FORMAT. */
void
settings_set_output_integer_format (
                           enum integer_format integer_format)
{
  the_settings.output_integer_format = integer_format;
}

/* Returns the current output float format. */
enum float_format
settings_get_output_float_format (void)
{
  return the_settings.output_float_format;
}

/* Sets the output float format to FLOAT_FORMAT. */
void
settings_set_output_float_format (enum float_format float_format)
{
  the_settings.output_float_format = float_format;
}

/* Screen length in lines. */
int
settings_get_viewlength (void)
{
  return the_settings.viewlength;
}

/* Sets the view length. */
void
settings_set_viewlength (int viewlength_)
{
  the_settings.viewlength = viewlength_;
}

/* Screen width. */
int
settings_get_viewwidth(void)
{
  return the_settings.viewwidth;
}

/* Sets the screen width. */
void
settings_set_viewwidth (int viewwidth_)
{
  the_settings.viewwidth = viewwidth_;
}

/* Whether PSPP can erase and overwrite files. */
bool
settings_get_safer_mode (void)
{
  return the_settings.safer_mode;
}

/* Set safer mode. */
void
settings_set_safer_mode (void)
{
  the_settings.safer_mode = true;
}

/* If echo is on, whether commands from include files are echoed. */
bool
settings_get_include (void)
{
  return the_settings.include;
}

/* Set include file echo. */
void
settings_set_include (bool include)
{
  the_settings.include = include;
}

/* Returns the year that starts the epoch. */
int
settings_get_epoch (void)
{
  return the_settings.styles.epoch;
}

/* Sets the year that starts the epoch. */
void
settings_set_epoch (int epoch)
{
  the_settings.styles.epoch = epoch;
}

/* Compress system files by default? */
bool
settings_get_scompression (void)
{
  return the_settings.scompress;
}

/* Set system file default compression. */
void
settings_set_scompression (bool scompress)
{
  the_settings.scompress = scompress;
}

/* Whether to warn on undefined values in numeric data. */
bool
settings_get_undefined (void)
{
  return the_settings.undefined;
}

/* Set whether to warn on undefined values. */
void
settings_set_undefined (bool undefined)
{
  the_settings.undefined = undefined;
}

/* The value that blank numeric fields are set to when read in. */
double
settings_get_blanks (void)
{
  return the_settings.blanks;
}

/* Set the value that blank numeric fields are set to when read
   in. */
void
settings_set_blanks (double blanks)
{
  the_settings.blanks = blanks;
}

/* Returns the maximum number of messages to show of the given SEVERITY before
   aborting.  (The value for MSG_S_WARNING is interpreted as maximum number of
   warnings and errors combined.) */
int
settings_get_max_messages (enum msg_severity severity)
{
  assert (severity < MSG_N_SEVERITIES);
  return the_settings.max_messages[severity];
}

/* Sets the maximum number of messages to show of the given SEVERITY before
   aborting to MAX.  (The value for MSG_S_WARNING is interpreted as maximum
   number of warnings and errors combined.)  In addition, in the case of
   warnings the special value of zero indicates that no warnings are to be
   issued.
*/
void
settings_set_max_messages (enum msg_severity severity, int max)
{
  assert (severity < MSG_N_SEVERITIES);

  if (severity == MSG_S_WARNING)
    {
      if (max == 0)
        {
          msg (MW,
               _("MXWARNS set to zero.  No further warnings will be given even when potentially problematic situations are encountered."));
          msg_ui_disable_warnings (true);
        }
      else if (the_settings.max_messages [MSG_S_WARNING] == 0)
        {
          msg_ui_disable_warnings (false);
          the_settings.max_messages[MSG_S_WARNING] = max;
          msg (MW, _("Warnings re-enabled. %d warnings will be issued before aborting syntax processing."), max);
        }
    }

  the_settings.max_messages[severity] = max;
}

/* Returns whether to expand macro invocations. */
bool
settings_get_mexpand (void)
{
  return the_settings.mexpand;
}

/* Sets whether to expand macro invocations. */
void
settings_set_mexpand (bool mexpand)
{
  the_settings.mexpand = mexpand;
}

/* Independent of get_printback, controls whether the commands
   generated by macro invocations are displayed. */
bool
settings_get_mprint (void)
{
  return the_settings.mprint;
}

/* Sets whether the commands generated by macro invocations are
   displayed. */
void
settings_set_mprint (bool mprint)
{
  the_settings.mprint = mprint;
}

/* Returns the limit for loop iterations within a macro. */
int
settings_get_miterate (void)
{
  return the_settings.miterate;
}

/* Sets the limit for loop iterations within a macro. */
void
settings_set_miterate (int miterate)
{
  the_settings.miterate = miterate;
}

/* Returns the limit for recursion macro expansions. */
int settings_get_mnest (void)
{
  return the_settings.mnest;
}

/* Sets the limit for recursion macro expansions. */
void
settings_set_mnest (int mnest)
{
  the_settings.mnest = mnest;
}

int settings_get_mxloops (void);
void settings_set_mxloops (int);
/* Implied limit of unbounded loop. */
int
settings_get_mxloops (void)
{
  return the_settings.mxloops;
}

/* Set implied limit of unbounded loop. */
void
settings_set_mxloops (int mxloops)
{
  the_settings.mxloops = mxloops;
}

/* Approximate maximum amount of memory to use for cases, in
   bytes. */
size_t
settings_get_workspace (void)
{
  return the_settings.workspace;
}

/* Approximate maximum number of cases to allocate in-core, given
   that each case has the format given in PROTO. */
size_t
settings_get_workspace_cases (const struct caseproto *proto)
{
  size_t n_cases = settings_get_workspace () / case_get_cost (proto);
  return MAX (n_cases, 4);
}

/* Set approximate maximum amount of memory to use for cases, in
   bytes. */

void
settings_set_workspace (size_t workspace)
{
  the_settings.workspace = workspace;
}

/* Default format for variables created by transformations and by
   DATA LIST {FREE,LIST}. */
struct fmt_spec
settings_get_format (void)
{
  return the_settings.default_format;
}

/* Set default format for variables created by transformations
   and by DATA LIST {FREE,LIST}. */
void
settings_set_format (const struct fmt_spec default_format)
{
  the_settings.default_format = default_format;
}

/* Are we in testing mode?  (e.g. --testing-mode command line
   option) */
bool
settings_get_testing_mode (void)
{
  return the_settings.testing_mode;
}

/* Set testing mode. */
void
settings_set_testing_mode (bool testing_mode)
{
  the_settings.testing_mode = testing_mode;
}

int
settings_get_fuzzbits (void)
{
  return the_settings.fuzzbits;
}

void
settings_set_fuzzbits (int fuzzbits)
{
  the_settings.fuzzbits = fuzzbits;
}

int
settings_get_scalemin (void)
{
  return the_settings.scalemin;
}

void
settings_set_scalemin (int scalemin)
{
  the_settings.scalemin = scalemin;
}

/* Return the current algorithm setting */
enum behavior_mode
settings_get_algorithm (void)
{
  return the_settings.cmd_algorithm;
}

/* Set the algorithm option globally. */
void
settings_set_algorithm (enum behavior_mode mode)
{
  the_settings.global_algorithm = the_settings.cmd_algorithm = mode;
}

/* Set the algorithm option for this command only */
void
settings_set_cmd_algorithm (enum behavior_mode mode)
{
  the_settings.cmd_algorithm = mode;
}

/* Unset the algorithm option for this command */
void
unset_cmd_algorithm (void)
{
  the_settings.cmd_algorithm = the_settings.global_algorithm;
}

/* Get the current syntax setting */
enum behavior_mode
settings_get_syntax (void)
{
  return the_settings.syntax;
}

/* Set the syntax option */
void
settings_set_syntax (enum behavior_mode mode)
{
  the_settings.syntax = mode;
}


/* Sets custom currency specifier CC having name CC_NAME ('A' through 'E') to
   correspond to the settings in CC_STRING.  Returns NULL if successful,
   otherwise an error message that the caller must free. */
char * WARN_UNUSED_RESULT
settings_set_cc (const char *cc_string, enum fmt_type type)
{
  struct fmt_number_style *style = fmt_number_style_from_string (cc_string);
  if (!style)
    return xasprintf (_("Custom currency string `%s' for %s does not contain "
                        "exactly three periods or commas (or it contains "
                        "both)."),
                      fmt_name (type), cc_string);

  fmt_settings_set_cc (&the_settings.styles, type, style);
  return NULL;
}

void
settings_set_decimal_char (char decimal)
{
  the_settings.styles.decimal = decimal;
}

void
settings_set_include_leading_zero (bool include_leading_zero)
{
  the_settings.styles.include_leading_zero = include_leading_zero;
}

const struct fmt_settings *
settings_get_fmt_settings (void)
{
  return &the_settings.styles;
}

char *
settings_get_summary (void)
{
  return the_settings.table_summary;
}

void
settings_set_summary (const char *s)
{
  free (the_settings.table_summary);

  if (s == NULL)
    {
      the_settings.table_summary = NULL;
      return;
    }

  the_settings.table_summary = xstrdup (s);
}

double
settings_get_small (void)
{
  return the_settings.small;
}

void
settings_set_small (double small)
{
  the_settings.small = small;
}

/* Returns a string of the form "$#,###.##" according to FMT,
   which must be of type FMT_DOLLAR.  The caller must free the
   string. */
char *
settings_dollar_template (const struct fmt_spec fmt)
{
  struct string str = DS_EMPTY_INITIALIZER;
  int c;
  const struct fmt_number_style *fns ;

  assert (fmt.type == FMT_DOLLAR);

  fns = fmt_settings_get_style (&the_settings.styles, fmt.type);

  ds_put_byte (&str, '$');
  for (c = MAX (fmt.w - fmt.d - 1, 0); c > 0;)
    {
      ds_put_byte (&str, '#');
      if (--c % 4 == 0 && c > 0)
        {
          ds_put_byte (&str, fns->grouping);
          --c;
        }
    }
  if (fmt.d > 0)
    {
      ds_put_byte (&str, fns->decimal);
      ds_put_byte_multiple (&str, '#', fmt.d);
    }

  return ds_cstr (&str);
}

void
settings_set_output_routing (enum settings_output_type type,
                             enum settings_output_devices devices)
{
  assert (type < SETTINGS_N_OUTPUT_TYPES);
  the_settings.output_routing[type] = devices;
}

enum settings_output_devices
settings_get_output_routing (enum settings_output_type type)
{
  assert (type < SETTINGS_N_OUTPUT_TYPES);
  return the_settings.output_routing[type] | SETTINGS_DEVICE_UNFILTERED;
}

enum settings_value_show
settings_get_show_values (void)
{
  return the_settings.show_values;
}

void
settings_set_show_values (enum settings_value_show s)
{
  the_settings.show_values = s;
}


enum settings_value_show
settings_get_show_variables (void)
{
  return the_settings.show_variables;
}

void
settings_set_show_variables (enum settings_value_show s)
{
  the_settings.show_variables = s;
}

enum settings_mdisplay
settings_get_mdisplay (void)
{
  return the_settings.mdisplay;
}

void
settings_set_mdisplay (enum settings_mdisplay mdisplay)
{
  the_settings.mdisplay = mdisplay;
}
