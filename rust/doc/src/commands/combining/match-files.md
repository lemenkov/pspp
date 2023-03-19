# MATCH FILES

```
MATCH FILES

Per input file:
        /{FILE,TABLE}={*,'FILE_NAME'}
        [/RENAME=(SRC_NAMES=TARGET_NAMES)...]
        [/IN=VAR_NAME]
        [/SORT]

Once per command:
        /BY VAR_LIST[({D|A}] [VAR_LIST[({D|A})]...]
        [/DROP=VAR_LIST]
        [/KEEP=VAR_LIST]
        [/FIRST=VAR_NAME]
        [/LAST=VAR_NAME]
        [/MAP]
```

`MATCH FILES` merges sets of corresponding cases in multiple input
files into single cases in the output, combining their data.

`MATCH FILES` shares the bulk of its syntax with other PSPP commands
for combining multiple data files (see [Common
Syntax](index.md#common-syntax) for details).

How `MATCH FILES` matches up cases from the input files depends on
whether `BY` is specified:

- If `BY` is not used, `MATCH FILES` combines the first case from
  each input file to produce the first output case, then the second
  case from each input file for the second output case, and so on.
  If some input files have fewer cases than others, then the shorter
  files do not contribute to cases output after their input has been
  exhausted.

- If `BY` is used, `MATCH FILES` combines cases from each input file
  that have identical values for the `BY` variables.

  When `BY` is used, `TABLE` subcommands may be used to introduce
  "table lookup files".  `TABLE` has same syntax as `FILE`, and the
  `RENAME`, `IN`, and `SORT` subcommands may follow a `TABLE` in the
  same way as `FILE`.  Regardless of the number of `TABLE`s, at least
  one `FILE` must specified.  Table lookup files are treated in the
  same way as other input files for most purposes and, in particular,
  table lookup files must be sorted on the `BY` variables or the
  `SORT` subcommand must be specified for that `TABLE`.

  Cases in table lookup files are not consumed after they have been
  used once.  This means that data in table lookup files can
  correspond to any number of cases in `FILE` input files.  Table
  lookup files are analogous to lookup tables in traditional
  relational database systems.

  If a table lookup file contains more than one case with a given set
  of `BY` variables, only the first case is used.

When `MATCH FILES` creates an output case, variables that are only in
files that are not present for the current case are set to the
system-missing value for numeric variables or spaces for string
variables.

