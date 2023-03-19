# Set Membership Functions

Set membership functions determine whether a value is a member of a set.
They take a set of numeric arguments or a set of string arguments, and
produce Boolean results.

   String comparisons are performed according to the rules given for
[Relational Operators](../operators.md#relational-operators).
User-missing string values are treated as valid values.

* `ANY(VALUE, SET [, SET]...)`  
  Returns true if `VALUE` is equal to any of the `SET` values, and false
  otherwise.  For numeric arguments, returns system-missing if `VALUE`
  is system-missing or if all the values in `SET` are system-missing.

* `RANGE(VALUE, LOW, HIGH [, LOW, HIGH]...)`  
  Returns true if `VALUE` is in any of the intervals bounded by `LOW`
  and `HIGH`, inclusive, and false otherwise.  `LOW` and `HIGH` must
  be given in pairs.  Returns system-missing if any `HIGH` is less
  than its `LOW` or, for numeric arguments, if `VALUE` is system-missing
  or if all the `LOW`-`HIGH` pairs contain one (or two) system-missing
  values.  A pair does not match `VALUE` if either `LOW` or `HIGH` is
  missing, even if `VALUE` equals the non-missing endpoint.

