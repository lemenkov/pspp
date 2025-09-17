# SPSS/PC+ System File Format

SPSS/PC+, first released in 1984, was a simplified version of SPSS for
IBM PC and compatible computers.  It used a data file format related to
the one described in the previous chapter, but simplified and
incompatible.  The SPSS/PC+ software became obsolete in the 1990s, so
files in this format are rarely encountered today.  Nevertheless, for
completeness, and because it is not very difficult, it seems worthwhile
to support at least reading these files.  This chapter documents this
format, based on examination of a corpus of about 60 files from a
variety of sources.

System files use four data types: 8-bit characters, 16-bit unsigned
integers, 32-bit unsigned integers, and 64-bit floating points, called
here `char`, `uint16`, `uint32`, and `flt64`, respectively.  Data is not
necessarily aligned on a word or double-word boundary.

SPSS/PC+ ran only on IBM PC and compatible computers.  Therefore,
values in these files are always in little-endian byte order.
Floating-point numbers are always in IEEE 754 format.

SPSS/PC+ system files represent the system-missing value as
-1.66e308, or `f5 1e 26 02 8a 8c ed ff` expressed as hexadecimal.  (This
is an unusual choice: it is close to, but not equal to, the largest
negative 64-bit IEEE 754, which is about -1.8e308.)

Text in SPSS/PC+ system file is encoded in ASCII-based 8-bit MS DOS
codepages.  The corpus used for investigating the format were all
ASCII-only.

An SPSS/PC+ system file begins with the following 256-byte directory:

```
uint32              two;
uint32              zero;
struct {
    uint32          ofs;
    uint32          len;
} records[15];
char                filename[128];
```

* `uint32 two;`  
  `uint32 zero;`  
  Always set to 2 and 0, respectively.

  These fields could be used as a signature for the file format, but
  the `product` field in [record 0](#record-0-main-header-record)
  seems more likely to be unique.

* `struct { ... } records[15];`  
  Each of the elements in this array identifies a record in the
  system file.  The `ofs` is a byte offset, from the beginning of the
  file, that identifies the start of the record.  `len` specifies the
  length of the record, in bytes.  Many records are optional or not
  used.  If a record is not present, `ofs` and `len` for that record
  are both are zero.

* `char filename[128];`  
  In most files in the corpus, this field is entirely filled with
  spaces or null bytes.  In others, it contains a filename, which
  generally contains doubled backslashes,
  e.g. `c:\\doli\\altm\\f_sum94.sys`.  The unusual extension `(_)` is
  also common, e.g. `DER56.(_)`.

The following sections describe the contents of each record,
identified by the index into the `records` array.

<!-- toc -->

## Record 0: Main Header Record

All files in the corpus have this record at offset 0x100 with length
0xb0 (but readers should find this record, like the others, via the
`records` table in the directory).  Its format is:

```
uint16              one0;
char                family[2];
char                product[60];
flt64               sysmis;
uint32              zero0;
uint32              zero1;
uint16              one1;
uint16              compressed;
uint16              nominal_case_size;
uint16              n_cases0;
uint16              weight_index;
uint16              unknown;
uint16              n_cases1;
uint16              zero2;
char                creation_date[8];
char                creation_time[8];
char                file_label[64];
```

* `uint16 one0;`  
  `uint16 one1;`  
  Always set to 1.

* `uint32 zero0;`  
  `uint32 zero1;`  
  `uint16 zero2;`  
  Always set to 0.

* `uint16 unknown;`
  Unknown meaning.  Usually set to 0.

* `char family[2];`  
  Identifies the product family that created the file.  This is either
  `PC` for SPSS/PC+ and related software, or `DE` for SPSS Data Entry
  and related software.

* `char product[60];`  
  Name of the program that created the file.  Only the following
  unique values have been observed, in each case padded on the right
  with spaces:

  ```
  SPSS/PC+ System File Written by Data Entry II
  SPSS SYSTEM FILE.  IBM PC DOS, SPSS/PC+
  SPSS SYSTEM FILE.  IBM PC DOS, SPSS/PC+ V3.0
  SPSS SYSTEM FILE.  IBM PC DOS, SPSS for Windows
  ```

  Thus, it is reasonable to use the presence of the string `SPSS` at
  offset 0x104 as a simple test for an SPSS/PC+ data file.

* `flt64 sysmis;`  
  The system-missing value, as described previously.

* `uint16 compressed;`  
  Set to 0 if the data in the file is not compressed, 1 if the data
  is compressed with simple bytecode compression.

  > The corpus contains a mix of compressed and uncompressed files.

* `uint16 nominal_case_size;`  
  Number of data elements per case.  This is the number of variables,
  except that long string variables add extra data elements (one for
  every 8 bytes after the first 8).  String variables in SPSS/PC+
  system files are limited to 255 bytes.

* `uint16 n_cases0;`  
  `uint16 n_cases1;`  
  The number of cases in the data record.  Both values are the same.

  > Readers must use these case counts because some files in the corpus
  contain garbage that somewhat resembles data after the specified
  number of cases.

* `uint16 weight_index;`  
  0, if the file is unweighted, otherwise a 1-based index into the
  data record of the weighting variable, e.g. 4 for the first
  variable after the 3 system-defined variables.

* `char creation_date[8];`  
  The date that the file was created, in `mm/dd/yy` format.

  > Single-digit days and months are not prefixed by zeros.  The string
  is padded with spaces on right or left or both, e.g.  `_2/4/93_`,
  `10/5/87_`, and `_1/11/88` (with `_` standing in for a space) are
  all actual examples from the corpus.

* `char creation_time[8];`  
  The time that the file was created, in `HH:MM:SS` format.

  > Single-digit hours are padded on the left with a space.  Minutes
  and seconds are always written as two digits.

* `char file_label[64];`  
  [File label](commands/file-label.md) declared by the user, if any.
  Padded on the right with spaces.

## Record 1: Variables Record

The variables record most commonly starts at offset 0x1b0, but it can be
placed elsewhere.  The record contains instances of the following
32-byte structure:

```
uint32              value_label_start;
uint32              value_label_end;
uint32              var_label_ofs;
uint32              format;
char                name[8];
union {
    flt64           f;
    char            s[8];
} missing;
```

The number of instances is the `nominal_case_size` specified in the
main header record.  There is one instance for each numeric variable
and each string variable with width 8 bytes or less.  String variables
wider than 8 bytes have one instance for each 8 bytes, rounding up.
The first instance for a long string specifies the variable's correct
dictionary information.  Subsequent instances for a long string are
generally filled with all-zero bytes, although the `missing` field
contains the numeric system-missing value, and some writers also fill
in `var_label_ofs`, `format`, and `name`, sometimes filling the latter
with the numeric system-missing value rather than a text string.
Regardless of the values used, readers should ignore the contents of
these additional instances for long strings.

* `uint32 value_label_start;`  
  `uint32 value_label_end;`  
  These specify offsets into the label record of the start and end of
  value labels for this variable.  They are zero if there are no value
  labels.  See the [labels record](#record-2-labels-record), for more
  information.  A long string variable may not have value labels.

  Sometimes the data is, instead of value labels, some form of data
  validation rules for SPSS Data Entry.  There is no known way to
  distinguish, except that data validation rules often cannot be
  interpreted as valid value labels because the label length field
  makes them not fit exactly in the allocated space.

  > It appears that SPSS products cannot properly read these either.
  > All the files in the corpus with these problems are closely
  > related, so it's also possible that they are corrupted in some
  > way.

* `uint32 var_label_ofs;`  
  For a variable with a variable label, this specifies an offset into
  the label record.  See the [labels record](#record-2-labels-record),
  for more information.

  For a variable without a variable label, this is zero.

* `uint32 format;`  
  The variable's output format, in the [format used in system
  files](system-file.md#variable-record).  SPSS/PC+ system files only
  use format types 5 (F, for numeric variables) and 1 (A, for string
  variables).

* `char name[8];`  
  The variable's name, padded on the right with spaces.

* `union { ... } missing;`  
  A user-missing value.  For numeric variables, `missing.f` is the
  variable's user-missing value.  For string variables, `missing.s`
  is a string missing value.  A variable without a user-missing value
  is indicated with `missing.f` set to the system-missing value, even
  for string variables (!).  A long string variable may not have a
  missing value.

In addition to the user-defined variables, every SPSS/PC+ system file
contains, as its first three variables, the following system-defined
variables, in the following order.  The system-defined variables have
no variable label, value labels, or missing values.  PSPP renames
these variables to start with `@` when it reads an SPSS/PC+ system
file.

* `$CASENUM`  
  A numeric variable with format `F8.0`.  Most of the time this is a
  sequence number, starting with 1 for the first case and counting up
  for each subsequent case.  Some files skip over values, which
  probably reflects cases that were deleted.

* `$DATE`  
  A string variable with format `A8`.  Same format (including varying
  padding) as the `creation_date` field in the [main header
  record](#record-0-main-header-record).  The actual date can differ
  from `creation_date` and from record to record.  This may reflect
  when individual cases were added or updated.

* `$WEIGHT`  
  A numeric variable with format `F8.2`.  This represents the case's
  weight.  If weighting has not been enabled, every case has value
  1.0.

## Record 2: Labels Record

The labels record holds value labels and variable labels.  Unlike the
other records, it is not meant to be read directly and sequentially.
Instead, this record must be interpreted one piece at a time, by
following pointers from the variables record.

The `value_label_start`, `value_label_end`, and `var_label_ofs`
fields in a variable record are all offsets relative to the beginning of
the labels record, with an additional 7-byte offset.  That is, if the
labels record starts at byte offset `labels_ofs` and a variable has a
given `var_label_ofs`, then the variable label begins at byte offset
`labels_ofs` + `var_label_ofs + 7` in the file.

A variable label, starting at the offset indicated by
`var_label_ofs`, consists of a one-byte length followed by the specified
number of bytes of the variable label string, like this:

```
uint8               length;
char                s[length];
```

A set of value labels, extending from `value_label_start` to
`value_label_end` (exclusive), consists of a numeric or string value
followed by a string in the format just described.  String values are
padded on the right with spaces to fill the 8-byte field, like this:

```
union {
    flt64           f;
    char            s[8];
} value;
uint8               length;
char                s[length];
```

The labels record begins with a pair of `uint32` values.  The first of
these is always 3.  The second is between 8 and 16 less than the
number of bytes in the record.  Neither value is important for
interpreting the file.

## Record 3: Data Record

The format of the data record varies depending on the value of
`compressed` in the file header record:

* 0: no compression  
  Data is arranged as a series of 8-byte elements, one per variable
  instance variable in the [variable
  record](#record-1-variables-record).  Numeric values are given in
  `flt64` format; string values are literal characters string, padded
  on the right with spaces when necessary to fill out 8-byte units.

* 1: bytecode compression  
  The first 8 bytes of the data record is divided into a series of
  1-byte command codes.  These codes have meanings as described
  below:

  - 0  
    The system-missing value.

  - 1  
    A numeric or string value that is not compressible.  The value
    is stored in the 8 bytes following the current block of
    command bytes.  If this value appears twice in a block of
    command bytes, then it indicates the second group of 8 bytes
    following the command bytes, and so on.

  - 2 through 255  
    A number with value `CODE - 100`, where `CODE` is the value of the
    compression code.  For example, code 105 indicates a numeric
    variable of value 5.

  The end of the 8-byte group of command codes is followed by any
  8-byte blocks of non-compressible values indicated by code 1.  After
  that follows another 8-byte group of command codes, then those
  command codes' non-compressible values.  The pattern repeats up to
  the number of cases specified by the main header record have been
  seen.

  The corpus does not contain any files with command codes 2 through
  95, so it is possible that some of these codes are used for special
  purposes.

Cases of data often, but not always, fill the entire data record.
Readers should stop reading after the number of cases specified in the
main header record.  Otherwise, readers may try to interpret garbage
following the data as additional cases.

## Records 4 and 5: Data Entry

Records 4 and 5 appear to be related to SPSS/PC+ Data Entry.

