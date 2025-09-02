# GET DATA

```
GET DATA
        /TYPE={GNM,ODS,PSQL,TXT}
        ...additional subcommands depending on TYPE...
```

   The `GET DATA` command is used to read files and other data sources
created by other applications.  When this command is executed, the
current dictionary and active dataset are replaced with variables and
data read from the specified source.

   The `TYPE` subcommand is mandatory and must be the first subcommand
specified.  It determines the type of the file or source to read.
PSPP currently supports the following `TYPE`s:

* `GNM`  
  Spreadsheet files created by Gnumeric (<http://gnumeric.org>).

* `ODS`  
  Spreadsheet files in OpenDocument format
  (<http://opendocumentformat.org>).

* `PSQL`  
  Relations from PostgreSQL databases (<http://postgresql.org>).

* `TXT`  
  Textual data files in columnar and delimited formats.

Each supported file type has additional subcommands, explained in
separate sections below.

## Spreadsheet Files

```
GET DATA /TYPE={GNM, ODS}
         /FILE={'FILE_NAME'}
         /SHEET={NAME 'SHEET_NAME', INDEX N}
         /CELLRANGE={RANGE 'RANGE', FULL}
         /READNAMES={ON, OFF}
         /ASSUMEDSTRWIDTH=N.
```

`GET DATA` can read Gnumeric spreadsheets (<http://gnumeric.org>), and
spreadsheets in OpenDocument format
(<http://libreplanet.org/wiki/Group:OpenDocument/Software>).  Use the
`TYPE` subcommand to indicate the file's format.  `/TYPE=GNM`
indicates Gnumeric files, `/TYPE=ODS` indicates OpenDocument.  The
`FILE` subcommand is mandatory.  Use it to specify the name file to be
read.  All other subcommands are optional.

   The format of each variable is determined by the format of the
spreadsheet cell containing the first datum for the variable.  If this
cell is of string (text) format, then the width of the variable is
determined from the length of the string it contains, unless the
`ASSUMEDSTRWIDTH` subcommand is given.

   The `SHEET` subcommand specifies the sheet within the spreadsheet
file to read.  There are two forms of the `SHEET` subcommand.  In the
first form, `/SHEET=name SHEET_NAME`, the string SHEET_NAME is the name
of the sheet to read.  In the second form, `/SHEET=index IDX`, IDX is a
integer which is the index of the sheet to read.  The first sheet has
the index 1.  If the `SHEET` subcommand is omitted, then the command
reads the first sheet in the file.

   The `CELLRANGE` subcommand specifies the range of cells within the
sheet to read.  If the subcommand is given as `/CELLRANGE=FULL`, then
the entire sheet is read.  To read only part of a sheet, use the form
`/CELLRANGE=range 'TOP_LEFT_CELL:BOTTOM_RIGHT_CELL'`.  For example,
the subcommand `/CELLRANGE=range 'C3:P19'` reads columns C-P and rows
3-19, inclusive.  Without the `CELLRANGE` subcommand, the entire sheet
is read.

   If `/READNAMES=ON` is specified, then the contents of cells of the
first row are used as the names of the variables in which to store the
data from subsequent rows.  This is the default.  If `/READNAMES=OFF` is
used, then the variables receive automatically assigned names.

   The `ASSUMEDSTRWIDTH` subcommand specifies the maximum width of
string variables read from the file.  If omitted, the default value is
determined from the length of the string in the first spreadsheet cell
for each variable.

## Postgres Database Queries

```
GET DATA /TYPE=PSQL
         /CONNECT={CONNECTION INFO}
         /SQL={QUERY}
         [/ASSUMEDSTRWIDTH=W]
         [/UNENCRYPTED]
         [/BSIZE=N].
```

   `GET DATA /TYPE=PSQL` imports data from a local or remote Postgres
database server.  It automatically creates variables based on the table
column names or the names specified in the SQL query.  PSPP cannot
support the full precision of some Postgres data types, so data of those
types will lose some precision when PSPP imports them.  PSPP does not
support all Postgres data types.  If PSPP cannot support a datum, `GET
DATA` issues a warning and substitutes the system-missing value.

   The `CONNECT` subcommand must be a string for the parameters of the
database server from which the data should be fetched.  The format of
the string is given [in the Postgres
manual](http://www.postgresql.org/docs/8.0/static/libpq.html#LIBPQ-CONNECT).

   The `SQL` subcommand must be a valid SQL statement to retrieve data
from the database.

   The `ASSUMEDSTRWIDTH` subcommand specifies the maximum width of
string variables read from the database.  If omitted, the default value
is determined from the length of the string in the first value read for
each variable.

   The `UNENCRYPTED` subcommand allows data to be retrieved over an
insecure connection.  If the connection is not encrypted, and the
`UNENCRYPTED` subcommand is not given, then an error occurs.  Whether or
not the connection is encrypted depends upon the underlying psql library
and the capabilities of the database server.

   The `BSIZE` subcommand serves only to optimise the speed of data
transfer.  It specifies an upper limit on number of cases to fetch from
the database at once.  The default value is 4096.  If your SQL statement
fetches a large number of cases but only a small number of variables,
then the data transfer may be faster if you increase this value.
Conversely, if the number of variables is large, or if the machine on
which PSPP is running has only a small amount of memory, then a smaller
value is probably better.

### Example

```
GET DATA /TYPE=PSQL
     /CONNECT='host=example.com port=5432 dbname=product user=fred passwd=xxxx'
     /SQL='select * from manufacturer'.
```

## Textual Data Files

```
GET DATA /TYPE=TXT
        /FILE={'FILE_NAME',FILE_HANDLE}
        [ENCODING='ENCODING']
        [/ARRANGEMENT={DELIMITED,FIXED}]
        [/FIRSTCASE={FIRST_CASE}]
        [/IMPORTCASES=...]
        ...additional subcommands depending on ARRANGEMENT...
```

   When `TYPE=TXT` is specified, `GET DATA` reads data in a delimited
or fixed columnar format, much like [`DATA
LIST`](data-list.md).

   The `FILE` subcommand must specify the file to be read as a string
file name or (for textual data only) a [file
handle](../language/files/file-handles.md)).

   The `ENCODING` subcommand specifies the character encoding of the
file to be read.  See [`INSERT`](insert.md), for information on
supported encodings.

   The `ARRANGEMENT` subcommand determines the file's basic format.
`DELIMITED`, the default setting, specifies that fields in the input data
are separated by spaces, tabs, or other user-specified delimiters.
`FIXED` specifies that fields in the input data appear at particular fixed
column positions within records of a case.

   By default, cases are read from the input file starting from the
first line.  To skip lines at the beginning of an input file, set
`FIRSTCASE` to the number of the first line to read: 2 to skip the
first line, 3 to skip the first two lines, and so on.

   `IMPORTCASES` is ignored, for compatibility.  Use [`N OF
CASES`](n.md) to limit the number of cases read from a file, or
[`SAMPLE`](sample.md) to obtain a random sample of cases.

   The remaining subcommands apply only to one of the two file
arrangements, described below.

### Delimited Data

```
GET DATA /TYPE=TXT
        /FILE={'FILE_NAME',FILE_HANDLE}
        [/ARRANGEMENT={DELIMITED,FIXED}]
        [/FIRSTCASE={FIRST_CASE}]
        [/IMPORTCASE={ALL,FIRST MAX_CASES,PERCENT PERCENT}]

        /DELIMITERS="DELIMITERS"
        [/QUALIFIER="QUOTES"
        [/DELCASE={LINE,VARIABLES N_VARIABLES}]
        /VARIABLES=DEL_VAR1 [DEL_VAR2]...
where each DEL_VAR takes the form:
        variable format
```

   The `GET DATA` command with `TYPE=TXT` and `ARRANGEMENT=DELIMITED`
reads input data from text files in delimited format, where fields are
separated by a set of user-specified delimiters.  Its capabilities are
similar to those of [`DATA LIST FREE`](data-list.md#data-list-free),
with a few enhancements.

   The required `FILE` subcommand and optional `FIRSTCASE` and
`IMPORTCASE` subcommands are described [above](#textual-data-files).

   `DELIMITERS`, which is required, specifies the set of characters that
may separate fields.  Each character in the string specified on
`DELIMITERS` separates one field from the next.  The end of a line also
separates fields, regardless of `DELIMITERS`.  Two consecutive
delimiters in the input yield an empty field, as does a delimiter at the
end of a line.  A space character as a delimiter is an exception:
consecutive spaces do not yield an empty field and neither does any
number of spaces at the end of a line.

   To use a tab as a delimiter, specify `\t` at the beginning of the
`DELIMITERS` string.  To use a backslash as a delimiter, specify `\\` as
the first delimiter or, if a tab should also be a delimiter, immediately
following `\t`.  To read a data file in which each field appears on a
separate line, specify the empty string for `DELIMITERS`.

   The optional `QUALIFIER` subcommand names one or more characters that
can be used to quote values within fields in the input.  A field that
begins with one of the specified quote characters ends at the next
matching quote.  Intervening delimiters become part of the field,
instead of terminating it.  The ability to specify more than one quote
character is a PSPP extension.

   The character specified on `QUALIFIER` can be embedded within a field
that it quotes by doubling the qualifier.  For example, if `'` is
specified on `QUALIFIER`, then `'a''b'` specifies a field that contains
`a'b`.

   The `DELCASE` subcommand controls how data may be broken across
lines in the data file.  With `LINE`, the default setting, each line
must contain all the data for exactly one case.  For additional
flexibility, to allow a single case to be split among lines or
multiple cases to be contained on a single line, specify `VARIABLES
n_variables`, where `n_variables` is the number of variables per case.

   The `VARIABLES` subcommand is required and must be the last
subcommand.  Specify the name of each variable and its [input
format](../language/datasets/formats/index.md), in the order they
should be read from the input file.

#### Example 1

On a Unix-like system, the `/etc/passwd` file has a format similar to
this:

```
root:$1$nyeSP5gD$pDq/:0:0:,,,:/root:/bin/bash
blp:$1$BrP/pFg4$g7OG:1000:1000:Ben Pfaff,,,:/home/blp:/bin/bash
john:$1$JBuq/Fioq$g4A:1001:1001:John Darrington,,,:/home/john:/bin/bash
jhs:$1$D3li4hPL$88X1:1002:1002:Jason Stover,,,:/home/jhs:/bin/csh
```

The following syntax reads a file in the format used by `/etc/passwd`:

```
GET DATA /TYPE=TXT /FILE='/etc/passwd' /DELIMITERS=':'
        /VARIABLES=username A20
                   password A40
                   uid F10
                   gid F10
                   gecos A40
                   home A40
                   shell A40.
```

#### Example 2

Consider the following data on used cars:

```
model   year    mileage price   type    age
Civic   2002    29883   15900   Si      2
Civic   2003    13415   15900   EX      1
Civic   1992    107000  3800    n/a     12
Accord  2002    26613   17900   EX      1
```

The following syntax can be used to read the used car data:

```
GET DATA /TYPE=TXT /FILE='cars.data' /DELIMITERS=' ' /FIRSTCASE=2
        /VARIABLES=model A8
                   year F4
                   mileage F6
                   price F5
                   type A4
                   age F2.
```

#### Example 3

Consider the following information on animals in a pet store:

```
'Pet''s Name', "Age", "Color", "Date Received", "Price", "Height", "Type"
, (Years), , , (Dollars), ,
"Rover", 4.5, Brown, "12 Feb 2004", 80, '1''4"', "Dog"
"Charlie", , Gold, "5 Apr 2007", 12.3, "3""", "Fish"
"Molly", 2, Black, "12 Dec 2006", 25, '5"', "Cat"
"Gilly", , White, "10 Apr 2007", 10, "3""", "Guinea Pig"
```

The following syntax can be used to read the pet store data:

```
GET DATA /TYPE=TXT /FILE='pets.data' /DELIMITERS=', ' /QUALIFIER='''"' /ESCAPE
        /FIRSTCASE=3
        /VARIABLES=name A10
                   age F3.1
                   color A5
                   received EDATE10
                   price F5.2
                   height a5
                   type a10.
```

### Fixed Columnar Data

```
GET DATA /TYPE=TXT
        /FILE={'file_name',FILE_HANDLE}
        [/ARRANGEMENT={DELIMITED,FIXED}]
        [/FIRSTCASE={FIRST_CASE}]
        [/IMPORTCASE={ALL,FIRST MAX_CASES,PERCENT PERCENT}]

        [/FIXCASE=N]
        /VARIABLES FIXED_VAR [FIXED_VAR]...
            [/rec# FIXED_VAR [FIXED_VAR]...]...
where each FIXED_VAR takes the form:
        VARIABLE START-END FORMAT
```

   The `GET DATA` command with `TYPE=TXT` and `ARRANGEMENT=FIXED`
reads input data from text files in fixed format, where each field is
located in particular fixed column positions within records of a case.
Its capabilities are similar to those of [`DATA LIST
FIXED`](data-list.md#data-list-fixed), with a few enhancements.

   The required `FILE` subcommand and optional `FIRSTCASE` and
`IMPORTCASE` subcommands are described [above](#textual-data-files).

   The optional `FIXCASE` subcommand may be used to specify the positive
integer number of input lines that make up each case.  The default value
is 1.

   The `VARIABLES` subcommand, which is required, specifies the
positions at which each variable can be found.  For each variable,
specify its name, followed by its start and end column separated by `-`
(e.g. `0-9`), followed by an input format type (e.g. `F`) or a full
format specification (e.g. `DOLLAR12.2`).  For this command, columns are
numbered starting from 0 at the left column.  Introduce the variables in
the second and later lines of a case by a slash followed by the number
of the line within the case, e.g. `/2` for the second line.

#### Example

Consider the following data on used cars:

```
model   year    mileage price   type    age
Civic   2002    29883   15900   Si      2
Civic   2003    13415   15900   EX      1
Civic   1992    107000  3800    n/a     12
Accord  2002    26613   17900   EX      1
```

The following syntax can be used to read the used car data:

```
GET DATA /TYPE=TXT /FILE='cars.data' /ARRANGEMENT=FIXED /FIRSTCASE=2
        /VARIABLES=model 0-7 A
                   year 8-15 F
                   mileage 16-23 F
                   price 24-31 F
                   type 32-40 A
                   age 40-47 F.
```
