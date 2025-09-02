# Binary and Hexadecimal Numeric Formats

The binary and hexadecimal formats are primarily designed for
compatibility with existing machine formats, not for human
readability.  All of them therefore have a `F` format as default
output format.  Some of these formats are only portable between
machines with compatible byte ordering (endianness).

   Binary formats use byte values that in text files are interpreted
as special control functions, such as carriage return and line feed.
Thus, data in binary formats should not be included in syntax files or
read from data files with variable-length records, such as ordinary
text files.  They may be read from or written to data files with
fixed-length records.  See [`FILE
HANDLE`](../../../commands/file-handle.md), for information on
working with fixed-length records.

## `P` and `PK` Formats

These are binary-coded decimal formats, in which every byte (except
the last, in `P` format) represents two decimal digits.  The
most-significant 4 bits of the first byte is the most-significant
decimal digit, the least-significant 4 bits of the first byte is the
next decimal digit, and so on.

   In `P` format, the most-significant 4 bits of the last byte are the
least-significant decimal digit.  The least-significant 4 bits
represent the sign: decimal 15 indicates a negative value, decimal 13
indicates a positive value.

   Numbers are rounded downward on output.  The system-missing value and
numbers outside representable range are output as zero.

   The maximum field width is 16.  Decimal places may range from 0 up to
the number of decimal digits represented by the field.

   The default output format is an `F` format with twice the input
field width, plus one column for a decimal point (if decimal places
were requested).

## `IB` and `PIB` Formats

These are integer binary formats.  `IB` reads and writes 2's
complement binary integers, and `PIB` reads and writes unsigned binary
integers.  The byte ordering is by default the host machine's, but
[`SET RIB`](../../../commands/set.md#rib) may be used to select a
specific byte ordering for reading and [`SET
WIB`](../../../commands/set.md#wib), similarly, for writing.

   The maximum field width is 8.  Decimal places may range from 0 up to
the number of decimal digits in the largest value representable in the
field width.

   The default output format is an `F` format whose width is the
number of decimal digits in the largest value representable in the
field width, plus 1 if the format has decimal places.

## `RB` Format

This is a binary format for real numbers.  It reads and writes the
host machine's floating-point format.  The byte ordering is by default
the host machine's, but [`SET RIB`](../../../commands/set.md#rib) may
be used to select a specific byte ordering for reading and [`SET
WIB`](../../../commands/set.md#wib), similarly, for writing.

The field width should be 4, for 32-bit floating-point numbers, or 8,
for 64-bit floating-point numbers.  Other field widths do not produce
useful results.  The maximum field width is 8.  No decimal places may
be specified.

   The default output format is `F8.2`.

## `PIBHEX` and `RBHEX` Formats

These are hexadecimal formats, for reading and writing binary formats
where each byte has been recoded as a pair of hexadecimal digits.

   A hexadecimal field consists solely of hexadecimal digits `0`...`9`
and `A`...`F`.  Uppercase and lowercase are accepted on input; output is
in uppercase.

   Other than the hexadecimal representation, these formats are
equivalent to `PIB` and `RB` formats, respectively.  However, bytes in
`PIBHEX` format are always ordered with the most-significant byte
first (big-endian order), regardless of the host machine's native byte
order or PSPP settings.

   Field widths must be even and between 2 and 16.  `RBHEX` format
allows no decimal places; `PIBHEX` allows as many decimal places as a
`PIB` format with half the given width.

