# Legacy Numeric Formats

The `N` and `Z` numeric formats provide compatibility with legacy file
formats.  They have much in common:

   - Output is rounded to the nearest representable value, with ties
     rounded away from zero.

   - Numbers too large to display are output as a field filled with
     asterisks (`*`).

   - The decimal point is always implicitly the specified number of
     digits from the right edge of the field, except that `Z` format input
     allows an explicit decimal point.

   - Scientific notation may not be used.

   - The system-missing value is output as a period in a field of
     spaces.  The period is placed just to the right of the implied
     decimal point in `Z` format, or at the right end in `N` format or
     in `Z` format if no decimal places are requested.  A period is
     used even if the decimal point character is a comma.

   - Field width may range from 1 to 40.  Decimal places may range from
     0 up to the field width, to a maximum of 16.

   - When a legacy numeric format used for input is converted to an
     output format, it is changed into the equivalent `F` format.  The
     field width is increased by 1 if any decimal places are
     specified, to make room for a decimal point.  For `Z` format, the
     field width is increased by 1 more column, to make room for a
     negative sign.  The output field width is capped at 40 columns.

## `N` Format

The `N` format supports input and output of fields that contain only
digits.  On input, leading or trailing spaces, a decimal point, or any
other non-digit character causes the field to be read as the
system-missing value.  As a special exception, an `N` format used on
`DATA LIST FREE` or `DATA LIST LIST` is treated as the equivalent `F`
format.

   On output, `N` pads the field on the left with zeros.  Negative
numbers are output like the system-missing value.

## `Z` Format

The `Z` format is a "zoned decimal" format used on IBM mainframes.  `Z`
format encodes the sign as part of the final digit, which must be one of
the following:

```
0123456789
{ABCDEFGHI
}JKLMNOPQR
```

where the characters on each line represent digits 0 through 9 in
order.  Characters on the first two lines indicate a positive sign;
those on the third indicate a negative sign.

   On output, `Z` fields are padded on the left with spaces.  On
input, leading and trailing spaces are ignored.  Any character in an
input field other than spaces, the digit characters above, and `.`
causes the field to be read as system-missing.

   The decimal point character for input and output is always `.`,
even if the decimal point character is a comma (see [`SET
DECIMAL`](../../../commands/set.md#decimal)).

   Nonzero, negative values output in `Z` format are marked as
negative even when no nonzero digits are output.  For example, -0.2 is
output in `Z1.0` format as `J`.  The "negative zero" value supported
by most machines is output as positive.

