# PRINT

```
PRINT
        [OUTFILE='FILE_NAME']
        [RECORDS=N_LINES]
        [{NOTABLE,TABLE}]
        [ENCODING='ENCODING']
        [/[LINE_NO] ARG...]

ARG takes one of the following forms:
        'STRING' [START]
        VAR_LIST START-END [TYPE_SPEC]
        VAR_LIST (FORTRAN_SPEC)
        VAR_LIST *
```

   The `PRINT` transformation writes variable data to the listing file
or an output file.  `PRINT` is executed when a procedure causes the
data to be read.  Follow `PRINT` by
[`EXECUTE`](../utilities/execute.md) to print variable data without
invoking a procedure.

   All `PRINT` subcommands are optional.  If no strings or variables are
specified, `PRINT` outputs a single blank line.

   The `OUTFILE` subcommand specifies the file to receive the output.
The file may be a file name as a string or a [file
handle](../../language/files/file-handles.md).  If `OUTFILE` is not
present then output is sent to PSPP's output listing file.  When
`OUTFILE` is present, the output is written to the file in a plain
text format, with a space inserted at beginning of each output line,
even lines that otherwise would be blank.

   The `ENCODING` subcommand may only be used if the `OUTFILE`
subcommand is also used.  It specifies the character encoding of the
file.  See [INSERT](../utilities/insert.md), for information on
supported encodings.

   The `RECORDS` subcommand specifies the number of lines to be output.
The number of lines may optionally be surrounded by parentheses.

   `TABLE` will cause the `PRINT` command to output a table to the
listing file that describes what it will print to the output file.
`NOTABLE`, the default, suppresses this output table.

   Introduce the strings and variables to be printed with a slash (`/`).
Optionally, the slash may be followed by a number indicating which
output line is specified.  In the absence of this line number, the next
line number is specified.  Multiple lines may be specified using
multiple slashes with the intended output for a line following its
respective slash.

   Literal strings may be printed.  Specify the string itself.
Optionally the string may be followed by a column number, specifying the
column on the line where the string should start.  Otherwise, the string
is printed at the current position on the line.

   Variables to be printed can be specified in the same ways as
available for [`DATA LIST FIXED`](data-list.md#data-list-fixed).  In addition,
a variable list may be followed by an asterisk (`*`), which indicates
that the variables should be printed in their dictionary print formats,
separated by spaces.  A variable list followed by a slash or the end of
command is interpreted in the same way.

   If a FORTRAN type specification is used to move backwards on the
current line, then text is written at that point on the line, the line
is truncated to that length, although additional text being added will
again extend the line to that length.

