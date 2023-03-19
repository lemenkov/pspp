# Basic Numeric Formats

The basic numeric formats are used for input and output of real numbers
in standard or scientific notation.  The following table shows an
example of how each format displays positive and negative numbers with
the default decimal point setting:

|Format         |3141.59        |-3141.59|
|:--------------|--------------:|---------:|
|`F8.2`         |` 3141.59`     |`-3141.59`|
|`COMMA9.2`     |` 3,141.59`    |`-3,141.59`|
|`DOT9.2`       |` 3.141,59`    |`-3.141,59`|
|`DOLLAR10.2`   |` $3,141.59`   |`-$3,141.59`|
|`PCT9.2`       |` 3141.59%`    |`-3141.59%`|
|`E8.1`         |` 3.1E+003`    |`-3.1E+003`|

   On output, numbers in `F` format are expressed in standard decimal
notation with the requested number of decimal places.  The other formats
output some variation on this style:

   - Numbers in `COMMA` format are additionally grouped every three digits
     by inserting a grouping character.  The grouping character is
     ordinarily a comma, but it can be changed to a period (with [`SET
     DECIMAL`](../../../commands/utilities/set.md#decimal)).

   - `DOT` format is like `COMMA` format, but it interchanges the role of
     the decimal point and grouping characters.  That is, the current
     grouping character is used as a decimal point and vice versa.

   - `DOLLAR` format is like `COMMA` format, but it prefixes the number with
     `$`.

   - `PCT` format is like `F` format, but adds `%` after the number.

   - The `E` format always produces output in scientific notation.

   On input, the basic numeric formats accept positive and numbers in
standard decimal notation or scientific notation.  Leading and trailing
spaces are allowed.  An empty or all-spaces field, or one that contains
only a single period, is treated as the system missing value.

   In scientific notation, the exponent may be introduced by a sign (`+`
or `-`), or by one of the letters `e` or `d` (in uppercase or
lowercase), or by a letter followed by a sign.  A single space may
follow the letter or the sign or both.

   On [fixed-format `DATA
LIST`](../../../commands/data-io/data-list.md#data-list-fixed) and in
a few other contexts, decimals are implied when the field does not
contain a decimal point.  In `F6.5` format, for example, the field
`314159` is taken as the value 3.14159 with implied decimals.
Decimals are never implied if an explicit decimal point is present or
if scientific notation is used.

   `E` and `F` formats accept the basic syntax already described.  The other
formats allow some additional variations:

- `COMMA`, `DOLLAR`, and `DOT` formats ignore grouping characters within
  the integer part of the input field.  The identity of the grouping
  character depends on the format.

- `DOLLAR` format allows a dollar sign to precede the number.  In a
  negative number, the dollar sign may precede or follow the minus
  sign.

- `PCT` format allows a percent sign to follow the number.

   All of the basic number formats have a maximum field width of 40 and
accept no more than 16 decimal places, on both input and output.  Some
additional restrictions apply:

- As input formats, the basic numeric formats allow no more decimal
  places than the field width.  As output formats, the field width
  must be greater than the number of decimal places; that is, large
  enough to allow for a decimal point and the number of requested
  decimal places.  `DOLLAR` and `PCT` formats must allow an additional
  column for `$` or `%`.

- The default output format for a given input format increases the
  field width enough to make room for optional input characters.  If
  an input format calls for decimal places, the width is increased by
  1 to make room for an implied decimal point.  `COMMA`, `DOT`, and
  `DOLLAR` formats also increase the output width to make room for
  grouping characters.  `DOLLAR` and `PCT` further increase the output
  field width by 1 to make room for `$` or `%`.  The increased output
  width is capped at 40, the maximum field width.

- The `E` format is exceptional.  For output, `E` format has a minimum
  width of 7 plus the number of decimal places.  The default output
  format for an `E` input format is an `E` format with at least 3 decimal
  places and thus a minimum width of 10.

More details of basic numeric output formatting are given below:

- Output rounds to nearest, with ties rounded away from zero.  Thus,
  2.5 is output as `3` in `F1.0` format, and -1.125 as `-1.13` in `F5.1`
  format.

- The system-missing value is output as a period in a field of
  spaces, placed in the decimal point's position, or in the rightmost
  column if no decimal places are requested.  A period is used even
  if the decimal point character is a comma.

- A number that does not fill its field is right-justified within the
  field.

- A number is too large for its field causes decimal places to be
  dropped to make room.  If dropping decimals does not make enough
  room, scientific notation is used if the field is wide enough.  If
  a number does not fit in the field, even in scientific notation,
  the overflow is indicated by filling the field with asterisks
  (`*`).

- `COMMA`, `DOT`, and `DOLLAR` formats insert grouping characters only if
  space is available for all of them.  Grouping characters are never
  inserted when all decimal places must be dropped.  Thus, 1234.56 in
  `COMMA5.2` format is output as ` 1235` without a comma, even though
  there is room for one, because all decimal places were dropped.

- `DOLLAR` or `PCT` format drop the `$` or `%` only if the number would
  not fit at all without it.  Scientific notation with `$` or `%` is
  preferred to ordinary decimal notation without it.

- Except in scientific notation, a decimal point is included only when
  it is followed by a digit.  If the integer part of the number being
  output is 0, and a decimal point is included, then PSPP ordinarily
  drops the zero before the decimal point.  However, in `F`, `COMMA`,
  or `DOT` formats, PSPP keeps the zero if [`SET
  LEADZERO`](../../../commands/utilities/set.md#leadzero) is set to
  `ON`.

  In scientific notation, the number always includes a decimal point,
  even if it is not followed by a digit.

- A negative number includes a minus sign only in the presence of a
  nonzero digit: -0.01 is output as `-.01` in `F4.2` format but as
  `  .0` in `F4.1` format.  Thus, a "negative zero" never includes a
  minus sign.

- In negative numbers output in `DOLLAR` format, the dollar sign
  follows the negative sign.  Thus, -9.99 in `DOLLAR6.2` format is
  output as `-$9.99`.

- In scientific notation, the exponent is output as `E` followed by
  `+` or `-` and exactly three digits.  Numbers with magnitude less
  than 10**-999 or larger than 10**999 are not supported by most
  computers, but if they are supported then their output is
  considered to overflow the field and they are output as asterisks.

- On most computers, no more than 15 decimal digits are significant
  in output, even if more are printed.  In any case, output precision
  cannot be any higher than input precision; few data sets are
  accurate to 15 digits of precision.  Unavoidable loss of precision
  in intermediate calculations may also reduce precision of output.

- Special values such as infinities and "not a number" values are
  usually converted to the system-missing value before printing.  In
  a few circumstances, these values are output directly.  In fields
  of width 3 or greater, special values are output as however many
  characters fit from `+Infinity` or `-Infinity` for infinities, from
  `NaN` for "not a number," or from `Unknown` for other values (if
  any are supported by the system).  In fields under 3 columns wide,
  special values are output as asterisks.
