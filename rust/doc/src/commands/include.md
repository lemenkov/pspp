# INCLUDE

```
INCLUDE [FILE=]'FILE_NAME' [ENCODING='ENCODING'].
```

`INCLUDE` causes the PSPP command processor to read an additional
command file as if it were included bodily in the current command file.
If errors are encountered in the included file, then command processing
stops and no more commands are processed.  Include files may be nested
to any depth, up to the limit of available memory.

The [`INSERT`](insert.md) command is a more flexible alternative to
`INCLUDE`.  An `INCLUDE` command acts the same as `INSERT` with
`ERROR=STOP CD=NO SYNTAX=BATCH` specified.

The optional `ENCODING` subcommand has the same meaning as with
`INSERT`.

