# LIST

```
LIST
        /VARIABLES=VAR_LIST
        /CASES=FROM START_INDEX TO END_INDEX BY INCR_INDEX
        /FORMAT={UNNUMBERED,NUMBERED} {WRAP,SINGLE}
```

   The `LIST` procedure prints the values of specified variables to the
listing file.

   The `VARIABLES` subcommand specifies the variables whose values are
to be printed.  Keyword `VARIABLES` is optional.  If the `VARIABLES`
subcommand is omitted then all variables in the active dataset are
printed.

   The `CASES` subcommand can be used to specify a subset of cases to be
printed.  Specify `FROM` and the case number of the first case to print,
`TO` and the case number of the last case to print, and `BY` and the
number of cases to advance between printing cases, or any subset of
those settings.  If `CASES` is not specified then all cases are printed.

   The `FORMAT` subcommand can be used to change the output format.
`NUMBERED` will print case numbers along with each case; `UNNUMBERED`,
the default, causes the case numbers to be omitted.  The `WRAP` and
`SINGLE` settings are currently not used.

   Case numbers start from 1.  They are counted after all
transformations have been considered.

   `LIST` is a procedure.  It causes the data to be read.

