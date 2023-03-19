# Portable File Format

These days, most computers use the same internal data formats for
integer and floating-point data, if one ignores little differences like
big- versus little-endian byte ordering.  However, occasionally it is
necessary to exchange data between systems with incompatible data
formats.  This is what portable files are designed to do.

The portable file format is mostly obsolete.  [System
files](system-file.md) are a better alternative.

> This information is gleaned from examination of ASCII-formatted
portable files only, so some of it may be incorrect for portable files
formatted in EBCDIC or other character sets.

<!-- toc -->

## Portable File Characters

Portable files are arranged as a series of lines of 80 characters each.
Each line is terminated by a carriage-return, line-feed sequence
("new-lines").  New-lines are only used to avoid line length limits
imposed by some OSes; they are not meaningful.

Most lines in portable files are exactly 80 characters long.  The
only exception is a line that ends in one or more spaces, in which the
spaces may optionally be omitted.  Thus, a portable file reader must act
as though a line shorter than 80 characters is padded to that length
with spaces.

The file must be terminated with a `Z` character.  In addition, if
the final line in the file does not have exactly 80 characters, then it
is padded on the right with `Z` characters.  (The file contents may be
in any character set; the file contains a description of its own
character set, as explained in the next section.  Therefore, the `Z`
character is not necessarily an ASCII `Z`.)

For the rest of the description of the portable file format,
new-lines and the trailing `Z`s will be ignored, as if they did not
exist, because they are not an important part of understanding the file
contents.

## Portable File Structure

Every portable file consists of the following records, in sequence:

- File header.

- Version and date info.

- Product identification.

- Author identification (optional).

- Subproduct identification (optional).

- Variable count.

- Case weight variable (optional).

- Variables.  Each variable record may optionally be followed by a
  missing value record and a variable label record.

- Value labels (optional).

- Documents (optional).

- Data.

Most records are identified by a single-character tag code.  The file
header and version info record do not have a tag.

Other than these single-character codes, there are three types of
fields in a portable file: floating-point, integer, and string.
Floating-point fields have the following format:

- Zero or more leading spaces.

- Optional asterisk (`*`), which indicates a missing value.  The
  asterisk must be followed by a single character, generally a period
  (`.`), but it appears that other characters may also be possible.
  This completes the specification of a missing value.

- Optional minus sign (`-`) to indicate a negative number.

- A whole number, consisting of one or more base-30 digits: `0`
  through `9` plus capital letters `A` through `T`.

- Optional fraction, consisting of a radix point (`.`) followed by
  one or more base-30 digits.

- Optional exponent, consisting of a plus or minus sign (`+` or `-`)
  followed by one or more base-30 digits.

- A forward slash (`/`).

Integer fields take a form identical to floating-point fields, but
they may not contain a fraction.

String fields take the form of a integer field having value N,
followed by exactly N characters, which are the string content.

## Portable File Header

Every portable file begins with a 464-byte header, consisting of a
200-byte collection of vanity splash strings, followed by a 256-byte
character set translation table, followed by an 8-byte tag string.

The 200-byte segment is divided into five 40-byte sections, each of
which represents the string `CHARSET SPSS PORT FILE` in a different
character set encoding, where `CHARSET` is the name of the character set
used in the file, e.g. `ASCII` or `EBCDIC`.  Each string is padded on
the right with spaces in its respective character set.

It appears that these strings exist only to inform those who might
view the file on a screen, and that they are not parsed by SPSS
products.  Thus, they can be safely ignored.  For those interested, the
strings are supposed to be in the following character sets, in the
specified order: EBCDIC, 7-bit ASCII, CDC 6-bit ASCII, 6-bit ASCII,
Honeywell 6-bit ASCII.

The 256-byte segment describes a mapping from the character set used
in the portable file to an arbitrary character set having characters at
the following positions:

* 0-60: Control characters.  Not important enough to describe in full here.

* 61-63: Reserved.

* 64-73: Digits `0` through `9`.

* 74-99: Capital letters `A` through `Z`.

* 100-125: Lowercase letters `a` through `z`.

* 126: Space.

* 127-130: Symbols `.<(+`

* 131: Solid vertical pipe.

* 132-142: Symbols `&[]!$*);^-/`

* 143: Broken vertical pipe.

* 144-150: Symbols `,%_>`?``:`

* 151: British pound symbol.

* 152-155: Symbols `@'="`.

* 156: Less than or equal symbol.

* 157: Empty box.

* 158: Plus or minus.

* 159: Filled box.

* 160: Degree symbol.

* 161: Dagger.

* 162: Symbol `~`.

* 163: En dash.

* 164: Lower left corner box draw.

* 165: Upper left corner box draw.

* 166: Greater than or equal symbol.

* 167-176: Superscript `0` through `9`.

* 177: Lower right corner box draw.

* 178: Upper right corner box draw.

* 179: Not equal symbol.

* 180: Em dash.

* 181: Superscript `(`.

* 182: Superscript `)`.

* 183: Horizontal dagger (?).

* 184-186: Symbols `{}\`.

* 187: Cents symbol.

* 188: Centered dot, or bullet.

* 189-255: Reserved.

Symbols that are not defined in a particular character set are set to
the same value as symbol 64; i.e., to `0`.

The 8-byte tag string consists of the exact characters `SPSSPORT` in
the portable file's character set, which can be used to verify that the
file is indeed a portable file.

## Version and Date Info Record

This record does not have a tag code.  It has the following structure:

- A single character identifying the file format version.  The letter
  A represents version 0, and so on.

- An 8-character string field giving the file creation date in the
  format YYYYMMDD.

- A 6-character string field giving the file creation time in the
  format HHMMSS.

## Identification Records

The product identification record has tag code `1`.  It consists of a
single string field giving the name of the product that wrote the
portable file.

The author identification record has tag code `2`.  It is optional.
If present, it consists of a single string field giving the name of the
person who caused the portable file to be written.

The subproduct identification record has tag code `3`.  It is
optional.  If present, it consists of a single string field giving
additional information on the product that wrote the portable file.

## Variable Count Record

The variable count record has tag code `4`.  It consists of a single
integer field giving the number of variables in the file dictionary.

## Precision Record

The precision record has tag code `5`.  It consists of a single integer
field specifying the maximum number of base-30 digits used in data in
the file.

## Case Weight Variable Record

The case weight variable record is optional.  If it is present, it
indicates the variable used for weighting cases; if it is absent, cases
are unweighted.  It has tag code `6`.  It consists of a single string
field that names the weighting variable.

## Variable Records

Each variable record represents a single variable.  Variable records
have tag code `7`.  They have the following structure:

- Width (integer).  This is 0 for a numeric variable, and a number
  between 1 and 255 for a string variable.

- Name (string).  1-8 characters long.  Must be in all capitals.

  A few portable files that contain duplicate variable names have
  been spotted in the wild.  PSPP handles these by renaming the
  duplicates with numeric extensions: `VAR_1`, `VAR_2`, and so on.

- Print format.  This is a set of three integer fields:

  - [Format type](system-file.md#format-types) encoded the same as in
    system files.

  - Format width.  1-40.

  - Number of decimal places.  1-40.

  A few portable files with invalid format types or formats that are
  not of the appropriate width for their variables have been spotted
  in the wild.  PSPP assigns a default `F` or `A` format to a variable
  with an invalid format.

- Write format.  Same structure as the print format described above.

Each variable record can optionally be followed by a missing value
record, which has tag code `8`.  A missing value record has one field,
the missing value itself (a floating-point or string, as appropriate).
Up to three of these missing value records can be used.

There is also a record for missing value ranges, which has tag code
`B`.  It is followed by two fields representing the range, which are
floating-point or string as appropriate.  If a missing value range is
present, it may be followed by a single missing value record.

Tag codes `9` and `A` represent `LO THRU X` and `X THRU HI` ranges,
respectively.  Each is followed by a single field representing X.  If
one of the ranges is present, it may be followed by a single missing
value record.

In addition, each variable record can optionally be followed by a
variable label record, which has tag code `C`.  A variable label record
has one field, the variable label itself (string).

## Value Label Records

Value label records have tag code `D`.  They have the following format:

- Variable count (integer).

- List of variables (strings).  The variable count specifies the
  number in the list.  Variables are specified by their names.  All
  variables must be of the same type (numeric or string), but string
  variables do not necessarily have the same width.

- Label count (integer).

- List of (value, label) tuples.  The label count specifies the
  number of tuples.  Each tuple consists of a value, which is numeric
  or string as appropriate to the variables, followed by a label
  (string).

A few portable files that specify duplicate value labels, that is,
two different labels for a single value of a single variable, have been
spotted in the wild.  PSPP uses the last value label specified in these
cases.

## Document Record

One document record may optionally follow the value label record.  The
document record consists of tag code `E`, following by the number of
document lines as an integer, followed by that number of strings, each
of which represents one document line.  Document lines must be 80 bytes
long or shorter.

## Portable File Data

The data record has tag code `F`.  There is only one tag for all the
data; thus, all the data must follow the dictionary.  The data is
terminated by the end-of-file marker `Z`, which is not valid as the
beginning of a data element.

Data elements are output in the same order as the variable records
describing them.  String variables are output as string fields, and
numeric variables are output as floating-point fields.

