# FILTER

```
FILTER BY VAR_NAME.
FILTER OFF.
```

`FILTER` allows a boolean-valued variable to be used to select cases
from the data stream for processing.

To set up filtering, specify `BY` and a variable name.  Keyword `BY` is
optional but recommended.  Cases which have a zero or system- or
user-missing value are excluded from analysis, but not deleted from
the data stream.  Cases with other values are analyzed.  To filter
based on a different condition, use transformations such as `COMPUTE`
or `RECODE` to compute a filter variable of the required form, then
specify that variable on `FILTER`.

`FILTER OFF` turns off case filtering.

Filtering takes place immediately before cases pass to a procedure for
analysis.  Only one filter variable may be active at a time.
Normally, case filtering continues until it is explicitly turned off
with `FILTER OFF`.  However, if `FILTER` is placed after `TEMPORARY`,
it filters only the next procedure or procedure-like command.

