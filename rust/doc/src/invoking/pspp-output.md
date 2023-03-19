# Invoking `pspp-output`

`pspp-output` is a command-line utility accompanying PSPP.  It supports
multiple operations on SPSS viewer or `.spv` files, here called SPV
files.  SPSS 16 and later writes SPV files to represent the contents of
its output editor.

SPSS 15 and earlier versions instead use `.spo` files.  `pspp-output`
does not support this format.

`pspp-options` may be invoked in the following ways:

```
pspp-output detect FILE

pspp-output [OPTIONS] dir FILE

pspp-output [OPTIONS] convert SOURCE DESTINATION

pspp-output [OPTIONS] get-table-look SOURCE DESTINATION

pspp-output [OPTIONS] convert-table-look SOURCE DESTINATION

pspp-output --help

pspp-output --version
```

Each of these forms is documented separately below.  `pspp-output`
also has several undocumented command forms that developers may find
useful for debugging.

## The `detect` Command

```
pspp-output detect FILE
```

When FILE is an SPV file, `pspp-output` exits successfully without
outputting anything.  When FILE is not an SPV file or some other error
occurs, `pspp-output` prints an error message and exits with a failure
indication.

## The `dir` Command

```
pspp-output [OPTIONS] dir FILE
```

Prints on stdout a table of contents for SPV file FILE.  By default,
this table lists every object in the file, except for hidden objects.
See [Input Selection Options](#input-selection-options), for
information on the options available to select a subset of objects.

The following additional option for `dir` is intended mainly for use
by PSPP developers:

* `--member-names`  
  Also show the names of the Zip members associated with each object.

## The `convert` Command

```
pspp-output [OPTIONS] convert SOURCE DESTINATION
```

Reads SPV file SOURCE and converts it to another format, writing the
output to DESTINATION.

By default, the intended format for DESTINATION is inferred based on
its extension, in the same way that the `pspp` program does for its
output files.  See [Invoking `pspp`](pspp.md), for details.

See [Input Selection Options](#input-selection-options), for
information on the options available to select a subset of objects to
include in the output.  The following additional options are accepted:

* `-O format=FORMAT`  
  Overrides the format inferred from the output file's extension.  Use
  `--help` to list the available formats.  See [Invoking
  `pspp`](pspp.md) for details of the available output formats.

* `-O OPTION=VALUE`  
  Sets an option for the output file format.  See [Invoking
  `pspp`](pspp.md) for details of the available output options.

* `-F`, `--force`  
  By default, if the source is corrupt or otherwise cannot be
  processed, the destination is not written.  With `-F` or `--force`,
  the destination is written as best it can, even with errors.

* `--table-look=FILE`  
  Reads a table style from FILE and applies it to all of the output
  tables.  The file should be a TableLook `.stt` or `.tlo` file.

* `--use-page-setup`  
  By default, the `convert` command uses the default page setup (for
  example, page size and margins) for DESTINATION, or the one
  specified with `-O` options, if any.  Specify this option to ignore
  these sources of page setup in favor of the one embedded in the
  SPV, if any.

## The `get-table-look` Command

```
pspp-output [OPTIONS] get-table-look SOURCE DESTINATION
```

Reads SPV file SOURCE, applies any [input selection
options](#input-selection-options), picks the first table from the
selected object, extracts the TableLook from that table, and writes it
to DESTINATION (typically with an `.stt` extension) in the TableLook
XML format.

Use `-` for SOURCE to instead write the default look to DESTINATION.

The user may use the TableLook file to change the style of tables in
other files, by passing it to the `--table-look` option on the `convert`
command.

## The `convert-table-look` Command

```
pspp-output [OPTIONS] convert-table-look SOURCE DESTINATION
```

Reads `.stt` or `.tlo` file SOURCE, and writes it back to DESTINATION
(typically with an `.stt` extension) in the TableLook XML format.  This
is useful for converting a TableLook `.tlo` file from SPSS 15 or earlier
into the newer `.stt` format.

## Input Selection Options

The `dir` and `convert` commands, by default, operate on all of the
objects in the source SPV file, except for objects that are not visible
in the output viewer window.  The user may specify these options to
select a subset of the input objects.  When multiple options are used,
only objects that satisfy all of them are selected:

* `--select=[^]CLASS...`  
  Include only objects of the given CLASS; with leading `^`, include
  only objects not in the class.  Use commas to separate multiple
  classes.  The supported classes are `charts`, `headings`, `logs`,
  `models`, `tables`, `texts`, `trees`, `warnings`, `outlineheaders`,
  `pagetitle`, `notes`, `unknown`, and `other`.

  Use `--select=help` to print this list of classes.

* `--commands=[^]COMMAND...`  
  `--subtypes=[^]SUBTYPE...`  
  `--labels=[^]LABEL...`  
  Include only objects with the specified COMMAND, SUBTYPE, or LABEL.
  With a leading `^`, include only the objects that do not match.
  Multiple values may be specified separated by commas.  An asterisk
  at the end of a value acts as a wildcard.

  The `--command` option matches command identifiers, case
  insensitively.  All of the objects produced by a single command use
  the same, unique command identifier.  Command identifiers are
  always in English regardless of the language used for output.  They
  often differ from the command name in PSPP syntax.  Use the
  `pspp-output` program's `dir` command to print command identifiers
  in particular output.

  The `--subtypes` option matches particular tables within a command,
  case insensitively.  Subtypes are not necessarily unique: two
  commands that produce similar output tables may use the same
  subtype.  Subtypes are always in English and `dir` will print them.

  The `--labels` option matches the labels in table output (that is,
  the table titles).  Labels are affected by the output language,
  variable names and labels, split file settings, and other factors.

* `--nth-commands=N...`  
  Include only objects from the Nth command that matches `--command`
  (or the Nth command overall if `--command` is not specified), where
  N is 1 for the first command, 2 for the second, and so on.

* `--instances=INSTANCE...`  
  Include the specified INSTANCE of an object that matches the other
  criteria within a single command.  The INSTANCE may be a number (1
  for the first instance, 2 for the second, and so on) or `last` for
  the last instance.

* `--show-hidden`  
  Include hidden output objects in the output.  By default, they are
  excluded.

* `--or`  
  Separates two sets of selection options.  Objects selected by
  either set of options are included in the output.

The following additional input selection options are intended mainly
for use by PSPP developers:

* `--errors`  
  Include only objects that cause an error when read.  With the
  `convert` command, this is most useful in conjunction with the
  `--force` option.

* `--members=MEMBER...`  
  Include only the objects that include a listed Zip file MEMBER.
  More than one name may be included, comma-separated.  The members
  in an SPV file may be listed with the `dir` command by adding the
  `--show-members` option or with the `zipinfo` program included with
  many operating systems.  Error messages that `pspp-output` prints
  when it reads SPV files also often include member names.

* `--member-names`  
  Displays the name of the Zip member or members associated with each
  object just above the object itself.
