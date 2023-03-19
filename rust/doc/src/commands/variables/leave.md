# LEAVE

`LEAVE` prevents the specified variables from being reinitialized
whenever a new case is processed.

```
LEAVE VAR_LIST.
```

Normally, when a data file is processed, every variable in the active
dataset is initialized to the system-missing value or spaces at the
beginning of processing for each case.  When a variable has been
specified on `LEAVE`, this is not the case.  Instead, that variable is
initialized to 0 (not system-missing) or spaces for the first case.
After that, it retains its value between cases.

This becomes useful for counters.  For instance, in the example below
the variable `SUM` maintains a running total of the values in the
`ITEM` variable.

```
DATA LIST /ITEM 1-3.
COMPUTE SUM=SUM+ITEM.
PRINT /ITEM SUM.
LEAVE SUM
BEGIN DATA.
123
404
555
999
END DATA.
```

Partial output from this example:

```
123   123.00
404   527.00
555  1082.00
999  2081.00
```

It is best to use `LEAVE` command immediately before invoking a
procedure command, because the left status of variables is reset by
certain transformations—for instance, `COMPUTE` and `IF`.  Left status
is also reset by all procedure invocations.

