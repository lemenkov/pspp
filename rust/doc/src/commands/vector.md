# VECTOR

```
Two possible syntaxes:
        VECTOR VEC_NAME=VAR_LIST.
        VECTOR VEC_NAME_LIST(COUNT [FORMAT]).
```

`VECTOR` allows a group of variables to be accessed as if they were
consecutive members of an array with a `vector(index)` notation.

To make a vector out of a set of existing variables, specify a name
for the vector followed by an equals sign (`=`) and the variables to
put in the vector.  The variables must be all numeric or all string,
and string variables must have the same width.

To make a vector and create variables at the same time, specify one or
more vector names followed by a count in parentheses.  This will
create variables named `VEC1` through `VEC<count>`.  By default, the
new variables are numeric with format `F8.2`, but an alternate format
may be specified inside the parentheses before or after the count and
separated from it by white space or a comma.  With a string format
such as `A8`, the variables will be string variables; with a numeric
format, they will be numeric.  Variable names including the suffixes
may not exceed 64 characters in length, and none of the variables may
exist prior to `VECTOR`.

Vectors created with `VECTOR` disappear after any procedure or
procedure-like command is executed.  The variables contained in the
vectors remain, unless they are [scratch
variables](../language/datasets/scratch-variables.md).
variables](../language/datasets/scratch-variables.md).

Variables within a vector may be referenced in expressions using
`vector(index)` syntax.

