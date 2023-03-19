# STRING

`STRING` creates new string variables.

```
STRING VAR_LIST (FMT_SPEC) [/VAR_LIST (FMT_SPEC)] [...].
```

Specify a list of names for the variable you want to create, followed
by the desired [output
format](../../language/datasets/formats/index.html) in parentheses.
Variable widths are implicitly derived from the specified output
formats.  The created variables will be initialized to spaces.

If you want to create several variables with distinct output formats,
you can either use two or more separate `STRING` commands, or you can
specify further variable list and format specification pairs, each
separated from the previous by a slash (`/`).

The following example is one way to create three string variables; Two
of the variables have format `A24` and the other `A80`:

```
STRING firstname lastname (A24) / address (A80).
```

Here is another way to achieve the same result:

```
STRING firstname lastname (A24).
STRING address (A80).
```

... and here is yet another way:

```
STRING firstname (A24).
STRING lastname (A24).
STRING address (A80).
```
