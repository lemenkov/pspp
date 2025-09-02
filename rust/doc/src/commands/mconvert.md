# MCONVERT

```
MCONVERT
    [[MATRIX=]
     [IN({‘*’|'FILE'})]
     [OUT({‘*’|'FILE'})]]
    [/{REPLACE,APPEND}].
```

The `MCONVERT` command converts matrix data from a correlation matrix
and a vector of standard deviations into a covariance matrix, or vice
versa.

By default, `MCONVERT` both reads and writes the active file.  Use
the `MATRIX` subcommand to specify other files.  To read a matrix file,
specify its name inside parentheses following `IN`.  To write a matrix
file, specify its name inside parentheses following `OUT`.  Use `*` to
explicitly specify the active file for input or output.

When `MCONVERT` reads the input, by default it substitutes a
correlation matrix and a vector of standard deviations each time it
encounters a covariance matrix, and vice versa.  Specify `/APPEND` to
instead have `MCONVERT` add the other form of data without removing the
existing data.  Use `/REPLACE` to explicitly request removing the
existing data.

The `MCONVERT` command requires its input to be a matrix file.  Use
[`MATRIX DATA`](matrix-data.md) to convert text input into matrix file
format.

