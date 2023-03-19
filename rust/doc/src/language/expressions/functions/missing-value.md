# Missing-Value Functions

Missing-value functions take various numeric arguments and yield various
types of results.  Except where otherwise stated below, the normal rules
of evaluation apply within expression arguments to these functions.  In
particular, user-missing values for numeric variables are converted to
system-missing values.

* `MISSING(EXPR)`  
  When `EXPR` is simply the name of a numeric variable, returns 1 if
  the variable has the system-missing value or if it is user-missing.
  For any other value 0 is returned.  If `EXPR` is any other kind of
  expression, the function returns 1 if the value is system-missing, 0
  otherwise.

* `NMISS(EXPR [, EXPR]...)`  
  Each argument must be a numeric expression.  Returns the number of
  system-missing values in the list, which may include variable ranges
  using the `VAR1 TO VAR2` syntax.

* `NVALID(EXPR [, EXPR]...)`  
  Each argument must be a numeric expression.  Returns the number of
  values in the list that are not system-missing.  The list may
  include variable ranges using the `VAR1 TO VAR2` syntax.

* `SYSMIS(EXPR)`  
  Returns 1 if `EXPR` has the system-missing value, 0 otherwise.

* `VALUE(VARIABLE)`  
  `VALUE(VECTOR(INDEX))`  
  Prevents the user-missing values of the variable or vector element
  from being transformed into system-missing values, and always
  results in its actual value, whether it is valid, user-missing, or
  system-missing.

