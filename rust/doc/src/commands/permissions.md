# PERMISSIONS

```
PERMISSIONS
        FILE='FILE_NAME'
        /PERMISSIONS = {READONLY,WRITEABLE}.
```

`PERMISSIONS` changes the permissions of a file.  There is one
mandatory subcommand which specifies the permissions to which the file
should be changed.  If you set a file's permission to `READONLY`, then
the file will become unwritable either by you or anyone else on the
system.  If you set the permission to `WRITEABLE`, then the file
becomes writeable by you; the permissions afforded to others are
unchanged.  This command cannot be used if the [`SAFER`](set.md#safer)
setting is active.

