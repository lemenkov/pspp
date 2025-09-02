# RENAME VARIABLES

`RENAME VARIABLES` changes the names of variables in the active dataset.

```
RENAME VARIABLES (OLD_NAMES=NEW_NAMES)... .
```

Specify lists of the old variable names and new variable names,
separated by an equals sign (`=`), within parentheses.  There must be
the same number of old and new variable names.  Each old variable is
renamed to the corresponding new variable name.  Multiple
parenthesized groups of variables may be specified.  When the old and
new variable names contain only a single variable name, the
parentheses are optional.

`RENAME VARIABLES` takes effect immediately.  It does not cause the
data to be read.

`RENAME VARIABLES` may not be specified following
[`TEMPORARY`](temporary.md).

