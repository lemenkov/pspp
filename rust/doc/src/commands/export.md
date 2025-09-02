# EXPORT

```
EXPORT
        /OUTFILE='FILE_NAME'
        /UNSELECTED={RETAIN,DELETE}
        /DIGITS=N
        /DROP=VAR_LIST
        /KEEP=VAR_LIST
        /RENAME=(SRC_NAMES=TARGET_NAMES)...
        /TYPE={COMM,TAPE}
        /MAP
```

   The `EXPORT` procedure writes the active dataset's dictionary and
data to a specified portable file.

   `UNSELECTED` controls whether cases excluded with
[`FILTER`](filter.md) are written to the file.  These can
be excluded by specifying `DELETE` on the `UNSELECTED` subcommand.
The default is `RETAIN`.

   Portable files express real numbers in base 30.  Integers are
always expressed to the maximum precision needed to make them exact.
Non-integers are, by default, expressed to the machine's maximum
natural precision (approximately 15 decimal digits on many machines).
If many numbers require this many digits, the portable file may
significantly increase in size.  As an alternative, the `DIGITS`
subcommand may be used to specify the number of decimal digits of
precision to write.  `DIGITS` applies only to non-integers.

   The `OUTFILE` subcommand, which is the only required subcommand,
specifies the portable file to be written as a file name string or a
[file handle](../language/files/file-handles.md).

`DROP`, `KEEP`, and `RENAME` have the same syntax and meaning as for
the [`SAVE`](save.md) command.

   The `TYPE` subcommand specifies the character set for use in the
portable file.  Its value is currently not used.

   The `MAP` subcommand is currently ignored.

   `EXPORT` is a procedure.  It causes the active dataset to be read.

