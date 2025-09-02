# REPEATING DATA

```
REPEATING DATA
        /STARTS=START-END
        /OCCURS=N_OCCURS
        /FILE='FILE_NAME'
        /LENGTH=LENGTH
        /CONTINUED[=CONT_START-CONT_END]
        /ID=ID_START-ID_END=ID_VAR
        /{TABLE,NOTABLE}
        /DATA=VAR_SPEC...

where each VAR_SPEC takes one of the forms
        VAR_LIST START-END [TYPE_SPEC]
        VAR_LIST (FORTRAN_SPEC)
```

`REPEATING DATA` parses groups of data repeating in a uniform format,
possibly with several groups on a single line.  Each group of data
corresponds with one case.  `REPEATING DATA` may only be used within
[`INPUT PROGRAM`](input-program.md).  When used with [`DATA
LIST`](data-list.md), it can be used to parse groups of cases that
share a subset of variables but differ in their other data.

The `STARTS` subcommand is required.  Specify a range of columns,
using literal numbers or numeric variable names.  This range specifies
the columns on the first line that are used to contain groups of data.
The ending column is optional.  If it is not specified, then the
record width of the input file is used.  For the [inline
file](begin-data.md), this is 80 columns; for a file with fixed record
widths it is the record width; for other files it is 1024 characters
by default.

The `OCCURS` subcommand is required.  It must be a number or the name
of a numeric variable.  Its value is the number of groups present in the
current record.

The `DATA` subcommand is required.  It must be the last subcommand
specified.  It is used to specify the data present within each
repeating group.  Column numbers are specified relative to the
beginning of a group at column 1.  Data is specified in the same way
as with [`DATA LIST FIXED`](data-list.md#data-list-fixed).

All other subcommands are optional.

`FILE` specifies the file to read, either a file name as a string or a
[file handle](../language/files/file-handles.md).  If `FILE` is not
[file handle](../language/files/file-handles.md).  If `FILE` is not
present then the default is the last file handle used on the most
recent `DATA LIST` command.

By default `REPEATING DATA` will output a table describing how it
will parse the input data.  Specifying `NOTABLE` will disable this
behavior; specifying `TABLE` will explicitly enable it.

The `LENGTH` subcommand specifies the length in characters of each
group.  If it is not present then length is inferred from the `DATA`
subcommand.  `LENGTH` may be a number or a variable name.

Normally all the data groups are expected to be present on a single
line.  Use the `CONTINUED` command to indicate that data can be
continued onto additional lines.  If data on continuation lines starts
at the left margin and continues through the entire field width, no
column specifications are necessary on `CONTINUED`.  Otherwise, specify
the possible range of columns in the same way as on `STARTS`.

When data groups are continued from line to line, it is easy for
cases to get out of sync through careless hand editing.  The `ID`
subcommand allows a case identifier to be present on each line of
repeating data groups.  `REPEATING DATA` will check for the same
identifier on each line and report mismatches.  Specify the range of
columns that the identifier will occupy, followed by an equals sign
(`=`) and the identifier variable name.  The variable must already have
been declared with `NUMERIC` or another command.

`REPEATING DATA` should be the last command given within an [`INPUT
PROGRAM`](input-program.md).  It should not be enclosed within
[`LOOP`…`END LOOP`](loop.md).  Use `DATA LIST` before, not after,
[`REPEATING DATA`](repeating-data.md).
