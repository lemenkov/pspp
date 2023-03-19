# Preparation of Data Files

Before analysis can commence, the data must be loaded into PSPP and
arranged such that both PSPP and humans can understand what the data
represents.  There are two aspects of data:

- The variables—these are the parameters of a quantity which has
  been measured or estimated in some way.  For example height, weight
  and geographic location are all variables.

- The observations (also called 'cases') of the variables—each
  observation represents an instance when the variables were measured
  or observed.

For example, a data set which has the variables height, weight, and
name, might have the observations:

```
1881 89.2 Ahmed
1192 107.01 Frank
1230 67 Julie
```

The following sections explain how to define a dataset.

## Defining Variables

Variables come in two basic types: "numeric" and "string".
Variables such as age, height and satisfaction are numeric, whereas name
is a string variable.  String variables are best reserved for commentary
data to assist the human observer.  However they can also be used for
nominal or categorical data.

The following example defines two variables, `forename` and `height`,
and reads data into them by manual input:

```
PSPP> data list list /forename (A12) height.
PSPP> begin data.
data> Ahmed 188
data> Bertram 167
data> Catherine 134.231
data> David 109.1
data> end data
PSPP>
```

There are several things to note about this example.

- The words `data list list` are an example of the [`DATA
  LIST`](../../commands/data-io/data-list.md).  command, which tells
  PSPP to prepare for reading data.  The word `list` intentionally
  appears twice.  The first occurrence is part of the `DATA LIST`
  call, whilst the second tells PSPP that the data is to be read as
  free format data with one record per line.

  Usually this manual shows command names and other fixed elements of
  syntax in upper case, but case doesn't matter in most parts of
  command syntax.  In the tutorial, we usually show them in lowercase
  because they are easier to type that way.

- The `/` character is important.  It marks the start of the list of
  variables which you wish to define.

- The text `forename` is the name of the first variable, and `(A12)`
  says that the variable forename is a string variable and that its
  maximum length is 12 bytes.  The second variable's name is specified
  by the text `height`.  Since no format is given, this variable has
  the default format.  Normally the default format expects numeric
  data, which should be entered in the locale of the operating system.
  Thus, the example is correct for English locales and other locales
  which use a period (`.`) as the decimal separator.  However if you
  are using a system with a locale which uses the comma (`,`) as the
  decimal separator, then you should in the subsequent lines
  substitute `.` with `,`.  Alternatively, you could explicitly tell
  PSPP that the height variable is to be read using a period as its
  decimal separator by appending the text `DOT8.3` after the word
  `height`.  For more information on data formats, see [Input and
  Output Formats](../../language/datasets/formats/index.md).

- PSPP displays the prompt `PSPP>` when it's expecting a command.
  When it's expecting data, the prompt changes to `data>` so that you
  know to enter data and not a command.

- At the end of every command there is a terminating `.` which tells
  PSPP that the end of a command has been encountered.  You should not
  enter `.` when data is expected (ie.  when the `data>` prompt is
  current) since it is appropriate only for terminating commands.

  You can also terminate a command with a blank line.

## Listing the data

Once the data has been entered, you could type
```
PSPP> list /format=numbered.
```
to list the data.  The optional text `/format=numbered` requests the
case numbers to be shown along with the data.  It should show the
following output:

```
           Data List
┌───────────┬─────────┬──────┐
│Case Number│ forename│height│
├───────────┼─────────┼──────┤
│1          │Ahmed    │188.00│
│2          │Bertram  │167.00│
│3          │Catherine│134.23│
│4          │David    │109.10│
└───────────┴─────────┴──────┘
```


Note that the numeric variable height is displayed to 2 decimal
places, because the format for that variable is `F8.2`.  For a
complete description of the `LIST` command, see
[`LIST`](../../commands/data-io/list.md).

## Reading data from a text file

The previous example showed how to define a set of variables and to
manually enter the data for those variables.  Manual entering of data is
tedious work, and often a file containing the data will be have been
previously prepared.  Let us assume that you have a file called
`mydata.dat` containing the ascii encoded data:

```
Ahmed          188.00
Bertram        167.00
Catherine      134.23
David          109.10
              .
              .
              .
Zachariah      113.02
```

You can can tell the `DATA LIST` command to read the data directly
from this file instead of by manual entry, with a command like: PSPP>
data list file='mydata.dat' list /forename (A12) height.  Notice
however, that it is still necessary to specify the names of the
variables and their formats, since this information is not contained
in the file.  It is also possible to specify the file's character
encoding and other parameters.  For full details refer to [`DATA
LIST`](../../commands/data-io/data-list.md).

## Reading data from a pre-prepared PSPP file

When working with other PSPP users, or users of other software which
uses the PSPP data format, you may be given the data in a pre-prepared
PSPP file.  Such files contain not only the data, but the variable
definitions, along with their formats, labels and other meta-data.
Conventionally, these files (sometimes called "system" files) have the
suffix `.sav`, but that is not mandatory.  The following syntax loads a
file called `my-file.sav`.

```
PSPP> get file='my-file.sav'.
```

You will encounter several instances of this in future examples.

## Saving data to a PSPP file.

If you want to save your data, along with the variable definitions so
that you or other PSPP users can use it later, you can do this with the
`SAVE` command.

   The following syntax will save the existing data and variables to a
file called `my-new-file.sav`.

```
PSPP> save outfile='my-new-file.sav'.
```

If `my-new-file.sav` already exists, then it will be overwritten.
Otherwise it will be created.

## Reading data from other sources

Sometimes it's useful to be able to read data from comma separated
text, from spreadsheets, databases or other sources.  In these
instances you should use the [`GET
DATA`](../../commands/spss-io/get-data.md) command.

## Exiting PSPP

Use the `FINISH` command to exit PSPP:
     PSPP> finish.

