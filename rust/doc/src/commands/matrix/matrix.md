# MATRIX…END MATRIX

<!-- toc -->

## Summary

```
MATRIX.
…matrix commands…
END MATRIX.
```

The following basic matrix commands are supported:

```
COMPUTE variable[(index[,index])]=expression.
CALL procedure(argument, …).
PRINT [expression]
      [/FORMAT=format]
      [/TITLE=title]
      [/SPACE={NEWPAGE | n}]
      [{/RLABELS=string… | /RNAMES=expression}]
      [{/CLABELS=string… | /CNAMES=expression}].
```

The following matrix commands offer support for flow control:

```
DO IF expression.
  …matrix commands…
[ELSE IF expression.
  …matrix commands…]…
[ELSE
  …matrix commands…]
END IF.

LOOP [var=first TO last [BY step]] [IF expression].
  …matrix commands…
END LOOP [IF expression].

BREAK.
```

The following matrix commands support matrix input and output:

```
READ variable[(index[,index])]
     [/FILE=file]
     /FIELD=first TO last [BY width]
     [/FORMAT=format]
     [/SIZE=expression]
     [/MODE={RECTANGULAR | SYMMETRIC}]
     [/REREAD].
WRITE expression
      [/OUTFILE=file]
      /FIELD=first TO last [BY width]
      [/MODE={RECTANGULAR | TRIANGULAR}]
      [/HOLD]
      [/FORMAT=format].
GET variable[(index[,index])]
    [/FILE={file | *}]
    [/VARIABLES=variable…]
    [/NAMES=expression]
    [/MISSING={ACCEPT | OMIT | number}]
    [/SYSMIS={OMIT | number}].
SAVE expression
     [/OUTFILE={file | *}]
     [/VARIABLES=variable…]
     [/NAMES=expression]
     [/STRINGS=variable…].
MGET [/FILE=file]
     [/TYPE={COV | CORR | MEAN | STDDEV | N | COUNT}].
MSAVE expression
      /TYPE={COV | CORR | MEAN | STDDEV | N | COUNT}
      [/OUTFILE=file]
      [/VARIABLES=variable…]
      [/SNAMES=variable…]
      [/SPLIT=expression]
      [/FNAMES=variable…]
      [/FACTOR=expression].
```

The following matrix commands provide additional support:

```
DISPLAY [{DICTIONARY | STATUS}].
RELEASE variable….
```

`MATRIX` and `END MATRIX` enclose a special PSPP sub-language, called
the matrix language.  The matrix language does not require an active
dataset to be defined and only a few of the matrix language commands
work with any datasets that are defined.  Each instance of
`MATRIX`…`END MATRIX` is a separate program whose state is independent
of any instance, so that variables declared within a matrix program are
forgotten at its end.

The matrix language works with matrices, where a "matrix" is a
rectangular array of real numbers.  An `N`×`M` matrix has `N` rows and
`M` columns.  Some special cases are important: a `N`×1 matrix is a
"column vector", a 1×`N` is a "row vector", and a 1×1 matrix is a
"scalar".

The matrix language also has limited support for matrices that
contain 8-byte strings instead of numbers.  Strings longer than 8 bytes
are truncated, and shorter strings are padded with spaces.  String
matrices are mainly useful for labeling rows and columns when printing
numerical matrices with the `MATRIX PRINT` command.  Arithmetic
operations on string matrices will not produce useful results.  The user
should not mix strings and numbers within a matrix.

The matrix language does not work with cases.  A variable in the
matrix language represents a single matrix.

The matrix language does not support missing values.

`MATRIX` is a procedure, so it cannot be enclosed inside `DO IF`,
`LOOP`, etc.

Macros defined before a matrix program may be used within a matrix
program, and macros may expand to include entire matrix programs.  The
[`DEFINE`](../../commands/control/define.md) command to define new
macros may not appear within a matrix program.

The following sections describe the details of the matrix language:
first, the syntax of matrix expressions, then each of the supported
commands.  The [`COMMENT`](../utilities/comment.md) command is also
supported.

## Matrix Expressions

Many matrix commands use expressions.  A matrix expression may use the
following operators, listed in descending order of operator precedence.
Within a single level, operators associate from left to right.

- [Function call `()`](#matrix-functions) and [matrix construction `{}`](#matrix-construction-operator-)

- [Indexing `()`](#index-operator-)

- [Unary `+` and `-`](#unary-operators)

- [Integer sequence `:`](#integer-sequence-operator-)

- Matrix [`**`](#matrix-exponentiation-operator-) and elementwise [`&**`](#elementwise-binary-operators) exponentiation.

- Matrix [`*`](#matrix-multiplication-operator-) and elementwise [`&*`](#elementwise-binary-operators) multiplication; [elementwise division `/` and `&/`](#elementwise-binary-operators).

- [Addition `+` and subtraction `-`](#elementwise-binary-operators)

- [Relational `<` `<=` `=` `>=` `>` `<>`](#elementwise-binary-operators)

- [Logical `NOT`](#unary-operators)

- [Logical `AND`](#elementwise-binary-operators)

- [Logical `OR` and `XOR`](#elementwise-binary-operators)

The operators are described in more detail below.  [Matrix
Functions](#matrix-functions) documents matrix functions.

Expressions appear in the matrix language in some contexts where there
would be ambiguity whether `/` is an operator or a separator between
subcommands.  In these contexts, only the operators with higher
precedence than `/` are allowed outside parentheses.  Later sections
call these "restricted expressions".

### Matrix Construction Operator `{}`

Use the `{}` operator to construct matrices.  Within the curly braces,
commas separate elements within a row and semicolons separate rows.  The
following examples show a 2×3 matrix, a 1×4 row vector, a 3×1 column
vector, and a scalar.

```
{1, 2, 3; 4, 5, 6}            ⇒    [1 2 3]
                                   [4 5 6]  
{3.14, 6.28, 9.24, 12.57}     ⇒    [3.14 6.28 9.42 12.57]  
{1.41; 1.73; 2}               ⇒    [1.41]
                                   [1.73]
                                   [2.00]  
{5}                           ⇒    5
```

   Curly braces are not limited to holding numeric literals.  They can
contain calculations, and they can paste together matrices and vectors
in any way as long as the result is rectangular.  For example, if `m` is
matrix `{1, 2; 3, 4}`, `r` is row vector `{5, 6}`, and `c` is column
vector `{7, 8}`, then curly braces can be used as follows:

```
{m, c; r, 10}                 ⇒    [1 2 7]
                                   [3 4 8]
                                   [5 6 10]  
{c, 2 * c, T(r)}              ⇒    [7 14 5]
                                   [8 16 6]
```

   The final example above uses the transposition function `T`.

### Integer Sequence Operator `:`

The syntax `FIRST:LAST:STEP` yields a row vector of consecutive integers
from FIRST to LAST counting by STEP.  The final `:STEP` is optional and
defaults to 1 when omitted.

`FIRST`, `LAST`, and `STEP` must each be a scalar and should be an
integer (any fractional part is discarded).  Because `:` has a high
precedence, operands other than numeric literals must usually be
parenthesized.

When `STEP` is positive (or omitted) and `END < START`, or if `STEP`
is negative and `END > START`, then the result is an empty matrix.  If
`STEP` is 0, then PSPP reports an error.

Here are some examples:

```
1:6                           ⇒    {1, 2, 3, 4, 5, 6}
1:6:2                         ⇒    {1, 3, 5}
-1:-5:-1                      ⇒    {-1, -2, -3, -4, -5}
-1:-5                         ⇒    {}
2:1:0                         ⇒    (error)
```

### Index Operator `()`

The result of the submatrix or indexing operator, written `M(RINDEX,
CINDEX)`, contains the rows of `M` whose indexes are given in vector
`RINDEX` and the columns whose indexes are given in vector `CINDEX`.

   In the simplest case, if `RINDEX` and `CINDEX` are both scalars, the
result is also a scalar:

```
{10, 20; 30, 40}(1, 1)        ⇒    10
{10, 20; 30, 40}(1, 2)        ⇒    20
{10, 20; 30, 40}(2, 1)        ⇒    30
{10, 20; 30, 40}(2, 2)        ⇒    40
```

If the index arguments have multiple elements, then the result
includes multiple rows or columns:

```
{10, 20; 30, 40}(1:2, 1)      ⇒    {10; 30}
{10, 20; 30, 40}(2, 1:2)      ⇒    {30, 40}
{10, 20; 30, 40}(1:2, 1:2)    ⇒    {10, 20; 30, 40}
```

The special argument `:` may stand in for all the rows or columns in
the matrix being indexed, like this:

```
{10, 20; 30, 40}(:, 1)        ⇒    {10; 30}
{10, 20; 30, 40}(2, :)        ⇒    {30, 40}
{10, 20; 30, 40}(:, :)        ⇒    {10, 20; 30, 40}
```

The index arguments do not have to be in order, and they may contain
repeated values, like this:

```
{10, 20; 30, 40}({2, 1}, 1)   ⇒    {30; 10}
{10, 20; 30, 40}(2, {2; 2;    ⇒    {40, 40, 30}
1})
{10, 20; 30, 40}(2:1:-1, :)   ⇒    {30, 40; 10, 20}
```

When the matrix being indexed is a row or column vector, only a
single index argument is needed, like this:

```
{11, 12, 13, 14, 15}(2:4)     ⇒    {12, 13, 14}
{11; 12; 13; 14; 15}(2:4)     ⇒    {12; 13; 14}
```

When an index is not an integer, PSPP discards the fractional part.
It is an error for an index to be less than 1 or greater than the number
of rows or columns:

```
{11, 12, 13, 14}({2.5,        ⇒    {12, 14}
4.6})
{11; 12; 13; 14}(0)           ⇒    (error)
```

### Unary Operators

The unary operators take a single operand of any dimensions and operate
on each of its elements independently.  The unary operators are:

* `-`: Inverts the sign of each element.
* `+`: No change.
* `NOT`: Logical inversion: each positive value becomes 0 and each
  zero or negative value becomes 1.

Examples:

```
-{1, -2; 3, -4}               ⇒    {-1, 2; -3, 4}
+{1, -2; 3, -4}               ⇒    {1, -2; 3, -4}
NOT {1, 0; -1, 1}             ⇒    {0, 1; 1, 0}
```

### Elementwise Binary Operators

The elementwise binary operators require their operands to be matrices
with the same dimensions.  Alternatively, if one operand is a scalar,
then its value is treated as if it were duplicated to the dimensions of
the other operand.  The result is a matrix of the same size as the
operands, in which each element is the result of the applying the
operator to the corresponding elements of the operands.

The elementwise binary operators are listed below.

- The arithmetic operators, for familiar arithmetic operations:

  - `+`: Addition.

  - `-`: Subtraction.

  - `*`: Multiplication, if one operand is a scalar.  (Otherwise this
    is matrix multiplication, described below.)

  - `/` or `&/`: Division.

  - `&*`: Multiplication.

  - `&**`: Exponentiation.

- The relational operators, whose results are 1 when a comparison is
  true and 0 when it is false:

  - `<` or `LT`: Less than.

  - `<=` or `LE`: Less than or equal.

  - `=` or `EQ`: Equal.

  - `>` or `GT`: Greater than.

  - `>=` or `GE`: Greater than or equal.

  - `<>` or `~=` or `NE`: Not equal.

- The logical operators, which treat positive operands as true and
  nonpositive operands as false.  They yield 0 for false and 1 for
  true:

  - `AND`: True if both operands are true.

  - `OR`: True if at least one operand is true.

  - `XOR`: True if exactly one operand is true.

Examples:

```
1 + 2                         ⇒    3
1 + {3; 4}                    ⇒    {4; 5}
{66, 77; 88, 99} + 5          ⇒    {71, 82; 93, 104}
{4, 8; 3, 7} + {1, 0; 5, 2}   ⇒    {5, 8; 8, 9}
{1, 2; 3, 4} < {4, 3; 2, 1}   ⇒    {1, 1; 0, 0}
{1, 3; 2, 4} >= 3             ⇒    {0, 1; 0, 1}
{0, 0; 1, 1} AND {0, 1; 0,    ⇒    {0, 0; 0, 1}
1}
```

### Matrix Multiplication Operator `*`

If `A` is an `M`×`N` matrix and `B` is an `N`×`P` matrix, then `A*B` is the
`M`×`P` matrix multiplication product `C`.  PSPP reports an error if the
number of columns in `A` differs from the number of rows in `B`.

The `*` operator performs elementwise multiplication (see above) if
one of its operands is a scalar.

No built-in operator yields the inverse of matrix multiplication.
Instead, multiply by the result of `INV` or `GINV`.

Some examples:

```
{1, 2, 3} * {4; 5; 6}         ⇒    32
{4; 5; 6} * {1, 2, 3}         ⇒    {4,  8, 12;
                                    5, 10, 15;
                                    6, 12, 18}
```

### Matrix Exponentiation Operator `**`

The result of `A**B` is defined as follows when `A` is a square matrix
and `B` is an integer scalar:

   - For `B > 0`, `A**B` is `A*…*A`, where there are `B` `A`s.  (PSPP
     implements this efficiently for large `B`, using exponentiation by
     squaring.)

   - For `B < 0`, `A**B` is `INV(A**(-B))`.

   - For `B = 0`, `A**B` is the identity matrix.

PSPP reports an error if `A` is not square or `B` is not an integer.

Examples:

```
{2, 5; 1, 4}**3               ⇒    {48, 165; 33, 114}
{2, 5; 1, 4}**0               ⇒    {1, 0; 0, 1}
10*{4, 7; 2, 6}**-1           ⇒    {6, -7; -2, 4}
```

## Matrix Functions

The matrix language support numerous functions in multiple categories.
The following subsections document each of the currently supported
functions.  The first letter of each parameter's name indicate the
required argument type:

* `S`: A scalar.

* `N`: A nonnegative integer scalar.  (Non-integers are accepted and
  silently rounded down to the nearest integer.)

* `V`: A row or column vector.

* `M`: A matrix.

### Elementwise Functions

These functions act on each element of their argument independently,
like the [elementwise operators](#elementwise-binary-operators).

* `ABS(M)`  
     Takes the absolute value of each element of M.

     ```
     ABS({-1, 2; -3, 0}) ⇒ {1, 2; 3, 0}
     ```

* `ARSIN(M)`  
  `ARTAN(M)`  
     Computes the inverse sine or tangent, respectively, of each
     element in M.  The results are in radians, between \\(-\pi/2\\)
     and \\(+\pi/2\\), inclusive.

     The value of \\(\pi\\) can be computed as `4*ARTAN(1)`.

     ```
     ARSIN({-1, 0, 1}) ⇒ {-1.57, 0, 1.57} (approximately)

     ARTAN({-5, -1, 1, 5}) ⇒ {-1.37, -.79, .79, 1.37} (approximately)
     ```

* `COS(M)`  
  `SIN(M)`  
     Computes the cosine or sine, respectively, of each element in `M`,
     which must be in radians.

     ```
     COS({0.785, 1.57; 3.14, 1.57 + 3.14}) ⇒ {.71, 0; -1, 0}
     (approximately)
     ```

* `EXP(M)`  
     Computes \\(e^x\\) for each element \\(x\\) in `M`.

     ```
     EXP({2, 3; 4, 5}) ⇒ {7.39, 20.09; 54.6, 148.4} (approximately)
     ```

* `LG10(M)`  
  `LN(M)`  
     Takes the logarithm with base 10 or base \\(e\\), respectively, of each
     element in `M`.

     ```
     LG10({1, 10, 100, 1000}) ⇒ {0, 1, 2, 3}
     LG10(0) ⇒ (error)

     LN({EXP(1), 1, 2, 3, 4}) ⇒ {1, 0, .69, 1.1, 1.39} (approximately)
     LN(0) ⇒ (error)
     ```

* `MOD(M, S)`  
     Takes each element in `M` modulo nonzero scalar value `S`, that
     is, the remainder of division by `S`.  The sign of the result is
     the same as the sign of the dividend.

     ```
     MOD({5, 4, 3, 2, 1, 0}, 3) ⇒ {2, 1, 0, 2, 1, 0}
     MOD({5, 4, 3, 2, 1, 0}, -3) ⇒ {2, 1, 0, 2, 1, 0}
     MOD({-5, -4, -3, -2, -1, 0}, 3) ⇒ {-2, -1, 0, -2, -1, 0}
     MOD({-5, -4, -3, -2, -1, 0}, -3) ⇒ {-2, -1, 0, -2, -1, 0}
     MOD({5, 4, 3, 2, 1, 0}, 1.5) ⇒ {.5, 1.0, .0, .5, 1.0, .0}
     MOD({5, 4, 3, 2, 1, 0}, 0) ⇒ (error)
     ```

* `RND(M)`  
  `TRUNC(M)`  
     Rounds each element of `M` to an integer.  `RND` rounds to the
     nearest integer, with halves rounded to even integers, and
     `TRUNC` rounds toward zero.

     ```
     RND({-1.6, -1.5, -1.4}) ⇒ {-2, -2, -1}
     RND({-.6, -.5, -.4}) ⇒ {-1, 0, 0}
     RND({.4, .5, .6} ⇒ {0, 0, 1}
     RND({1.4, 1.5, 1.6}) ⇒ {1, 2, 2}

     TRUNC({-1.6, -1.5, -1.4}) ⇒ {-1, -1, -1}
     TRUNC({-.6, -.5, -.4}) ⇒ {0, 0, 0}
     TRUNC({.4, .5, .6} ⇒ {0, 0, 0}
     TRUNC({1.4, 1.5, 1.6}) ⇒ {1, 1, 1}
     ```

* `SQRT(M)`  
     Takes the square root of each element of `M`, which must not be
     negative.

     ```
     SQRT({0, 1, 2, 4, 9, 81}) ⇒ {0, 1, 1.41, 2, 3, 9} (approximately)
     SQRT(-1) ⇒ (error)
     ```

### Logical Functions

* `ALL(M)`  
     Returns a scalar with value 1 if all of the elements in `M` are
     nonzero, or 0 if at least one element is zero.

     ```
     ALL({1, 2, 3} < {2, 3, 4}) ⇒ 1
     ALL({2, 2, 3} < {2, 3, 4}) ⇒ 0
     ALL({2, 3, 3} < {2, 3, 4}) ⇒ 0
     ALL({2, 3, 4} < {2, 3, 4}) ⇒ 0
     ```

* `ANY(M)`  
     Returns a scalar with value 1 if any of the elements in `M` is
     nonzero, or 0 if all of them are zero.

     ```
     ANY({1, 2, 3} < {2, 3, 4}) ⇒ 1
     ANY({2, 2, 3} < {2, 3, 4}) ⇒ 1
     ANY({2, 3, 3} < {2, 3, 4}) ⇒ 1
     ANY({2, 3, 4} < {2, 3, 4}) ⇒ 0
     ```

### Matrix Construction Functions

* `BLOCK(M1, …, MN)`  
     Returns a block diagonal matrix with as many rows as the sum of
     its arguments' row counts and as many columns as the sum of their
     columns.  Each argument matrix is placed along the main diagonal
     of the result, and all other elements are zero.

     ```
     BLOCK({1, 2; 3, 4}, 5, {7; 8; 9}, {10, 11}) ⇒
        1   2   0   0   0   0
        3   4   0   0   0   0
        0   0   5   0   0   0
        0   0   0   7   0   0
        0   0   0   8   0   0
        0   0   0   9   0   0
        0   0   0   0  10  11
     ```

* `IDENT(N)`  
  `IDENT(NR, NC)`  
     Returns an identity matrix, whose main diagonal elements are one
     and whose other elements are zero.  The returned matrix has `N`
     rows and columns or `NR` rows and `NC` columns, respectively.

     ```
     IDENT(1) ⇒ 1
     IDENT(2) ⇒
       1  0
       0  1
     IDENT(3, 5) ⇒
       1  0  0  0  0
       0  1  0  0  0
       0  0  1  0  0
     IDENT(5, 3) ⇒
       1  0  0
       0  1  0
       0  0  1
       0  0  0
       0  0  0
     ```

* `MAGIC(N)`  
     Returns an `N`×`N` matrix that contains each of the integers 1…`N`
     once, in which each column, each row, and each diagonal sums to
     \\(n(n^2+1)/2\\).  There are many magic squares with given dimensions,
     but this function always returns the same one for a given value of
     N.

     ```
     MAGIC(3) ⇒ {8, 1, 6; 3, 5, 7; 4, 9, 2}
     MAGIC(4) ⇒ {1, 5, 12, 16; 15, 11, 6, 2; 14, 8, 9, 3; 4, 10, 7, 13}
     ```

* `MAKE(NR, NC, S)`  
     Returns an `NR`×`NC` matrix whose elements are all `S`.

     ```
     MAKE(1, 2, 3) ⇒ {3, 3}
     MAKE(2, 1, 4) ⇒ {4; 4}
     MAKE(2, 3, 5) ⇒ {5, 5, 5; 5, 5, 5}
     ```

* <a name="mdiag">`MDIAG(V)`</a>  
     Given `N`-element vector `V`, returns a `N`×`N` matrix whose main
     diagonal is copied from `V`.  The other elements in the returned
     vector are zero.

     Use [`CALL SETDIAG`](#setdiag) to replace the main diagonal of a
     matrix in-place.

     ```
     MDIAG({1, 2, 3, 4}) ⇒
       1  0  0  0
       0  2  0  0
       0  0  3  0
       0  0  0  4
     ```

* `RESHAPE(M, NR, NC)`  
     Returns an `NR`×`NC` matrix whose elements come from `M`, which
     must have the same number of elements as the new matrix, copying
     elements from `M` to the new matrix row by row.

     ```
     RESHAPE(1:12, 1, 12) ⇒
        1   2   3   4   5   6   7   8   9  10  11  12
     RESHAPE(1:12, 2, 6) ⇒
        1   2   3   4   5   6
        7   8   9  10  11  12
     RESHAPE(1:12, 3, 4) ⇒
        1   2   3   4
        5   6   7   8
        9  10  11  12
     RESHAPE(1:12, 4, 3) ⇒
        1   2   3
        4   5   6
        7   8   9
       10  11  12
     ```

* `T(M)`  
  `TRANSPOS(M)`  
     Returns `M` with rows exchanged for columns.

     ```
     T({1, 2, 3}) ⇒ {1; 2; 3}
     T({1; 2; 3}) ⇒ {1, 2, 3}
     ```

* `UNIFORM(NR, NC)`  
     Returns a `NR`×`NC` matrix in which each element is randomly
     chosen from a uniform distribution of real numbers between 0
     and 1.  Random number generation honors the current
     [seed](../utilities/set.md#seed) setting.

     The following example shows one possible output, but of course
     every result will be different (given different seeds):

     ```
     UNIFORM(4, 5)*10 ⇒
       7.71  2.99   .21  4.95  6.34
       4.43  7.49  8.32  4.99  5.83
       2.25   .25  1.98  7.09  7.61
       2.66  1.69  2.64   .88  1.50
     ```

### Minimum, Maximum, and Sum Functions

* `CMIN(M)`  
  `CMAX(M)`  
  `CSUM(M)`  
  `CSSQ(M)`  
     Returns a row vector with the same number of columns as `M`, in
     which each element is the minimum, maximum, sum, or sum of
     squares, respectively, of the elements in the same column of `M`.

     ```
     CMIN({1, 2, 3; 4, 5, 6; 7, 8, 9} ⇒ {1, 2, 3}
     CMAX({1, 2, 3; 4, 5, 6; 7, 8, 9} ⇒ {7, 8, 9}
     CSUM({1, 2, 3; 4, 5, 6; 7, 8, 9} ⇒ {12, 15, 18}
     CSSQ({1, 2, 3; 4, 5, 6; 7, 8, 9} ⇒ {66, 93, 126}
     ```

* `MMIN(M)`  
  `MMAX(M)`  
  `MSUM(M)`  
  `MSSQ(M)`  
     Returns the minimum, maximum, sum, or sum of squares, respectively,
     of the elements of `M`.

     ```
     MMIN({1, 2, 3; 4, 5, 6; 7, 8, 9} ⇒ 1
     MMAX({1, 2, 3; 4, 5, 6; 7, 8, 9} ⇒ 9
     MSUM({1, 2, 3; 4, 5, 6; 7, 8, 9} ⇒ 45
     MSSQ({1, 2, 3; 4, 5, 6; 7, 8, 9} ⇒ 285
     ```

* `RMIN(M)`  
  `RMAX(M)`  
  `RSUM(M)`  
  `RSSQ(M)`  
     Returns a column vector with the same number of rows as `M`, in
     which each element is the minimum, maximum, sum, or sum of
     squares, respectively, of the elements in the same row of `M`.

     ```
     RMIN({1, 2, 3; 4, 5, 6; 7, 8, 9} ⇒ {1; 4; 7}
     RMAX({1, 2, 3; 4, 5, 6; 7, 8, 9} ⇒ {3; 6; 9}
     RSUM({1, 2, 3; 4, 5, 6; 7, 8, 9} ⇒ {6; 15; 24}
     RSSQ({1, 2, 3; 4, 5, 6; 7, 8, 9} ⇒ {14; 77; 194}
     ```

* `SSCP(M)`  
     Returns \\({\bf M}^{\bf T} × \bf M\\).

     ```
     SSCP({1, 2, 3; 4, 5, 6}) ⇒ {17, 22, 27; 22, 29, 36; 27, 36, 45}
     ```

* `TRACE(M)`  
     Returns the sum of the elements along `M`'s main diagonal,
     equivalent to `MSUM(DIAG(M))`.

     ```
     TRACE(MDIAG(1:5)) ⇒ 15
     ```

### Matrix Property Functions

* `NROW(M)`  
  `NCOL(M)`  
     Returns the number of row or columns, respectively, in `M`.

     ```
     NROW({1, 0; -2, -3; 3, 3}) ⇒ 3
     NROW(1:5) ⇒ 1

     NCOL({1, 0; -2, -3; 3, 3}) ⇒ 2
     NCOL(1:5) ⇒ 5
     ```

* `DIAG(M)`  
     Returns a column vector containing a copy of M's main diagonal.
     The vector's length is the lesser of `NCOL(M)` and `NROW(M)`.

     ```
     DIAG({1, 0; -2, -3; 3, 3}) ⇒ {1; -3}
     ```

### Matrix Rank Ordering Functions

The `GRADE` and `RANK` functions each take a matrix `M` and return a
matrix `R` with the same dimensions.  Each element in `R` ranges
between 1 and the number of elements `N` in `M`, inclusive.  When the
elements in `M` all have unique values, both of these functions yield
the same results: the smallest element in `M` corresponds to value 1
in R, the next smallest to 2, and so on, up to the largest to `N`.
When multiple elements in `M` have the same value, these functions use
different rules for handling the ties.

* `GRADE(M)`  
     Returns a ranking of `M`, turning duplicate values into sequential
     ranks.  The returned matrix always contains each of the integers 1
     through the number of elements in the matrix exactly once.

     ```
     GRADE({1, 0, 3; 3, 1, 2; 3, 0, 5}) ⇒ {3, 1, 6; 7, 4, 5; 8, 2, 9}
     ```

* `RNKORDER(M)`  
     Returns a ranking of `M`, turning duplicate values into the mean
     of their sequential ranks.

     ```
     RNKORDER({1, 0, 3; 3, 1, 2; 3, 0, 5})
      ⇒ {3.5, 1.5, 7; 7, 3.5, 5; 7, 1.5, 9}
     ```

One may use `GRADE` to sort a vector:

```
COMPUTE v(GRADE(v))=v.   /* Sort v in ascending order.
COMPUTE v(GRADE(-v))=v.  /* Sort v in descending order.
```

### Matrix Algebra Functions

* `CHOL(M)`  
  Matrix `M` must be an `N`×`N` symmetric positive-definite matrix.
  Returns an `N`×`N` matrix `B` such that \\({\bf B}^{\bf T}×{\bf
  B}=\bf M\\).

  ```
  CHOL({4, 12, -16; 12, 37, -43; -16, -43, 98}) ⇒
    2  6 -8
    0  1  5
    0  0  3
  ```

* `DESIGN(M)`  
  Returns a design matrix for `M`.  The design matrix has the same
  number of rows as `M`.  Each column C in `M`, from left to right,
  yields a group of columns in the output.  For each unique value
  `V` in `C`, from top to bottom, add a column to the output in
  which `V` becomes 1 and other values become 0.

  PSPP issues a warning if a column only contains a single unique
  value.

  ```
  DESIGN({1; 2; 3}) ⇒ {1, 0, 0; 0, 1, 0; 0, 0, 1}
  DESIGN({5; 8; 5}) ⇒ {1, 0; 0, 1; 1, 0}
  DESIGN({1, 5; 2, 8; 3, 5})
   ⇒ {1, 0, 0, 1, 0; 0, 1, 0, 0, 1; 0, 0, 1, 1, 0}
  DESIGN({5; 5; 5}) ⇒ (warning)
  ```

* `DET(M)`  
  Returns the determinant of square matrix `M`.

  ```
  DET({3, 7; 1, -4}) ⇒ -19
  ```

* <a name="eval">`EVAL(M)`</a>  
  Returns a column vector containing the eigenvalues of symmetric
  matrix `M`, sorted in ascending order.

  Use [`CALL EIGEN`](#eigen) to compute eigenvalues and eigenvectors
  of a matrix.

  ```
  EVAL({2, 0, 0; 0, 3, 4; 0, 4, 9}) ⇒ {11; 2; 1}
  ```

* `GINV(M)`  
  Returns the `K`×`N` matrix `A` that is the "generalized inverse"
  of `N`×`K` matrix `M`, defined such that \\({\bf M}×{\bf A}×{\bf
  M}={\bf M}\\) and \\({\bf A}×{\bf M}×{\bf A}={\bf A}\\).

  ```
  GINV({1, 2}) ⇒ {.2; .4} (approximately)
  {1:9} * GINV(1:9) * {1:9} ⇒ {1:9} (approximately)
  ```

* `GSCH(M)`  
  `M` must be a `N`×`M` matrix, `M` ≥ `N`, with rank `N`.  Returns
  an `N`×`N` orthonormal basis for `M`, obtained using the
  [Gram-Schmidt
  process](https://en.wikipedia.org/wiki/Gram%E2%80%93Schmidt_process).

  ```
  GSCH({3, 2; 1, 2}) * SQRT(10) ⇒ {3, -1; 1, 3} (approximately)
  ```

* `INV(M)`  
  Returns the `N`×`N` matrix A that is the inverse of `N`×`N` matrix M,
  defined such that \\({\bf M}×{\bf A} = {\bf A}×{\bf M} = {\bf I}\\), where I is the identity matrix.  M
  must not be singular, that is, \\(\det({\bf M}) ≠ 0\\).

  ```
  INV({4, 7; 2, 6}) ⇒ {.6, -.7; -.2, .4} (approximately)
  ```

* `KRONEKER(MA, MB)`  
  Returns the `PM`×`QN` matrix P that is the [Kroneker
  product](https://en.wikipedia.org/wiki/Kronecker_product) of `M`×`N`
  matrix `MA` and `P`×`Q` matrix `MB`.  One may view P as the
  concatenation of multiple `P`×`Q` blocks, each of which is the
  scalar product of `MB` by a different element of `MA`.  For example,
  when `A` is a 2×2 matrix, `KRONEKER(A, B)` is equivalent to
  `{A(1,1)*B, A(1,2)*B; A(2,1)*B, A(2,2)*B}`.

  ```
  KRONEKER({1, 2; 3, 4}, {0, 5; 6, 7}) ⇒
     0   5   0  10
     6   7  12  14
     0  15   0  20
    18  21  24  28
  ```

* `RANK(M)`  
  Returns the rank of matrix `M`, a integer scalar whose value is the
  dimension of the vector space spanned by its columns or,
  equivalently, by its rows.

  ```
  RANK({1, 0, 1; -2, -3, 1; 3, 3, 0}) ⇒ 2
  RANK({1, 1, 0, 2; -1, -1, 0, -2}) ⇒ 1
  RANK({1, -1; 1, -1; 0, 0; 2, -2}) ⇒ 1
  RANK({1, 2, 1; -2, -3, 1; 3, 5, 0}) ⇒ 2
  RANK({1, 0, 2; 2, 1, 0; 3, 2, 1}) ⇒ 3
  ```

* `SOLVE(MA, MB)`  
  MA must be an `N`×`N` matrix, with \\(\det({\bf MA}) ≠ 0\\), and MB an `P`×`Q` matrix.
  Returns an `P`×`Q` matrix X such that \\({\bf MA} × {\bf X} = {\bf MB}\\).

  All of the following examples show approximate results:

  ```
  SOLVE({2, 3; 4, 9}, {6, 2; 15, 5}) ⇒
     1.50    .50
     1.00    .33
  SOLVE({1, 3, -2; 3, 5, 6; 2, 4, 3}, {5; 7; 8}) ⇒
   -15.00
     8.00
     2.00
  SOLVE({2, 1, -1; -3, -1, 2; -2, 1, 2}, {8; -11; -3}) ⇒
     2.00
     3.00
    -1.00
  ```

* <a name="sval">`SVAL(M)`</a>  

  Given `P`×`Q` matrix `M`, returns a \\(\min(N,K)\\)-element column vector
  containing the singular values of `M` in descending order.

  Use [`CALL SVD`](#svd) to compute the full singular value
  decomposition of a matrix.

  ```
  SVAL({1, 1; 0, 0}) ⇒ {1.41; .00}
  SVAL({1, 0, 1; 0, 1, 1; 0, 0, 0}) ⇒ {1.73; 1.00; .00}
  SVAL({2, 4; 1, 3; 0, 0; 0, 0}) ⇒ {5.46; .37}
  ```

* `SWEEP(M, NK)`  
  Given `P`×`Q` matrix `M` and integer scalar \\(k\\) = `NK` such that \\(1 ≤ k ≤
  \min(R,C)\\), returns the `P`×`Q` sweep matrix A.

  If \\({\bf M}_{kk} ≠ 0\\), then:

  $$
  \begin{align}
   A_{kk} &= 1/M_{kk},\\\\
   A_{ik} &= -M_{ik}/M_{kk} \text{ for } i ≠ k,\\\\
   A_{kj} &= M_{kj}/M_{kk} \text{ for } j ≠ k,\\\\
   A_{ij} &= M_{ij} - M_{ik}M_{kj}/M_{kk} \text{ for } i ≠ k \text{ and } j ≠ k.
  \end{align}
  $$

  If \\({\bf M}_{kk}\\) = 0, then:

  $$
  \begin{align}
  A_{ik} &= A_{ki} = 0, \\\\
  A_{ij} &= M_{ij}, \text{ for } i ≠ k \text{ and } j ≠ k.
  \end{align}
  $$

  Given `M = {0, 1, 2; 3, 4, 5; 6, 7, 8}`, then (approximately):

  ```
  SWEEP(M, 1) ⇒
     .00   .00   .00
     .00  4.00  5.00
     .00  7.00  8.00
  SWEEP(M, 2) ⇒
    -.75  -.25   .75
     .75   .25  1.25
     .75 -1.75  -.75
  SWEEP(M, 3) ⇒
   -1.50  -.75  -.25
    -.75  -.38  -.63
     .75   .88   .13
  ```

### Matrix Statistical Distribution Functions

The matrix language can calculate several functions of standard
statistical distributions using the same syntax and semantics as in
PSPP transformation expressions.  See [Statistical Distribution
Functions](../../language/expressions/functions/statistical-distributions.md)
for details.

   The matrix language extends the `PDF`, `CDF`, `SIG`, `IDF`, `NPDF`,
and `NCDF` functions by allowing the first parameters to each of these
functions to be a vector or matrix with any dimensions.  In addition,
`CDF.BVNOR` and `PDF.BVNOR` allow either or both of their first two
parameters to be vectors or matrices; if both are non-scalar then they
must have the same dimensions.  In each case, the result is a matrix
or vector with the same dimensions as the input populated with
elementwise calculations.

### `EOF` Function

This function works with files being used on the `READ` statement.

* `EOF(FILE)`  

  Given a file handle or file name `FILE`, returns an integer scalar 1
  if the last line in the file has been read or 0 if more lines are
  available.  Determining this requires attempting to read another
  line, which means that `REREAD` on the next `READ` command
  following `EOF` on the same file will be ineffective.

The `EOF` function gives a matrix program the flexibility to read a
file with text data without knowing the length of the file in advance.
For example, the following program will read all the lines of data in
`data.txt`, each consisting of three numbers, as rows in matrix `data`:

```
MATRIX.
COMPUTE data={}.
LOOP IF NOT EOF('data.txt').
  READ row/FILE='data.txt'/FIELD=1 TO 1000/SIZE={1,3}.
  COMPUTE data={data; row}.
END LOOP.
PRINT data.
END MATRIX.
```

## `COMPUTE` Command

```
COMPUTE variable[(index[,index])]=expression.
```

   The `COMPUTE` command evaluates an expression and assigns the
result to a variable or a submatrix of a variable.  Assigning to a
submatrix uses the same syntax as the [index
operator](#index-operator-).

## `CALL` Command

A matrix function returns a single result.  The `CALL` command
implements procedures, which take a similar syntactic form to functions
but yield results by modifying their arguments rather than returning a
value.

Output arguments to a `CALL` procedure must be a single variable
name.

The following procedures are implemented via `CALL` to allow them to
return multiple results.  For these procedures, the output arguments
need not name existing variables; if they do, then their previous
values are replaced:

* <a name="eigen">`CALL EIGEN(M, EVEC, EVAL)`</a>

  Computes the eigenvalues and eigenvector of symmetric `N`×`N` matrix `M`.
  Assigns the eigenvectors of `M` to the columns of `N`×`N` matrix EVEC and
  the eigenvalues in descending order to `N`-element column vector
  `EVAL`.

  Use the [`EVAL`](#eval) function to compute just the eigenvalues of
  a symmetric matrix.

  For example, the following matrix language commands:

  ```
  CALL EIGEN({1, 0; 0, 1}, evec, eval).
  PRINT evec.
  PRINT eval.

  CALL EIGEN({3, 2, 4; 2, 0, 2; 4, 2, 3}, evec2, eval2).
  PRINT evec2.
  PRINT eval2.
  ```

  yield this output:

  ```
  evec
    1  0
    0  1

  eval
    1
    1

  evec2
    -.6666666667   .0000000000   .7453559925
    -.3333333333  -.8944271910  -.2981423970
    -.6666666667   .4472135955  -.5962847940

  eval2
    8.0000000000
   -1.0000000000
   -1.0000000000
  ```

* <a name="svd">`CALL SVD(M, U, S, V)`</a>

  Computes the singular value decomposition of `P`×`Q` matrix `M`,
  assigning `S` a `P`×`Q` diagonal matrix and to `U` and `V` unitary `P`×`Q`
  matrices such that M = U×S×V^T. The main diagonal of `Q` contains the
  singular values of `M`.

  Use the [`SVAL`](#sval) function to compute just the singular values
  of a matrix.

  For example, the following matrix program:

  ```
  CALL SVD({3, 2, 2; 2, 3, -2}, u, s, v).
  PRINT (u * s * T(v))/FORMAT F5.1.
  ```

  yields this output:

  ```
  (u * s * T(v))
     3.0   2.0   2.0
     2.0   3.0  -2.0
  ```

The final procedure is implemented via `CALL` to allow it to modify a
matrix instead of returning a modified version.  For this procedure,
the output argument must name an existing variable.

* <a name="setdiag">`CALL SETDIAG(M, V)`</a>

  Replaces the main diagonal of `N`×`P` matrix M by the contents of
  `K`-element vector `V`.  If `K` = 1, so that `V` is a scalar, replaces all
  of the diagonal elements of `M` by `V`.  If K < \min(N,P), only the
  upper K diagonal elements are replaced; if K > \min(N,P), then the
  extra elements of V are ignored.

  Use the [`MDIAG`](#mdiag) function to construct a new matrix with a
  specified main diagonal.

  For example, this matrix program:

  ```
  COMPUTE x={1, 2, 3; 4, 5, 6; 7, 8, 9}.
  CALL SETDIAG(x, 10).
  PRINT x.
  ```

  outputs the following:

  ```
  x
    10   2   3
     4  10   6
     7   8  10
  ```

## `PRINT` Command

```
PRINT [expression]
      [/FORMAT=format]
      [/TITLE=title]
      [/SPACE={NEWPAGE | n}]
      [{/RLABELS=string… | /RNAMES=expression}]
      [{/CLABELS=string… | /CNAMES=expression}].
```

   The `PRINT` command is commonly used to display a matrix.  It
evaluates the restricted EXPRESSION, if present, and outputs it either
as text or a pivot table, depending on the setting of
[`MDISPLAY`](../utilities/set.md#mdisplay).

   Use the `FORMAT` subcommand to specify a format, such as `F8.2`, for
displaying the matrix elements.  `FORMAT` is optional for numerical
matrices.  When it is omitted, PSPP chooses how to format entries
automatically using \\(m\\), the magnitude of the largest-magnitude element in
the matrix to be displayed:

  1. If \\(m < 10^{11}\\) and the matrix's elements are all integers,
     PSPP chooses the narrowest `F` format that fits \\(m\\) plus a
     sign.  For example, if the matrix is `{1:10}`, then \\(m = 10\\),
     which fits in 3 columns with room for a sign, the format is
     `F3.0`.

  2. Otherwise, if \\(m ≥ 10^9\\) or \\(m ≤ 10^{-4}\\), PSPP scales
     all of the numbers in the matrix by \\(10^x\\), where \\(x\\) is
     the exponent that would be used to display \\(m\\) in scientific
     notation.  For example, for \\(m = 5.123×10^{20}\\), the scale
     factor is \\(10^{20}\\).  PSPP displays the scaled values in
     format `F13.10` and notes the scale factor in the output.

  3. Otherwise, PSPP displays the matrix values, without scaling, in
     format `F13.10`.

   The optional `TITLE` subcommand specifies a title for the output text
or table, as a quoted string.  When it is omitted, the syntax of the
matrix expression is used as the title.

   Use the `SPACE` subcommand to request extra space above the matrix
output.  With a numerical argument, it adds the specified number of
lines of blank space above the matrix.  With `NEWPAGE` as an argument,
it prints the matrix at the top of a new page.  The `SPACE` subcommand
has no effect when a matrix is output as a pivot table.

   The `RLABELS` and `RNAMES` subcommands, which are mutually exclusive,
can supply a label to accompany each row in the output.  With `RLABELS`,
specify the labels as comma-separated strings or other tokens.  With
`RNAMES`, specify a single expression that evaluates to a vector of
strings.  Either way, if there are more labels than rows, the extra
labels are ignored, and if there are more rows than labels, the extra
rows are unlabeled.  For output to a pivot table with `RLABELS`, the
labels can be any length; otherwise, the labels are truncated to 8
bytes.

   The `CLABELS` and `CNAMES` subcommands work for labeling columns as
`RLABELS` and `RNAMES` do for labeling rows.

   When the EXPRESSION is omitted, `PRINT` does not output a matrix.
Instead, it outputs only the text specified on `TITLE`, if any, preceded
by any space specified on the `SPACE` subcommand, if any.  Any other
subcommands are ignored, and the command acts as if `MDISPLAY` is set to
`TEXT` regardless of its actual setting.

### Example

   The following syntax demonstrates two different ways to label the
rows and columns of a matrix with `PRINT`:

```
MATRIX.
COMPUTE m={1, 2, 3; 4, 5, 6; 7, 8, 9}.
PRINT m/RLABELS=a, b, c/CLABELS=x, y, z.

COMPUTE rlabels={"a", "b", "c"}.
COMPUTE clabels={"x", "y", "z"}.
PRINT m/RNAMES=rlabels/CNAMES=clabels.
END MATRIX.
```

With `MDISPLAY=TEXT` (the default), this program outputs the following
(twice):

```
m
                x        y        z
a               1        2        3
b               4        5        6
c               7        8        9
```

With `SET MDISPLAY=TABLES.` added above `MATRIX.`, the output becomes
the following (twice):

```
    m
┌─┬─┬─┬─┐
│ │x│y│z│
├─┼─┼─┼─┤
│a│1│2│3│
│b│4│5│6│
│c│7│8│9│
└─┴─┴─┴─┘
```


## `DO IF` Command

```
DO IF expression.
  …matrix commands…
[ELSE IF expression.
  …matrix commands…]…
[ELSE
  …matrix commands…]
END IF.
```

   A `DO IF` command evaluates its expression argument.  If the `DO IF`
expression evaluates to true, then PSPP executes the associated
commands.  Otherwise, PSPP evaluates the expression on each `ELSE IF`
clause (if any) in order, and executes the commands associated with the
first one that yields a true value.  Finally, if the `DO IF` and all the
`ELSE IF` expressions all evaluate to false, PSPP executes the commands
following the `ELSE` clause (if any).

   Each expression on `DO IF` and `ELSE IF` must evaluate to a scalar.
Positive scalars are considered to be true, and scalars that are zero or
negative are considered to be false.

### Example

   The following matrix language fragment sets `b` to the term
following `a` in the [Juggler
sequence](https://en.wikipedia.org/wiki/Juggler_sequence):

```
DO IF MOD(a, 2) = 0.
  COMPUTE b = TRUNC(a &** (1/2)).
ELSE.
  COMPUTE b = TRUNC(a &** (3/2)).
END IF.
```

## `LOOP` and `BREAK` Commands

```
LOOP [var=first TO last [BY step]] [IF expression].
  …matrix commands…
END LOOP [IF expression].

BREAK.
```

   The `LOOP` command executes a nested group of matrix commands,
called the loop's "body", repeatedly.  It has three optional clauses
that control how many times the loop body executes.  Regardless of
these clauses, the global `MXLOOPS` setting, which defaults to 40,
also limits the number of iterations of a loop.  To iterate more
times, raise the maximum with [`SET
MXLOOPS`](../utilities/set.md#mxloops) outside of the `MATRIX`
command.

   The optional index clause causes VAR to be assigned successive
values on each trip through the loop: first `FIRST`, then `FIRST +
STEP`, then `FIRST + 2 × STEP`, and so on.  The loop ends when `VAR >
LAST`, for positive `STEP`, or `VAR < LAST`, for negative `STEP`.  If
`STEP` is not specified, it defaults to 1.  All the index clause
expressions must evaluate to scalars, and non-integers are rounded
toward zero.  If `STEP` evaluates as zero (or rounds to zero), then
the loop body never executes.

   The optional `IF` on `LOOP` is evaluated before each iteration
through the loop body.  If its expression, which must evaluate to a
scalar, is zero or negative, then the loop terminates without executing
the loop body.

   The optional `IF` on `END LOOP` is evaluated after each iteration
through the loop body.  If its expression, which must evaluate to a
scalar, is zero or negative, then the loop terminates.

### Example

   The following computes and prints \\(l(n)\\), whose value is the
number of steps in the [Juggler
sequence](https://en.wikipedia.org/wiki/Juggler_sequence) for \\(n\\),
for \\( 2 \le n \le 10\\):

```
COMPUTE l = {}.
LOOP n = 2 TO 10.
  COMPUTE a = n.
  LOOP i = 1 TO 100.
    DO IF MOD(a, 2) = 0.
      COMPUTE a = TRUNC(a &** (1/2)).
    ELSE.
      COMPUTE a = TRUNC(a &** (3/2)).
    END IF.
  END LOOP IF a = 1.
  COMPUTE l = {l; i}.
END LOOP.
PRINT l.
```

### `BREAK` Command

The `BREAK` command may be used inside a loop body, ordinarily within a
`DO IF` command.  If it is executed, then the loop terminates
immediately, jumping to the command just following `END LOOP`.  When
multiple `LOOP` commands nest, `BREAK` terminates the innermost loop.

#### Example

The following example is a revision of the one above that shows how
`BREAK` could substitute for the index and `IF` clauses on `LOOP` and
`END LOOP`:

```
COMPUTE l = {}.
LOOP n = 2 TO 10.
  COMPUTE a = n.
  COMPUTE i = 1.
  LOOP.
    DO IF MOD(a, 2) = 0.
      COMPUTE a = TRUNC(a &** (1/2)).
    ELSE.
      COMPUTE a = TRUNC(a &** (3/2)).
    END IF.
    DO IF a = 1.
      BREAK.
    END IF.
    COMPUTE i = i + 1.
  END LOOP.
  COMPUTE l = {l; i}.
END LOOP.
PRINT l.
```

## `READ` and `WRITE` Commands

The `READ` and `WRITE` commands perform matrix input and output with
text files.  They share the following syntax for specifying how data is
divided among input lines:

```
/FIELD=first TO last [BY width]
[/FORMAT=format]
```

Both commands require the `FIELD` subcommand.  It specifies the range
of columns, from FIRST to LAST, inclusive, that the data occupies on
each line of the file.  The leftmost column is column 1.  The columns
must be literal numbers, not expressions.  To use entire lines, even if
they might be very long, specify a column range such as `1 TO 100000`.

The `FORMAT` subcommand is optional for numerical matrices.  For
string matrix input and output, specify an `A` format.  In addition to
`FORMAT`, the optional `BY` specification on `FIELD` determine the
meaning of each text line:

- With neither `BY` nor `FORMAT`, the numbers in the text file are in
  `F` format separated by spaces or commas.  For `WRITE`, PSPP uses
  as many digits of precision as needed to accurately represent the
  numbers in the matrix.

- `BY width` divides the input area into fixed-width fields with the
  given width.  The input area must be a multiple of width columns
  wide.  Numbers are read or written as `Fwidth.0` format.

- `FORMAT="countF"` divides the input area into integer count
  equal-width fields per line.  The input area must be a multiple of
  count columns wide.  Another format type may be substituted for
  `F`.

- `FORMAT=Fw[.d]` divides the input area into fixed-width fields
  with width `w`.  The input area must be a multiple of `w` columns
  wide.  Another format type may be substituted for `F`.  The
  `READ` command disregards `d`.

- `FORMAT=F` specifies format `F` without indicating a field width.
  Another format type may be substituted for `F`.  The `WRITE`
  command accepts this form, but it has no effect unless `BY` is also
  used to specify a field width.

If `BY` and `FORMAT` both specify or imply a field width, then they
must indicate the same field width.

### `READ` Command

```
READ variable[(index[,index])]
     [/FILE=file]
     /FIELD=first TO last [BY width]
     [/FORMAT=format]
     [/SIZE=expression]
     [/MODE={RECTANGULAR | SYMMETRIC}]
     [/REREAD].
```

The `READ` command reads from a text file into a matrix variable.
Specify the target variable just after the command name, either just a
variable name to create or replace an entire variable, or a variable
name followed by an indexing expression to replace a submatrix of an
existing variable.

The `FILE` subcommand is required in the first `READ` command that
appears within `MATRIX`.  It specifies the text file to be read,
either as a file name in quotes or a file handle previously declared
on [`FILE HANDLE`](../data-io/file-handle.md).  Later `READ`
commands (in syntax order) use the previous referenced file if `FILE`
is omitted.

The `FIELD` and `FORMAT` subcommands specify how input lines are
interpreted.  `FIELD` is required, but `FORMAT` is optional.  See
[`READ` and `WRITE` Commands](#read-and-write-commands), for details.

The `SIZE` subcommand is required for reading into an entire
variable.  Its restricted expression argument should evaluate to a
2-element vector `{N, M}` or `{N; M}`, which indicates a `N`×`M`
matrix destination.  A scalar `N` is also allowed and indicates a
`N`×1 column vector destination.  When the destination is a submatrix,
`SIZE` is optional, and if it is present then it must match the size
of the submatrix.

By default, or with `MODE=RECTANGULAR`, the command reads an entry
for every row and column.  With `MODE=SYMMETRIC`, the command reads only
the entries on and below the matrix's main diagonal, and copies the
entries above the main diagonal from the corresponding symmetric entries
below it.  Only square matrices may use `MODE=SYMMETRIC`.

Ordinarily, each `READ` command starts from a new line in the text
file.  Specify the `REREAD` subcommand to instead start from the last
line read by the previous `READ` command.  This has no effect for the
first `READ` command to read from a particular file.  It is also
ineffective just after a command that uses the [`EOF` matrix
function](#eof-function) on a particular file, because `EOF` has to
try to read the next line from the file to determine whether the file
contains more input.

#### Example 1: Basic Use

The following matrix program reads the same matrix `{1, 2, 4; 2, 3, 5;
4, 5, 6}` into matrix variables `v`, `w`, and `x`:

```
READ v /FILE='input.txt' /FIELD=1 TO 100 /SIZE={3, 3}.
READ w /FIELD=1 TO 100 /SIZE={3; 3} /MODE=SYMMETRIC.
READ x /FIELD=1 TO 100 BY 1/SIZE={3, 3} /MODE=SYMMETRIC.
```
given that `input.txt` contains the following:

```
1, 2, 4
2, 3, 5
4, 5, 6
1
2 3
4 5 6
1
23
456
```
The `READ` command will read as many lines of input as needed for a
particular row, so it's also acceptable to break any of the lines above
into multiple lines.  For example, the first line `1, 2, 4` could be
written with a line break following either or both commas.

#### Example 2: Reading into a Submatrix

The following reads a 5×5 matrix from `input2.txt`, reversing the order
of the rows:

```
COMPUTE m = MAKE(5, 5, 0).
LOOP r = 5 TO 1 BY -1.
  READ m(r, :) /FILE='input2.txt' /FIELD=1 TO 100.
END LOOP.
```
#### Example 3: Using `REREAD`

Suppose each of the 5 lines in a file `input3.txt` starts with an
integer COUNT followed by COUNT numbers, e.g.:

```
1 5
3 1 2 3
5 6 -1 2 5 1
2 8 9
3 1 3 2
```
Then, the following reads this file into a matrix `m`:

```
COMPUTE m = MAKE(5, 5, 0).
LOOP i = 1 TO 5.
  READ count /FILE='input3.txt' /FIELD=1 TO 1 /SIZE=1.
  READ m(i, 1:count) /FIELD=3 TO 100 /REREAD.
END LOOP.
```
### `WRITE` Command

```
WRITE expression
      [/OUTFILE=file]
      /FIELD=first TO last [BY width]
      [/FORMAT=format]
      [/MODE={RECTANGULAR | TRIANGULAR}]
      [/HOLD].
```
The `WRITE` command evaluates expression and writes its value to a
text file in a specified format.  Write the expression to evaluate just
after the command name.

The `OUTFILE` subcommand is required in the first `WRITE` command that
appears within `MATRIX`.  It specifies the text file to be written,
either as a file name in quotes or a file handle previously declared
on [`FILE HANDLE`](../data-io/file-handle.md).  Later `WRITE` commands
(in syntax order) use the previous referenced file if `FILE` is
omitted.

The `FIELD` and `FORMAT` subcommands specify how output lines are
formed.  `FIELD` is required, but `FORMAT` is optional.  See [`READ`
and `WRITE` Commands](#read-and-write-commands), for details.

By default, or with `MODE=RECTANGULAR`, the command writes an entry
for every row and column.  With `MODE=TRIANGULAR`, the command writes
only the entries on and below the matrix's main diagonal.  Entries above
the diagonal are not written.  Only square matrices may be written with
`MODE=TRIANGULAR`.

Ordinarily, each `WRITE` command writes complete lines to the output
file.  With `HOLD`, the final line written by `WRITE` will be held back
for the next `WRITE` command to augment.  This can be useful to write
more than one matrix on a single output line.

#### Example 1: Basic Usage

This matrix program:

```
WRITE {1, 2; 3, 4} /OUTFILE='matrix.txt' /FIELD=1 TO 80.
```
writes the following to `matrix.txt`:

```
 1 2
 3 4
```
#### Example 2: Triangular Matrix

This matrix program:

```
WRITE MAGIC(5) /OUTFILE='matrix.txt' /FIELD=1 TO 80 BY 5 /MODE=TRIANGULAR.
```
writes the following to `matrix.txt`:

```
    17
    23    5
     4    6   13
    10   12   19   21
    11   18   25    2    9
```
## `GET` Command

```
GET variable[(index[,index])]
    [/FILE={file | *}]
    [/VARIABLES=variable…]
    [/NAMES=variable]
    [/MISSING={ACCEPT | OMIT | number}]
    [/SYSMIS={OMIT | number}].
```
   The `READ` command reads numeric data from an SPSS system file,
SPSS/PC+ system file, or SPSS portable file into a matrix variable or
submatrix:

- To read data into a variable, specify just its name following
  `GET`.  The variable need not already exist; if it does, it is
  replaced.  The variable will have as many columns as there are
  variables specified on the `VARIABLES` subcommand and as many rows
  as there are cases in the input file.

- To read data into a submatrix, specify the name of an existing
  variable, followed by an indexing expression, just after `GET`.
  The submatrix must have as many columns as variables specified on
  `VARIABLES` and as many rows as cases in the input file.

Specify the name or handle of the file to be read on `FILE`.  Use
`*`, or simply omit the `FILE` subcommand, to read from the active file.
Reading from the active file is only permitted if it was already defined
outside `MATRIX`.

List the variables to be read as columns in the matrix on the
`VARIABLES` subcommand.  The list can use `TO` for collections of
variables or `ALL` for all variables.  If `VARIABLES` is omitted, all
variables are read.  Only numeric variables may be read.

If a variable is named on `NAMES`, then the names of the variables
read as data columns are stored in a string vector within the given
name, replacing any existing matrix variable with that name.  Variable
names are truncated to 8 bytes.

The `MISSING` and `SYSMIS` subcommands control the treatment of
missing values in the input file.  By default, any user- or
system-missing data in the variables being read from the input causes an
error that prevents `GET` from executing.  To accept missing values,
specify one of the following settings on `MISSING`:

* `ACCEPT`: Accept user-missing values with no change.

  By default, system-missing values still yield an error.  Use the
  `SYSMIS` subcommand to change this treatment:

  - `OMIT`: Skip any case that contains a system-missing value.

  - `number`: Recode the system-missing value to `number`.

* `OMIT`: Skip any case that contains any user- or system-missing value.

* `number`: Recode all user- and system-missing values to `number`.

The `SYSMIS` subcommand has an effect only with `MISSING=ACCEPT`.

## `SAVE` Command

```
SAVE expression
     [/OUTFILE={file | *}]
     [/VARIABLES=variable…]
     [/NAMES=expression]
     [/STRINGS=variable…].
```
The `SAVE` matrix command evaluates expression and writes the
resulting matrix to an SPSS system file.  In the system file, each
matrix row becomes a case and each column becomes a variable.

Specify the name or handle of the SPSS system file on the `OUTFILE`
subcommand, or `*` to write the output as the new active file.  The
`OUTFILE` subcommand is required on the first `SAVE` command, in syntax
order, within `MATRIX`.  For `SAVE` commands after the first, the
default output file is the same as the previous.

When multiple `SAVE` commands write to one destination within a
single `MATRIX`, the later commands append to the same output file.  All
the matrices written to the file must have the same number of columns.
The `VARIABLES`, `NAMES`, and `STRINGS` subcommands are honored only for
the first `SAVE` command that writes to a given file.

By default, `SAVE` names the variables in the output file `COL1`
through `COLn`.  Use `VARIABLES` or `NAMES` to give the variables
meaningful names.  The `VARIABLES` subcommand accepts a comma-separated
list of variable names.  Its alternative, `NAMES`, instead accepts an
expression that must evaluate to a row or column string vector of names.
The number of names need not exactly match the number of columns in the
matrix to be written: extra names are ignored; extra columns use default
names.

By default, `SAVE` assumes that the matrix to be written is all
numeric.  To write string columns, specify a comma-separated list of the
string columns' variable names on `STRINGS`.

## `MGET` Command

```
MGET [/FILE=file]
     [/TYPE={COV | CORR | MEAN | STDDEV | N | COUNT}].
```
The `MGET` command reads the data from a [matrix file](index.md#matrix-files) into matrix variables.

All of `MGET`'s subcommands are optional.  Specify the name or handle
of the matrix file to be read on the `FILE` subcommand; if it is
omitted, then the command reads the active file.

By default, `MGET` reads all of the data from the matrix file.
Specify a space-delimited list of matrix types on `TYPE` to limit the
kinds of data to the one specified:

* `COV`: Covariance matrix.
* `CORR`: Correlation coefficient matrix.
* `MEAN`: Vector of means.
* `STDDEV`: Vector of standard deviations.
* `N`: Vector of case counts.
* `COUNT`: Vector of counts.

`MGET` reads the entire matrix file and automatically names, creates,
and populates matrix variables using its contents.  It constructs the
name of each variable by concatenating the following:

- A 2-character prefix that identifies the type of the matrix:

  * `CV`: Covariance matrix.
  * `CR`: Correlation coefficient matrix.
  * `MN`: Vector of means.
  * `SD`: Vector of standard deviations.
  * `NC`: Vector of case counts.
  * `CN`: Vector of counts.

- If the matrix file has factor variables, `Fn`, where `n` is a number
  identifying a group of factors: `F1` for the first group, `F2` for
  the second, and so on.  This part is omitted for pooled data (where
  the factors all have the system-missing value).

- If the matrix file has split file variables, `Sn`, where n is a
  number identifying a split group: `S1` for the first group, `S2`
  for the second, and so on.

If `MGET` chooses the name of an existing variable, it issues a
warning and does not change the variable.

## `MSAVE` Command

```
MSAVE expression
      /TYPE={COV | CORR | MEAN | STDDEV | N | COUNT}
      [/FACTOR=expression]
      [/SPLIT=expression]
      [/OUTFILE=file]
      [/VARIABLES=variable…]
      [/SNAMES=variable…]
      [/FNAMES=variable…].
```
The `MSAVE` command evaluates the expression specified just after the
command name, and writes the resulting matrix to a [matrix file](index.md#matrix-files).

The `TYPE` subcommand is required.  It specifies the `ROWTYPE_` to
write along with this matrix.

The `FACTOR` and `SPLIT` subcommands are required on the first
`MSAVE` if and only if the matrix file has factor or split variables,
respectively.  After that, their values are carried along from one
`MSAVE` command to the next in syntax order as defaults.  Each one takes
an expression that must evaluate to a vector with the same number of
entries as the matrix has factor or split variables, respectively.  Each
`MSAVE` only writes data for a single combination of factor and split
variables, so many `MSAVE` commands (or one inside a loop) may be needed
to write a complete set.

The remaining `MSAVE` subcommands define the format of the matrix
file.  All of the `MSAVE` commands within a given matrix program write
to the same matrix file, so these subcommands are only meaningful on the
first `MSAVE` command within a matrix program.  (If they are given again
on later `MSAVE` commands, then they must have the same values as on the
first.)

The `OUTFILE` subcommand specifies the name or handle of the matrix
file to be written.  Output must go to an external file, not a data set
or the active file.

The `VARIABLES` subcommand specifies a comma-separated list of the
names of the continuous variables to be written to the matrix file.  The
`TO` keyword can be used to define variables named with consecutive
integer suffixes.  These names become column names and names that appear
in `VARNAME_` in the matrix file.  `ROWTYPE_` and `VARNAME_` are not
allowed on `VARIABLES`.  If `VARIABLES` is omitted, then PSPP uses the
names `COL1`, `COL2`, and so on.

The `FNAMES` subcommand may be used to supply a comma-separated list
of factor variable names.  The default names are `FAC1`, `FAC2`, and so
on.

The `SNAMES` subcommand can supply a comma-separated list of split
variable names.  The default names are `SPL1`, `SPL2`, and so on.

## `DISPLAY` Command

```
DISPLAY [{DICTIONARY | STATUS}].
```
The `DISPLAY` command makes PSPP display a table with the name and
dimensions of each matrix variable.  The `DICTIONARY` and `STATUS`
keywords are accepted but have no effect.

## `RELEASE` Command

```
RELEASE variable….
```
The `RELEASE` command accepts a comma-separated list of matrix
variable names.  It deletes each variable and releases the memory
associated with it.

The `END MATRIX` command releases all matrix variables.
