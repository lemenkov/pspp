/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2015, 2023 Free Software Foundation, Inc.

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

#if !settings_h
#define settings_h 1

#include <stdbool.h>
#include <stddef.h>

#include "data/format.h"
#include "libpspp/compiler.h"
#include "libpspp/float-format.h"
#include "libpspp/integer-format.h"
#include "libpspp/message.h"

struct caseproto;
struct settings;

void settings_init (void);
void settings_done (void);

struct settings *settings_get (void);
void settings_set (const struct settings *);
void settings_destroy (struct settings *);

enum float_format settings_get_input_float_format (void);
void settings_set_input_float_format (enum float_format);

/* Returns the integer format used for IB and PIB input. */
enum integer_format settings_get_input_integer_format (void);

/* Sets the integer format used for IB and PIB input to
   FORMAT. */
void settings_set_input_integer_format (enum integer_format);


/* Returns the current output integer format. */
enum integer_format settings_get_output_integer_format (void);

/* Sets the output integer format to INTEGER_FORMAT. */
void settings_set_output_integer_format (enum integer_format integer_format);

/* Returns the current output float format. */
enum float_format settings_get_output_float_format (void);

/* Sets the output float format to FLOAT_FORMAT. */
void settings_set_output_float_format (enum float_format float_format);



int settings_get_viewlength (void);
void settings_set_viewlength (int);

int settings_get_viewwidth (void);
void settings_set_viewwidth (int);

bool settings_get_safer_mode (void);
void settings_set_safer_mode (void);

bool settings_get_include (void);
void settings_set_include (bool);

int settings_get_epoch (void);
void settings_set_epoch (int);

bool settings_get_scompression (void);
void settings_set_scompression (bool);

bool settings_get_undefined (void);
void settings_set_undefined (bool);
double settings_get_blanks (void);
void settings_set_blanks (double);

int settings_get_max_messages (enum msg_severity);
void settings_set_max_messages (enum msg_severity, int max);

/* Macro settings. */
bool settings_get_mexpand (void);
void settings_set_mexpand (bool);

bool settings_get_mprint (void);
void settings_set_mprint (bool);

int settings_get_miterate (void);
void settings_set_miterate (int);

int settings_get_mnest (void);
void settings_set_mnest (int);

int settings_get_mxloops (void);
void settings_set_mxloops (int);

size_t settings_get_workspace (void);
size_t settings_get_workspace_cases (const struct caseproto *);
void settings_set_workspace (size_t);

struct fmt_spec settings_get_format (void);
void settings_set_format (const struct fmt_spec);

bool settings_get_testing_mode (void);
void settings_set_testing_mode (bool);

int settings_get_fuzzbits (void);
void settings_set_fuzzbits (int);

int settings_get_scalemin (void);
void settings_set_scalemin (int);

/* Whether to show variable or value labels or the underlying value or variable
   name. */
enum ATTRIBUTE ((packed)) settings_value_show
  {
    /* Use higher-level default.
       In a pivot_value, the default is taken from the pivot_table.
       In a pivot_table, the default is a global default.
       As a global default, this is invalid. */
    SETTINGS_VALUE_SHOW_DEFAULT = 0,

    SETTINGS_VALUE_SHOW_VALUE = 1, /* Show value or variable name only. */
    SETTINGS_VALUE_SHOW_LABEL = 2, /* Show label only. */
    SETTINGS_VALUE_SHOW_BOTH = 3,  /* Show both value/name and label. */
  };

enum settings_value_show settings_get_show_values (void);
enum settings_value_show settings_get_show_variables (void);

void settings_set_show_values (enum settings_value_show);
void settings_set_show_variables (enum settings_value_show);


enum behavior_mode {
  ENHANCED,             /* Use improved PSPP behavior. */
  COMPATIBLE            /* Be as compatible as possible. */
};

enum behavior_mode settings_get_algorithm (void);
void settings_set_algorithm (enum behavior_mode);
enum behavior_mode settings_get_syntax (void);
void settings_set_syntax (enum behavior_mode);

void settings_set_cmd_algorithm (enum behavior_mode);
void unset_cmd_algorithm (void);

enum fmt_type;
char *settings_set_cc (const char *cc_string, enum fmt_type) WARN_UNUSED_RESULT;

void settings_set_decimal_char (char decimal);
void settings_set_include_leading_zero (bool include_leading_zero);

const struct fmt_settings *settings_get_fmt_settings (void);

double settings_get_small (void);
void settings_set_small (double);

char * settings_get_summary (void);
void settings_set_summary (const char *s);

char *settings_dollar_template (struct fmt_spec);

/* Routing of different kinds of output. */
enum settings_output_devices
  {
    SETTINGS_DEVICE_LISTING = 1 << 0,  /* File or device. */
    SETTINGS_DEVICE_TERMINAL = 1 << 1, /* Screen. */
    SETTINGS_DEVICE_UNFILTERED = 1 << 2 /* Gets all output, no filtering. */
  };

enum settings_output_type
  {
    SETTINGS_OUTPUT_ERROR,      /* Errors and warnings. */
    SETTINGS_OUTPUT_NOTE,       /* Notes. */
    SETTINGS_OUTPUT_SYNTAX,     /* Syntax. */
    SETTINGS_OUTPUT_RESULT,     /* Everything else. */
    SETTINGS_N_OUTPUT_TYPES
  };



void settings_set_output_routing (enum settings_output_type,
                                  enum settings_output_devices);
enum settings_output_devices settings_get_output_routing (
  enum settings_output_type);

enum settings_mdisplay
  {
    SETTINGS_MDISPLAY_TEXT,
    SETTINGS_MDISPLAY_TABLES
  };

enum settings_mdisplay settings_get_mdisplay (void);
void settings_set_mdisplay (enum settings_mdisplay);

#endif /* !settings_h */
