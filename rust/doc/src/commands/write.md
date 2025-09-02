# WRITE

```
WRITE
        OUTFILE='FILE_NAME'
        RECORDS=N_LINES
        {NOTABLE,TABLE}
        /[LINE_NO] ARG...

ARG takes one of the following forms:
        'STRING' [START-END]
        VAR_LIST START-END [TYPE_SPEC]
        VAR_LIST (FORTRAN_SPEC)
        VAR_LIST *
```

`WRITE` writes text or binary data to an output file.  `WRITE` differs
from [`PRINT`](print.md) in only a few ways:

- `WRITE` uses write formats by default, whereas `PRINT` uses print
  formats.

- `PRINT` inserts a space between variables unless a format is
  explicitly specified, but `WRITE` never inserts space between
  variables in output.

- `PRINT` inserts a space at the beginning of each line that it writes
  to an output file (and `PRINT EJECT` inserts `1` at the beginning of
  each line that should begin a new page), but `WRITE` does not.

- `PRINT` outputs the system-missing value according to its specified
  output format, whereas `WRITE` outputs the system-missing value as a
  field filled with spaces.  Binary formats are an exception.

