/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2010, 2011, 2015, 2016 Free Software Foundation, Inc.

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

#include "language/expressions/helpers.h"

#include <gsl/gsl_roots.h>
#include <gsl/gsl_sf.h>

#include "language/expressions/private.h"
#include "libpspp/assertion.h"
#include "libpspp/pool.h"

#include "gl/minmax.h"

const struct substring empty_string = {NULL, 0};

double
expr_ymd_to_ofs (double year, double month, double day)
{
  int y = year;
  int m = month;
  int d = day;
  char *error;
  double ofs;

  if (y != year || m != month || d != day)
    {
      msg (SE, _("One of the arguments to a DATE function is not an integer.  "
                 "The result will be system-missing."));
      return SYSMIS;
    }

  ofs = calendar_gregorian_to_offset (y, m, d, settings_get_fmt_settings (),
                                      &error);
  if (error != NULL)
    {
      msg (SE, "%s", error);
      free (error);
    }
  return ofs;
}

double
expr_ymd_to_date (double year, double month, double day)
{
  double ofs = expr_ymd_to_ofs (year, month, day);
  return ofs != SYSMIS ? ofs * DAY_S : SYSMIS;
}

double
expr_wkyr_to_date (double week, double year)
{
  int w = week;

  if (w != week)
    {
      msg (SE, _("The week argument to DATE.WKYR is not an integer.  "
                 "The result will be system-missing."));
      return SYSMIS;
    }
  else if (w < 1 || w > 53)
    {
      msg (SE, _("The week argument to DATE.WKYR is outside the acceptable "
                 "range of 1 to 53.  "
                 "The result will be system-missing."));
      return SYSMIS;
    }
  else
    {
      double yr_1_1 = expr_ymd_to_ofs (year, 1, 1);
      if (yr_1_1 != SYSMIS)
        return DAY_S * (yr_1_1 + WEEK_DAY * (w - 1));
      else
        return SYSMIS;
    }
}

double
expr_yrday_to_date (double year, double yday)
{
  int yd = yday;

  if (yd != yday)
    {
      msg (SE, _("The day argument to DATE.YRDAY is not an integer.  "
                 "The result will be system-missing."));
      return SYSMIS;
    }
  else if (yd < 1 || yd > 366)
    {
      msg (SE, _("The day argument to DATE.YRDAY is outside the acceptable "
                 "range of 1 to 366.  "
                 "The result will be system-missing."));
      return SYSMIS;
    }
  else
    {
      double yr_1_1 = expr_ymd_to_ofs (year, 1, 1);
      if (yr_1_1 != SYSMIS)
        return DAY_S * (yr_1_1 + yd - 1.);
      else
        return SYSMIS;
    }
}

double
expr_yrmoda (double year, double month, double day)
{
  if (year >= 0 && year <= 99)
    year += 1900;
  else if (year != (int) year && year > 47516)
    {
      msg (SE, _("The year argument to YRMODA is greater than 47516.  "
                 "The result will be system-missing."));
      return SYSMIS;
    }

  return expr_ymd_to_ofs (year, month, day);
}

/* A date unit. */
enum date_unit
  {
    DATE_YEARS,
    DATE_QUARTERS,
    DATE_MONTHS,
    DATE_WEEKS,
    DATE_DAYS,
    DATE_HOURS,
    DATE_MINUTES,
    DATE_SECONDS
  };

/* Stores in *UNIT the unit whose name is NAME.
   Return success. */
static enum date_unit
recognize_unit (struct substring name, enum date_unit *unit)
{
  struct unit_name
    {
      enum date_unit unit;
      const struct substring name;
    };
  static const struct unit_name unit_names[] =
    {
      { DATE_YEARS, SS_LITERAL_INITIALIZER ("years") },
      { DATE_QUARTERS, SS_LITERAL_INITIALIZER ("quarters") },
      { DATE_MONTHS, SS_LITERAL_INITIALIZER ("months") },
      { DATE_WEEKS, SS_LITERAL_INITIALIZER ("weeks") },
      { DATE_DAYS, SS_LITERAL_INITIALIZER ("days") },
      { DATE_HOURS, SS_LITERAL_INITIALIZER ("hours") },
      { DATE_MINUTES, SS_LITERAL_INITIALIZER ("minutes") },
      { DATE_SECONDS, SS_LITERAL_INITIALIZER ("seconds") },
    };
  const int n_unit_names = sizeof unit_names / sizeof *unit_names;

  const struct unit_name *un;

  for (un = unit_names; un < &unit_names[n_unit_names]; un++)
    if (ss_equals_case (un->name, name))
      {
        *unit = un->unit;
        return true;
      }

  msg (SE, _("Unrecognized date unit `%.*s'.  "
             "Valid date units are `%s', `%s', `%s', "
             "`%s', `%s', `%s', `%s', and `%s'."),
       (int) ss_length (name), ss_data (name),
       "years", "quarters", "months",
       "weeks", "days", "hours", "minutes", "seconds");

  return false;
}

/* Returns the number of whole years from DATE1 to DATE2,
   where a year is defined as the same or later month, day, and
   time of day. */
static int
year_diff (double date1, double date2)
{
  int y1, m1, d1, yd1;
  int y2, m2, d2, yd2;
  int diff;

  assert (date2 >= date1);
  calendar_offset_to_gregorian (date1 / DAY_S, &y1, &m1, &d1, &yd1);
  calendar_offset_to_gregorian (date2 / DAY_S, &y2, &m2, &d2, &yd2);

  diff = y2 - y1;
  if (diff > 0)
    {
      int yd1 = 32 * m1 + d1;
      int yd2 = 32 * m2 + d2;
      if (yd2 < yd1
          || (yd2 == yd1 && fmod (date2, DAY_S) < fmod (date1, DAY_S)))
        diff--;
    }
  return diff;
}

/* Returns the number of whole months from DATE1 to DATE2,
   where a month is defined as the same or later day and time of
   day. */
static int
month_diff (double date1, double date2)
{
  int y1, m1, d1, yd1;
  int y2, m2, d2, yd2;
  int diff;

  assert (date2 >= date1);
  calendar_offset_to_gregorian (date1 / DAY_S, &y1, &m1, &d1, &yd1);
  calendar_offset_to_gregorian (date2 / DAY_S, &y2, &m2, &d2, &yd2);

  diff = ((y2 * 12) + m2) - ((y1 * 12) + m1);
  if (diff > 0
      && (d2 < d1
          || (d2 == d1 && fmod (date2, DAY_S) < fmod (date1, DAY_S))))
    diff--;
  return diff;
}

/* Returns the number of whole quarter from DATE1 to DATE2,
   where a quarter is defined as three months. */
static int
quarter_diff (double date1, double date2)
{
  return month_diff (date1, date2) / 3;
}

/* Returns the number of seconds in the given UNIT. */
static int
date_unit_duration (enum date_unit unit)
{
  switch (unit)
    {
    case DATE_WEEKS:
      return WEEK_S;

    case DATE_DAYS:
      return DAY_S;

    case DATE_HOURS:
      return H_S;

    case DATE_MINUTES:
      return MIN_S;

    case DATE_SECONDS:
      return 1;

    default:
      NOT_REACHED ();
    }
}

/* Returns the span from DATE1 to DATE2 in terms of UNIT_NAME. */
double
expr_date_difference (double date1, double date2, struct substring unit_name)
{
  enum date_unit unit;

  if (!recognize_unit (unit_name, &unit))
    return SYSMIS;

  switch (unit)
    {
    case DATE_YEARS:
      return (date2 >= date1
              ? year_diff (date1, date2)
              : -year_diff (date2, date1));

    case DATE_QUARTERS:
      return (date2 >= date1
              ? quarter_diff (date1, date2)
              : -quarter_diff (date2, date1));

    case DATE_MONTHS:
      return (date2 >= date1
              ? month_diff (date1, date2)
              : -month_diff (date2, date1));

    case DATE_WEEKS:
    case DATE_DAYS:
    case DATE_HOURS:
    case DATE_MINUTES:
    case DATE_SECONDS:
      return trunc ((date2 - date1) / date_unit_duration (unit));
    }

  NOT_REACHED ();
}

/* How to deal with days out of range for a given month. */
enum date_sum_method
  {
    SUM_ROLLOVER,       /* Roll them over to the next month. */
    SUM_CLOSEST         /* Use the last day of the month. */
  };

/* Stores in *METHOD the method whose name is NAME.
   Return success. */
static bool
recognize_method (struct substring method_name, enum date_sum_method *method)
{
  if (ss_equals_case (method_name, ss_cstr ("closest")))
    {
      *method = SUM_CLOSEST;
      return true;
    }
  else if (ss_equals_case (method_name, ss_cstr ("rollover")))
    {
      *method = SUM_ROLLOVER;
      return true;
    }
  else
    {
      msg (SE, _("Invalid DATESUM method.  "
                 "Valid choices are `%s' and `%s'."), "closest", "rollover");
      return false;
    }
}

/* Returns DATE advanced by the given number of MONTHS, with
   day-of-month overflow resolved using METHOD. */
static double
add_months (double date, int months, enum date_sum_method method)
{
  int y, m, d, yd;
  double output;
  char *error;

  calendar_offset_to_gregorian (date / DAY_S, &y, &m, &d, &yd);
  y += months / 12;
  m += months % 12;
  if (m < 1)
    {
      m += 12;
      y--;
    }
  else if (m > 12)
    {
      m -= 12;
      y++;
    }
  assert (m >= 1 && m <= 12);

  if (method == SUM_CLOSEST && d > calendar_days_in_month (y, m))
    d = calendar_days_in_month (y, m);

  output = calendar_gregorian_to_offset (y, m, d, settings_get_fmt_settings (),
                                         &error);
  if (output != SYSMIS)
    output = (output * DAY_S) + fmod (date, DAY_S);
  else
    {
      msg (SE, "%s", error);
      free (error);
    }
  return output;
}

/* Returns DATE advanced by the given QUANTITY of units given in
   UNIT_NAME, with day-of-month overflow resolved using
   METHOD_NAME. */
double
expr_date_sum (double date, double quantity, struct substring unit_name,
               struct substring method_name)
{
  enum date_unit unit;
  enum date_sum_method method;

  if (!recognize_unit (unit_name, &unit)
      || !recognize_method (method_name, &method))
    return SYSMIS;

  switch (unit)
    {
    case DATE_YEARS:
      return add_months (date, trunc (quantity) * 12, method);

    case DATE_QUARTERS:
      return add_months (date, trunc (quantity) * 3, method);

    case DATE_MONTHS:
      return add_months (date, trunc (quantity), method);

    case DATE_WEEKS:
    case DATE_DAYS:
    case DATE_HOURS:
    case DATE_MINUTES:
    case DATE_SECONDS:
      return date + quantity * date_unit_duration (unit);
    }

  NOT_REACHED ();
}

int
compare_string_3way (const struct substring *a, const struct substring *b)
{
  size_t i;

  for (i = 0; i < a->length && i < b->length; i++)
    if (a->string[i] != b->string[i])
      return a->string[i] < b->string[i] ? -1 : 1;
  for (; i < a->length; i++)
    if (a->string[i] != ' ')
      return 1;
  for (; i < b->length; i++)
    if (b->string[i] != ' ')
      return -1;
  return 0;
}

size_t
count_valid (double *d, size_t n)
{
  size_t n_valid = 0;
  for (size_t i = 0; i < n; i++)
    n_valid += is_valid (d[i]);
  return n_valid;
}

struct substring
alloc_string (struct expression *e, size_t length)
{
  struct substring s;
  s.length = length;
  s.string = pool_alloc (e->eval_pool, length);
  return s;
}

struct substring
copy_string (struct expression *e, const char *old, size_t length)
{
  struct substring s = alloc_string (e, length);
  memcpy (s.string, old, length);
  return s;
}

static double
round__ (double x, double mult, double fuzzbits, double adjustment)
{
  if (fuzzbits <= 0)
    fuzzbits = settings_get_fuzzbits ();
  adjustment += exp2 (fuzzbits - DBL_MANT_DIG);

  x /= mult;
  x = x >= 0. ? floor (x + adjustment) : -floor (-x + adjustment);
  return x * mult;
}

double
round_nearest (double x, double mult, double fuzzbits)
{
  return round__ (x, mult, fuzzbits, .5);
}

double
round_zero (double x, double mult, double fuzzbits)
{
  return round__ (x, mult, fuzzbits, 0);
}

struct substring
replace_string (struct expression *e,
                struct substring haystack,
                struct substring needle,
                struct substring replacement,
                double n)
{
  if (!needle.length
      || haystack.length < needle.length
      || n <= 0
      || n == SYSMIS)
    return haystack;

  struct substring result = alloc_string (e, MAX_STRING);
  result.length = 0;

  size_t i = 0;
  while (i <= haystack.length - needle.length)
    if (!memcmp (&haystack.string[i], needle.string, needle.length))
      {
        size_t copy_len = MIN (replacement.length, MAX_STRING - result.length);
        memcpy (&result.string[result.length], replacement.string, copy_len);
        result.length += copy_len;
        i += needle.length;

        if (--n < 1)
          break;
      }
    else
      {
        if (result.length < MAX_STRING)
          result.string[result.length++] = haystack.string[i];
        i++;
      }
  while (i < haystack.length && result.length < MAX_STRING)
    result.string[result.length++] = haystack.string[i++];

  return result;
}

static int
compare_doubles (const void *a_, const void *b_)
{
  const double *ap = a_;
  const double *bp = b_;
  double a = *ap;
  double b = *bp;

  /* Sort SYSMIS to the end. */
  return (a == b ? 0
          : a == SYSMIS ? 1
          : b == SYSMIS ? -1
          : a > b ? 1 : -1);
}

double
median (double *a, size_t n)
{
  /* Sort the array in-place, sorting SYSMIS to the end. */
  qsort (a, n, sizeof *a, compare_doubles);

  /* Drop SYSMIS. */
  n = count_valid (a, n);

  return (!n ? SYSMIS
          : n % 2 ? a[n / 2]
          : (a[n / 2 - 1] + a[n / 2]) / 2.0);
}
