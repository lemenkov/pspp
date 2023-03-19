# Mathematical Expressions

Expressions share a common syntax each place they appear in PSPP
commands.  Expressions are made up of "operands", which can be
numbers, strings, variable names, or invocations of functions,
separated by "operators".

## Boolean Values

Some PSPP operators and expressions work with Boolean values, which
represent true/false conditions.  Booleans have only three possible
values: 0 (false), 1 (true), and system-missing (unknown).
System-missing is neither true nor false and indicates that the true
value is unknown.

   Boolean-typed operands or function arguments must take on one of
these three values.  Other values are considered false, but provoke a
warning when the expression is evaluated.

   Strings and Booleans are not compatible, and neither may be used in
place of the other.

## Missing Values

Most numeric operators yield system-missing when given any
system-missing operand.  A string operator given any system-missing
operand typically results in the empty string.  Exceptions are listed
under particular operator descriptions.

   String user-missing values are not treated specially in expressions.

   User-missing values for numeric variables are always transformed into
the system-missing value, except inside the arguments to the `VALUE` and
`SYSMIS` functions.

   The [missing-value functions](functions/missing-value.md) can be
used to precisely control how missing values are treated in
expressions.

## Order of Operations

The following table describes operator precedence.  Smaller-numbered
levels in the table have higher precedence.  Within a level,
operations are always performed from left to right.

1. `()`
2. `**`
3. Unary `+` and `-`
4. `* /`
5. Binary `+` and `-`
6. `= >= > <= < <>`
7. `NOT`
8. `AND`
9. `OR`
