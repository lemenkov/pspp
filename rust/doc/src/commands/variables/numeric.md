# NUMERIC

`NUMERIC` explicitly declares new numeric variables, optionally setting
their output formats.

```
NUMERIC VAR_LIST [(FMT_SPEC)] [/VAR_LIST [(FMT_SPEC)]]...
```

   Specify the names of the new numeric variables as `VAR_LIST`.  If
you wish to set the variables' output formats, follow their names by
an [output format](../../language/datasets/formats/index.html) in
parentheses; otherwise, the default is `F8.2`.

   Variables created with `NUMERIC` are initialized to the
system-missing value.

