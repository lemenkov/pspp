# XSAVE

```
XSAVE
        /OUTFILE='FILE_NAME'
        /{UNCOMPRESSED,COMPRESSED,ZCOMPRESSED}
        /PERMISSIONS={WRITEABLE,READONLY}
        /DROP=VAR_LIST
        /KEEP=VAR_LIST
        /VERSION=VERSION
        /RENAME=(SRC_NAMES=TARGET_NAMES)...
        /NAMES
        /MAP
```

The `XSAVE` transformation writes the active dataset's dictionary and
data to a system file.  It is similar to the `SAVE` procedure, with
two differences:

- `XSAVE` is a transformation, not a procedure.  It is executed when
  the data is read by a procedure or procedure-like command.

- `XSAVE` does not support the `UNSELECTED` subcommand.

See [`SAVE`](save.md) for more information.

