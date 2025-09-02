# SAVE TRANSLATE

```
SAVE TRANSLATE
        /OUTFILE={'FILE_NAME',FILE_HANDLE}
        /TYPE={CSV,TAB}
        [/REPLACE]
        [/MISSING={IGNORE,RECODE}]

        [/DROP=VAR_LIST]
        [/KEEP=VAR_LIST]
        [/RENAME=(SRC_NAMES=TARGET_NAMES)...]
        [/UNSELECTED={RETAIN,DELETE}]
        [/MAP]

        ...additional subcommands depending on TYPE...
```

The `SAVE TRANSLATE` command is used to save data into various
formats understood by other applications.

The `OUTFILE` and `TYPE` subcommands are mandatory.  `OUTFILE`
specifies the file to be written, as a string file name or a [file
handle](../language/files/file-handles.md).  `TYPE` determines the
handle](../language/files/file-handles.md).  `TYPE` determines the
type of the file or source to read.  It must be one of the following:

* `CSV`  
  Comma-separated value format,

* `TAB`  
  Tab-delimited format.

By default, `SAVE TRANSLATE` does not overwrite an existing file.
Use `REPLACE` to force an existing file to be overwritten.

With `MISSING=IGNORE`, the default, `SAVE TRANSLATE` treats
user-missing values as if they were not missing.  Specify
`MISSING=RECODE` to output numeric user-missing values like
system-missing values and string user-missing values as all spaces.

By default, all the variables in the active dataset dictionary are
saved to the system file, but `DROP` or `KEEP` can select a subset of
variable to save.  The `RENAME` subcommand can also be used to change
the names under which variables are saved; because they are used only
in the output, these names do not have to conform to the usual PSPP
variable naming rules.  `UNSELECTED` determines whether cases filtered
out by the `FILTER` command are written to the output file.  These
subcommands have the same syntax and meaning as on the
[`SAVE`](save.md) command.

Each supported file type has additional subcommands, explained in
separate sections below.

`SAVE TRANSLATE` causes the data to be read.  It is a procedure.

## Comma- and Tab-Separated Data Files

```
SAVE TRANSLATE
        /OUTFILE={'FILE_NAME',FILE_HANDLE}
        /TYPE=CSV
        [/REPLACE]
        [/MISSING={IGNORE,RECODE}]

        [/DROP=VAR_LIST]
        [/KEEP=VAR_LIST]
        [/RENAME=(SRC_NAMES=TARGET_NAMES)...]
        [/UNSELECTED={RETAIN,DELETE}]

        [/FIELDNAMES]
        [/CELLS={VALUES,LABELS}]
        [/TEXTOPTIONS DELIMITER='DELIMITER']
        [/TEXTOPTIONS QUALIFIER='QUALIFIER']
        [/TEXTOPTIONS DECIMAL={DOT,COMMA}]
        [/TEXTOPTIONS FORMAT={PLAIN,VARIABLE}]
```

The `SAVE TRANSLATE` command with `TYPE=CSV` or `TYPE=TAB` writes data in a
comma- or tab-separated value format similar to that described by
RFC 4180.  Each variable becomes one output column, and each case
becomes one line of output.  If `FIELDNAMES` is specified, an additional
line at the top of the output file lists variable names.

The `CELLS` and `TEXTOPTIONS FORMAT` settings determine how values are
written to the output file:

* `CELLS=VALUES FORMAT=PLAIN` (the default settings)  
  Writes variables to the output in "plain" formats that ignore the
  details of variable formats.  Numeric values are written as plain
  decimal numbers with enough digits to indicate their exact values
  in machine representation.  Numeric values include `e` followed by
  an exponent if the exponent value would be less than -4 or greater
  than 16.  Dates are written in MM/DD/YYYY format and times in
  HH:MM:SS format.  `WKDAY` and `MONTH` values are written as decimal
  numbers.

  Numeric values use, by default, the decimal point character set with
  [`SET DECIMAL`](set.md#decimal).  Use `DECIMAL=DOT` or
  `DECIMAL=COMMA` to force a particular decimal point character.

* `CELLS=VALUES FORMAT=VARIABLE`  
  Writes variables using their print formats.  Leading and trailing
  spaces are removed from numeric values, and trailing spaces are
  removed from string values.

* `CELLS=LABEL FORMAT=PLAIN`  
  `CELLS=LABEL FORMAT=VARIABLE`  
  Writes value labels where they exist, and otherwise writes the
  values themselves as described above.

   Regardless of `CELLS` and `TEXTOPTIONS FORMAT`, numeric system-missing
values are output as a single space.

   For `TYPE=TAB`, tab characters delimit values.  For `TYPE=CSV`, the
`TEXTOPTIONS DELIMITER` and `DECIMAL` settings determine the character
that separate values within a line.  If `DELIMITER` is specified, then
the specified string separate values.  If `DELIMITER` is not
specified, then the default is a comma with `DECIMAL=DOT` or a
semicolon with `DECIMAL=COMMA`. If `DECIMAL` is not given either, it
is inferred from the decimal point character set with [`SET
DECIMAL`](set.md#decimal).

   The `TEXTOPTIONS QUALIFIER` setting specifies a character that is
output before and after a value that contains the delimiter character or
the qualifier character.  The default is a double quote (`"`).  A
qualifier character that appears within a value is doubled.

