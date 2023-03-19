# String Formats

The `A` and `AHEX` formats are the only ones that may be assigned to
string variables.  Neither format allows any decimal places.

   In `A` format, the entire field is treated as a string value.  The
field width may range from 1 to 32,767, the maximum string width.  The
default output format is the same as the input format.

   In `AHEX` format, the field is composed of characters in a string
encoded as hex digit pairs.  On output, hex digits are output in
uppercase; on input, uppercase and lowercase are both accepted.  The
default output format is `A` format with half the input width.

