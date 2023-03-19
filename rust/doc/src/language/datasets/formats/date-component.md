# Date Component Formats

The `WKDAY` and `MONTH` formats provide input and output for the names of
weekdays and months, respectively.

   On output, these formats convert a number between 1 and 7, for
`WKDAY`, or between 1 and 12, for `MONTH`, into the English name of a
day or month, respectively.  If the name is longer than the field, it
is trimmed to fit.  If the name is shorter than the field, it is
padded on the right with spaces.  Values outside the valid range, and
the system-missing value, are output as all spaces.

   On input, English weekday or month names (in uppercase or lowercase)
are converted back to their corresponding numbers.  Weekday and month
names may be abbreviated to their first 2 or 3 letters, respectively.

   The field width may range from 2 to 40, for `WKDAY`, or from 3 to
40, for `MONTH`. No decimal places are allowed.

   The default output format is the same as the input format.

