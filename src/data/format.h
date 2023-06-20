/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#ifndef DATA_FORMAT_H
#define DATA_FORMAT_H 1

/* Display format types. */

#include <stdbool.h>
#include <stdint.h>
#include "data/val-type.h"
#include "libpspp/str.h"

struct fmt_settings;
struct msg_location;

/* How a format is going to be used. */
enum fmt_use
  {
    FMT_FOR_INPUT,           /* For parsing data input, e.g. data_in(). */
    FMT_FOR_OUTPUT           /* For formatting data output, e.g. data_out(). */
  };

/* Format type categories.

   Each format is in exactly one category.  We give categories
   bitwise disjoint values only to enable bitwise comparisons
   against a mask of FMT_CAT_* values, not to allow multiple
   categories per format. */
enum fmt_category
  {
    /* Numeric formats. */
    FMT_CAT_BASIC          = 0x001,     /* Basic numeric formats. */
    FMT_CAT_CUSTOM         = 0x002,     /* Custom currency formats. */
    FMT_CAT_LEGACY         = 0x004,     /* Legacy numeric formats. */
    FMT_CAT_BINARY         = 0x008,     /* Binary formats. */
    FMT_CAT_HEXADECIMAL    = 0x010,     /* Hexadecimal formats. */
    FMT_CAT_DATE           = 0x020,     /* Date formats. */
    FMT_CAT_TIME           = 0x040,     /* Time formats. */
    FMT_CAT_DATE_COMPONENT = 0x080,     /* Date component formats. */

    /* String formats. */
    FMT_CAT_STRING         = 0x100      /* String formats. */
  };

/* Format type. */
enum ATTRIBUTE ((packed)) fmt_type
  {
#define FMT(NAME, METHOD, IMIN, OMIN, IO, CATEGORY) FMT_##NAME,
#include "format.def"
    FMT_NUMBER_OF_FORMATS,
  };

/* Length of longest format specifier name,
   not including terminating null. */
#define FMT_TYPE_LEN_MAX 8

/* Length of longest string representation of fmt_spec,
   not including terminating null. */
#define FMT_STRING_LEN_MAX 32

/* Display format. */
struct fmt_spec
  {
    enum fmt_type type;                /* One of FMT_*. */
    uint8_t d;                        /* Number of decimal places. */
    uint16_t w;                        /* Width. */
  };

/* Maximum width of any numeric format. */
#define FMT_MAX_NUMERIC_WIDTH 40

/* Constructing formats. */
struct fmt_spec fmt_for_input (enum fmt_type, int w, int d) PURE_FUNCTION;
struct fmt_spec fmt_for_output (enum fmt_type, int w, int d) PURE_FUNCTION;
struct fmt_spec fmt_for_output_from_input (struct fmt_spec,
                                           const struct fmt_settings *);
struct fmt_spec fmt_default_for_width (int width);

/* Verifying formats. */
bool fmt_check (struct fmt_spec, enum fmt_use);
bool fmt_check_input (struct fmt_spec);
bool fmt_check_output (struct fmt_spec);
bool fmt_check_type_compat (struct fmt_spec, enum val_type);
bool fmt_check_width_compat (struct fmt_spec, int var_width);

char *fmt_check__ (struct fmt_spec, enum fmt_use);
char *fmt_check_input__ (struct fmt_spec);
char *fmt_check_output__ (struct fmt_spec);
char *fmt_check_type_compat__ (struct fmt_spec, const char *varname,
                               enum val_type);
char *fmt_check_width_compat__ (struct fmt_spec, const char *varname,
                                int var_width);

/* Working with formats. */
int fmt_var_width (struct fmt_spec);
char *fmt_to_string (struct fmt_spec, char s[FMT_STRING_LEN_MAX + 1]);
bool fmt_equal (struct fmt_spec, struct fmt_spec);
bool fmt_resize (struct fmt_spec *, int new_width);

void fmt_fix (struct fmt_spec *, enum fmt_use);
void fmt_fix_input (struct fmt_spec *);
void fmt_fix_output (struct fmt_spec *);

void fmt_change_width (struct fmt_spec *, int width, enum fmt_use);
void fmt_change_decimals (struct fmt_spec *, int decimals, enum fmt_use);

/* Format types. */
bool is_fmt_type (enum fmt_type);

const char *fmt_name (enum fmt_type) PURE_FUNCTION;
bool fmt_from_name (const char *name, enum fmt_type *);

bool fmt_takes_decimals (enum fmt_type) PURE_FUNCTION;

int fmt_min_width (enum fmt_type, enum fmt_use) PURE_FUNCTION;
int fmt_max_width (enum fmt_type, enum fmt_use) PURE_FUNCTION;
int fmt_max_decimals (enum fmt_type, int width, enum fmt_use) PURE_FUNCTION;
int fmt_min_input_width (enum fmt_type) PURE_FUNCTION;
int fmt_max_input_width (enum fmt_type) PURE_FUNCTION;
int fmt_max_input_decimals (enum fmt_type, int width) PURE_FUNCTION;
int fmt_min_output_width (enum fmt_type) PURE_FUNCTION;
int fmt_max_output_width (enum fmt_type) PURE_FUNCTION;
int fmt_max_output_decimals (enum fmt_type, int width) PURE_FUNCTION;
int fmt_step_width (enum fmt_type) PURE_FUNCTION;

bool fmt_is_string (enum fmt_type) PURE_FUNCTION;
bool fmt_is_numeric (enum fmt_type) PURE_FUNCTION;
enum fmt_category fmt_get_category (enum fmt_type) PURE_FUNCTION;

enum fmt_type fmt_input_to_output (enum fmt_type) PURE_FUNCTION;
bool fmt_usable_for_input (enum fmt_type) PURE_FUNCTION;

int fmt_to_io (enum fmt_type) PURE_FUNCTION;
bool fmt_from_io (int io, enum fmt_type *);
bool fmt_from_u32 (uint32_t, int var_width, bool loose, struct fmt_spec *);

const char *fmt_date_template (enum fmt_type, int width) PURE_FUNCTION;
const char *fmt_gui_name (enum fmt_type);

/* A prefix or suffix for a numeric output format. */
struct fmt_affix
  {
    char *s;                    /* String contents of affix, in UTF-8. */
    int width;                  /* Display width in columns (see wcwidth()). */
  };

/* A numeric output style.  This can express the basic numeric formats (in the
   FMT_CAT_BASIC category) and custom currency formats (FMT_CCx). */
struct fmt_number_style
  {
    struct fmt_affix neg_prefix; /* Negative prefix. */
    struct fmt_affix prefix;     /* Prefix. */
    struct fmt_affix suffix;     /* Suffix. */
    struct fmt_affix neg_suffix; /* Negative suffix. */
    char decimal;                /* Decimal point: '.' or ','. */
    char grouping;               /* Grouping character: ',', '.', or 0. */
    bool include_leading_zero;   /* Format as ".5" or "0.5"? */

    /* A fmt_affix may require more bytes than its display width; for example,
       U+00A5 (¥) is 2 bytes in UTF-8 but occupies only one display column.
       This member is the sum of the number of bytes required by all of the
       fmt_affix members in this struct, minus their display widths.  Thus, it
       can be used to size memory allocations: for example, the formatted
       result of CCA20.5 requires no more than (20 + extra_bytes) bytes in
       UTF-8. */
    int extra_bytes;
  };

struct fmt_number_style *fmt_number_style_from_string (const char *);
struct fmt_number_style *fmt_number_style_clone (
  const struct fmt_number_style *);
void fmt_number_style_destroy (struct fmt_number_style *);

char *fmt_number_style_to_string (const struct fmt_number_style *);

int fmt_affix_width (const struct fmt_number_style *);
int fmt_neg_affix_width (const struct fmt_number_style *);

/* Number of custom currency styles. */
#define FMT_N_CCS 5             /* FMT_CCA through FMT_CCE. */

struct fmt_settings
  {
    int epoch;                               /* 0 for default epoch. */
    char decimal;                            /* '.' or ','. */

    /* Format F, E, COMMA, and DOT with leading zero (e.g. "0.5" instead of
       ".5")? */
    bool include_leading_zero;

    struct fmt_number_style *ccs[FMT_N_CCS]; /* CCA through CCE. */
  };
#define FMT_SETTINGS_INIT { .decimal = '.' }

void fmt_settings_init (struct fmt_settings *);
void fmt_settings_uninit (struct fmt_settings *);
struct fmt_settings fmt_settings_copy (const struct fmt_settings *);

const struct fmt_number_style *fmt_settings_get_style (
  const struct fmt_settings *, enum fmt_type);
int fmt_settings_get_epoch (const struct fmt_settings *);

void fmt_settings_set_cc (struct fmt_settings *, enum fmt_type,
                          struct fmt_number_style *);

extern const struct fmt_spec F_8_0 ;
extern const struct fmt_spec F_8_2 ;
extern const struct fmt_spec F_4_3 ;
extern const struct fmt_spec F_5_1 ;

#endif /* data/format.h */
