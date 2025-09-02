# Time and Date Formats

In PSPP, a "time" is an interval.  The time formats translate between
human-friendly descriptions of time intervals and PSPP's internal
representation of time intervals, which is simply the number of seconds
in the interval.  PSPP has three time formats:

|Time Format   |Template                    |Example|
|:-------------|:---------------------------|:---------------------------|
|MTIME         |`MM:SS.ss`                  |`91:17.01`|
|TIME          |`hh:MM:SS.ss`               |`01:31:17.01`|
|DTIME         |`DD HH:MM:SS.ss`            |`00 04:31:17.01`|

   A "date" is a moment in the past or the future.  Internally, PSPP
represents a date as the number of seconds since the "epoch", midnight,
Oct.  14, 1582.  The date formats translate between human-readable dates
and PSPP's numeric representation of dates and times.  PSPP has several
date formats:

|Date Format   |Template                    |Example|
|:-------------|:---------------------------|:---------------------------|
|DATE          |`dd-mmm-yyyy`               |`01-OCT-1978`|
|ADATE         |`mm/dd/yyyy`                |`10/01/1978`|
|EDATE         |`dd.mm.yyyy`                |`01.10.1978`|
|JDATE         |`yyyyjjj`                   |`1978274`|
|SDATE         |`yyyy/mm/dd`                |`1978/10/01`|
|QYR           |`q Q yyyy`                  |`3 Q 1978`|
|MOYR          |`mmm yyyy`                  |`OCT 1978`|
|WKYR          |`ww WK yyyy`                |`40 WK 1978`|
|DATETIME      |`dd-mmm-yyyy HH:MM:SS.ss`   |`01-OCT-1978 04:31:17.01`|
|YMDHMS        |`yyyy-mm-dd HH:MM:SS.ss`    |`1978-01-OCT 04:31:17.01`|

   The templates in the preceding tables describe how the time and date
formats are input and output:

* `dd`  
  Day of month, from 1 to 31.  Always output as two digits.

* `mm`  
  `mmm`  
  Month.  In output, `mm` is output as two digits, `mmm` as the first
  three letters of an English month name (January, February, ...).
  In input, both of these formats, plus Roman numerals, are accepted.

* `yyyy`  
  Year.  In output, `DATETIME` and `YMDHMS` always produce 4-digit
  years; other formats can produce a 2- or 4-digit year.  The century
  assumed for 2-digit years depends on the
  [`EPOCH`](../../../commands/set.md#epoch) setting.  In output, a
  year outside the epoch causes the whole field to be filled with
  asterisks (`*`).

* `jjj`  
  Day of year (Julian day), from 1 to 366.  This is exactly three
  digits giving the count of days from the start of the year.
  January 1 is considered day 1.

* `q`  
  Quarter of year, from 1 to 4.  Quarters start on January 1, April
  1, July 1, and October 1.

* `ww`  
  Week of year, from 1 to 53.  Output as exactly two digits.  January
  1 is the first day of week 1.

* `DD`  
  Count of days, which may be positive or negative.  Output as at
  least two digits.

* `hh`  
  Count of hours, which may be positive or negative.  Output as at
  least two digits.

* `HH`  
  Hour of day, from 0 to 23.  Output as exactly two digits.

* `MM`  
  In MTIME, count of minutes, which may be positive or negative.
  Output as at least two digits.

  In other formats, minute of hour, from 0 to 59.  Output as exactly
  two digits.

* `SS.ss`  
  Seconds within minute, from 0 to 59.  The integer part is output as
  exactly two digits.  On output, seconds and fractional seconds may
  or may not be included, depending on field width and decimal places.
  On input, seconds and fractional seconds are optional.  The
  `DECIMAL` setting controls the character accepted and displayed as
  the decimal point (see [`SET
  DECIMAL`](../../../commands/set.md#decimal)).

   For output, the date and time formats use the delimiters indicated in
the table.  For input, date components may be separated by spaces or by
one of the characters `-`, `/`, `.`, or `,`, and time components may be
separated by spaces or `:`.  On input, the `Q` separating quarter from
year and the `WK` separating week from year may be uppercase or
lowercase, and the spaces around them are optional.

   On input, all time and date formats accept any amount of leading and
trailing white space.

   The maximum width for time and date formats is 40 columns.  Minimum
input and output width for each of the time and date formats is shown
below:

|Format       |Min. Input Width    |Min. Output Width    |Option|
|:------------|-------------------:|--------------------:|:------------|
|`DATE`       |8                   |9                    |4-digit year|
|`ADATE`      |8                   |8                    |4-digit year|
|`EDATE`      |8                   |8                    |4-digit year|
|`JDATE`      |5                   |5                    |4-digit year|
|`SDATE`      |8                   |8                    |4-digit year|
|`QYR`        |4                   |6                    |4-digit year|
|`MOYR`       |6                   |6                    |4-digit year|
|`WKYR`       |6                   |8                    |4-digit year|
|`DATETIME`   |17                  |17                   |seconds|
|`YMDHMS`     |12                  |16                   |seconds|
|`MTIME`      |4                   |5
|`TIME`       |5                   |5                    |seconds|
|`DTIME`      |8                   |8                    |seconds|

In the table, "Option" describes what increased output width enables:

* "4-digit year": A field 2 columns wider than the minimum includes a
  4-digit year.  (`DATETIME` and `YMDHMS` formats always include a
  4-digit year.)

* "seconds": A field 3 columns wider than the minimum includes seconds
  as well as minutes.  A field 5 columns wider than minimum, or more,
  can also include a decimal point and fractional seconds (but no more
  than allowed by the format's decimal places).

   For the time and date formats, the default output format is the same
as the input format, except that PSPP increases the field width, if
necessary, to the minimum allowed for output.

   Time or dates narrower than the field width are right-justified
within the field.

   When a time or date exceeds the field width, characters are trimmed
from the end until it fits.  This can occur in an unusual situation,
e.g. with a year greater than 9999 (which adds an extra digit), or for
a negative value on `MTIME`, `TIME`, or `DTIME` (which adds a leading
minus sign).

   The system-missing value is output as a period at the right end of
the field.

