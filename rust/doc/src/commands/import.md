# IMPORT

```
IMPORT
        /FILE='FILE_NAME'
        /TYPE={COMM,TAPE}
        /DROP=VAR_LIST
        /KEEP=VAR_LIST
        /RENAME=(SRC_NAMES=TARGET_NAMES)...
```

The `IMPORT` transformation clears the active dataset dictionary and
data and replaces them with a dictionary and data from a system file or
portable file.

> `IMPORT` is obsolete and retained only for compatibility with
> existing portable files.  New syntax should use [`SAVE`](save.md) to
> write system files instead, and [`GET`](get.md) to read them.

The `FILE` subcommand, which is the only required subcommand,
specifies the portable file to be read as a file name string or a
[file handle](../language/files/file-handles.md).

The `TYPE` subcommand is currently not used.

`DROP`, `KEEP`, and `RENAME` follow the syntax used by
[`GET`](get.md).

`IMPORT` does not cause the data to be read; only the dictionary.
The data is read later, when a procedure is executed.

Use of `IMPORT` to read a system file is a PSPP extension.

