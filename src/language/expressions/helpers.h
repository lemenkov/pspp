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

#ifndef EXPRESSIONS_HELPERS_H
#define EXPRESSIONS_HELPERS_H

#include <ctype.h>
#include <float.h>
#include <gsl/gsl_cdf.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_sf.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>

#include "data/calendar.h"
#include "data/case.h"
#include "data/data-in.h"
#include "data/data-out.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/settings.h"
#include "data/value.h"
#include "data/variable.h"
#include "data/vector.h"
#include "language/expressions/public.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"
#include "math/distributions.h"
#include "math/moments.h"
#include "math/random.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct expr_node;

static inline double check_errno (double x)
{
  return errno == 0 ? x : SYSMIS;
}

#define check_errno(EXPRESSION) (errno = 0, check_errno (EXPRESSION))

#define DAY_S (60. * 60. * 24.)         /* Seconds per day. */
#define DAY_H 24.                       /* Hours per day. */
#define H_S (60 * 60.)                  /* Seconds per hour. */
#define H_MIN 60.                       /* Minutes per hour. */
#define MIN_S 60.                       /* Seconds per minute. */
#define WEEK_DAY 7.                     /* Days per week. */
#define WEEK_S (WEEK_DAY * DAY_S)       /* Seconds per week. */

extern const struct substring empty_string;

int compare_string_3way (const struct substring *, const struct substring *);

double expr_ymd_to_date (int year, int month, int day,
                         const struct expression *, const struct expr_node *,
                         int ya, int ma, int da);
double expr_ymd_to_ofs (int y, int m, int d,
                        const struct expression *, const struct expr_node *,
                        int ya, int ma, int da);
double expr_date_difference (double date1, double date2,
                             struct substring unit, const struct expression *,
                             const struct expr_node *);
double expr_date_sum (double date, double quantity, struct substring unit_name,
                      struct substring method_name,
                      const struct expression *, const struct expr_node *);
double expr_date_sum_closest (double date, double quantity,
                              struct substring unit_name,
                              const struct expression *,
                              const struct expr_node *);

struct substring alloc_string (struct expression *, size_t length);
struct substring copy_string (struct expression *,
                              const char *, size_t length);

static inline bool
is_valid (double d)
{
  return isfinite (d) && d != SYSMIS;
}

size_t count_valid (double *, size_t);

double round_nearest (double x, double mult, double fuzzbits);
double round_zero (double x, double mult, double fuzzbits);

struct substring replace_string (struct expression *,
                                 struct substring haystack,
                                 struct substring needle,
                                 struct substring replacement,
                                 int n);

double median (double *, size_t n);

const struct variable *expr_index_vector (const struct expression *,
                                          const struct expr_node *,
                                          const struct vector *, double idx);

#endif /* expressions/helpers.h */
