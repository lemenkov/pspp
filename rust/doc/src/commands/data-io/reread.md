# REREAD

```
REREAD [FILE=handle] [COLUMN=column] [ENCODING='ENCODING'].
```

The `REREAD` transformation allows the previous input line in a data
file already processed by `DATA LIST` or another input command to be
re-read for further processing.

The `FILE` subcommand, which is optional, is used to specify the file
to have its line re-read.  The file must be specified as the name of a
[file handle](../../language/files/file-handles.md).  If `FILE` is not
specified then the file specified on the most recent `DATA LIST`
command is assumed.

By default, the line re-read is re-read in its entirety.  With the
`COLUMN` subcommand, a prefix of the line can be exempted from
re-reading.  Specify an
[expression](../../language/expressions/index.md) evaluating to the
first column that should be included in the re-read line.  Columns are
numbered from 1 at the left margin.

The `ENCODING` subcommand may only be used if the `FILE` subcommand is
also used.  It specifies the character encoding of the file.  See
[`INSERT`](../utilities/insert.md) for information on supported
encodings.

Issuing `REREAD` multiple times will not back up in the data file.
Instead, it will re-read the same line multiple times.

