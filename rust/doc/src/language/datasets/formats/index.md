# Input and Output Formats

An "input format" describes how to interpret the contents of an input
field as a number or a string.  It might specify that the field contains
an ordinary decimal number, a time or date, a number in binary or
hexadecimal notation, or one of several other notations.  Input formats
are used by commands such as `DATA LIST` that read data or syntax files
into the PSPP active dataset.

   Every input format corresponds to a default "output format" that
specifies the formatting used when the value is output later.  It is
always possible to explicitly specify an output format that resembles
the input format.  Usually, this is the default, but in cases where the
input format is unfriendly to human readability, such as binary or
hexadecimal formats, the default output format is an easier-to-read
decimal format.

   Every variable has two output formats, called its "print format"
and "write format".  Print formats are used in most output contexts;
only the [`WRITE`](../../../commands/data-io/write.md) command uses
write formats.  Newly created variables have identical print and write
formats, and [`FORMATS`](../../../commands/variables/formats.md), the
most commonly used command for changing formats, sets both of them to
the same value as well.  This means that the distinction between print
and write formats is usually unimportant.

   Input and output formats are specified to PSPP with a "format
specification" of the form `TypeW` or `TypeW.D`, where `Type` is one
of the format types described later, `W` is a field width measured in
columns, and `D` is an optional number of decimal places.  If `D` is
omitted, a value of 0 is assumed.  Some formats do not allow a nonzero
`D` to be specified.
