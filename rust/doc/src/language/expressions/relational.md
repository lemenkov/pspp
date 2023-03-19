# Relational Operators

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

