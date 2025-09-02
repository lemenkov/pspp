# Statistical Functions

Statistical functions compute descriptive statistics on a list of
values.  Some statistics can be computed on numeric or string values;
other can only be computed on numeric values.  Their results have the
same type as their arguments.  The current case's
[weight](../../../commands/weight.md) has no effect on statistical
functions.

   These functions' argument lists may include entire ranges of
variables using the `VAR1 TO VAR2` syntax.

   Unlike most functions, statistical functions can return non-missing
values even when some of their arguments are missing.  Most
statistical functions, by default, require only one non-missing value
to have a non-missing return; `CFVAR`, `SD`, and `VARIANCE` require 2.
These defaults can be increased (but not decreased) by appending a dot
and the minimum number of valid arguments to the function name.  For
example, `MEAN.3(X, Y, Z)` would only return non-missing if all of
`X`, `Y`, and `Z` were valid.

* `CFVAR(NUMBER, NUMBER[, ...])`  
  Results in the coefficient of variation of the values of `NUMBER`.
  (The coefficient of variation is the standard deviation divided by
  the mean.)

* `MAX(VALUE, VALUE[, ...])`  
  Results in the value of the greatest `VALUE`.  The `VALUE`s may be
  numeric or string.

* `MEAN(NUMBER, NUMBER[, ...])`  
  Results in the mean of the values of `NUMBER`.

* `MEDIAN(NUMBER, NUMBER[, ...])`  
  Results in the median of the values of `NUMBER`.  Given an even
  number of nonmissing arguments, yields the mean of the two middle
  values.

* `MIN(NUMBER, NUMBER[, ...])`  
  Results in the value of the least `VALUE`.  The `VALUE`s may be
  numeric or string.

* `SD(NUMBER, NUMBER[, ...])`  
  Results in the standard deviation of the values of `NUMBER`.

* `SUM(NUMBER, NUMBER[, ...])`  
  Results in the sum of the values of `NUMBER`.

* `VARIANCE(NUMBER, NUMBER[, ...])`  
  Results in the variance of the values of `NUMBER`.

