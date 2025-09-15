# System File Format

An SPSS system file holds a set of cases and dictionary information that
describes how they may be interpreted.  The system file format dates
back 40+ years and has evolved greatly over that time to support new
features, but in a way to facilitate interchange between even the oldest
and newest versions of software.  This chapter describes the system file
format.

<!-- toc -->

## Introduction

System files use four data types: 8-bit characters, 32-bit integers,
64-bit integers, and 64-bit floating points, called here `char’,
`int32’, `int64’, and `flt64’, respectively.  Data is not necessarily
aligned on a word or double-word boundary: the [long variable name
record](#long-variable-names-record) and [very long string
record](#very-long-string-record) have arbitrary byte length and can
therefore cause all data coming after them in the file to be
misaligned.

Integer data in system files may be big-endian or little-endian.  A
reader may detect the endianness of a system file by examining
`layout_code` in the [file header record](#file-header-record).

Floating-point data in system files may nominally be in IEEE 754, IBM,
or VAX formats.  A reader may detect the floating-point format in use
by examining `bias` in the [file header record](#file-header-record).
Only files with IEEE 754 floating point data have actually been
encountered.

PSPP detects big-endian and little-endian integer formats in system
files and translates as necessary.  PSPP also detects the floating-point
format in use, as well as the endianness of IEEE 754 floating-point
numbers, and translates as needed.  However, only IEEE 754 numbers with
the same endianness as integer data in the same file have actually been
observed in system files, and it is likely that other formats are
obsolete or were never used.

System files use a few floating point values for special purposes:

* `SYSMIS`

  The system-missing value is represented by the largest possible
  negative number in the floating point format (`-DBL_MAX` or
  `f64::MIN`).

* `HIGHEST`

  `HIGHEST` is used as the high end of a missing value range with an
  unbounded maximum.  It is represented by the largest possible
  positive number (`DBL_MAX` or `f64::MAX`).

* `LOWEST`

  `LOWEST` is used as the low end of a missing value range with an
  unbounded minimum.  It was originally represented by the
  second-largest negative number (in IEEE 754 format,
  `0xffeffffffffffffe`).  System files written by SPSS 21 and later
  instead use the largest negative number (`-DBL_MAX` or `f64::MIN`),
  the same value as `SYSMIS`. This does not lead to ambiguity because
  `LOWEST` appears in system files only in missing value ranges, which
  never contain `SYSMIS`.

System files may use most character encodings based on an 8-bit unit.
UTF-16 and UTF-32, based on wider units, appear to be unacceptable.
`rec_type` in the file header record is sufficient to distinguish
between ASCII and EBCDIC based encodings.  The best way to determine
the specific encoding in use is to consult the [character encoding
record](#character-encoding-record), if present, and failing that
`character_code` in the [machine integer info
record](#machine-integer-info-record).  The same encoding should be
used for the dictionary and the data in the file, although it is
possible to artificially synthesize files that use different
encodings.

## System File Record Structure

System files are divided into records with the following format:

```
     int32               type;
     char                data[];
```

   This header does not identify the length of the `data` or any
information about what it contains, so the system file reader must
understand the format of `data` based on `type`.  However, records with
type 7, called “extension records”, have a stricter format:

```
     int32               type;
     int32               subtype;
     int32               size;
     int32               count;
     char                data[size * count];
```

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  This value identifies a particular kind of
  extension record.

* `int32 size;`

  The size of each piece of data that follows the header, in bytes.
  Known extension records use 1, 4, or 8, for `char`, `int32`, and
  `flt64` format data, respectively.

* `int32 count;`

  The number of pieces of data that follow the header.

* `char data[size * count];`

  Data, whose format and interpretation depend on the subtype.

An extension record contains exactly `size * count` bytes of data,
which allows a reader that does not understand an extension record to
skip it.  Extension records provide only nonessential information, so
this allows for files written by newer software to preserve backward
compatibility with older or less capable readers.

Records in a system file must appear in the following order:

* File header record.

* Variable records.

* All pairs of value labels records and value label variables
  records, if present.

* Document record, if present.

* Extension (type 7) records, in ascending numerical order of their
  subtypes.

  System files written by SPSS include at most one of each kind of
  extension record.  This is generally true of system files written
  by other software as well, with known exceptions noted below in the
  individual sections about each type of record.

* Dictionary termination record.

* Data record.

We advise authors of programs that read system files to tolerate
format variations.  Various kinds of misformatting and corruption have
been observed in system files written by SPSS and other software
alike.  In particular, because extension records provide nonessential
information, it is generally better to ignore an extension record
entirely than to refuse to read a system file.

The following sections describe the known kinds of records.

## File Header Record

A system file begins with the file header, with the following format:

```
     char                rec_type[4];
     char                prod_name[60];
     int32               layout_code;
     int32               nominal_case_size;
     int32               compression;
     int32               weight_index;
     int32               ncases;
     flt64               bias;
     char                creation_date[9];
     char                creation_time[8];
     char                file_label[64];
     char                padding[3];
```

* `char rec_type[4];`

  Record type code, either `$FL2` for system files with uncompressed
  data or data compressed with simple bytecode compression, or `$FL3`
  for system files with ZLIB compressed data.

  This is truly a character field that uses the character encoding as
  other strings.  Thus, in a file with an ASCII-based character
  encoding this field contains `24 46 4c 32` or `24 46 4c 33`, and in
  a file with an EBCDIC-based encoding this field contains `5b c6 d3
  f2`.  (No EBCDIC-based ZLIB-compressed files have been observed.)

* `char prod_name[60];`

  Product identification string.  This always begins with the
  characters `@(#) SPSS DATA FILE`.  PSPP uses the remaining
  characters to give its version and the operating system name; for
  example, `GNU pspp 0.1.4 - sparc-sun-solaris2.5.2`.  The string is
  truncated if it would be longer than 60 characters; otherwise it is
  padded on the right with spaces.

  The product name field allow readers to behave differently based on
  quirks in the way that particular software writes system files.  See
  [Value Labels Records](#value-labels-records), for the detail of the
  quirk that the PSPP system file reader tolerates in files written by
  ReadStat, which has `https://github.com/WizardMac/ReadStat` in
  `prod_name`.

* `int32 layout_code;`

  Normally set to 2, although a few system files have been spotted in
  the wild with a value of 3 here.  PSPP use this value to determine
  the file's integer endianness.

* `int32 nominal_case_size;`

  Number of data elements per case.  This is the number of variables,
  except that long string variables add extra data elements (one for
  every 8 characters after the first 8).  However, string variables
  do not contribute to this value beyond the first 255 bytes.
  Further, some software always writes -1 or 0 in this field.  In
  general, it is unsafe for systems reading system files to rely upon
  this value.

* `int32 compression;`

  Set to 0 if the data in the file is not compressed, 1 if the data is
  compressed with simple bytecode compression, 2 if the data is ZLIB
  compressed.  This field has value 2 if and only if `rec_type` is
  `$FL3`.

* `int32 weight_index;`

  If one of the variables in the data set is used as a weighting
  variable, set to the [dictionary index](#dictionary-index) of that
  variable.  Otherwise, set to 0.

* `int32 ncases;`

  Set to the number of cases in the file if it is known, or -1
  otherwise.

  In the general case it is not possible to determine the number of
  cases that will be output to a system file at the time that the
  header is written.  The way that this is dealt with is by writing
  the entire system file, including the header, then seeking back to
  the beginning of the file and writing just the `ncases` field.  For
  files in which this is not valid, the seek operation fails.  In
  this case, `ncases` remains -1.

* `flt64 bias;`

  Compression bias, usually 100.  Only integers between `1 - bias` and
  `251 - bias` can be compressed.

  By assuming that its value is 100, PSPP uses `bias` to determine the
  file's floating-point format and endianness.  If the compression
  bias is not 100, PSPP cannot auto-detect the floating-point format
  and assumes that it is IEEE 754 format with the same endianness as
  the system file's integers, which is correct for all known system
  files.

* `char creation_date[9];`

  Date of creation of the system file, in `dd mmm yy` format, with
  the month as standard English abbreviations, using an initial
  capital letter and following with lowercase.

  > Some files in the corpus have the date in `dd-mmm-yy` format.

* `char creation_time[8];`

  Time of creation of the system file, in `hh:mm:ss` format and using
  24-hour time.

* `char file_label[64];`

  File label declared by the user, if any.  Padded on the right with
  spaces.

  A product that identifies itself as `VOXCO INTERVIEWER 4.3` uses
  CR-only line ends in this field, rather than the more usual LF-only
  or CR LF line ends.

* `char padding[3];`

  Ignored padding bytes to make the structure a multiple of 32 bits
  in length.  Set to zeros.

## Variable Record

There must be one variable record for each numeric variable and each
string variable with width 8 bytes or less.  String variables wider than
8 bytes have one variable record for each 8 bytes, rounding up.  The
first variable record for a long string specifies the variable's correct
dictionary information.  Subsequent variable records for a long string
are filled with dummy information: a type of -1, no variable label or
missing values, print and write formats that are ignored, and an empty
string as name.  A few system files have been encountered that include a
variable label on dummy variable records, so readers should take care to
parse dummy variable records in the same way as other variable records.

The "<a name="dictionary-index">dictionary index</a>" of a variable is
a 1-based offset in the set of variable records, including dummy
variable records for long string variables.  The first variable record
has a dictionary index of 1, the second has a dictionary index of 2,
and so on.

The system file format does not directly support string variables
wider than 255 bytes.  Such very long string variables are represented
by a number of narrower string variables.  See [very long string
record](#very-long-string-record) for details.

   A system file should contain at least one variable and thus at least
one variable record, but system files have been observed in the wild
without any variables (thus, no data either).

```
     int32               rec_type;
     int32               type;
     int32               has_var_label;
     int32               n_missing_values;
     int32               print;
     int32               write;
     char                name[8];

     /* Present only if `has_var_label` is 1. */
     int32               label_len;
     char                label[];

     /* Present only if `n_missing_values` is nonzero. */
     flt64               missing_values[];
```

* `int32 rec_type;`

  Record type code.  Always set to 2.

* `int32 type;`

  Variable type code.  Set to 0 for a numeric variable.  For a short
  string variable or the first part of a long string variable, this
  is set to the width of the string.  For the second and subsequent
  parts of a long string variable, set to -1, and the remaining
  fields in the structure are ignored.

* `int32 has_var_label;`

  If this variable has a variable label, set to 1; otherwise, set to
  0.

* `int32 n_missing_values;`

  If the variable has no missing values, set to 0.  If the variable
  has one, two, or three discrete missing values, set to 1, 2, or 3,
  respectively.  If the variable has a range for missing variables,
  set to -2; if the variable has a range for missing variables plus a
  single discrete value, set to -3.

  A long string variable always has the value 0 here.  A separate
  record indicates [missing values for long string
  variables](#long-string-missing-values-record).

* `int32 print;`

  Print format for this variable.  See below.

* `int32 write;`

  Write format for this variable.  See below.

* `char name[8];`

  Variable name.  The variable name must begin with a capital letter
  or the at-sign (`@`).  Subsequent characters may also be digits,
  octothorpes (`#`), dollar signs (`$`), underscores (`_`), or full
  stops (`.`).  The variable name is padded on the right with spaces.

  The `name` fields should be unique within a system file.  System
  files written by SPSS that contain very long string variables with
  similar names sometimes contain duplicate names that are later
  eliminated by resolving the [very long string
  names](#very-long-string-record).  PSPP handles duplicates by
  assigning them new, unique names.

* `int32 label_len;`

  This field is present only if `has_var_label` is set to 1.  It is
  set to the length, in characters, of the variable label.  The
  documented maximum length varies from 120 to 255 based on SPSS
  version, but some files have been seen with longer labels.  PSPP
  accepts labels of any length.

* `char label[];`

  This field is present only if `has_var_label` is set to 1.  It has
  length `label_len`, rounded up to the nearest multiple of 32 bits.
  The first `label_len` characters are the variable's variable label.

* `flt64 missing_values[];`

  This field is present only if `n_missing_values` is nonzero.  It has
  the same number of 8-byte elements as the absolute value of
  `n_missing_values`.  Each element is interpreted as a number for
  numeric variables (with `HIGHEST` and `LOWEST` indicated as
  described in the [introduction](#introduction)).  For string
  variables of width less than 8 bytes, elements are right-padded with
  spaces.

  For discrete missing values, each element represents one missing
  value.  When a range is present, the first element denotes the
  minimum value in the range, and the second element denotes the
  maximum value in the range.  When a range plus a value are present,
  the third element denotes the additional discrete missing value.

### Format Types

The `print` and `write` members of `sysfile_variable` are output
formats coded into `int32` types.  The least-significant byte of the
`int32` represents the number of decimal places, and the next two bytes
in order of increasing significance represent field width and format
type, respectively.  The most-significant byte is not used and should be
set to zero.

Format types are defined as follows:

| Value |  Meaning    |
|------:|:------------|
| 0     |  Not used.  |
| 1     |  `A`        |
| 2     |  `AHEX`     |
| 3     |  `COMMA`    |
| 4     |  `DOLLAR`   |
| 5     |  `F`        |
| 6     |  `IB`       |
| 7     |  `PIBHEX`   |
| 8     |  `P`        |
| 9     |  `PIB`      |
| 10    |  `PK`       |
| 11    |  `RB`       |
| 12    |  `RBHEX`    |
| 13    |  Not used.  |
| 14    |  Not used.  |
| 15    |  `Z`        |
| 16    |  `N`        |
| 17    |  `E`        |
| 18    |  Not used.  |
| 19    |  Not used.  |
| 20    |  `DATE`     |
| 21    |  `TIME`     |
| 22    |  `DATETIME` |
| 23    |  `ADATE`    |
| 24    |  `JDATE`    |
| 25    |  `DTIME`    |
| 26    |  `WKDAY`    |
| 27    |  `MONTH`    |
| 28    |  `MOYR`     |
| 29    |  `QYR`      |
| 30    |  `WKYR`     |
| 31    |  `PCT`      |
| 32    |  `DOT`      |
| 33    |  `CCA`      |
| 34    |  `CCB`      |
| 35    |  `CCC`      |
| 36    |  `CCD`      |
| 37    |  `CCE`      |
| 38    |  `EDATE`    |
| 39    |  `SDATE`    |
| 40    |  `MTIME`    |
| 41    |  `YMDHMS`   |

A few system files have been observed in the wild with invalid
`write` fields, in particular with value 0.  Readers should probably
treat invalid `print` or `write` fields as some default format.

### Obsolete Treatment of Long String Missing Values

SPSS and most versions of PSPP write missing values for string
variables wider than 8 bytes with a [Long String Missing Values
Record](#long-string-missing-values-record).  Very old versions of
PSPP instead wrote these missing values on the variables record,
writing only the first 8 bytes of each missing value, with the
remainder implicitly all spaces.  Any new software should use the
[Long String Missing Values
Record](#long-string-missing-values-record), but it might possibly be
worthwhile also to accept the old format used by PSPP.

## Value Labels Records

The value label records documented in this section are used for
numeric and short string variables only.  Long string variables may
have value labels, but their value labels are recorded using a
[different record type](#long-string-value-labels-record).

[ReadStat](#file-header-record) writes value labels that label a
single value more than once.  In more detail, it emits value labels
whose values are longer than string variables' widths, that are
identical in the actual width of the variable, e.g. labels for values
`ABC123` and `ABC456` for a string variable with width 3.  For files
written by this software, PSPP ignores such labels.

### Value Label Record for Labels

The value label record has the following format:

```
     int32               rec_type;
     int32               label_count;

     /* Repeated `n_label` times. */
     char                value[8];
     char                label_len;
     char                label[];
```

* `int32 rec_type;`

  Record type.  Always set to 3.

* `int32 label_count;`

  Number of value labels present in this record.

The remaining fields are repeated `count` times.  Each repetition
specifies one value label.

* `char value[8];`

  A numeric value or a short string value padded as necessary to 8
  bytes in length.  Its type and width cannot be determined until the
  following value label variables record (see below) is read.

* `char label_len;`

  The label's length, in bytes.  The documented maximum length varies
  from 60 to 120 based on SPSS version.  PSPP supports value labels
  up to 255 bytes long.

* `char label[];`

  `label_len` bytes of the actual label, followed by up to 7 bytes of
  padding to bring `label` and `label_len` together to a multiple of
  8 bytes in length.

### Value Label Record for Variables

The value label record is always immediately followed by a value
label variables record with the following format:

```
  int32               rec_type;
  int32               var_count;
  int32               vars[];
```

* `int32 rec_type;`

  Record type.  Always set to 4.

* `int32 var_count;`

  Number of variables that the associated value labels from the value
  label record are to be applied.

* `int32 vars[];`

  A list of 1-based [dictionary indexes](#dictionary-index) of
  variables to which to apply the value labels.  There are `var_count`
  elements.

  String variables wider than 8 bytes may not be specified in this
  list.

## Document Record

The document record, if present, has the following format:

```
     int32               rec_type;
     int32               n_lines;
     char                lines[][80];
```

* `int32 rec_type;`

  Record type.  Always set to 6.

* `int32 n_lines;`

  Number of lines of documents present.  This should be greater than
  zero, but ReadStats writes system files with zero `n_lines`.

* `char lines[][80];`

  Document lines.  The number of elements is defined by `n_lines`.
  Lines shorter than 80 characters are padded on the right with
  spaces.

## Machine Integer Info Record

The integer info record, if present, has the following format:

```
     /* Header. */
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;

     /* Data. */
     int32               version_major;
     int32               version_minor;
     int32               version_revision;
     int32               machine_code;
     int32               floating_point_rep;
     int32               compression_code;
     int32               endianness;
     int32               character_code;
```

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Always set to 3.

* `int32 size;`

  Size of each piece of data in the data part, in bytes.  Always set
  to 4.

* `int32 count;`

  Number of pieces of data in the data part.  Always set to 8.

* `int32 version_major;`

  PSPP major version number.  In version X.Y.Z, this is X.

* `int32 version_minor;`

  PSPP minor version number.  In version X.Y.Z, this is Y.

* `int32 version_revision;`

  PSPP version revision number.  In version X.Y.Z, this is Z.

* `int32 machine_code;`

  Machine code.  PSPP always set this field to value to -1, but other
  values may appear.

* `int32 floating_point_rep;`

  Floating point representation code.  For IEEE 754 systems (the most
  common) this is 1.  IBM 370 is supposed to set this to 2, and DEC
  VAX E to 3, but neither of these has been observed.

* `int32 compression_code;`

  Compression code.  Always set to 1, regardless of whether or how
  the file is compressed.

* `int32 endianness;`

  Machine endianness.  1 indicates big-endian, 2 indicates
  little-endian.

* `int32 character_code;`

  <a name="character-code">Character code</a>.  The following values
  have been actually observed in system files:

  - 1

    EBCDIC. Only one example has been observed.

  - 2

    7-bit ASCII. Old versions of SPSS for Unix and Windows always
    wrote value 2 in this field, regardless of the encoding in
    use, so it is not reliable and should be ignored.

  - 3

    8-bit "ASCII".

  - 819

    ISO 8859-1 (IBM AIX code page number).

  - 874  
    9066

    The `windows-874` code page for Thai.

  - 932

    The `windows-932` code page for Japanese (aka `Shift_JIS`).

  - 936

    The `windows-936` code page for simplified Chinese (aka `GBK`).

  - 949

    Probably `ks_c_5601-1987`, Unified Hangul Code.

  - 950

    The `big5` code page for traditional Chinese.

  - 1250

    The `windows-1250` code page for Central European and Eastern
    European languages.

  - 1251

    The `windows-1251` code page for Cyrillic languages.

  - 1252

    The `windows-1252` code page for Western European languages.

  - 1253

    The `windows-1253` code page for modern Greek.

  - 1254

    The `windows-1254` code page for Turkish.

  - 1255

    The `windows-1255` code page for Hebrew.

  - 1256

    The `windows-1256` code page for Arabic script.

  - 1257

    The `windows-1257` code page for Estonian, Latvian, and
    Lithuanian.

  - 1258

    The `windows-1258` code page for Vietnamese.

  - 20127

    US-ASCII.

  - 28591

    ISO 8859-1 (Latin-1).

  - 25592

    ISO 8859-2 (Central European).

  - 28605

    ISO 8895-9 (Latin-9).

  - 51949

    The `euc-kr` code page for Korean.

  - 65001

    UTF-8.

  The following additional values are known to be defined:

  - 3

    8-bit "ASCII".

  - 4

    DEC Kanji.

  The most common values observed, from most to least common, are
  1252, 65001, 2, and 28591.

  Other Windows code page numbers are known to be generally valid.

  Newer versions also write the character encoding [as a
  string](#character-encoding-record).

## Machine Floating-Point Info Record

The floating-point info record, if present, has the following format:

```
     /* Header. */
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;

     /* Data. */
     flt64               sysmis;
     flt64               highest;
     flt64               lowest;
```

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Always set to 4.

* `int32 size;`

  Size of each piece of data in the data part, in bytes.  Always set
  to 8.

* `int32 count;`

  Number of pieces of data in the data part.  Always set to 3.

* `flt64 sysmis;`  
  `flt64 highest;`  
  `flt64 lowest;`

  The system missing value, the value used for `HIGHEST` in missing
  values, and the value used for `LOWEST` in missing values,
  respectively.  See the [introduction](#introduction) for more
  information.

  The SPSSWriter library in PHP, which identifies itself as `FOM SPSS
  1.0.0` in the file header record `prod_name` field, writes
  unexpected values to these fields, but it uses the same values
  consistently throughout the rest of the file.

## Multiple Response Sets Records

The system file format has two different types of records that
represent multiple response sets.  The first type of record describes
multiple response sets that can be understood by SPSS before
version 14.  The second type of record, with a closely related format,
is used for multiple dichotomy sets that use the
`CATEGORYLABELS=COUNTEDVALUES` feature added in version 14.

```
     /* Header. */
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;

     /* Exactly `count` bytes of data. */
     char                mrsets[];
```

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Set to 7 for records that describe multiple
  response sets understood by SPSS before version 14, or to 19 for
  records that describe dichotomy sets that use the
  `CATEGORYLABELS=COUNTEDVALUES` feature added in version 14.

* `int32 size;`

  The size of each element in the `mrsets` member.  Always set to 1.

* `int32 count;`

  The total number of bytes in `mrsets`.

* `char mrsets[];`

  Zero or more line feeds (byte 0x0a), followed by a series of
  multiple response sets, each of which consists of the following:

     - The set's name (an identifier that begins with `$`), in mixed
       upper and lower case.

     - An equals sign (`=`).

     - `C` for a multiple category set, `D` for a multiple dichotomy
       set with `CATEGORYLABELS=VARLABELS`, or `E` for a multiple
       dichotomy set with `CATEGORYLABELS=COUNTEDVALUES`.

     - For a multiple dichotomy set with
       `CATEGORYLABELS=COUNTEDVALUES`, a space, followed by a number
       expressed as decimal digits, followed by a space.  If
       `LABELSOURCE=VARLABEL` was specified on MRSETS, then the number
       is 11; otherwise it is 1.[^note]

     - For either kind of multiple dichotomy set, the counted value,
       as a positive integer count specified as decimal digits,
       followed by a space, followed by as many string bytes as
       specified in the count.  If the set contains numeric
       variables, the string consists of the counted integer value
       expressed as decimal digits.  If the set contains string
       variables, the string contains the counted string value.
       Either way, the string may be padded on the right with spaces
       (older versions of SPSS seem to always pad to a width of 8
       bytes; newer versions don't).

     - A space.

     - The multiple response set's label, using the same format as
       for the counted value for multiple dichotomy sets.  A string
       of length 0 means that the set does not have a label.  A
       string of length 0 is also written if LABELSOURCE=VARLABEL was
       specified.

     - The short names of the variables in the set, converted to
       lowercase, each preceded by a single space.

       Even though a multiple response set must have at least two
       variables, some system files contain multiple response sets
       with no variables or one variable.  The source and meaning of
       these multiple response sets is unknown.  (Perhaps they arise
       from creating a multiple response set then deleting all the
       variables that it contains?)

     - One line feed (byte 0x0a).  Sometimes multiple, even hundreds,
       of line feeds are present.

> Example: Given appropriate variable definitions, consider the
> following MRSETS command:
>
> ```
> MRSETS /MCGROUP NAME=$a LABEL='my mcgroup' VARIABLES=a b c
>        /MDGROUP NAME=$b VARIABLES=g e f d VALUE=55
>        /MDGROUP NAME=$c LABEL='mdgroup #2' VARIABLES=h i j VALUE='Yes'
>        /MDGROUP NAME=$d LABEL='third mdgroup' CATEGORYLABELS=COUNTEDVALUES
>         VARIABLES=k l m VALUE=34
>        /MDGROUP NAME=$e CATEGORYLABELS=COUNTEDVALUES LABELSOURCE=VARLABEL
>         VARIABLES=n o p VALUE='choice'.
> ```
>
> The above would generate the following multiple response set record
> of subtype 7:
>
> ```
> $a=C 10 my mcgroup a b c
> $b=D2 55 0  g e f d
> $c=D3 Yes 10 mdgroup #2 h i j
> ```
>
> It would also generate the following multiple response set record
> with subtype 19:
>
> ```
> $d=E 1 2 34 13 third mdgroup k l m
> $e=E 11 6 choice 0  n o p
> ```

[^note]: This part of the format may not be fully understood, because
    only a single example of each possibility has been examined.

## Extra Product Info Record

This optional record appears to contain a text string that describes
the program that wrote the file and the source of the data.  (This is
redundant with the file label and product info found in the [file
header record](#file-header-record).)

     /* Header. */
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;

     /* Exactly `count` bytes of data. */
     char                info[];

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Always set to 10.

* `int32 size;`

  The size of each element in the `info` member.  Always set to 1.

* `int32 count;`

  The total number of bytes in `info`.

* `char info[];`

  A text string.  A product that identifies itself as `VOXCO
  INTERVIEWER 4.3` uses CR-only line ends in this field, rather than
  the more usual LF-only or CR LF line ends.

## Variable Display Parameter Record

The variable display parameter record, if present, has the following
format:

     /* Header. */
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;

     /* Repeated `count` times. */
     int32               measure;
     int32               width;           /* Not always present. */
     int32               alignment;

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Always set to 11.

* `int32 size;`

  The size of `int32`.  Always set to 4.

* `int32 count;`

  The number of sets of variable display parameters (ordinarily the
  number of variables in the dictionary), times 2 or 3.

The remaining members are repeated `count` times, in the same order as
the variable records.  No element corresponds to variable records that
continue long string variables.  The meanings of these members are as
follows:

* `int32 measure;`

  The measurement level of the variable:

  | Value | Level    |
  |------:|:---------|
  | 0     | Unknown  |
  | 1     | Nominal  |
  | 2     | Ordinal  |
  | 3     | Scale    |

  An "unknown" `measure` of 0 means that the variable was created in
  some way that doesn't make the measurement level clear, e.g. with a
  `COMPUTE` transformation.  PSPP sets the measurement level the first
  time it reads the data, so this should rarely appear.

* `int32 width;`

  The width of the display column for the variable in characters.

  This field is present if `count` is 3 times the number of variables
  in the dictionary.  It is omitted if `count` is 2 times the number of
  variables.

* `int32 alignment;`

  The alignment of the variable for display purposes:

  | Value | Alignment      |
  |------:|:---------------|
  | 0     | Left aligned   |
  | 1     | Right aligned  |
  | 2     | Centre aligned |

## Variable Sets Record

The SPSS GUI offers users the ability to arrange variables in sets.
Users may enable and disable sets individually, and the data editor and
analysis dialog boxes only show enabled sets.  Syntax does not use
variable sets.

   The variable sets record, if present, has the following format:

```
     /* Header. */
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;

     /* Exactly `count` bytes of text. */
     char                text[];
```

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Always set to 5.

* `int32 size;`

  Always set to 1.

* `int32 count;`

  The total number of bytes in `text`.

* `char text[];`

  The variable sets, in a text-based format.

  Each variable set occupies one line of text, each of which ends
  with a line feed (byte 0x0a), optionally preceded by a carriage
  return (byte 0x0d).

  Each line begins with the name of the variable set, followed by an
  equals sign (`=`) and a space (byte 0x20), followed by the long
  variable names of the members of the set, separated by spaces.  A
  variable set may be empty, in which case the equals sign and the
  space following it are still present.

## Long Variable Names Record

If present, the long variable names record has the following format:

     /* Header. */
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;

     /* Exactly `count` bytes of data. */
     char                var_name_pairs[];

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Always set to 13.

* `int32 size;`

  The size of each element in the `var_name_pairs` member.  Always
  set to 1.

* `int32 count;`

  The total number of bytes in `var_name_pairs`.

* `char var_name_pairs[];`

  A list of key-value tuples, where each key is the name of a
  variable, and the value is its long variable name.  The key field is
  at most 8 bytes long and must match the name of a variable which
  appears in the [variable record](#variable-record).  The value
  field is at most 64 bytes long.  The key and value fields are
  separated by a `=` byte.  Each tuple is separated by a byte whose
  value is 09.  There is no trailing separator following the last
  tuple.  The total length is `count` bytes.

## Very Long String Record

Old versions of SPSS limited string variables to a width of 255 bytes.
For backward compatibility with these older versions, the system file
format represents a string longer than 255 bytes, called a “very long
string”, as a collection of strings no longer than 255 bytes each.  The
strings concatenated to make a very long string are called its
“segments”; for consistency, variables other than very long strings are
considered to have a single segment.

A very long string with a width of `w` has `n = (w + 251) / 252`
segments, that is, one segment for every 252 bytes of width, rounding
up.  It would be logical, then, for each of the segments except the
last to have a width of 252 and the last segment to have the
remainder, but this is not the case.  In fact, each segment except the
last has a width of 255 bytes.  The last has width `w - (n - 1) *
252`; some versions of SPSS make it slightly wider, but not wide
enough to make the last segment require another 8 bytes of data.

Data is packed tightly into segments of a very long string, 255 bytes
per segment.  Because 255 bytes of segment data are allocated for
every 252 bytes of the very long string's width (approximately), some
unused space is left over at the end of the allocated segments.  Data
in unused space is ignored.

> Example: Consider a very long string of width 20,000.  Such a very
long string has 20,000 / 252 = 80 (rounding up) segments.  The first
79 segments have width 255; the last segment has width 20,000 - 79 *
252 = 92 or slightly wider (up to 96 bytes, the next multiple of 8).
The very long string's data is actually stored in the 19,890 bytes in
the first 78 segments, plus the first 110 bytes of the 79th segment
(19,890 + 110 = 20,000).  The remaining 145 bytes of the 79th segment
and all 92 bytes of the 80th segment are unused.

The very long string record explains how to stitch together segments
to obtain very long string data.  For each of the very long string
variables in the dictionary, it specifies the name of its first
segment's variable and the very long string variable's actual width.
The remaining segments immediately follow the named variable in the
system file's dictionary.

The very long string record, which is present only if the system file
contains very long string variables, has the following format:

```
     /* Header. */
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;

     /* Exactly `count` bytes of data. */
     char                string_lengths[];
```

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Always set to 14.

* `int32 size;`

  The size of each element in the `string_lengths` member.  Always
  set to 1.

* `int32 count;`

  The total number of bytes in `string_lengths`.

* `char string_lengths[];`

  A list of key-value tuples, where key is the name of a variable, and
  value is its length.  The key field is at most 8 bytes long and must
  match the name of a variable which appears in the [variable
  record](#variable-record).  The value field is exactly 5 bytes long.
  It is a zero-padded, ASCII-encoded string that is the length of the
  variable.  The key and value fields are separated by a `=` byte.
  Tuples are delimited by a two-byte sequence {00, 09}.  After the
  last tuple, there may be a single byte 00, or {00, 09}.  The total
  length is `count` bytes.

## Character Encoding Record

This record, if present, indicates the character encoding for string
data, long variable names, variable labels, value labels and other
strings in the file.

     /* Header. */
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;

     /* Exactly `count` bytes of data. */
     char                encoding[];

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Always set to 20.

* `int32 size;`

  The size of each element in the `encoding` member.  Always set to 1.

* `int32 count;`

  The total number of bytes in `encoding`.

* `char encoding[];`

  The name of the character encoding.  Normally this will be an
  [official IANA character set name or
  alias](http://www.iana.org/assignments/character-sets).  Character
  set names are not case-sensitive, and SPSS is not consistent,
  e.g. both `windows-1251` and `WINDOWS-1252` have both been observed,
  as have `Big5` and `BIG5`.

This record is not present in files generated by older software.  See
also `character_code` in the [machine integer info
record](#machine-integer-info-record).

The following character encoding names have been observed.  The names
are shown in lowercase, even though they were not always in lowercase in
the file.  Alternative names for the same encoding are, when known,
listed together.  For each encoding, the `character_code` values that
they were observed paired with are also listed.  First, the following
are strictly single-byte, ASCII-compatible encodings:

* (encoding record missing)

  0, 2, 3, 874, 1250, 1251, 1252, 1253, 1254, 1255, 1256, 20127,
  28591, 28592, 28605

* `ansi_x3.4-1968`  
  `ascii`

  1252

* `cp28605`

  2

* `cp874`

  9066

* `iso-8859-1`

  819

* `windows-874`

  874

* `windows-1250`

  2, 1250, 1252

* `windows-1251`

  2, 1251

* `cp1252`  
  `windows-1252`

  2, 1250, 1252, 1253

* `cp1253`  
  `windows-1253`

  1253

* `windows-1254`

  2, 1254

* `windows-1255`

  2, 1255

* `windows-1256`

  2, 1252, 1256

* `windows-1257`

  2, 1257

* `windows-1258`

  1258

The others are multibyte encodings, in which some code points occupy
a single byte and others multiple bytes.  The following multibyte
encodings are "ASCII compatible," that is, they use ASCII values only to
indicate ASCII:

* (encoding record missing)

  65001, 949

* `euc-kr`

  2, 51949

* `utf-8`

  0, 2, 1250, 1251, 1252, 1256, 65001

The following multibyte encodings are not ASCII compatible, that is,
while they encode ASCII characters as their native values, they also use
ASCII values as second or later bytes in multibyte sequences:

* (encoding record missing)

  932, 936, 950

* `big5`  
  `cp950`

  2, 950

* `gbk`

  936

* `cp932`  
  `windows-31j`

  932

As the tables above show, when the character encoding record and the
machine integer info record are both present, they can contradict each
other.  Observations show that, in this case, the character encoding
record should be honored.

If, for testing purposes, a file is crafted with different
`character_code` and `encoding`, it seems that `character_code`
controls the encoding for all strings in the system file before the
dictionary termination record, including strings in data (e.g. string
missing values), and `encoding` controls the encoding for strings
following the dictionary termination record.

## Long String Value Labels Record

This record, if present, specifies value labels for long string
variables.

```
     /* Header. */
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;

     /* Repeated up to exactly `count` bytes. */
     int32               var_name_len;
     char                var_name[];
     int32               var_width;
     int32               n_labels;
     long_string_label   labels[];
```

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Always set to 21.

* `int32 size;`

  Always set to 1.

* `int32 count;`

  The number of bytes following the header until the next header.

* `int32 var_name_len;`  
  `char var_name[];`

  The number of bytes in the name of the variable that has long
  string value labels, plus the variable name itself, which consists
  of exactly `var_name_len` bytes.  The variable name is not padded
  to any particular boundary, nor is it null-terminated.

* `int32 var_width;`

  The width of the variable, in bytes, which will be between 9 and
  32767.

* `int32 n_labels;`  
  `long_string_label labels[];`

  The long string labels themselves.  The `labels` array contains
  exactly `n_labels` elements, each of which has the following
  substructure:

  ```
       int32               value_len;
       char                value[];
       int32               label_len;
       char                label[];
  ```

  - `int32 value_len;`  
    `char value[];`

       The string value being labeled.  `value_len` is the number of
       bytes in `value`; it is equal to `var_width`.  The `value`
       array is not padded or null-terminated.

  - `int32 label_len;`  
    `char label[];`

       The label for the string value.  `label_len`, which must be
       between 0 and 120, is the number of bytes in `label`.  The
       `label` array is not padded or null-terminated.

## Long String Missing Values Record

This record, if present, specifies missing values for long string
variables.

```
     /* Header. */
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;

     /* Repeated up to exactly `count` bytes. */
     int32               var_name_len;
     char                var_name[];
     char                n_missing_values;
     int32               value_len;
     char                values[value_len * n_missing_values];
```

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Always set to 22.

* `int32 size;`

  Always set to 1.

* `int32 count;`

  The number of bytes following the header until the next header.

* `int32 var_name_len;`  
  `char var_name[];`

  The number of bytes in the name of the long string variable that
  has missing values, plus the variable name itself, which consists
  of exactly `var_name_len` bytes.  The variable name is not padded
  to any particular boundary, nor is it null-terminated.

* `char n_missing_values;`

  The number of missing values, either 1, 2, or 3.  (This is,
  unusually, a single byte instead of a 32-bit number.)

* `int32 value_len;`

  The length of each missing value string, in bytes.  This value
  should be 8, because long string variables are at least 8 bytes
  wide (by definition), only the first 8 bytes of a long string
  variable's missing values are allowed to be non-spaces, and any
  spaces within the first 8 bytes are included in the missing value
  here.

* `char values[value_len * n_missing_values]`

  The missing values themselves, without any padding or null
  terminators.

An earlier version of this document stated that `value_len` was
repeated before each of the missing values, so that there was an extra
`int32` value of 8 before each missing value after the first.  Old
versions of PSPP wrote data files in this format.  Readers can tolerate
this mistake, if they wish, by noticing and skipping the extra `int32`
values, which wouldn't ordinarily occur in strings.

## Data File and Variable Attributes Records

The data file and variable attributes records represent custom
attributes for the system file or for individual variables in the
system file, as defined on the `DATAFILE ATTRIBUTE` and `VARIABLE
ATTRIBUTE` commands, respectively.

```
     /* Header. */
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;

     /* Exactly `count` bytes of data. */
     char                attributes[];
```

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Always set to 17 for a data file attribute record
  or to 18 for a variable attributes record.

* `int32 size;`

  The size of each element in the `attributes` member.  Always set to
  value 1.

* `int32 count;`

  The total number of bytes in `attributes`.

* `char attributes[];`

  The attributes, in a text-based format.

  In record subtype 17, this field contains a single attribute set.
  An attribute set is a sequence of one or more attributes
  concatenated together.  Each attribute consists of a name, which
  has the same syntax as a variable name, followed by, inside
  parentheses, a sequence of one or more values.  Each value consists
  of a string enclosed in single quotes (`'`) followed by a line feed
  (byte 0x0a).  A value may contain single quote characters, which
  are not themselves escaped or quoted or required to be present in
  pairs.  There is no apparent way to embed a line feed in a value.
  There is no distinction between an attribute with a single value
  and an attribute array with one element.

  In record subtype 18, this field contains a sequence of one or more
  variable attribute sets.  If more than one variable attribute set
  is present, each one after the first is delimited from the previous
  by `/`.  Each variable attribute set consists of a long variable
  name, followed by `:`, followed by an attribute set with the same
  syntax as on record subtype 17.

  System files written by `Stata 14.1/-savespss- 1.77 by S.Radyakin`
  may include multiple records with subtype 18, one per variable that
  has variable attributes.

  The total length is `count` bytes.

> Example: A system file produced with the following `VARIABLE
> ATTRIBUTE` commands in effect:
>
> ```
> VARIABLE ATTRIBUTE VARIABLES=dummy ATTRIBUTE=fred[1]('23') fred[2]('34').
> VARIABLE ATTRIBUTE VARIABLES=dummy ATTRIBUTE=bert('123').
> ```
>
> will contain a variable attribute record with the following contents:
>
> ```
> 0000  07 00 00 00 12 00 00 00  01 00 00 00 22 00 00 00  |............"...|
> 0010  64 75 6d 6d 79 3a 66 72  65 64 28 27 32 33 27 0a  |dummy:fred('23'.|
> 0020  27 33 34 27 0a 29 62 65  72 74 28 27 31 32 33 27  |'34'.)bert('123'|
> 0030  0a 29                                             |.)              |
> ```

### Variable Roles

A variable's role is represented as an attribute named `$@Role`.  This
attribute has a single element whose values and their meanings are:

|Value|Role       |
|----:|:----------|
|   0 | Input     |
|   1 | Target    |
|   2 | Both      |
|   3 | None      |
|   4 | Partition |
|   5 | Split     |

The default and most common role is 0 (input).

## Extended Number of Cases Record

`ncases` in the [file header record](#file-header-record) expresses
the number of cases in the system file as an `int32`.  This record
allows the number of cases in the system file to be expressed as a
64-bit number.

```
     int32               rec_type;
     int32               subtype;
     int32               size;
     int32               count;
     int64               unknown;
     int64               ncases64;
```

* `int32 rec_type;`

  Record type.  Always set to 7.

* `int32 subtype;`

  Record subtype.  Always set to 16.

* `int32 size;`

  Size of each element.  Always set to 8.

* `int32 count;`

  Number of pieces of data in the data part.  Alway set to 2.

* `int64 unknown;`

  Meaning unknown.  Always set to 1.

* `int64 ncases64;`

  Number of cases in the file as a 64-bit integer.  Presumably this
  could be -1 to indicate that the number of cases is unknown, for
  the same reason as `ncases` in the file header record, but this has
  not been observed in the wild.

## Other Informational Records

This chapter documents many specific types of extension records are
documented here, but others are known to exist.  PSPP ignores unknown
extension records when reading system files.

   The following extension record subtypes have also been observed, with
the following believed meanings:

* 6

  Date info, probably related to USE (according to Aapi Hämäläinen).

* 12

  A UUID in the format described in RFC 4122.  Only two examples
  observed, both written by SPSS 13, and in each case the UUID
  contained both upper and lower case.

* 24

  XML that describes how data in the file should be displayed
  on-screen.

## Dictionary Termination Record

The dictionary termination record separates all other records from the
data records.

```
     int32               rec_type;
     int32               filler;
```

* `int32 rec_type;`

  Record type.  Always set to 999.

* `int32 filler;`

  Ignored padding.  Should be set to 0.

## Data Record

The data record must follow all other records in the system file.  Every
system file must have a data record that specifies data for at least one
case.  The format of the data record varies depending on the value of
`compression` in the file header record:

* 0: no compression

  Data is arranged as a series of 8-byte elements.  Each element
  corresponds to the variable declared in the respective [variable
  record](#variable-record).  Numeric values are given in `flt64`
  format; string values are literal characters string, padded on the
  right when necessary to fill out 8-byte units.

* 1: bytecode compression

  The first 8 bytes of the data record is divided into a series of
  1-byte command codes.  These codes have meanings as described
  below:

  - 0

    Ignored.  If the program writing the system file accumulates
    compressed data in blocks of fixed length, 0 bytes can be used
    to pad out extra bytes remaining at the end of a fixed-size
    block.

  - 1 through 251

    A number with value `code - bias`, where `code` is the value of
    the compression code and `bias` comes from the file header.

    > Example: Code 105 with bias 100.0 (the normal value) indicates a
    > numeric variable of value 5.

    A code of 0 (after subtracting the bias) in a string field encodes
    null bytes.  This is unusual, since a string field normally
    encodes text data, but it exists in real system files.

  - 252

    End of file.  This code may or may not appear at the end of the
    data stream.  PSPP always outputs this code but its use is not
    required.

  - 253

    A numeric or string value that is not compressible.  The value is
    stored in the 8 bytes following the current block of command
    bytes.  If this value appears twice in a block of command bytes,
    then it indicates the second group of 8 bytes following the
    command bytes, and so on.

  - 254

    An 8-byte string value that is all spaces.

  - 255

    The system-missing value.

  The end of the 8-byte group of bytecodes is followed by any 8-byte
  blocks of non-compressible values indicated by code 253.  After that
  follows another 8-byte group of bytecodes, then those bytecodes'
  non-compressible values.  The pattern repeats to the end of the file
  or a code with value 252.

* 2: ZLIB compression

  The data record consists of the following, in order:

  - ZLIB data header, 24 bytes long.

  - One or more variable-length blocks of ZLIB compressed data.

  - ZLIB data trailer, with a 24-byte fixed header plus an
    additional 24 bytes for each preceding ZLIB compressed data
    block.

  The ZLIB data header has the following format:

  ```
       int64               zheader_ofs;
       int64               ztrailer_ofs;
       int64               ztrailer_len;
  ```

  - `int64 zheader_ofs;`

    The offset, in bytes, of the beginning of this structure within
    the system file.  A reader does not need to use this, so it can
    ignore it (PSPP issues a warning if it does not match its own
    offset).

  - `int64 ztrailer_ofs;`

    The offset, in bytes, of the first byte of the ZLIB data
    trailer.

  - `int64 ztrailer_len;`

    The number of bytes in the ZLIB data trailer.  This and the
    previous field sum to the size of the system file in bytes.

  The data header is followed by `(ztrailer_len - 24) / 24` ZLIB
  compressed data blocks.  Each ZLIB compressed data block begins
  with a ZLIB header as specified in RFC 1950, e.g. hex bytes `78 01`
  (the only header yet observed in practice).  Each block
  decompresses to a fixed number of bytes (in practice only
  `0x3ff000`-byte blocks have been observed), except that the last
  block of data may be shorter.  The last ZLIB compressed data block
  gends just before offset `ztrailer_ofs`.

  The result of ZLIB decompression is bytecode compressed data as
  described above for compression format 1.

  The ZLIB data trailer begins with the following 24-byte fixed
  header:

  ```
       int64               bias;
       int64               zero;
       int32               block_size;
       int32               n_blocks;
  ```

  - `int64 int_bias;`

    The compression bias as a negative integer, e.g. if `bias` in
    the file header record is 100.0, then `int_bias` is −100 (this
    is the only value yet observed in practice).

  - `int64 zero;`

    Always observed to be zero.

  - `int32 block_size;`

    The number of bytes in each ZLIB compressed data block, except
    possibly the last, following decompression.  Only `0x3ff000`
    has been observed so far.

  - `int32 n_blocks;`

    The number of ZLIB compressed data blocks, always exactly
    `(ztrailer_len - 24) / 24`.

  The fixed header is followed by `n_blocks` 24-byte ZLIB data block
  descriptors, each of which describes the compressed data block
  corresponding to its offset.  Each block descriptor has the
  following format:

  ```
       int64               uncompressed_ofs;
       int64               compressed_ofs;
       int32               uncompressed_size;
       int32               compressed_size;
  ```

  - `int64 uncompressed_ofs;`

    The offset, in bytes, that this block of data would have in a
    similar system file that uses compression format 1.  This is
    `zheader_ofs` in the first block descriptor, and in each
    succeeding block descriptor it is the sum of the previous
    desciptor's `uncompressed_ofs` and `uncompressed_size`.

  - `int64 compressed_ofs;`

    The offset, in bytes, of the actual beginning of this
    compressed data block.  This is `zheader_ofs + 24` in the
    first block descriptor, and in each succeeding block
    descriptor it is the sum of the previous descriptor's
    `compressed_ofs` and `compressed_size`.  The final block
    descriptor's `compressed_ofs` and `compressed_size` sum to
    `ztrailer_ofs`.

  - `int32 uncompressed_size;`

    The number of bytes in this data block, after decompression.
    This is `block_size` in every data block except the last,
    which may be smaller.

  - `int32 compressed_size;`

    The number of bytes in this data block, as stored compressed
    in this system file.

