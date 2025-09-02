# SAMPLE

```
SAMPLE NUM1 [FROM NUM2].
```

`SAMPLE` randomly samples a proportion of the cases in the active
file.  Unless it follows `TEMPORARY`, it permanently removes cases
from the active dataset.

The proportion to sample may be expressed as a single number between 0
and 1.  If `N` is the number of currently-selected cases in the active
dataset, then `SAMPLE K.` will select approximately `K×N` cases.

The proportion to sample can also be specified in the style `SAMPLE M
FROM N`.  With this style, cases are selected as follows:

1. If `N` is the number of currently-selected cases in the active
   dataset, exactly `M` cases are selected.

2. If `N` is greater than the number of currently-selected cases in
   the active dataset, an equivalent proportion of cases are selected.

3. If `N` is less than the number of currently-selected cases in the
   active, exactly `M` cases are selected *from the first `N` cases*
   in the active dataset.

`SAMPLE` and `SELECT IF` are performed in the order specified by the
syntax file.

`SAMPLE` is always performed before [`N OF CASES`](n.md), regardless
of ordering in the syntax file.

The same values for `SAMPLE` may result in different samples.  To
obtain the same sample, use the `SET` command to set the random number
seed to the same value before each `SAMPLE`.  Different samples may
still result when the file is processed on systems with different
machine types or PSPP versions.  By default, the random number seed is
based on the system time.

