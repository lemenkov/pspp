# Time and Date Functions

For compatibility, PSPP considers dates before 15 Oct 1582 invalid.
Most time and date functions will not accept earlier dates.

## Time and Date Representations

Times and dates are handled by PSPP as single numbers.  A "time" is an
interval.  PSPP measures times in seconds.  Thus, the following
intervals correspond with the numeric values given:

|Interval                  |Numeric Value|
|:-------------------------|----------:|
|10 minutes                |        600|
|1 hour                    |      3,600|
|1 day, 3 hours, 10 seconds|     97,210|
|40 days                   |  3,456,000|

A "date", on the other hand, is a particular instant in the past or
the future.  PSPP represents a date as a number of seconds since
midnight preceding 14 Oct 1582.  Because midnight preceding the dates
given below correspond with the numeric PSPP dates given:

|Date             |   Numeric Value|
|:----------------|---------------:|
|15 Oct 1582      |          86,400|
| 4 Jul 1776      |   6,113,318,400|
| 1 Jan 1900      |  10,010,390,400|
| 1 Oct 1978      |  12,495,427,200|
|24 Aug 1995      |  13,028,601,600|

## Constructing Times

These functions take numeric arguments and return numeric values that
represent times.

* `TIME.DAYS(NDAYS)`  
  Returns a time corresponding to NDAYS days.

* `TIME.HMS(NHOURS, NMINS, NSECS)`  
  Returns a time corresponding to NHOURS hours, NMINS minutes, and
  NSECS seconds.  The arguments may not have mixed signs: if any of
  them are positive, then none may be negative, and vice versa.

## Examining Times

These functions take numeric arguments in PSPP time format and give
numeric results.

* `CTIME.DAYS(TIME)`  
  Results in the number of days and fractional days in TIME.

* `CTIME.HOURS(TIME)`  
  Results in the number of hours and fractional hours in TIME.

* `CTIME.MINUTES(TIME)`  
  Results in the number of minutes and fractional minutes in TIME.

* `CTIME.SECONDS(TIME)`  
  Results in the number of seconds and fractional seconds in TIME.
  (`CTIME.SECONDS` does nothing; `CTIME.SECONDS(X)` is equivalent to
  `X`.)

## Constructing Dates

These functions take numeric arguments and give numeric results that
represent dates.  Arguments taken by these functions are:

* `DAY`  
  Refers to a day of the month between 1 and 31.  Day 0 is also
  accepted and refers to the final day of the previous month.  Days
  29, 30, and 31 are accepted even in months that have fewer days and
  refer to a day near the beginning of the following month.

* `MONTH`  
  Refers to a month of the year between 1 and 12.  Months 0 and 13
  are also accepted and refer to the last month of the preceding year
  and the first month of the following year, respectively.

* `QUARTER`  
  Refers to a quarter of the year between 1 and 4.  The quarters of
  the year begin on the first day of months 1, 4, 7, and 10.

* `WEEK`  
  Refers to a week of the year between 1 and 53.

* `YDAY`  
  Refers to a day of the year between 1 and 366.

* `YEAR`  
  Refers to a year, 1582 or greater.  Years between 0 and 99 are
  treated according to the epoch set on [`SET
  EPOCH`](../../../commands/utilities/set.md#epoch), by default
  beginning 69 years before the current date.

If these functions' arguments are out-of-range, they are correctly
normalized before conversion to date format.  Non-integers are rounded
toward zero.

* `DATE.DMY(DAY, MONTH, YEAR)`  
  `DATE.MDY(MONTH, DAY, YEAR)`  
  Results in a date value corresponding to the midnight before day
  DAY of month MONTH of year YEAR.

* `DATE.MOYR(MONTH, YEAR)`  
  Results in a date value corresponding to the midnight before the
  first day of month MONTH of year YEAR.

* `DATE.QYR(QUARTER, YEAR)`  
  Results in a date value corresponding to the midnight before the
  first day of quarter QUARTER of year YEAR.

* `DATE.WKYR(WEEK, YEAR)`  
  Results in a date value corresponding to the midnight before the
  first day of week WEEK of year YEAR.

* `DATE.YRDAY(YEAR, YDAY)`  
  Results in a date value corresponding to the day YDAY of year YEAR.

## Examining Dates

These functions take numeric arguments in PSPP date or time format and
give numeric results.  These names are used for arguments:

* `DATE`  
  A numeric value in PSPP date format.

* `TIME`  
  A numeric value in PSPP time format.

* `TIME-OR-DATE`  
  A numeric value in PSPP time or date format.

The functions for examining dates are:

* `XDATE.DATE(TIME-OR-DATE)`  
  For a time, results in the time corresponding to the number of
  whole days DATE-OR-TIME includes.  For a date, results in the date
  corresponding to the latest midnight at or before DATE-OR-TIME;
  that is, gives the date that DATE-OR-TIME is in.

* `XDATE.HOUR(TIME-OR-DATE)`  
  For a time, results in the number of whole hours beyond the number
  of whole days represented by DATE-OR-TIME.  For a date, results in
  the hour(as an integer between 0 and 23) corresponding to
  DATE-OR-TIME.

* `XDATE.JDAY(DATE)`  
  Results in the day of the year (as an integer between 1 and 366)
  corresponding to DATE.

* `XDATE.MDAY(DATE)`  
  Results in the day of the month (as an integer between 1 and 31)
  corresponding to DATE.

* `XDATE.MINUTE(TIME-OR-DATE)`  
  Results in the number of minutes (as an integer between 0 and 59)
  after the last hour in TIME-OR-DATE.

* `XDATE.MONTH(DATE)`  
  Results in the month of the year (as an integer between 1 and 12)
  corresponding to DATE.

* `XDATE.QUARTER(DATE)`  
  Results in the quarter of the year (as an integer between 1 and 4)
  corresponding to DATE.

* `XDATE.SECOND(TIME-OR-DATE)`  
  Results in the number of whole seconds after the last whole minute
  (as an integer between 0 and 59) in TIME-OR-DATE.

* `XDATE.TDAY(DATE)`  
  Results in the number of whole days from 14 Oct 1582 to DATE.

* `XDATE.TIME(DATE)`  
  Results in the time of day at the instant corresponding to DATE, as
  a time value.  This is the number of seconds since midnight on the
  day corresponding to DATE.

* `XDATE.WEEK(DATE)`  
  Results in the week of the year (as an integer between 1 and 53)
  corresponding to DATE.

* `XDATE.WKDAY(DATE)`  
  Results in the day of week (as an integer between 1 and 7)
  corresponding to DATE, where 1 represents Sunday.

* `XDATE.YEAR(DATE)`  
  Returns the year (as an integer 1582 or greater) corresponding to
  DATE.

## Time and Date Arithmetic

Ordinary arithmetic operations on dates and times often produce sensible
results.  Adding a time to, or subtracting one from, a date produces a
new date that much earlier or later.  The difference of two dates yields
the time between those dates.  Adding two times produces the combined
time.  Multiplying a time by a scalar produces a time that many times
longer.  Since times and dates are just numbers, the ordinary addition
and subtraction operators are employed for these purposes.

   Adding two dates does not produce a useful result.

   Dates and times may have very large values.  Thus, it is not a good
idea to take powers of these values; also, the accuracy of some
procedures may be affected.  If necessary, convert times or dates in
seconds to some other unit, like days or years, before performing
analysis.

   PSPP supplies a few functions for date arithmetic:

* `DATEDIFF(DATE2, DATE1, UNIT)`  
  Returns the span of time from `DATE1` to `DATE2` in terms of `UNIT`,
  which must be a quoted string, one of `years`, `quarters`, `months`,
  `weeks`, `days`, `hours`, `minutes`, and `seconds`.  The result is
  an integer, truncated toward zero.

  One year is considered to span from a given date to the same month,
  day, and time of day the next year.  Thus, from January 1 of one
  year to January 1 the next year is considered to be a full year, but
  February 29 of a leap year to the following February 28 is not.
  Similarly, one month spans from a given day of the month to the same
  day of the following month.  Thus, there is never a full month from
  Jan. 31 of a given year to any day in the following February.

* `DATESUM(DATE, QUANTITY, UNIT[, METHOD])`  
  Returns `DATE` advanced by the given `QUANTITY` of the specified
  `UNIT`, which must be one of the strings `years`, `quarters`,
  `months`, `weeks`, `days`, `hours`, `minutes`, and `seconds`.

  When `UNIT` is `years`, `quarters`, or `months`, only the integer
  part of `QUANTITY` is considered.  Adding one of these units can
  cause the day of the month to exceed the number of days in the
  month.  In this case, the `METHOD` comes into play: if it is omitted
  or specified as `closest` (as a quoted string), then the resulting
  day is the last day of the month; otherwise, if it is specified as
  `rollover`, then the extra days roll over into the following month.

  When `UNIT` is `weeks`, `days`, `hours`, `minutes`, or `seconds`,
  the `QUANTITY` is not rounded to an integer and `METHOD`, if
  specified, is ignored.

