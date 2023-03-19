# IF

```
  IF CONDITION VARIABLE=EXPRESSION.
or
  IF CONDITION vector(INDEX)=EXPRESSION.
```

The `IF` transformation evaluates a test expression and, if it is
true, assigns the value of a target expression to a target variable.

Specify a boolean-valued test
[expression](../../language/expressions/index.md) to be tested following the
`IF` keyword.  The test expression is evaluated for each case:

- If it is true, then the target expression is evaluated and assigned
  to the specified variable.

- If it is false or missing, nothing is done.

Numeric and string variables may be assigned.  When a string
expression's width differs from the target variable's width, the
string result is truncated or padded with spaces on the right as
necessary.  The expression and variable types must match.

The target variable may be specified as an element of a
[vector](../../commands/variables/vector.md).  In this case, a vector
index expression must be specified in parentheses following the vector
name.  The index expression must evaluate to a numeric value that,
after rounding down to the nearest integer, is a valid index for the
named vector.

Using `IF` to assign to a variable specified on
[`LEAVE`](../../commands/variables/leave.md) resets the variable's
left state.  Therefore, use `LEAVE` after `IF`, not before.

When `IF` follows [`TEMPORARY`](../selection/temporary.md), the
[`LAG`](../../language/expressions/functions/miscellaneous.md) function may not
be used.

