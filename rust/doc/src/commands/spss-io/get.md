# GET

```
GET
        /FILE={'FILE_NAME',FILE_HANDLE}
        /DROP=VAR_LIST
        /KEEP=VAR_LIST
        /RENAME=(SRC_NAMES=TARGET_NAMES)...
        /ENCODING='ENCODING'
```

   `GET` clears the current dictionary and active dataset and replaces
them with the dictionary and data from a specified file.

   The `FILE` subcommand is the only required subcommand.  Specify the
SPSS system file, SPSS/PC+ system file, or SPSS portable file to be
read as a string file name or a [file
handle](../../language/files/file-handles.md).

   By default, all the variables in a file are read.  The `DROP`
subcommand can be used to specify a list of variables that are not to
be read.  By contrast, the `KEEP` subcommand can be used to specify
variable that are to be read, with all other variables not read.

   Normally variables in a file retain the names that they were saved
under.  Use the `RENAME` subcommand to change these names.  Specify,
within parentheses, a list of variable names followed by an equals sign
(`=`) and the names that they should be renamed to.  Multiple
parenthesized groups of variable names can be included on a single
`RENAME` subcommand.  Variables' names may be swapped using a `RENAME`
subcommand of the form `/RENAME=(A B=B A)`.

   Alternate syntax for the `RENAME` subcommand allows the parentheses
to be omitted.  When this is done, only a single variable may be
renamed at once.  For instance, `/RENAME=A=B`.  This alternate syntax
is discouraged.

   `DROP`, `KEEP`, and `RENAME` are executed in left-to-right order.
Each may be present any number of times.  `GET` never modifies a file on
disk.  Only the active dataset read from the file is affected by these
subcommands.

   PSPP automatically detects the encoding of string data in the file,
when possible.  The character encoding of old SPSS system files cannot
always be guessed correctly, and SPSS/PC+ system files do not include
any indication of their encoding.  Specify the `ENCODING` subcommand
with an IANA character set name as its string argument to override the
default.  Use `SYSFILE INFO` to analyze the encodings that might be
valid for a system file.  The `ENCODING` subcommand is a PSPP extension.

   `GET` does not cause the data to be read, only the dictionary.  The
data is read later, when a procedure is executed.

   Use of `GET` to read a portable file is a PSPP extension.

