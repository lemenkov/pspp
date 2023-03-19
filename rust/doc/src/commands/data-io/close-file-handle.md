# CLOSE FILE HANDLE

```
CLOSE FILE HANDLE HANDLE_NAME.
```

`CLOSE FILE HANDLE` disassociates the name of a [file
handle](../../language/files/file-handles.md) with a given file.  The
only specification is the name of the handle to close.  Afterward
`FILE HANDLE`.

The file named INLINE, which represents data entered between `BEGIN
DATA` and `END DATA`, cannot be closed.  Attempts to close it with
`CLOSE FILE HANDLE` have no effect.

`CLOSE FILE HANDLE` is a PSPP extension.

