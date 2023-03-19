# Operators

Every operator takes one or more operands as input and yields exactly
one result as output.  Depending on the operator, operands accept
strings or numbers as operands.  With few exceptions, operands may be
full-fledged expressions in themselves.

## Grouping Operators

Parentheses (`()`) are the grouping operators.  Surround an expression
with parentheses to force early evaluation.

   Parentheses also surround the arguments to functions, but in that
situation they act as punctuators, not as operators.

## Arithmetic Operators

The arithmetic operators take numeric operands and produce numeric
results.

* `A + B`  
  `A - B`  
  Addition and subtraction.

* `A * B`  
  Multiplication.  If either `A` or `B` is 0, then the result is 0,
  even if the other operand is missing.

* `A / B`  
  Division.  If `A` is 0, then the result is 0, even if `B` is
  missing.  If `B` is zero, the result is system-missing.

* `A ** B`  
  `A` raised to the power `B`.  If `A` is negative and `B` is not an
  integer, the result is system-missing.  `0**0` is also
  system-missing.

* `-A`  
  Reverses the sign of `A`.

## Logical Operators

The logical operators take logical operands and produce logical
results, meaning "true or false."  Logical operators are not true
Boolean operators because they may also result in a system-missing
value.  See [Boolean Values](#boolean-values), above, for more
information.

* `A AND B`  
  `A & B`  
  True if both `A` and `B` are true, false otherwise.  If one operand
  is false, the result is false even if the other is missing.  If both
  operands are missing, the result is missing.

* `A OR B`  
  `A | B`  
  True if at least one of `A` and `B` is true.  If one operand is
  true, the result is true even if the other operand is missing.  If
  both operands are missing, the result is missing.

* `NOT A`  
  `~A`  
  True if `A` is false.  If the operand is missing, then the result is
  missing.

The overall truth table for the binary logical operators is:

|`A`|`B`|`A AND B`|`A OR B`|
|-|-|-|-|
|false|false|false|false|
|false|true|false|true|
|true|false|false|true|
|true|true|true|true|
|false|missing|false|missing|
|true|missing|missing|true|
|missing|false|false|missing|
|missing|true|missing|true|
|missing|missing|missing|missing|

## Relational Operators

The relational operators take numeric or string operands and produce
Boolean results.

   Strings cannot be compared to numbers.  When strings of different
lengths are compared, the shorter string is right-padded with spaces to
match the length of the longer string.

   The results of string comparisons, other than tests for equality or
inequality, depend on the character set in use.  String comparisons are
case-sensitive.

* `A EQ B`  
  `A = B`  
  True if `A` is equal to `B`.

* `A LE B`  
  `A <= B`  
  True if `A` is less than or equal to `B`.

* `A LT B`  
  `A < B`  
  True if `A` is less than `B`.

* `A GE B`  
  `A >= B`  
  True if `A` is greater than or equal to `B`.

* `A GT B`  
  `A > B`  
  True if `A` is greater than `B`.

* `A NE B`  
  `A ~= B`  
  `A <> B`  
  True if `A` is not equal to `B`.

