# Miscellaneous Functions

* `LAG(VARIABLE[, N])`  
  `VARIABLE` must be a numeric or string variable name.  `LAG` yields
  the value of that variable for the case `N` before the current one.
  Results in system-missing (for numeric variables) or blanks (for
  string variables) for the first `N` cases.

  `LAG` obtains values from the cases that become the new active
  dataset after a procedure executes.  Thus, `LAG` will not return
  values from cases dropped by transformations such as `SELECT IF`,
  and transformations like `COMPUTE` that modify data will change the
  values returned by `LAG`.  These are both the case whether these
  transformations precede or follow the use of `LAG`.

  If `LAG` is used before `TEMPORARY`, then the values it returns are
  those in cases just before `TEMPORARY`.  `LAG` may not be used
  after `TEMPORARY`.

  If omitted, `N` defaults to 1.  Otherwise, `N` must be a small
  positive constant integer.  There is no explicit limit, but use of a
  large value will increase memory consumption.

* `YRMODA(YEAR, MONTH, DAY)`  
  YEAR is a year, either between 0 and 99 or at least 1582.  Unlike
  other PSPP date functions, years between 0 and 99 always correspond
  to 1900 through 1999.  `MONTH` is a month between 1 and 13.  `DAY`
  is a day between 0 and 31.  A `DAY` of 0 refers to the last day of
  the previous month, and a `MONTH` of 13 refers to the first month of
  the next year.  `YEAR` must be in range.  `YEAR`, `MONTH`, and `DAY`
  must all be integers.

  `YRMODA` results in the number of days between 15 Oct 1582 and the
  date specified, plus one.  The date passed to `YRMODA` must be on
  or after 15 Oct 1582.  15 Oct 1582 has a value of 1.

* `VALUELABEL(VARIABLE)`  
  Returns a string matching the label associated with the current
  value of `VARIABLE`.  If the current value of `VARIABLE` has no
  associated label, then this function returns the empty string.
  `VARIABLE` may be a numeric or string variable.

