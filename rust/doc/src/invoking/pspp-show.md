# Inspecting data files with `pspp show`

The `pspp show` command reads an SPSS data file and produces a report.
The basic syntax is:

```
pspp show <MODE> <INPUT> [OUTPUT]
```

where `<MODE>` is a mode of operation (see below), `<INPUT>` is the
SPSS data file to read, and `[OUTPUT]` is the output file name.  If
`[OUTPUT]` is omitted, output is written to the terminal.

The following `<MODE>`s are available:

* `identify`: Outputs a line of text to stdout that identifies the
  basic kind of system file.

* `dictionary`: Outputs the file dictionary in detail, including
  variables, value labels, attributes, documents, and so on.  With
  `--data`, also outputs cases from the system file.

  This can be useful as an alternative to PSPP syntax commands such as
  [`SYSFILE INFO`](../commands/sysfile-info.md) or [`DISPLAY
  DICTIONARY`](../commands/display.md).

  [`pspp convert`](pspp-convert.md) is a better way to convert a
  system file to another format.

* `encodings`: Analyzes text data in the system file dictionary and
  (with `--data`) cases and produces a report that can help the user
  to figure out what character encoding the file uses.

  This is useful for old system files that don't identify their own
  encodings.

* `raw`: Outputs the raw structure of the system file dictionary and
  (with `--data`) cases.  This command does not assume a particular
  character encoding for the system file, which means that some of the
  dictionary can't be printed in detail, only in summary.

  This is useful for debugging how PSPP reads system files and for
  investigating cases of system file corruption, especially when the
  character encoding is unknown or uncertain.

* `decoded`: Outputs the raw structure of the system file dictionary
  and (with `--data`) cases.  Versus `raw`, this command does decode
  the dictionary and data with a particular character encoding, which
  allows it to fully interpret system file records.

  This is useful for debugging how PSPP reads system files and for
  investigating cases of system file corruption.

## Options

The following options affect how `pspp show` reads `<INPUT>`:

* `--encoding <ENCODING>`  
  For modes `decoded` and `dictionary`, this reads the input file
  using the specified `<ENCODING>`, overriding the default.

  `<ENCODING>` must be one of the labels for encodings in the
  [Encoding Standard].  PSPP does not support UTF-16 or EBCDIC
  encodings in data files.

  `pspp show encodings` can help figure out the correct encoding for a
  system file.

  [Encoding Standard]: https://encoding.spec.whatwg.org/#names-and-labels

* `--data [<MAX_CASES>]`  
  For modes `raw`, `dictionary`, and `encodings`, this instructs `pspp
  show` to read cases from the file.  If `<MAX_CASES>` is given, then
  that sets a limit on the number of cases to read.  Without this
  option, PSPP will not read any cases.

The following options affect how `pspp show` writes its output:

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

