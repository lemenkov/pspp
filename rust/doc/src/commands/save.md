# SAVE

```
SAVE
        /OUTFILE={'FILE_NAME',FILE_HANDLE}
        /UNSELECTED={RETAIN,DELETE}
        /{UNCOMPRESSED,COMPRESSED,ZCOMPRESSED}
        /PERMISSIONS={WRITEABLE,READONLY}
        /DROP=VAR_LIST
        /KEEP=VAR_LIST
        /VERSION=VERSION
        /RENAME=(SRC_NAMES=TARGET_NAMES)...
        /NAMES
        /MAP
```

   The `SAVE` procedure causes the dictionary and data in the active
dataset to be written to a system file.

   `OUTFILE` is the only required subcommand.  Specify the system file
to be written as a string file name or a [file
handle](../language/files/file-handles.md).
handle](../language/files/file-handles.md).

   By default, cases excluded with `FILTER` are written to the system
file.  These can be excluded by specifying `DELETE` on the `UNSELECTED`
subcommand.  Specifying `RETAIN` makes the default explicit.

   The `UNCOMPRESSED`, `COMPRESSED`, and `ZCOMPRESSED` subcommand
determine the system file's compression level:

* `UNCOMPRESSED`  
  Data is not compressed.  Each numeric value uses 8 bytes of disk
  space.  Each string value uses one byte per column width, rounded
  up to a multiple of 8 bytes.

* `COMPRESSED`  
  Data is compressed in a simple way.  Each integer numeric value
  between −99 and 151, inclusive, or system missing value uses one
  byte of disk space.  Each 8-byte segment of a string that consists
  only of spaces uses 1 byte.  Any other numeric value or 8-byte
  string segment uses 9 bytes of disk space.

* `ZCOMPRESSED`  
  Data is compressed with the "deflate" compression algorithm
  specified in RFC 1951 (the same algorithm used by `gzip`).  Files
  written with this compression level cannot be read by PSPP 0.8.1 or
  earlier or by SPSS 20 or earlier.

`COMPRESSED` is the default compression level.  The [`SET`](set.md)
command can change this default.

The `PERMISSIONS` subcommand specifies operating system permissions
for the new system file.  `WRITEABLE`, the default, creates the file
with read and write permission.  `READONLY` creates the file for
read-only access.

By default, all the variables in the active dataset dictionary are
written to the system file.  The `DROP` subcommand can be used to
specify a list of variables not to be written.  In contrast, `KEEP`
specifies variables to be written, with all variables not specified
not written.

Normally variables are saved to a system file under the same names
they have in the active dataset.  Use the `RENAME` subcommand to change
these names.  Specify, within parentheses, a list of variable names
followed by an equals sign (`=`) and the names that they should be
renamed to.  Multiple parenthesized groups of variable names can be
included on a single `RENAME` subcommand.  Variables' names may be
swapped using a `RENAME` subcommand of the form `/RENAME=(A B=B A)`.

Alternate syntax for the `RENAME` subcommand allows the parentheses to
be eliminated.  When this is done, only a single variable may be
renamed at once.  For instance, `/RENAME=A=B`.  This alternate syntax
is discouraged.

`DROP`, `KEEP`, and `RENAME` are performed in left-to-right order.
They each may be present any number of times.  `SAVE` never modifies
the active dataset.  `DROP`, `KEEP`, and `RENAME` only affect the
system file written to disk.

The `VERSION` subcommand specifies the version of the file format.
Valid versions are 2 and 3.  The default version is 3.  In version 2
system files, variable names longer than 8 bytes are truncated.  The
two versions are otherwise identical.

The `NAMES` and `MAP` subcommands are currently ignored.

`SAVE` causes the data to be read.  It is a procedure.

