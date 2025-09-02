# SAVE DATA COLLECTION

```
SAVE DATA COLLECTION
        /OUTFILE={'FILE_NAME',FILE_HANDLE}
        /METADATA={'FILE_NAME',FILE_HANDLE}
        /{UNCOMPRESSED,COMPRESSED,ZCOMPRESSED}
        /PERMISSIONS={WRITEABLE,READONLY}
        /DROP=VAR_LIST
        /KEEP=VAR_LIST
        /VERSION=VERSION
        /RENAME=(SRC_NAMES=TARGET_NAMES)...
        /NAMES
        /MAP
```

Like `SAVE`, `SAVE DATA COLLECTION` writes the dictionary and data in
the active dataset to a system file.  In addition, it writes metadata to
an additional XML metadata file.

`OUTFILE` is required.  Specify the system file to be written as a
string file name or a [file
handle](../language/files/file-handles.md).
handle](../language/files/file-handles.md).

`METADATA` is also required.  Specify the metadata file to be written
as a string file name or a file handle.  Metadata files customarily use
a `.mdd` extension.

The current implementation of this command is experimental.  It only
outputs an approximation of the metadata file format.  Please report
bugs.

Other subcommands are optional.  They have the same meanings as in
the `SAVE` command.

`SAVE DATA COLLECTION` causes the data to be read.  It is a procedure.

