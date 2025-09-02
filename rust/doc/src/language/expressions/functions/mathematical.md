# Mathematical Functions

Mathematical functions take numeric arguments and produce numeric
results.

* `ABS(X)`  
  Results in the absolute value of `X`.

* `EXP(EXPONENT)`  
  Returns *e* (approximately 2.71828) raised to power `EXPONENT`.

* `LG10(X)`  
  Takes the base-10 logarithm of `X`.  If `X` is not positive, the
  result is system-missing.

* `LN(X)`  
  Takes the base-*e* logarithm of `X`.  If `X` is not positive, the
  result is system-missing.

* `LNGAMMA(X)`  
  Yields the base-*e* logarithm of the complete gamma of `X`.  If `X` is
  a negative integer, the result is system-missing.

* `MOD(A, B)`  
  Returns the remainder (modulus) of `A` divided by `B`.  If `A` is 0,
  then the result is 0, even if `B` is missing.  If `B` is 0, the
  result is system-missing.

* `MOD10(X)`  
  Returns the remainder when `X` is divided by 10.  If `X` is
  negative, `MOD10(X)` is negative or zero.

* <a name="rnd">`RND(X [, MULT[, FUZZBITS]])`</a>  
  Rounds `X` and rounds it to a multiple of `MULT` (by default 1).
  Halves are rounded away from zero, as are values that fall short of
  halves by less than `FUZZBITS` of errors in the least-significant
  bits of X.  If `FUZZBITS` is not specified then the default is taken
  from [`SET FUZZBITS`](../../../commands/set.md#fuzzbits), which is 6
  unless overridden.

* `SQRT(X)`  
  Takes the square root of `X`.  If `X` is negative, the result is
  system-missing.

* `TRUNC(X [, MULT[, FUZZBITS]])`  
  Rounds `X` to a multiple of `MULT`, toward zero.  For the default
  `MULT` of 1, this is equivalent to discarding the fractional part of
  `X`.  Values that fall short of a multiple of `MULT` by less than
  `FUZZBITS` of errors in the least-significant bits of `X` are
  rounded away from zero.  If `FUZZBITS` is not specified then the
  default is taken from [`SET
  FUZZBITS`](../../../commands/set.md#fuzzbits), which is 6 unless
  overridden.

