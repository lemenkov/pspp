# FLIP

```
FLIP /VARIABLES=VAR_LIST /NEWNAMES=VAR_NAME.
```

`FLIP` transposes rows and columns in the active dataset.  It causes
cases to be swapped with variables, and vice versa.

All variables in the transposed active dataset are numeric.  String
variables take on the system-missing value in the transposed file.

`N` subcommands are required.  If specified, the `VARIABLES`
subcommand selects variables to be transformed into cases, and variables
not specified are discarded.  If the `VARIABLES` subcommand is omitted,
all variables are selected for transposition.

The variables specified by `NEWNAMES`, which must be a string
variable, is used to give names to the variables created by `FLIP`.
Only the first 8 characters of the variable are used.  If `NEWNAMES`
is not specified then the default is a variable named CASE_LBL, if it
exists.  If it does not then the variables created by `FLIP` are named
`VAR000` through `VAR999`, then `VAR1000`, `VAR1001`, and so on.

When a `NEWNAMES` variable is available, the names must be
canonicalized before becoming variable names.  Invalid characters are
replaced by letter `V` in the first position, or by `_` in subsequent
positions.  If the name thus generated is not unique, then numeric
extensions are added, starting with 1, until a unique name is found or
there are no remaining possibilities.  If the latter occurs then the
`FLIP` operation aborts.

The resultant dictionary contains a `CASE_LBL` variable, a string
variable of width 8, which stores the names of the variables in the
dictionary before the transposition.  Variables names longer than 8
characters are truncated.  If `FLIP` is called again on this dataset,
the `CASE_LBL` variable can be passed to the `NEWNAMES` subcommand to
recreate the original variable names.

`FLIP` honors [`N OF CASES`](../selection/n.md).  It ignores
[`TEMPORARY`](../selection/temporary.md), so that "temporary"
transformations become permanent.

## Example

In the syntax below, data has been entered using [`DATA
LIST`](../../commands/data-io/data-list.md) such that the first
variable in the dataset is a string variable containing a description
of the other data for the case.  Clearly this is not a convenient
arrangement for performing statistical analyses, so it would have been
better to think a little more carefully about how the data should have
been arranged.  However often the data is provided by some third party
source, and you have no control over the form.  Fortunately, we can
use `FLIP` to exchange the variables and cases in the active dataset.

```
data list notable list /heading (a16) v1 v2 v3 v4 v5 v6
begin data.
date-of-birth 1970 1989 2001 1966 1976 1982
sex 1 0 0 1 0 1
score 10 10 9 3 8 9
end data.

echo 'Before FLIP:'.
display variables.
list.

flip /variables = all /newnames = heading.

echo 'After FLIP:'.
display variables.
list.
```

As you can see in the results below, before the `FLIP` command has run
there are seven variables (six containing data and one for the
heading) and three cases.  Afterwards there are four variables (one
per case, plus the CASE_LBL variable) and six cases.  You can delete
the CASE_LBL variable (see [DELETE
VARIABLES](../../commands/variables/delete-variables.md)) if you don't
need it.

```
Before FLIP:

                  Variables
┌───────┬────────┬────────────┬────────────┐
│Name   │Position│Print Format│Write Format│
├───────┼────────┼────────────┼────────────┤
│heading│       1│A16         │A16         │
│v1     │       2│F8.2        │F8.2        │
│v2     │       3│F8.2        │F8.2        │
│v3     │       4│F8.2        │F8.2        │
│v4     │       5│F8.2        │F8.2        │
│v5     │       6│F8.2        │F8.2        │
│v6     │       7│F8.2        │F8.2        │
└───────┴────────┴────────────┴────────────┘

                           Data List
┌─────────────┬───────┬───────┬───────┬───────┬───────┬───────┐
│   heading   │   v1  │   v2  │   v3  │   v4  │   v5  │   v6  │
├─────────────┼───────┼───────┼───────┼───────┼───────┼───────┤
│date─of─birth│1970.00│1989.00│2001.00│1966.00│1976.00│1982.00│
│sex          │   1.00│    .00│    .00│   1.00│    .00│   1.00│
│score        │  10.00│  10.00│   9.00│   3.00│   8.00│   9.00│
└─────────────┴───────┴───────┴───────┴───────┴───────┴───────┘

After FLIP:

                     Variables
┌─────────────┬────────┬────────────┬────────────┐
│Name         │Position│Print Format│Write Format│
├─────────────┼────────┼────────────┼────────────┤
│CASE_LBL     │       1│A8          │A8          │
│date_of_birth│       2│F8.2        │F8.2        │
│sex          │       3│F8.2        │F8.2        │
│score        │       4│F8.2        │F8.2        │
└─────────────┴────────┴────────────┴────────────┘

             Data List
┌────────┬─────────────┬────┬─────┐
│CASE_LBL│date_of_birth│ sex│score│
├────────┼─────────────┼────┼─────┤
│v1      │      1970.00│1.00│10.00│
│v2      │      1989.00│ .00│10.00│
│v3      │      2001.00│ .00│ 9.00│
│v4      │      1966.00│1.00│ 3.00│
│v5      │      1976.00│ .00│ 8.00│
│v6      │      1982.00│1.00│ 9.00│
└────────┴─────────────┴────┴─────┘
```
