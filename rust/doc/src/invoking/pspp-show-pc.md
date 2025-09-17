# Inspecting SPSS/PC+ Files

The `pspp show-pc` command reads an SPSS/PC+ system file which
usually has a `.sys` extension, and produces a report.

> SPSS/PC+ has been obsolete since the 1990s, and its file format is
> also obsolete and rarely encountered.  Use [`pspp
> show`](pspp-show.md) to inspect modern SPSS system files.

The basic syntax is:

```
pspp show-pc <MODE> <INPUT> [OUTPUT]
```

where `<MODE>` is a mode of operation (see below), `<INPUT>` is the
SPSS/PC+ file to read, and `[OUTPUT]` is the output file name.  If
`[OUTPUT]` is omitted, output is written to the terminal.

The following `<MODE>`s are available:

* `dictionary`: Outputs the file dictionary in detail, including
  variables, value labels, and so on.  With `--data`, also outputs
  cases from the system file.

  This can be useful as an alternative to PSPP syntax commands such as
  [`DISPLAY DICTIONARY`](../commands/display.md).

  [`pspp convert`](pspp-convert.md) is a better way to convert an
  SPSS/PC+ file to another format.

* `metadata`: Outputs metadata not included in the dictionary:

  - The creation date and time declared inside the file (not in the
    file system).

  - The name of the product family and product that wrote the file, if
    present.

  - The file name embedded inside the file, if one is present.

  - Whether the file is bytecode-compressed.

  - The number of cases in the file.

## Options

The following options affect how `pspp show-pc` reads `<INPUT>`:

* `--data [<MAX_CASES>]`  
  For mode `dictionary`, and `encodings`, this instructs `pspp
  show-pc` to read cases from the file.  If `<MAX_CASES>` is given,
  then that sets a limit on the number of cases to read.  Without this
  option, PSPP will not read any cases.

The following options affect how `pspp show-pc` writes its output:

* `-f <FORMAT>`  
  `--format <FORMAT>`  
  Specifies the format to use for output.  `<FORMAT>` may be one of
  the following:

  - `json`: JSON using indentation and spaces for easy human
    consumption.
  - `ndjson`: [Newline-delimited JSON].
  - `output`: Pivot tables with the PSPP output engine.  Use `-o` for
    additional configuration.
  - `discard`: Do not produce any output.

  When these options are not used, the default output format is chosen
  based on the `[OUTPUT]` extension.  If `[OUTPUT]` is not specified,
  then output defaults to JSON.

  [Newline-delimited JSON]: https://github.com/ndjson/ndjson-spec

* `-o <OUTPUT_OPTIONS>`  
  Adds `<OUTPUT_OPTIONS>` to the output engine configuration.

