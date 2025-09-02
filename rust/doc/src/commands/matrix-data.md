MATRIX DATA
================

```
MATRIX DATA
        VARIABLES=VARIABLES
        [FILE={'FILE_NAME' | INLINE}
        [/FORMAT=[{LIST | FREE}]
                 [{UPPER | LOWER | FULL}]
                 [{DIAGONAL | NODIAGONAL}]]
        [/SPLIT=SPLIT_VARS]
        [/FACTORS=FACTOR_VARS]
        [/N=N]

The following subcommands are only needed when ROWTYPE_ is not
specified on the VARIABLES subcommand:
        [/CONTENTS={CORR,COUNT,COV,DFE,MAT,MEAN,MSE,
                    N_MATRIX,N|N_VECTOR,N_SCALAR,PROX,SD|STDDEV}]
        [/CELLS=N_CELLS]
```

The `MATRIX DATA` command convert matrices and vectors from text
format into the [matrix file format](matrices.md#matrix-files) for use
by procedures that read matrices.  It reads a text file or inline data
and outputs to the active file, replacing any data already in the
active dataset.  The matrix file may then be used by other commands
directly from the active file, or it may be written to a `.sav` file
using the `SAVE` command.

The text data read by `MATRIX DATA` can be delimited by spaces or
commas.  A plus or minus sign, except immediately following a `d` or
`e`, also begins a new value.  Optionally, values may be enclosed in
single or double quotes.

`MATRIX DATA` can read the types of matrix and vector data supported
in matrix files (see [Row Types](matrices.md#row-types)).

The `FILE` subcommand specifies the source of the command's input.  To
read input from a text file, specify its name in quotes.  To supply
input inline, omit `FILE` or specify `INLINE`.  Inline data must
directly follow `MATRIX DATA`, inside [`BEGIN DATA`](begin-data.md).

`VARIABLES` is the only required subcommand.  It names the variables
present in each input record in the order that they appear.  (`MATRIX
DATA` reorders the variables in the matrix file it produces, if needed
to fit the matrix file format.)  The variable list must include split
variables and factor variables, if they are present in the data, in
addition to the continuous variables that form matrix rows and columns.
It may also include a special variable named `ROWTYPE_`.

Matrix data may include split variables or factor variables or both.
List split variables, if any, on the `SPLIT` subcommand and factor
variables, if any, on the `FACTORS` subcommand.  Split and factor
variables must be numeric.  Split and factor variables must also be
listed on `VARIABLES`, with one exception: if `VARIABLES` does not
include `ROWTYPE_`, then `SPLIT` may name a single variable that is not
in `VARIABLES` (see [Example 8](#example-8-split-variable-with-sequential-values)).

The `FORMAT` subcommand accepts settings to describe the format of
the input data:

* `LIST` (default)  
  `FREE`

  `LIST` requires each row to begin at the start of a new input line.
  `FREE` allows rows to begin in the middle of a line.  Either setting
  allows a single row to continue across multiple input lines.

* `LOWER` (default)  
  `UPPER`  
  `FULL`

  With `LOWER`, only the lower triangle is read from the input data and
  the upper triangle is mirrored across the main diagonal.  `UPPER`
  behaves similarly for the upper triangle.  `FULL` reads the entire
  matrix.

* `DIAGONAL` (default)  
  `NODIAGONAL`

  With `DIAGONAL`, the main diagonal is read from the input data.  With
  `NODIAGONAL`, which is incompatible with `FULL`, the main diagonal is
  not read from the input data but instead set to 1 for correlation
  matrices and system-missing for others.

The `N` subcommand is a way to specify the size of the population.
It is equivalent to specifying an `N` vector with the specified value
for each split file.

`MATRIX DATA` supports two different ways to indicate the kinds of
matrices and vectors present in the data, depending on whether a
variable with the special name `ROWTYPE_` is present in `VARIABLES`.
The following subsections explain `MATRIX DATA` syntax and behavior in
each case.

<!-- toc -->

## With `ROWTYPE_`

If `VARIABLES` includes `ROWTYPE_`, each case's `ROWTYPE_` indicates
the type of data contained in the row.  See [Row
Types](matrices.md#row-types) for a list of supported row types.

### Example 1: Defaults with `ROWTYPE_`

This example shows a simple use of `MATRIX DATA` with `ROWTYPE_` plus 8
variables named `var01` through `var08`.

Because `ROWTYPE_` is the first variable in `VARIABLES`, it appears
first on each line.  The first three lines in the example data have
`ROWTYPE_` values of `MEAN`, `SD`, and `N`.  These indicate that these
lines contain vectors of means, standard deviations, and counts,
respectively, for `var01` through `var08` in order.

The remaining 8 lines have a ROWTYPE_ of `CORR` which indicates that
the values are correlation coefficients.  Each of the lines corresponds
to a row in the correlation matrix: the first line is for `var01`, the
next line for `var02`, and so on.  The input only contains values for
the lower triangle, including the diagonal, since `FORMAT=LOWER
DIAGONAL` is the default.

With `ROWTYPE_`, the `CONTENTS` subcommand is optional and the
`CELLS` subcommand may not be used.

```
MATRIX DATA
    VARIABLES=ROWTYPE_ var01 TO var08.
BEGIN DATA.
MEAN  24.3   5.4  69.7  20.1  13.4   2.7  27.9   3.7
SD     5.7   1.5  23.5   5.8   2.8   4.5   5.4   1.5
N       92    92    92    92    92    92    92    92
CORR  1.00
CORR   .18  1.00
CORR  -.22  -.17  1.00
CORR   .36   .31  -.14  1.00
CORR   .27   .16  -.12   .22  1.00
CORR   .33   .15  -.17   .24   .21  1.00
CORR   .50   .29  -.20   .32   .12   .38  1.00
CORR   .17   .29  -.05   .20   .27   .20   .04  1.00
END DATA.
```

### Example 2: `FORMAT=UPPER NODIAGONAL`

This syntax produces the same matrix file as example 1, but it uses
`FORMAT=UPPER NODIAGONAL` to specify the upper triangle and omit the
diagonal.  Because the matrix's `ROWTYPE_` is `CORR`, PSPP automatically
fills in the diagonal with 1.

```
MATRIX DATA
    VARIABLES=ROWTYPE_ var01 TO var08
    /FORMAT=UPPER NODIAGONAL.
BEGIN DATA.
MEAN  24.3   5.4  69.7  20.1  13.4   2.7  27.9   3.7
SD     5.7   1.5  23.5   5.8   2.8   4.5   5.4   1.5
N       92    92    92    92    92    92    92    92
CORR         .17   .50  -.33   .27   .36  -.22   .18
CORR               .29   .29  -.20   .32   .12   .38
CORR                     .05   .20  -.15   .16   .21
CORR                           .20   .32  -.17   .12
CORR                                 .27   .12  -.24
CORR                                      -.20  -.38
CORR                                             .04
END DATA.
```

### Example 3: `N` subcommand

This syntax uses the `N` subcommand in place of an `N` vector.  It
produces the same matrix file as examples 1 and 2.

```
MATRIX DATA
    VARIABLES=ROWTYPE_ var01 TO var08
    /FORMAT=UPPER NODIAGONAL
    /N 92.
BEGIN DATA.
MEAN  24.3   5.4  69.7  20.1  13.4   2.7  27.9   3.7
SD     5.7   1.5  23.5   5.8   2.8   4.5   5.4   1.5
CORR         .17   .50  -.33   .27   .36  -.22   .18
CORR               .29   .29  -.20   .32   .12   .38
CORR                     .05   .20  -.15   .16   .21
CORR                           .20   .32  -.17   .12
CORR                                 .27   .12  -.24
CORR                                      -.20  -.38
CORR                                             .04
END DATA.
```

### Example 4: Split variables

This syntax defines two matrices, using the variable `s1` to distinguish
between them.  Notice how the order of variables in the input matches
their order on `VARIABLES`.  This example also uses `FORMAT=FULL`.

```
MATRIX DATA
    VARIABLES=s1 ROWTYPE_  var01 TO var04
    /SPLIT=s1
    /FORMAT=FULL.
BEGIN DATA.
0 MEAN 34 35 36 37
0 SD   22 11 55 66
0 N    99 98 99 92
0 CORR  1 .9 .8 .7
0 CORR .9  1 .6 .5
0 CORR .8 .6  1 .4
0 CORR .7 .5 .4  1
1 MEAN 44 45 34 39
1 SD   23 15 51 46
1 N    98 34 87 23
1 CORR  1 .2 .3 .4
1 CORR .2  1 .5 .6
1 CORR .3 .5  1 .7
1 CORR .4 .6 .7  1
END DATA.
```

### Example 5: Factor variables

This syntax defines a matrix file that includes a factor variable `f1`.
The data includes mean, standard deviation, and count vectors for two
values of the factor variable, plus a correlation matrix for pooled
data.

```
MATRIX DATA
    VARIABLES=ROWTYPE_ f1 var01 TO var04
    /FACTOR=f1.
BEGIN DATA.
MEAN 0 34 35 36 37
SD   0 22 11 55 66
N    0 99 98 99 92
MEAN 1 44 45 34 39
SD   1 23 15 51 46
N    1 98 34 87 23
CORR .  1
CORR . .9  1
CORR . .8 .6  1
CORR . .7 .5 .4  1
END DATA.
```

## Without `ROWTYPE_`

If `VARIABLES` does not contain `ROWTYPE_`, the `CONTENTS` subcommand
defines the row types that appear in the file and their order.  If
`CONTENTS` is omitted, `CONTENTS=CORR` is assumed.

Factor variables without `ROWTYPE_` introduce special requirements,
illustrated below in Examples 8 and 9.

### Example 6: Defaults without `ROWTYPE_`

This example shows a simple use of `MATRIX DATA` with 8 variables named
`var01` through `var08`, without `ROWTYPE_`.  This yields the same
matrix file as [Example 1](#example-1-defaults-with-rowtype_).

```
MATRIX DATA
    VARIABLES=var01 TO var08
   /CONTENTS=MEAN SD N CORR.
BEGIN DATA.
24.3   5.4  69.7  20.1  13.4   2.7  27.9   3.7
 5.7   1.5  23.5   5.8   2.8   4.5   5.4   1.5
  92    92    92    92    92    92    92    92
1.00
 .18  1.00
-.22  -.17  1.00
 .36   .31  -.14  1.00
 .27   .16  -.12   .22  1.00
 .33   .15  -.17   .24   .21  1.00
 .50   .29  -.20   .32   .12   .38  1.00
 .17   .29  -.05   .20   .27   .20   .04  1.00
END DATA.
```

### Example 7: Split variables with explicit values

This syntax defines two matrices, using the variable `s1` to distinguish
between them.  Each line of data begins with `s1`.  This yields the same
matrix file as [Example 4](#example-4-split-variables).

```
MATRIX DATA
    VARIABLES=s1 var01 TO var04
    /SPLIT=s1
    /FORMAT=FULL
    /CONTENTS=MEAN SD N CORR.
BEGIN DATA.
0 34 35 36 37
0 22 11 55 66
0 99 98 99 92
0  1 .9 .8 .7
0 .9  1 .6 .5
0 .8 .6  1 .4
0 .7 .5 .4  1
1 44 45 34 39
1 23 15 51 46
1 98 34 87 23
1  1 .2 .3 .4
1 .2  1 .5 .6
1 .3 .5  1 .7
1 .4 .6 .7  1
END DATA.
```

### Example 8: Split variable with sequential values

Like this previous example, this syntax defines two matrices with split
variable `s1`.  In this case, though, `s1` is not listed in `VARIABLES`,
which means that its value does not appear in the data.  Instead,
`MATRIX DATA` reads matrix data until the input is exhausted, supplying
1 for the first split, 2 for the second, and so on.

```
MATRIX DATA
    VARIABLES=var01 TO var04
    /SPLIT=s1
    /FORMAT=FULL
    /CONTENTS=MEAN SD N CORR.
BEGIN DATA.
34 35 36 37
22 11 55 66
99 98 99 92
 1 .9 .8 .7
.9  1 .6 .5
.8 .6  1 .4
.7 .5 .4  1
44 45 34 39
23 15 51 46
98 34 87 23
 1 .2 .3 .4
.2  1 .5 .6
.3 .5  1 .7
.4 .6 .7  1
END DATA.
```

### Factor variables without `ROWTYPE_`

Without `ROWTYPE_`, factor variables introduce two new wrinkles to
`MATRIX DATA` syntax.  First, the `CELLS` subcommand must declare the
number of combinations of factor variables present in the data.  If
there is, for example, one factor variable for which the data contains
three values, one would write `CELLS=3`; if there are two (or more)
factor variables for which the data contains five combinations, one
would use `CELLS=5`; and so on.

Second, the `CONTENTS` subcommand must distinguish within-cell data
from pooled data by enclosing within-cell row types in parentheses.
When different within-cell row types for a single factor appear in
subsequent lines, enclose the row types in a single set of parentheses;
when different factors' values for a given within-cell row type appear
in subsequent lines, enclose each row type in individual parentheses.

Without `ROWTYPE_`, input lines for pooled data do not include factor
values, not even as missing values, but input lines for within-cell data
do.

The following examples aim to clarify this syntax.

#### Example 9: Factor variables, grouping within-cell records by factor

This syntax defines the same matrix file as [Example
5](#example-5-factor-variables), without using `ROWTYPE_`.  It
declares `CELLS=2` because the data contains two values (0 and 1) for
factor variable `f1`.  Within-cell vector row types `MEAN`, `SD`, and
`N` are in a single set of parentheses on `CONTENTS` because they are
grouped together in subsequent lines for a single factor value.  The
data lines with the pooled correlation matrix do not have any factor
values.

```
MATRIX DATA
    VARIABLES=f1 var01 TO var04
    /FACTOR=f1
    /CELLS=2
    /CONTENTS=(MEAN SD N) CORR.
BEGIN DATA.
0 34 35 36 37
0 22 11 55 66
0 99 98 99 92
1 44 45 34 39
1 23 15 51 46
1 98 34 87 23
   1
  .9  1
  .8 .6  1
  .7 .5 .4  1
END DATA.
```

#### Example 10: Factor variables, grouping within-cell records by row type

This syntax defines the same matrix file as the previous example.  The
only difference is that the within-cell vector rows are grouped
differently: two rows of means (one for each factor), followed by two
rows of standard deviations, followed by two rows of counts.

```
MATRIX DATA
    VARIABLES=f1 var01 TO var04
    /FACTOR=f1
    /CELLS=2
    /CONTENTS=(MEAN) (SD) (N) CORR.
BEGIN DATA.
0 34 35 36 37
1 44 45 34 39
0 22 11 55 66
1 23 15 51 46
0 99 98 99 92
1 98 34 87 23
   1
  .9  1
  .8 .6  1
  .7 .5 .4  1
END DATA.
```
