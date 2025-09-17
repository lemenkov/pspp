# Inspecting `.por` files with `pspp show-por`

The `pspp show-por` command reads an SPSS "portable file",
which usually has a `.por` extension, and produces a report.  The
basic syntax is:

```
pspp show-por <MODE> <INPUT> [OUTPUT]
```

where `<MODE>` is a mode of operation (see below), `<INPUT>` is the
SPSS portable file to read, and `[OUTPUT]` is the output file name.
If `[OUTPUT]` is omitted, output is written to the terminal.

> The portable file format is mostly obsolete.  The "system file" or
> `.sav` format should be used for writing new data files.  Use [`pspp
> show`](pspp-show.md) to inspect `.sav` files.

The following `<MODE>`s are available:

* `dictionary`: Outputs the file dictionary in detail, including
  variables, value labels, documents, and so on.  With `--data`, also
  outputs cases from the system file.

  This can be useful as an alternative to PSPP syntax commands such as
  [`DISPLAY DICTIONARY`](../commands/display.md).

  [`pspp convert`](pspp-convert.md) is a better way to convert a
  portable file to another format.

* `metadata`: Outputs portable file metadata not included in the
  dictionary:

  - The creation date and time declared inside the file (not in the
    file system).

  - The name of the product and subproduct that wrote the file, if
    present.

  - The author of the file, if present.  This is usually the name of
    the organization that licensed the product that wrote the file.

  - The character set [translation table] embedded in the file, as an
    array with 256 elements, one for each possible value of a byte in
    the file.  Each array element gives the byte value as a 2-digit
    hexadecimal number paired with the translation table's entry for
    that byte.  Since the file can technically be in any encoding
    (although [the corpus] universally uses extended ASCII), the entry
    is given as a character interpreted in two character sets:
    [windows-1252] and [code page 437], in that order.  (If the two
    character sets agree on the code point, then it is only given
    once.)

    For example, consider a portable's file translation table at
    offset 0x9e, which in the [portable character set] is `±`.
    Suppose it has value 0xb1, which is `±` in [windows-1252] and `▒`
    in [code page 437].  Then that array element would be `["9e", "±",
    "▒"]`.

    [translation table]: ../portable.md#translation-table
    [the corpus]: ../portable.md#corpus
    [portable character set]: ../portable.md#theory
    [windows-1252]: https://en.wikipedia.org/wiki/Windows-1252
    [code page 437]: https://en.wikipedia.org/wiki/Code_page_437

  This command is most useful with some knowledge of the [portable
  file format].

  [portable file format]: ../portable.md

* `histogram`: Reports on the usage of characters in the portable
  file.  Produces output in the form of an array for each possible
  value of a byte in the file.  Each array element gives the byte
  value, the byte's character, and the number of times that the byte
  appears in the file.  A given byte is omitted from the table if it
  does not appear in the file at all, or if the translation table
  leaves it unmapped.  It is also omitted if the byte's character is
  the ISO-8859-1 encoding of the byte (for example, if byte 0x41
  represents `A`, which is `A` in [ISO-8859-1]).

  This command is most useful with some knowledge of the [portable
  file format].

  [ISO-8859-1]: https://en.wikipedia.org/wiki/ISO/IEC_8859-1

## Options

The following options affect how `pspp show-por` reads `<INPUT>`:

* `--data [<MAX_CASES>]`  
  For mode `dictionary`, and `encodings`, this instructs `pspp
  show-por` to read cases from the file.  If `<MAX_CASES>` is given,
  then that sets a limit on the number of cases to read.  Without this
  option, PSPP will not read any cases.

The following options affect how `pspp show-por` writes its output:

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

