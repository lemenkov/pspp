# MISSING VALUES

In many situations, the data available for analysis is incomplete, so
that a placeholder must be used to indicate that the value is unknown.
One way that missing values are represented, for numeric data, is the
["system-missing value"](../../language/basics/missing-values.html).
Another, more flexible way is through "user-missing values" which are
determined on a per variable basis.

The `MISSING VALUES` command sets user-missing values for variables.

```
MISSING VALUES VAR_LIST (MISSING_VALUES).

where MISSING_VALUES takes one of the following forms:
        NUM1
        NUM1, NUM2
        NUM1, NUM2, NUM3
        NUM1 THRU NUM2
        NUM1 THRU NUM2, NUM3
        STRING1
        STRING1, STRING2
        STRING1, STRING2, STRING3
As part of a range, `LO` or `LOWEST` may take the place of NUM1;
`HI` or `HIGHEST` may take the place of NUM2.
```

`MISSING VALUES` sets user-missing values for numeric and string
variables.  Long string variables may have missing values, but
characters after the first 8 bytes of the missing value must be
spaces.

Specify a list of variables, followed by a list of their user-missing
values in parentheses.  Up to three discrete values may be given, or,
for numeric variables only, a range of values optionally accompanied
by a single discrete value.  Ranges may be open-ended on one end,
indicated through the use of the keyword `LO` or `LOWEST` or `HI` or
`HIGHEST`.

The `MISSING VALUES` command takes effect immediately.  It is not
affected by conditional and looping constructs such as `DO IF` or
`LOOP`.

