# DELETE VARIABLES

`DELETE VARIABLES` deletes the specified variables from the dictionary.

```
DELETE VARIABLES VAR_LIST.
```

`DELETE VARIABLES` should not be used after defining transformations
but before executing a procedure.  If it is anyhow, it causes the data
to be read.  If it is used while `TEMPORARY` is in effect, it causes
the temporary transformations to become permanent.

`DELETE VARIABLES` may not be used to delete all variables from the
dictionary; use [`NEW FILE`](../../commands/data-io/new-file.html)
instead.

