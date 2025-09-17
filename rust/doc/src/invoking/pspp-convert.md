# Converting data files with `pspp convert`

The `pspp convert` command reads data from one file and writes it to
another.  The basic syntax is:

```
pspp convert <INPUT> [OUTPUT]
```

which reads an SPSS system file or portable file or SPSS/PC+ system
file from `<INPUT>` and writes a copy of it to `[OUTPUT]`.  If
`[OUTPUT]` is omitted, output is written to the terminal.

If `[OUTPUT]` is specified, then `pspp convert` tries to guess the
output format based on its extension:

* `csv`  
  `txt`  
  Comma-separated value.  Each value is formatted according to its
  variable's print format.  The first line in the file contains
  variable names.

* `sav`  
  `sys`  
  SPSS system file.

Without an output file name, the default output format is CSV.  Use
`-O <output_format>` to override the default or to specify the format
for unrecognized extensions.

## Options

`pspp convert` accepts the following general options:

* `-O csv`  
  `-O sys`  
  Sets the output format.

* `-e <ENCODING>`  
  `--encoding=<ENCODING>`  
  Sets the character encoding used to read text strings in the input
  file.  This is not needed for new enough SPSS data files, but older
  data files do not identify their encoding, and PSPP cannot always
  guess correctly.

  `<ENCODING>` must be one of the labels for encodings in the
  [Encoding Standard].  PSPP does not support UTF-16 or EBCDIC
  encodings in data files.

  `pspp show encodings` can help figure out the correct encoding for a
  system file.

  [Encoding Standard]: https://encoding.spec.whatwg.org/#names-and-labels

* `-c <MAX_CASES>`  
  `--cases=<MAX_CASES>`  
  By default, all cases in the input are copied to the output.
  Specify this option to limit the number of copied cases.

* `-p <PASSWORD>`  
  `--password=<PASSWORD>`  
  Specifies the password for reading an encrypted SPSS system file.

  `pspp convert` reads, but does not write, encrypted system files.

  > ⚠️ The password (and other command-line options) may be visible to
  other users on multiuser systems.

## System File Output Options

These options only affect output to SPSS system files.

* `--unicode`  
  Writes system file output with Unicode (UTF-8) encoding.  If the
  input was not already in Unicode, then this causes string variables
  to be tripled in width.

* `--compression <COMPRESSION>`  
  Writes data in the system file with the specified format of
  compression:

  - `simple`: A simple form of compression that saves space writing
    small integer values and string segments that are all spaces.  All
    versions of SPSS support simple compression.

  - `zlib`: More advanced compression that saves space in more general
    cases.  Only SPSS 21 and later can read files written with `zlib`
    compression.

## CSV Output Options

These options only affect output to CSV files.

* `--no-var-names`  
  By default, `pspp convert` writes the variable names as the first
  line of output.  With this option, `pspp convert` omits this line.

* `--recode`  
  By default, `pspp convert` writes user-missing values to CSV output
  files as their regular values.  With this option, `pspp convert`
  recodes them to system-missing values (which are written as a
  single space).

* `--labels`  
  By default, `pspp convert` writes variables' values to CSV output
  files.  With this option, `pspp convert` writes value labels.

* `--print-formats`  
  By default, `pspp convert` writes numeric variables as plain
  numbers.  This option makes `pspp convert` honor variables' print
  formats.

* `--decimal=DECIMAL`  
  This option sets the character used as a decimal point in output.
  The default is `.`.  Only ASCII characters may be used.

* `--delimiter=DELIMITER`  
  This option sets the character used to separate fields in output.
  The default is `,`, unless the decimal point is `,`, in which case
  `;` is used.  Only ASCII characters may be used.

* `--qualifier=QUALIFIER`  
  The option sets the character used to quote fields that contain the
  delimiter.  The default is `"`.  Only ASCII characters may be used.
