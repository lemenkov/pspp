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

#include <config.h>

#include "format.h"

#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <uniwidth.h>

#include "data/identifier.h"
#include "data/settings.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/compiler.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"

#include "gl/c-strcase.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

bool is_fmt_type (enum fmt_type);

static bool valid_width (enum fmt_type, int width, enum fmt_use);

static int max_digits_for_bytes (int bytes);
static void fmt_clamp_width (struct fmt_spec *, enum fmt_use);
static void fmt_clamp_decimals (struct fmt_spec *, enum fmt_use);

void
fmt_settings_init (struct fmt_settings *settings)
{
  *settings = (struct fmt_settings) FMT_SETTINGS_INIT;
}

void
fmt_settings_uninit (struct fmt_settings *settings)
{
  for (int i = 0; i < FMT_N_CCS; i++)
    fmt_number_style_destroy (settings->ccs[i]);
}

struct fmt_settings
fmt_settings_copy (const struct fmt_settings *old)
{
  struct fmt_settings new = *old;
  for (int i = 0; i < FMT_N_CCS; i++)
    new.ccs[i] = fmt_number_style_clone (old->ccs[i]);
  return new;
}

static size_t
fmt_type_to_cc_index (enum fmt_type type)
{
  switch (type)
    {
    case FMT_CCA: return 0;
    case FMT_CCB: return 1;
    case FMT_CCC: return 2;
    case FMT_CCD: return 3;
    case FMT_CCE: return 4;
    default: NOT_REACHED ();
    }
}

/* Returns the number formatting style associated with the given
   format TYPE. */
const struct fmt_number_style *
fmt_settings_get_style (const struct fmt_settings *settings,
                        enum fmt_type type)
{
  verify (FMT_F < 6);
  verify (FMT_COMMA < 6);
  verify (FMT_DOT < 6);
  verify (FMT_DOLLAR < 6);
  verify (FMT_PCT < 6);
  verify (FMT_E < 6);

#define OPPOSITE(C) ((C) == ',' ? '.' : ',')
#define AFFIX(S) { .s = (char *) (S), .width = sizeof (S) - 1 }
#define NS(PREFIX, SUFFIX, DECIMAL, GROUPING) { \
    .neg_prefix = AFFIX ("-"),                  \
    .prefix = AFFIX (PREFIX),                   \
    .suffix = AFFIX (SUFFIX),                   \
    .neg_suffix = AFFIX (""),                   \
    .decimal = DECIMAL,                         \
    .grouping = GROUPING,                       \
  }
#define ANS(DECIMAL, GROUPING) {                        \
    [FMT_F]      = NS( "",  "", DECIMAL, 0),            \
    [FMT_E]      = NS( "",  "", DECIMAL, 0),            \
    [FMT_COMMA]  = NS( "",  "", DECIMAL, GROUPING),     \
    [FMT_DOT]    = NS( "",  "", GROUPING, DECIMAL),     \
    [FMT_DOLLAR] = NS("$",  "", DECIMAL, GROUPING),     \
    [FMT_PCT]    = NS( "", "%", DECIMAL, 0),            \
  }

  static const struct fmt_number_style period_styles[6] = ANS ('.', ',');
  static const struct fmt_number_style comma_styles[6] = ANS (',', '.');
  static const struct fmt_number_style default_style = NS ("", "", '.', 0);

  switch (type)
    {
    case FMT_F:
    case FMT_COMMA:
    case FMT_DOT:
    case FMT_DOLLAR:
    case FMT_PCT:
    case FMT_E:
      return (settings->decimal == '.'
              ? &period_styles[type]
              : &comma_styles[type]);

    case FMT_CCA:
    case FMT_CCB:
    case FMT_CCC:
    case FMT_CCD:
    case FMT_CCE:
      {
        size_t idx = fmt_type_to_cc_index (type);
        return settings->ccs[idx] ? settings->ccs[idx] : &default_style;
      }

    default:
      return &default_style;
    }
}

static int
default_epoch (void)
{
  static int epoch = 0;
  if (!epoch)
    {
      time_t t = time (0);
      struct tm *tm = localtime (&t);
      epoch = (tm != NULL ? tm->tm_year + 1900 : 2000) - 69;
    }
  return epoch;
}

int
fmt_settings_get_epoch (const struct fmt_settings *settings)
{
  return !settings->epoch ? default_epoch () : settings->epoch;
}

void
fmt_settings_set_cc (struct fmt_settings *settings, enum fmt_type type,
                     struct fmt_number_style *style)
{
  size_t idx = fmt_type_to_cc_index (type);

  fmt_number_style_destroy (settings->ccs[idx]);
  settings->ccs[idx] = style;
}


/* Returns an input format specification with type TYPE, width W,
   and D decimals. */
struct fmt_spec
fmt_for_input (enum fmt_type type, int w, int d)
{
  struct fmt_spec f;
  f.type = type;
  f.w = w;
  f.d = d;
  assert (fmt_check_input (&f));
  return f;
}

/* Returns an output format specification with type TYPE, width
   W, and D decimals. */
struct fmt_spec
fmt_for_output (enum fmt_type type, int w, int d)
{
  struct fmt_spec f;
  f.type = type;
  f.w = w;
  f.d = d;
  assert (fmt_check_output (&f));
  return f;
}

/* Returns the output format specifier corresponding to input
   format specifier INPUT. */
struct fmt_spec
fmt_for_output_from_input (const struct fmt_spec *input,
                           const struct fmt_settings *settings)
{
  struct fmt_spec output;

  assert (fmt_check_input (input));

  output.type = fmt_input_to_output (input->type);
  output.w = input->w;
  if (output.w > fmt_max_output_width (output.type))
    output.w = fmt_max_output_width (output.type);
  else if (output.w < fmt_min_output_width (output.type))
    output.w = fmt_min_output_width (output.type);
  output.d = input->d;

  switch (input->type)
    {
    case FMT_Z:
      output.w++;
      if (output.d > 0)
	output.w++;
      break;

    case FMT_F:
    case FMT_COMMA:
    case FMT_DOT:
    case FMT_DOLLAR:
    case FMT_PCT:
      {
        const struct fmt_number_style *style =
	  fmt_settings_get_style (settings, input->type);

        output.w += fmt_affix_width (style);
        if (style->grouping != 0 && input->w - input->d >= 3)
          output.w += (input->w - input->d - 1) / 3;
        if (output.d > 0)
          output.w++;
      }
      break;

    case FMT_N:
      if (output.d > 0)
        output.w++;
      break;

    case FMT_E:
      output.d = MAX (input->d, 3);
      output.w = MAX (input->w, output.d + 7);
      break;

    case FMT_PIBHEX:
      output.w = max_digits_for_bytes (input->w / 2) + 1;
      break;

    case FMT_RB:
    case FMT_RBHEX:
      output.w = 8;
      output.d = 2;
      break;

    case FMT_P:
    case FMT_PK:
      output.w = 2 * input->w + (input->d > 0);
      break;

    case FMT_IB:
    case FMT_PIB:
      output.w = max_digits_for_bytes (input->w) + 1;
      if (output.d > 0)
        output.w++;
      break;

    case FMT_CCA:
    case FMT_CCB:
    case FMT_CCC:
    case FMT_CCD:
    case FMT_CCE:
      NOT_REACHED ();

    case FMT_A:
      break;

    case FMT_AHEX:
      output.w = input->w / 2;
      break;

    case FMT_DATE:
    case FMT_EDATE:
    case FMT_SDATE:
    case FMT_ADATE:
    case FMT_JDATE:
    case FMT_QYR:
    case FMT_MOYR:
    case FMT_WKYR:
    case FMT_TIME:
    case FMT_DTIME:
    case FMT_DATETIME:
    case FMT_WKDAY:
    case FMT_MONTH:
      break;

    case FMT_MTIME:
      if (input->d)
        output.w = MAX (input->w, input->d + 6);
      break;

    case FMT_YMDHMS:
      if (input->w)
        output.w = MAX (input->w, input->d + 20);
      break;

    default:
      NOT_REACHED ();
    }

  if (output.w > fmt_max_output_width (output.type))
    output.w = fmt_max_output_width (output.type);

  assert (fmt_check_output (&output));
  return output;
}

/* Returns the default format for the given WIDTH: F8.2 format
   for a numeric value, A format for a string value. */
struct fmt_spec
fmt_default_for_width (int width)
{
  return (width == 0
          ? fmt_for_output (FMT_F, 8, 2)
          : fmt_for_output (FMT_A, width, 0));
}

/* Checks whether SPEC is valid for USE and returns nonzero if so.
   Otherwise, emits an error message and returns zero. */
bool
fmt_check (const struct fmt_spec *spec, enum fmt_use use)
{
  const char *io_fmt;
  char str[FMT_STRING_LEN_MAX + 1];
  int min_w, max_w, max_d;

  assert (is_fmt_type (spec->type));
  fmt_to_string (spec, str);

  io_fmt = use == FMT_FOR_INPUT ? _("Input format") : _("Output format");
  if (use == FMT_FOR_INPUT && !fmt_usable_for_input (spec->type))
    {
      msg (SE, _("Format %s may not be used for input."), str);
      return false;
    }

  if (spec->w % fmt_step_width (spec->type))
    {
      assert (fmt_step_width (spec->type) == 2);
      msg (SE, _("%s specifies width %d, but %s requires an even width."),
           str, spec->w, fmt_name (spec->type));
      return false;
    }

  min_w = fmt_min_width (spec->type, use);
  max_w = fmt_max_width (spec->type, use);
  if (spec->w < min_w || spec->w > max_w)
    {
      msg (SE, _("%s %s specifies width %d, but "
                 "%s requires a width between %d and %d."),
           io_fmt, str, spec->w, fmt_name (spec->type), min_w, max_w);
      return false;
    }

  max_d = fmt_max_decimals (spec->type, spec->w, use);
  if (!fmt_takes_decimals (spec->type) && spec->d != 0)
    {
      msg (SE, ngettext ("%s %s specifies %d decimal place, but "
                         "%s does not allow any decimals.",
                         "%s %s specifies %d decimal places, but "
                         "%s does not allow any decimals.",
                         spec->d),
           io_fmt, str, spec->d, fmt_name (spec->type));
      return false;
    }
  else if (spec->d > max_d)
    {
      if (max_d > 0)
        msg (SE, ngettext ("%s %s specifies %d decimal place, but "
                           "the given width allows at most %d decimals.",
                           "%s %s specifies %d decimal places, but "
                           "the given width allows at most %d decimals.",
                           spec->d),
             io_fmt, str, spec->d, max_d);
      else
        msg (SE, ngettext ("%s %s specifies %d decimal place, but "
                           "the given width does not allow for any decimals.",
                           "%s %s specifies %d decimal places, but "
                           "the given width does not allow for any decimals.",
                           spec->d),
             io_fmt, str, spec->d);
      return false;
    }

  return true;
}

/* Checks whether SPEC is valid as an input format and returns
   nonzero if so.  Otherwise, emits an error message and returns
   zero. */
bool
fmt_check_input (const struct fmt_spec *spec)
{
  return fmt_check (spec, FMT_FOR_INPUT);
}

/* Checks whether SPEC is valid as an output format and returns
   true if so.  Otherwise, emits an error message and returns false. */
bool
fmt_check_output (const struct fmt_spec *spec)
{
  return fmt_check (spec, FMT_FOR_OUTPUT);
}

/* Checks that FORMAT is appropriate for a variable of the given
   VAR_TYPE and returns true if so.  Otherwise returns false and
   emits an error message. */
bool
fmt_check_type_compat (const struct fmt_spec *format, enum val_type var_type)
{
  assert (val_type_is_valid (var_type));
  if ((var_type == VAL_STRING) != (fmt_is_string (format->type) != 0))
    {
      char str[FMT_STRING_LEN_MAX + 1];
      msg (SE, _("%s variables are not compatible with %s format %s."),
           var_type == VAL_STRING ? _("String") : _("Numeric"),
           var_type == VAL_STRING ? _("numeric") : _("string"),
           fmt_to_string (format, str));
      return false;
    }
  return true;
}

/* Checks that FORMAT is appropriate for a variable of the given
   WIDTH and returns true if so.  Otherwise returns false and
   emits an error message. */
bool
fmt_check_width_compat (const struct fmt_spec *format, int width)
{
  if (!fmt_check_type_compat (format, val_type_from_width (width)))
    return false;
  if (fmt_var_width (format) != width)
    {
      char str[FMT_STRING_LEN_MAX + 1];
      msg (SE, _("String variable with width %d is not compatible with "
                 "format %s."),
           width, fmt_to_string (format, str));
      return false;
    }
  return true;
}

/* Returns the width corresponding to FORMAT.  The return value
   is the width of the `union value's required by FORMAT. */
int
fmt_var_width (const struct fmt_spec *format)
{
  return (format->type == FMT_AHEX ? format->w / 2
          : format->type == FMT_A ? format->w
          : 0);
}

/* Converts F to its string representation (for instance, "F8.2")
   in BUFFER.  Returns BUFFER.

   If F has decimals, they are included in the output string,
   even if F's format type does not allow decimals, to allow
   accurately presenting incorrect formats to the user. */
char *
fmt_to_string (const struct fmt_spec *f, char buffer[FMT_STRING_LEN_MAX + 1])
{
  if (fmt_takes_decimals (f->type) || f->d > 0)
    snprintf (buffer, FMT_STRING_LEN_MAX + 1,
              "%s%d.%d", fmt_name (f->type), f->w, f->d);
  else
    snprintf (buffer, FMT_STRING_LEN_MAX + 1,
              "%s%d", fmt_name (f->type), f->w);
  return buffer;
}

/* Returns true if A and B are identical formats,
   false otherwise. */
bool
fmt_equal (const struct fmt_spec *a, const struct fmt_spec *b)
{
  return a->type == b->type && a->w == b->w && a->d == b->d;
}

/* Adjusts FMT to be valid for a value of the given WIDTH if necessary.
   If nothing needed to be changed the return value is false
 */
bool
fmt_resize (struct fmt_spec *fmt, int width)
{
  if ((width > 0) != fmt_is_string (fmt->type))
    {
      /* Changed from numeric to string or vice versa.  Set to
         default format for new width. */
      *fmt = fmt_default_for_width (width);
    }
  else if (width > 0)
    {
      /* Changed width of string.  Preserve format type, adjust
         width. */
      fmt->w = fmt->type == FMT_AHEX ? width * 2 : width;
    }
  else
    {
      /* Still numeric. */
      return false;
    }
  return true;
}

/* Adjusts FMT's width and decimal places to be valid for USE.  */
void
fmt_fix (struct fmt_spec *fmt, enum fmt_use use)
{
  /* Clamp width to those allowed by format. */
  fmt_clamp_width (fmt, use);

  /* If FMT has more decimal places than allowed, attempt to increase FMT's
     width until that number of decimal places can be achieved. */
  if (fmt->d > fmt_max_decimals (fmt->type, fmt->w, use)
      && fmt_takes_decimals (fmt->type))
    {
      int max_w = fmt_max_width (fmt->type, use);
      for (; fmt->w < max_w; fmt->w++)
        if (fmt->d <= fmt_max_decimals (fmt->type, fmt->w, use))
          break;
    }

  /* Clamp decimals to those allowed by format and width. */
  fmt_clamp_decimals (fmt, use);
}

/* Adjusts FMT's width and decimal places to be valid for an
   input format.  */
void
fmt_fix_input (struct fmt_spec *fmt)
{
  fmt_fix (fmt, FMT_FOR_INPUT);
}

/* Adjusts FMT's width and decimal places to be valid for an
   output format.  */
void
fmt_fix_output (struct fmt_spec *fmt)
{
  fmt_fix (fmt, FMT_FOR_OUTPUT);
}

/* Sets FMT's width to WIDTH (or the nearest width allowed by FMT's type) and
   reduces its decimal places as necessary (if necessary) for that width.  */
void
fmt_change_width (struct fmt_spec *fmt, int width, enum fmt_use use)
{
  fmt->w = width;
  fmt_clamp_width (fmt, use);
  fmt_clamp_decimals (fmt, use);
}

/* Sets FMT's decimal places to DECIMALS (or the nearest number of decimal
   places allowed by FMT's type) and increases its width as necessary (if
   necessary) for that number of decimal places.  */
void
fmt_change_decimals (struct fmt_spec *fmt, int decimals, enum fmt_use use)
{
  fmt->d = decimals;
  fmt_fix (fmt, use);
}

/* Describes a display format. */
struct fmt_desc
  {
    char name[9];
    int min_input_width, min_output_width;
    int io;
    enum fmt_category category;
  };

static const struct fmt_desc *get_fmt_desc (enum fmt_type type);

/* Returns the name of the given format TYPE. */
const char *
fmt_name (enum fmt_type type)
{
  return get_fmt_desc (type)->name;
}

/* Tries to parse NAME as a format type.
   If successful, stores the type in *TYPE and returns true.
   On failure, returns false. */
bool
fmt_from_name (const char *name, enum fmt_type *type)
{
  int i;

  for (i = 0; i < FMT_NUMBER_OF_FORMATS; i++)
    if (!c_strcasecmp (name, get_fmt_desc (i)->name))
      {
        *type = i;
        return true;
      }
  return false;
}

/* Returns true if TYPE accepts decimal places,
   false otherwise. */
bool
fmt_takes_decimals (enum fmt_type type)
{
  return fmt_max_output_decimals (type, fmt_max_output_width (type)) > 0;
}

/* Returns the minimum width of the given format TYPE for the given USE. */
int
fmt_min_width (enum fmt_type type, enum fmt_use use)
{
  return (use == FMT_FOR_INPUT
          ? fmt_min_input_width (type)
          : fmt_min_output_width (type));
}

/* Returns the maximum width of the given format TYPE,
   for input if FOR_INPUT is true,
   for output otherwise. */
int
fmt_max_width (enum fmt_type type, enum fmt_use use UNUSED)
{
  /* Maximum width is actually invariant of whether the format is
     for input or output, so FOR_INPUT is unused. */
  assert (is_fmt_type (type));
  switch (type)
    {
    case FMT_P:
    case FMT_PK:
    case FMT_PIBHEX:
    case FMT_RBHEX:
      return 16;

    case FMT_IB:
    case FMT_PIB:
    case FMT_RB:
      return 8;

    case FMT_A:
      return MAX_STRING;

    case FMT_AHEX:
      return 2 * MAX_STRING;

    default:
      return 40;
    }
}

/* Returns the maximum number of decimal places allowed for the
   given format TYPE with a width of WIDTH places, for the given USE. */
int
fmt_max_decimals (enum fmt_type type, int width, enum fmt_use use)
{
  int max_d;

  switch (type)
    {
    case FMT_F:
    case FMT_COMMA:
    case FMT_DOT:
      max_d = use == FMT_FOR_INPUT ? width : width - 1;
      break;

    case FMT_DOLLAR:
    case FMT_PCT:
      max_d = use == FMT_FOR_INPUT ? width : width - 2;
      break;

    case FMT_E:
      max_d = use == FMT_FOR_INPUT ? width : width - 7;
      break;

    case FMT_CCA:
    case FMT_CCB:
    case FMT_CCC:
    case FMT_CCD:
    case FMT_CCE:
      assert (use == FMT_FOR_OUTPUT);
      max_d = width - 1;
      break;

    case FMT_N:
    case FMT_Z:
      max_d = width;
      break;

    case FMT_P:
      max_d = width * 2 - 1;
      break;

    case FMT_PK:
      max_d = width * 2;
      break;

    case FMT_IB:
    case FMT_PIB:
      max_d = max_digits_for_bytes (width);
      break;

    case FMT_PIBHEX:
      max_d = 0;
      break;

    case FMT_RB:
    case FMT_RBHEX:
      max_d = 16;
      break;

    case FMT_DATE:
    case FMT_ADATE:
    case FMT_EDATE:
    case FMT_JDATE:
    case FMT_SDATE:
    case FMT_QYR:
    case FMT_MOYR:
    case FMT_WKYR:
      max_d = 0;
      break;

    case FMT_DATETIME:
      max_d = width - 21;
      break;

    case FMT_YMDHMS:
      max_d = width - 20;
      break;

    case FMT_MTIME:
      max_d = width - 6;
      break;

    case FMT_TIME:
      max_d = width - 9;
      break;

    case FMT_DTIME:
      max_d = width - 12;
      break;

    case FMT_WKDAY:
    case FMT_MONTH:
    case FMT_A:
    case FMT_AHEX:
      max_d = 0;
      break;

    default:
      NOT_REACHED ();
    }

  if (max_d < 0)
    max_d = 0;
  else if (max_d > 16)
    max_d = 16;
  return max_d;
}

/* Returns the minimum acceptable width for an input field
   formatted with the given TYPE. */
int
fmt_min_input_width (enum fmt_type type)
{
  return get_fmt_desc (type)->min_input_width;
}

/* Returns the maximum acceptable width for an input field
   formatted with the given TYPE. */
int
fmt_max_input_width (enum fmt_type type)
{
  return fmt_max_width (type, FMT_FOR_INPUT);
}

/* Returns the maximum number of decimal places allowed in an
   input field of the given TYPE and WIDTH. */
int
fmt_max_input_decimals (enum fmt_type type, int width)
{
  assert (valid_width (type, width, true));
  return fmt_max_decimals (type, width, FMT_FOR_INPUT);
}

/* Returns the minimum acceptable width for an output field
   formatted with the given TYPE. */
int
fmt_min_output_width (enum fmt_type type)
{
  return get_fmt_desc (type)->min_output_width;
}

/* Returns the maximum acceptable width for an output field
   formatted with the given TYPE. */
int
fmt_max_output_width (enum fmt_type type)
{
  return fmt_max_width (type, FMT_FOR_OUTPUT);
}

/* Returns the maximum number of decimal places allowed in an
   output field of the given TYPE and WIDTH. */
int
fmt_max_output_decimals (enum fmt_type type, int width)
{
  assert (valid_width (type, width, false));
  return fmt_max_decimals (type, width, FMT_FOR_OUTPUT);
}

/* Returns the width step for a field formatted with the given
   TYPE.  Field width must be a multiple of the width step. */
int
fmt_step_width (enum fmt_type type)
{
  return (fmt_get_category (type) == FMT_CAT_HEXADECIMAL || type == FMT_AHEX
          ? 2 : 1);
}

/* Returns true if TYPE is used for string fields,
   false if it is used for numeric fields. */
bool
fmt_is_string (enum fmt_type type)
{
  return fmt_get_category (type) == FMT_CAT_STRING;
}

/* Returns true if TYPE is used for numeric fields,
   false if it is used for string fields. */
bool
fmt_is_numeric (enum fmt_type type)
{
  return !fmt_is_string (type);
}

/* Returns the format TYPE's category.
   Each format type is in exactly one category,
   and each category's value is bitwise disjoint from every other
   category.  Thus, the return value may be tested for equality
   or compared bitwise against a mask of FMT_CAT_* values. */
enum fmt_category
fmt_get_category (enum fmt_type type)
{
  return get_fmt_desc (type)->category;
}

/* Returns the output format selected by default when TYPE is
   used as an input format. */
enum fmt_type
fmt_input_to_output (enum fmt_type type)
{
  switch (fmt_get_category (type))
    {
    case FMT_CAT_STRING:
      return FMT_A;

    case FMT_CAT_LEGACY:
    case FMT_CAT_BINARY:
    case FMT_CAT_HEXADECIMAL:
      return FMT_F;

    default:
      return type;
    }
}

/* Returns the SPSS format type corresponding to the given PSPP
   format type. */
int
fmt_to_io (enum fmt_type type)
{
  return get_fmt_desc (type)->io;
}

/* Determines the PSPP format corresponding to the given SPSS
   format type.  If successful, sets *FMT_TYPE to the PSPP format
   and returns true.  On failure, return false. */
bool
fmt_from_io (int io, enum fmt_type *fmt_type)
{
  switch (io)
    {
#define FMT(NAME, METHOD, IMIN, OMIN, IO, CATEGORY)     \
    case IO:                                          \
      *fmt_type = FMT_##NAME;                           \
      return true;
#include "format.def"
    default:
      return false;
    }
}

/* Translate U32, which is in the form found in SAV and SPV files, into a
   format specification, and stores the new specification in *F.

   If LOOSE is false, checks that the format specification is appropriate as an
   output format for a variable with the given WIDTH and reports an error if
   not.  If LOOSE is true, instead adjusts the format's width and decimals as
   necessary to be suitable.

   Return true if successful, false if there was an error.. */
bool
fmt_from_u32 (uint32_t u32, int width, bool loose, struct fmt_spec *f)
{
  uint8_t raw_type = u32 >> 16;
  uint8_t w = u32 >> 8;
  uint8_t d = u32;

  msg_disable ();
  f->w = w;
  f->d = d;
  bool ok = fmt_from_io (raw_type, &f->type);
  if (ok)
    {
      if (loose)
        fmt_fix_output (f);
      else
        ok = fmt_check_output (f);
    }
  if (ok)
    ok = fmt_check_width_compat (f, width);
  msg_enable ();

  return ok;
}

/* Returns true if TYPE may be used as an input format,
   false otherwise. */
bool
fmt_usable_for_input (enum fmt_type type)
{
  assert (is_fmt_type (type));
  return fmt_get_category (type) != FMT_CAT_CUSTOM;
}

/* For time and date formats, returns a template used for input and output in a
   field of the given WIDTH.

   WIDTH only affects whether a 2-digit year or a 4-digit year is used, that
   is, whether the returned string contains "yy" or "yyyy", and whether seconds
   are include, that is, whether the returned string contains ":SS".  A caller
   that doesn't care whether the returned string contains "yy" or "yyyy" or
   ":SS" can just specify 0 to omit them. */
const char *
fmt_date_template (enum fmt_type type, int width)
{
  const char *s1, *s2;

  switch (type)
    {
    case FMT_DATE:
      s1 = "dd-mmm-yy";
      s2 = "dd-mmm-yyyy";
      break;

    case FMT_ADATE:
      s1 = "mm/dd/yy";
      s2 = "mm/dd/yyyy";
      break;

    case FMT_EDATE:
      s1 = "dd.mm.yy";
      s2 = "dd.mm.yyyy";
      break;

    case FMT_JDATE:
      s1 = "yyddd";
      s2 = "yyyyddd";
      break;

    case FMT_SDATE:
      s1 = "yy/mm/dd";
      s2 = "yyyy/mm/dd";
      break;

    case FMT_QYR:
      s1 = "q Q yy";
      s2 = "q Q yyyy";
      break;

    case FMT_MOYR:
      s1 = "mmm yy";
      s2 = "mmm yyyy";
      break;

    case FMT_WKYR:
      s1 = "ww WK yy";
      s2 = "ww WK yyyy";
      break;

    case FMT_DATETIME:
      s1 = "dd-mmm-yyyy HH:MM";
      s2 = "dd-mmm-yyyy HH:MM:SS";
      break;

    case FMT_YMDHMS:
      s1 = "yyyy-mm-dd HH:MM";
      s2 = "yyyy-mm-dd HH:MM:SS";
      break;

    case FMT_MTIME:
      s1 = "MM";
      s2 = "MM:SS";
      break;

    case FMT_TIME:
      s1 = "HH:MM";
      s2 = "HH:MM:SS";
      break;

    case FMT_DTIME:
      s1 = "D HH:MM";
      s2 = "D HH:MM:SS";
      break;

    default:
      NOT_REACHED ();
    }

  return width >= strlen (s2) ? s2 : s1;
}

/* Returns a string representing the format TYPE for use in a GUI dialog. */
const char *
fmt_gui_name (enum fmt_type type)
{
  switch (type)
    {
    case FMT_F:
      return _("Numeric");

    case FMT_COMMA:
      return _("Comma");

    case FMT_DOT:
      return _("Dot");

    case FMT_E:
      return _("Scientific");

    case FMT_DATE:
    case FMT_EDATE:
    case FMT_SDATE:
    case FMT_ADATE:
    case FMT_JDATE:
    case FMT_QYR:
    case FMT_MOYR:
    case FMT_WKYR:
    case FMT_DATETIME:
    case FMT_YMDHMS:
    case FMT_MTIME:
    case FMT_TIME:
    case FMT_DTIME:
    case FMT_WKDAY:
    case FMT_MONTH:
      return _("Date");

    case FMT_DOLLAR:
      return _("Dollar");

    case FMT_CCA:
    case FMT_CCB:
    case FMT_CCC:
    case FMT_CCD:
    case FMT_CCE:
      return _("Custom");

    case FMT_A:
      return _("String");

    default:
      return fmt_name (type);
    }
}

/* Returns true if TYPE is a valid format type,
   false otherwise. */
bool
is_fmt_type (enum fmt_type type)
{
  return type < FMT_NUMBER_OF_FORMATS;
}

/* Returns true if WIDTH is a valid width for the given format
   TYPE, for the given USE. */
static bool
valid_width (enum fmt_type type, int width, enum fmt_use use)
{
  return (width >= fmt_min_width (type, use)
          && width <= fmt_max_width (type, use));
}

/* Returns the maximum number of decimal digits in an unsigned
   binary number that is BYTES bytes long. */
static int
max_digits_for_bytes (int bytes)
{
  int map[8] = {3, 5, 8, 10, 13, 15, 17, 20};
  assert (bytes > 0 && bytes <= sizeof map / sizeof *map);
  return map[bytes - 1];
}

/* Clamp FMT's width to the range and values allowed by FMT's type. */
static void
fmt_clamp_width (struct fmt_spec *fmt, enum fmt_use use)
{
  unsigned int step;
  int min_w, max_w;

  min_w = fmt_min_width (fmt->type, use);
  max_w = fmt_max_width (fmt->type, use);
  if (fmt->w < min_w)
    fmt->w = min_w;
  else if (fmt->w > max_w)
    fmt->w = max_w;

  /* Round width to step. */
  step = fmt_step_width (fmt->type);
  fmt->w = ROUND_DOWN (fmt->w, step);
}

/* Clamp FMT's decimal places to the range allowed by FMT's type and width. */
static void
fmt_clamp_decimals (struct fmt_spec *fmt, enum fmt_use use)
{
  int max_d;

  max_d = fmt_max_decimals (fmt->type, fmt->w, use);
  if (fmt->d < 0)
    fmt->d = 0;
  else if (fmt->d > max_d)
    fmt->d = max_d;
}

static struct fmt_affix
fmt_affix_clone (const struct fmt_affix *old)
{
  return (struct fmt_affix) {
    .s = old->s ? xstrdup (old->s) : NULL,
    .width = old->width,
  };
}

/* Frees data in AFFIX. */
static void
fmt_affix_free (struct fmt_affix *affix)
{
  if (affix->s[0])
    free (affix->s);
}

/* Find and returns the grouping character in CC_STRING (either '.' or ',') or
   0 on error. */
static int
find_cc_separators (const char *cc_string)
{
  /* Count commas and periods.  There must be exactly three of
     one or the other, except that an apostrophe escapes a
     following comma or period. */
  int n_commas = 0;
  int n_dots = 0;
  for (const char *p = cc_string; *p; p++)
    if (*p == ',')
      n_commas++;
    else if (*p == '.')
      n_dots++;
    else if (*p == '\'' && (p[1] == '.' || p[1] == ',' || p[1] == '\''))
      p++;

  return (n_commas == 3 ? (n_dots != 3 ? ',' : 0)
          : n_dots == 3 ? '.'
          : 0);
}

/* Extracts a token from IN into a newly allocated string AFFIXP.  Tokens are
   delimited by GROUPING.  Returns the first character following the token. */
static struct fmt_affix
extract_cc_token (const char **sp, int grouping, size_t *extra_bytes)
{
  const char *p = *sp;
  for (; *p && *p != grouping; p++)
    if (*p == '\'' && p[1] == grouping)
      p++;

  size_t length = p - *sp;
  char *affix = xmemdup0 (*sp, length);
  size_t width = u8_strwidth (CHAR_CAST (const uint8_t *, affix), "UTF-8");
  if (length > width)
    *extra_bytes += length - width;

  *sp = p + (*p != 0);

  return (struct fmt_affix) { .s = affix, .width = width };
}

struct fmt_number_style *
fmt_number_style_from_string (const char *s)
{
  char grouping = find_cc_separators (s);
  if (!grouping)
    return NULL;

  size_t extra_bytes = 0;
  struct fmt_affix neg_prefix = extract_cc_token (&s, grouping, &extra_bytes);
  struct fmt_affix prefix = extract_cc_token (&s, grouping, &extra_bytes);
  struct fmt_affix suffix = extract_cc_token (&s, grouping, &extra_bytes);
  struct fmt_affix neg_suffix = extract_cc_token (&s, grouping, &extra_bytes);

  struct fmt_number_style *style = xmalloc (sizeof *style);
  *style = (struct fmt_number_style) {
    .neg_prefix = neg_prefix,
    .prefix = prefix,
    .suffix = suffix,
    .neg_suffix = neg_suffix,
    .decimal = grouping == '.' ? ',' : '.',
    .grouping = grouping,
    .extra_bytes = extra_bytes,
  };
  return style;
}

static void
format_cc (struct string *out, const char *in, char grouping)
{
  while (*in != '\0')
    {
      char c = *in++;
      if (c == grouping || c == '\'')
        ds_put_byte (out, '\'');
      else if (c == '"')
        ds_put_byte (out, '"');
      ds_put_byte (out, c);
    }
}

char *
fmt_number_style_to_string (const struct fmt_number_style *cc)
{
  struct string out = DS_EMPTY_INITIALIZER;
  format_cc (&out, cc->neg_prefix.s, cc->grouping);
  ds_put_byte (&out, cc->grouping);
  format_cc (&out, cc->prefix.s, cc->grouping);
  ds_put_byte (&out, cc->grouping);
  format_cc (&out, cc->suffix.s, cc->grouping);
  ds_put_byte (&out, cc->grouping);
  format_cc (&out, cc->neg_suffix.s, cc->grouping);
  return ds_steal_cstr (&out);
}

struct fmt_number_style *
fmt_number_style_clone (const struct fmt_number_style *old)
{
  if (old)
    {
      struct fmt_number_style *new = xmalloc (sizeof *new);
      *new = (struct fmt_number_style) {
        .neg_prefix = fmt_affix_clone (&old->neg_prefix),
        .prefix = fmt_affix_clone (&old->prefix),
        .suffix = fmt_affix_clone (&old->suffix),
        .neg_suffix = fmt_affix_clone (&old->neg_suffix),
        .decimal = old->decimal,
        .grouping = old->grouping,
        .extra_bytes = old->extra_bytes,
      };
      return new;
    }
  else
    return NULL;
}

/* Destroys a struct fmt_number_style. */
void
fmt_number_style_destroy (struct fmt_number_style *style)
{
  if (style != NULL)
    {
      fmt_affix_free (&style->neg_prefix);
      fmt_affix_free (&style->prefix);
      fmt_affix_free (&style->suffix);
      fmt_affix_free (&style->neg_suffix);
      free (style);
    }
}

/* Returns the total width of the standard prefix and suffix for STYLE, in
   display columns (e.g. as returned by u8_strwidth()). */
int
fmt_affix_width (const struct fmt_number_style *style)
{
  return style->prefix.width + style->suffix.width;
}

/* Returns the total width of the negative prefix and suffix for STYLE, in
   display columns (e.g. as returned by u8_strwidth()). */
int
fmt_neg_affix_width (const struct fmt_number_style *style)
{
  return style->neg_prefix.width + style->neg_suffix.width;
}

/* Returns the struct fmt_desc for the given format TYPE. */
static const struct fmt_desc *
get_fmt_desc (enum fmt_type type)
{
  static const struct fmt_desc formats[FMT_NUMBER_OF_FORMATS] =
    {
#define FMT(NAME, METHOD, IMIN, OMIN, IO, CATEGORY) \
	{#NAME, IMIN, OMIN, IO, CATEGORY},
#include "format.def"
    };

  assert (is_fmt_type (type));
  return &formats[type];
}

const struct fmt_spec F_8_0 = {FMT_F, 8, 0};
const struct fmt_spec F_8_2 = {FMT_F, 8, 2};
const struct fmt_spec F_4_3 = {FMT_F, 4, 3};
const struct fmt_spec F_5_1 = {FMT_F, 5, 1};
