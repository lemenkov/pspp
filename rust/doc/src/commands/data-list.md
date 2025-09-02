# DATA LIST

Used to read text or binary data, `DATA LIST` is the most fundamental
data-reading command.  Even the more sophisticated input methods use
`DATA LIST` commands as a building block.  Understanding `DATA LIST` is
important to understanding how to use PSPP to read your data files.

   There are two major variants of `DATA LIST`, which are fixed format
and free format.  In addition, free format has a minor variant, list
format, which is discussed in terms of its differences from vanilla free
format.

   Each form of `DATA LIST` is described in detail below.

   See [`GET DATA`](get-data.md) for a command that offers a few
enhancements over DATA LIST and that may be substituted for DATA LIST
in many situations.

## DATA LIST FIXED

```
DATA LIST [FIXED]
        {TABLE,NOTABLE}
        [FILE='FILE_NAME' [ENCODING='ENCODING']]
        [RECORDS=RECORD_COUNT]
        [END=END_VAR]
        [SKIP=RECORD_COUNT]
        /[line_no] VAR_SPEC...

where each VAR_SPEC takes one of the forms
        VAR_LIST START-END [TYPE_SPEC]
        VAR_LIST (FORTRAN_SPEC)
```

   `DATA LIST FIXED` is used to read data files that have values at
fixed positions on each line of single-line or multiline records.  The
keyword `FIXED` is optional.

   The `FILE` subcommand must be used if input is to be taken from an
external file.  It may be used to specify a file name as a string or a
[file handle](../language/files/file-handles.md).  If the `FILE`
subcommand is not used, then input is assumed to be specified within
the command file using [`BEGIN DATA`...`END DATA`](begin-data.md).
The `ENCODING` subcommand may only be used if the `FILE` subcommand is
also used.  It specifies the character encoding of the file.  See
[`INSERT`](insert.md), for information on supported encodings.

   The optional `RECORDS` subcommand, which takes a single integer as an
argument, is used to specify the number of lines per record.  If
`RECORDS` is not specified, then the number of lines per record is
calculated from the list of variable specifications later in `DATA
LIST`.

   The `END` subcommand is only useful in conjunction with [`INPUT
PROGRAM`](input-program.md).

   The optional `SKIP` subcommand specifies a number of records to skip
at the beginning of an input file.  It can be used to skip over a row
that contains variable names, for example.

   `DATA LIST` can optionally output a table describing how the data
file is read.  The `TABLE` subcommand enables this output, and `NOTABLE`
disables it.  The default is to output the table.

   The list of variables to be read from the data list must come last.
Each line in the data record is introduced by a slash (`/`).
Optionally, a line number may follow the slash.  Following, any number
of variable specifications may be present.

   Each variable specification consists of a list of variable names
followed by a description of their location on the input line.  [Sets
of variables](../language/datasets/variable-lists.html) may be with
`TO`, e.g. `VAR1 TO VAR5`.  There are two ways to specify the location
of the variable on the line: columnar style and FORTRAN style.

   In columnar style, the starting column and ending column for the
field are specified after the variable name, separated by a dash
(`-`).  For instance, the third through fifth columns on a line would
be specified `3-5`.  By default, variables are considered to be in
[`F` format](../language/datasets/formats/basic.html).  (Use [`SET
FORMAT`](set.md#format) to change the default.)

   In columnar style, to use a variable format other than the default,
specify the format type in parentheses after the column numbers.  For
instance, for alphanumeric `A` format, use `(A)`.

   In addition, implied decimal places can be specified in parentheses
after the column numbers.  As an example, suppose that a data file has a
field in which the characters `1234` should be interpreted as having the
value 12.34.  Then this field has two implied decimal places, and the
corresponding specification would be `(2)`.  If a field that has implied
decimal places contains a decimal point, then the implied decimal places
are not applied.

   Changing the variable format and adding implied decimal places can be
done together; for instance, `(N,5)`.

   When using columnar style, the input and output width of each
variable is computed from the field width.  The field width must be
evenly divisible into the number of variables specified.

   FORTRAN style is an altogether different approach to specifying field
locations.  With this approach, a list of variable input format
specifications, separated by commas, are placed after the variable names
inside parentheses.  Each format specifier advances as many characters
into the input line as it uses.

   Implied decimal places also exist in FORTRAN style.  A format
specification with `D` decimal places also has `D` implied decimal places.

   In addition to the [standard
   formats](../language/datasets/formats/index.html), FORTRAN style
   defines some extensions:

* `X`  
  Advance the current column on this line by one character position.

* `T<X>`  
  Set the current column on this line to column `<X>`, with column
  numbers considered to begin with 1 at the left margin.

* `NEWREC<X>`  
  Skip forward `<X>` lines in the current record, resetting the active
  column to the left margin.

* Repeat count  
  Any format specifier may be preceded by a number.  This causes the
  action of that format specifier to be repeated the specified number
  of times.

* `(SPEC1, ..., SPECN)`  
  Use `()` to group specifiers together.  This is most useful when
  preceded by a repeat count.  Groups may be nested.

   FORTRAN and columnar styles may be freely intermixed.  Columnar style
leaves the active column immediately after the ending column specified.
Record motion using `NEWREC` in FORTRAN style also applies to later
FORTRAN and columnar specifiers.

### Example 1

```
DATA LIST TABLE /NAME 1-10 (A) INFO1 TO INFO3 12-17 (1).

BEGIN DATA.
John Smith 102311
Bob Arnold 122015
Bill Yates  918 6
END DATA.
```

Defines the following variables:

   - `NAME`, a 10-character-wide string variable, in columns 1
     through 10.

   - `INFO1`, a numeric variable, in columns 12 through 13.

   - `INFO2`, a numeric variable, in columns 14 through 15.

   - `INFO3`, a numeric variable, in columns 16 through 17.

The `BEGIN DATA`/`END DATA` commands cause three cases to be
defined:

|Case   |NAME         |INFO1   |INFO2   |INFO3|
|------:|:------------|-------:|-------:|----:|
|   1   |John Smith   |  10    |  23    |  11|
|   2   |Bob Arnold   |  12    |  20    |  15|
|   3   |Bill Yates   |   9    |  18    |   6|

The `TABLE` keyword causes PSPP to print out a table describing the
four variables defined.

### Example 2

```
DATA LIST FILE="survey.dat"
        /ID 1-5 NAME 7-36 (A) SURNAME 38-67 (A) MINITIAL 69 (A)
        /Q01 TO Q50 7-56
        /.
```

Defines the following variables:

   - `ID`, a numeric variable, in columns 1-5 of the first record.

   - `NAME`, a 30-character string variable, in columns 7-36 of the
     first record.

   - `SURNAME`, a 30-character string variable, in columns 38-67 of
     the first record.

   - `MINITIAL`, a 1-character string variable, in column 69 of the
     first record.

   - Fifty variables `Q01`, `Q02`, `Q03`, ..., `Q49`, `Q50`, all
     numeric, `Q01` in column 7, `Q02` in column 8, ..., `Q49` in
     column 55, `Q50` in column 56, all in the second record.

Cases are separated by a blank record.

Data is read from file `survey.dat` in the current directory.

## DATA LIST FREE

```
DATA LIST FREE
        [({TAB,'C'}, ...)]
        [{NOTABLE,TABLE}]
        [FILE='FILE_NAME' [ENCODING='ENCODING']]
        [SKIP=N_RECORDS]
        /VAR_SPEC...

where each VAR_SPEC takes one of the forms
        VAR_LIST [(TYPE_SPEC)]
        VAR_LIST *
```

   In free format, the input data is, by default, structured as a
series of fields separated by spaces, tabs, or line breaks.  If the
current [`DECIMAL`](set.md#decimal) separator is `DOT`, then commas
are also treated as field separators.  Each field's content may be
unquoted, or it may be quoted with a pairs of apostrophes (`'`) or
double quotes (`"`).  Unquoted white space separates fields but is not
part of any field.  Any mix of spaces, tabs, and line breaks is
equivalent to a single space for the purpose of separating fields, but
consecutive commas will skip a field.

   Alternatively, delimiters can be specified explicitly, as a
parenthesized, comma-separated list of single-character strings
immediately following `FREE`. The word `TAB` may also be used to
specify a tab character as a delimiter.  When delimiters are specified
explicitly, only the given characters, plus line breaks, separate
fields.  Furthermore, leading spaces at the beginnings of fields are
not trimmed, consecutive delimiters define empty fields, and no form
of quoting is allowed.

   The `NOTABLE` and `TABLE` subcommands are as in `DATA LIST FIXED`
above.  `NOTABLE` is the default.

   The `FILE`, `SKIP`, and `ENCODING` subcommands are as in `DATA LIST
FIXED` above.

   The variables to be parsed are given as a single list of variable
names.  This list must be introduced by a single slash (`/`).  The set
of variable names may contain [format
specifications](../language/datasets/formats/index.html) in
parentheses.  Format specifications apply to all variables back to the
previous parenthesized format specification.

   An asterisk on its own has the same effect as `(F8.0)`, assigning
the variables preceding it input/output format `F8.0`.

   Specified field widths are ignored on input, although all normal
limits on field width apply, but they are honored on output.

## DATA LIST LIST

```
DATA LIST LIST
        [({TAB,'C'}, ...)]
        [{NOTABLE,TABLE}]
        [FILE='FILE_NAME' [ENCODING='ENCODING']]
        [SKIP=RECORD_COUNT]
        /VAR_SPEC...

where each VAR_SPEC takes one of the forms
        VAR_LIST [(TYPE_SPEC)]
        VAR_LIST *
```

   With one exception, `DATA LIST LIST` is syntactically and
semantically equivalent to `DATA LIST FREE`.  The exception is that each
input line is expected to correspond to exactly one input record.  If
more or fewer fields are found on an input line than expected, an
appropriate diagnostic is issued.

