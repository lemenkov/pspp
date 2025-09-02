# PRINT EJECT

```
PRINT EJECT
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

`PRINT EJECT` advances to the beginning of a new output page in the
listing file or output file.  It can also output data in the same way as
`PRINT`.

All `PRINT EJECT` subcommands are optional.

Without `OUTFILE`, `PRINT EJECT` ejects the current page in the
listing file, then it produces other output, if any is specified.

With `OUTFILE`, `PRINT EJECT` writes its output to the specified
file.  The first line of output is written with `1` inserted in the
first column.  Commonly, this is the only line of output.  If additional
lines of output are specified, these additional lines are written with a
space inserted in the first column, as with `PRINT`.

See [PRINT](print.md) for more information on syntax and usage.

