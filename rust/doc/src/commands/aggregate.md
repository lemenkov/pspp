# AGGREGATE

```
AGGREGATE
        [OUTFILE={*,'FILE_NAME',FILE_HANDLE} [MODE={REPLACE,ADDVARIABLES}]]
        [/MISSING=COLUMNWISE]
        [/PRESORTED]
        [/DOCUMENT]
        [/BREAK=VAR_LIST]
        /DEST_VAR['LABEL']...=AGR_FUNC(SRC_VARS[, ARGS]...)...
```

`AGGREGATE` summarizes groups of cases into single cases.  It divides
cases into groups that have the same values for one or more variables
called "break variables".  Several functions are available for
summarizing case contents.

The `AGGREGATE` syntax consists of subcommands to control its
behavior, all of which are optional, followed by one or more
destination variable assigments, each of which uses an aggregation
function to define how it is calculated.

The `OUTFILE` subcommand, which must be first, names the destination
for `AGGREGATE` output.  It may name a system file by file name or
[file handle](../language/files/file-handles.md), a
[dataset](../language/datasets/index.md) by its name, or `*` to
replace the active dataset.  `AGGREGATE` writes its output to this
file.

With `OUTFILE=*` only, `MODE` may be specified immediately afterward
with the value `ADDVARIABLES` or `REPLACE`:

- With `REPLACE`, the default, the active dataset is replaced by a
  new dataset which contains just the break variables and the
  destination varibles.  The new file contains as many cases as there
  are unique combinations of the break variables.

- With `ADDVARIABLES`, the destination variables are added to those
  in the existing active dataset.  Cases that have the same
  combination of values in their break variables receive identical
  values for the destination variables.  The number of cases in the
  active dataset remains unchanged.  The data must be sorted on the
  break variables, that is, `ADDVARIABLES` implies `PRESORTED`

Without `OUTFILE`, `AGGREGATE` acts as if `OUTFILE=*
MODE=ADDVARIABLES` were specified.

By default, `AGGREGATE` first sorts the data on the break variables.
If the active dataset is already sorted or grouped by the break
variables, specify `PRESORTED` to save time.  With
`MODE=ADDVARIABLES`, the data must be pre-sorted.

Specify [`DOCUMENT`](document.md) to copy the documents from the
active dataset into the aggregate file.  Otherwise, the aggregate file
does not contain any documents, even if the aggregate file replaces
the active dataset.

Normally, `AGGREGATE` produces a non-missing value whenever there is
enough non-missing data for the aggregation function in use, that is,
just one non-missing value or, for the `SD` and `SD.` aggregation
functions, two non-missing values.  Specify `/MISSING=COLUMNWISE` to
make `AGGREGATE` output a missing value when one or more of the input
values are missing.

The `BREAK` subcommand is optionally but usually present.  On `BREAK`,
list the variables used to divide the active dataset into groups to be
summarized.

`AGGREGATE` is particular about the order of subcommands.  `OUTFILE`
must be first, followed by `MISSING`.  `PRESORTED` and `DOCUMENT`
follow `MISSING`, in either order, followed by `BREAK`, then followed
by aggregation variable specifications.

At least one set of aggregation variables is required.  Each set
comprises a list of aggregation variables, an equals sign (`=`), the
name of an aggregation function (see the list below), and a list of
source variables in parentheses.  A few aggregation functions do not
accept source variables, and some aggregation functions expect
additional arguments after the source variable names.

`AGGREGATE` typically creates aggregation variables with no variable
label, value labels, or missing values.  Their default print and write
formats depend on the aggregation function used, with details given in
the table below.  A variable label for an aggregation variable may be
specified just after the variable's name in the aggregation variable
list.

Each set must have exactly as many source variables as aggregation
variables.  Each aggregation variable receives the results of applying
the specified aggregation function to the corresponding source variable.

The following aggregation functions may be applied only to numeric
variables:

* `MEAN(VAR_NAME...)`  
  Arithmetic mean.  Limited to numeric values.  The default format is
  `F8.2`.

* `MEDIAN(VAR_NAME...)`  
  The median value.  Limited to numeric values.  The default format
  is `F8.2`.

* `SD(VAR_NAME...)`  
  Standard deviation of the mean.  Limited to numeric values.  The
  default format is `F8.2`.

* `SUM(VAR_NAME...)`  
  Sum.  Limited to numeric values.  The default format is `F8.2`.

   These aggregation functions may be applied to numeric and string
variables:

* `CGT(VAR_NAME..., VALUE)`  
  `CLT(VAR_NAME..., VALUE)`  
  `CIN(VAR_NAME..., LOW, HIGH)`  
  `COUT(VAR_NAME..., LOW, HIGH)`  
  Total weight of cases greater than or less than `VALUE` or inside or
  outside the closed range `[LOW,HIGH]`, respectively.  The default
  format is `F5.3`.

* `FGT(VAR_NAME..., VALUE)`  
  `FLT(VAR_NAME..., VALUE)`  
  `FIN(VAR_NAME..., LOW, HIGH)`  
  `FOUT(VAR_NAME..., LOW, HIGH)`  
  Fraction of values greater than or less than `VALUE` or inside or
  outside the closed range `[LOW,HIGH]`, respectively.  The default
  format is `F5.3`.

* `FIRST(VAR_NAME...)`  
  `LAST(VAR_NAME...)`  
  First or last non-missing value, respectively, in break group.  The
  aggregation variable receives the complete dictionary information
  from the source variable.  The sort performed by `AGGREGATE` (and
  by `SORT CASES`) is stable.  This means that the first (or last)
  case with particular values for the break variables before sorting
  is also the first (or last) case in that break group after sorting.

* `MIN(VAR_NAME...)`  
  `MAX(VAR_NAME...)`  
  Minimum or maximum value, respectively.  The aggregation variable
  receives the complete dictionary information from the source
  variable.

* `N(VAR_NAME...)`  
  `NMISS(VAR_NAME...)`  
  Total weight of non-missing or missing values, respectively.  The
  default format is `F7.0` if weighting is not enabled, `F8.2` if it
  is (see [`WEIGHT`](weight.md)).

* `NU(VAR_NAME...)`  
  `NUMISS(VAR_NAME...)`  
  Count of non-missing or missing values, respectively, ignoring case
  weights.  The default format is `F7.0`.

* `PGT(VAR_NAME..., VALUE)`  
  `PLT(VAR_NAME..., VALUE)`  
  `PIN(VAR_NAME..., LOW, HIGH)`  
  `POUT(VAR_NAME..., LOW, HIGH)`  
  Percentage between 0 and 100 of values greater than or less than
  `VALUE` or inside or outside the closed range `[LOW,HIGH]`,
  respectively.  The default format is `F5.1`.

These aggregation functions do not accept source variables:

* `N`  
  Total weight of cases aggregated to form this group.  The default
  format is `F7.0` if weighting is not enabled, `F8.2` if it is (see
  [`WEIGHT`](weight.md)).

* `NU`  
  Count of cases aggregated to form this group, ignoring case
  weights.  The default format is `F7.0`.

Aggregation functions compare string values in terms of Unicode
character codes.

The aggregation functions listed above exclude all user-missing values
from calculations.  To include user-missing values, insert a period
(`.`) at the end of the function name.  (e.g. `SUM.`).  (Be aware that
specifying such a function as the last token on a line causes the
period to be interpreted as the end of the command.)

`AGGREGATE` both ignores and cancels the current [`SPLIT
FILE`](split-file.md) settings.

## Example

The `personnel.sav` dataset provides the occupations and salaries of
many individuals.  For many purposes however such detailed information
is not interesting, but often the aggregated statistics of each
occupation are of interest.  Here, the `AGGREGATE` command is used to
calculate the mean, the median and the standard deviation of each
occupation.

```
GET FILE="personnel.sav".
AGGREGATE OUTFILE=* MODE=REPLACE
        /BREAK=occupation
        /occ_mean_salary=MEAN(salary)
        /occ_median_salary=MEDIAN(salary)
        /occ_std_dev_salary=SD(salary).
LIST.
```

Since we chose the `MODE=REPLACE` option, cases for the individual
persons are no longer present.  They have each been replaced by a
single case per aggregated value.

```
                                Data List
┌──────────────────┬───────────────┬─────────────────┬──────────────────┐
│    occupation    │occ_mean_salary│occ_median_salary│occ_std_dev_salary│
├──────────────────┼───────────────┼─────────────────┼──────────────────┤
│Artist            │       37836.18│         34712.50│           7631.48│
│Baker             │       45075.20│         45075.20│           4411.21│
│Barrister         │       39504.00│         39504.00│                 .│
│Carpenter         │       39349.11│         36190.04│           7453.40│
│Cleaner           │       41142.50│         39647.49│          14378.98│
│Cook              │       40357.79│         43194.00│          11064.51│
│Manager           │       46452.14│         45657.56│           6901.69│
│Mathematician     │       34531.06│         34763.06│           5267.68│
│Painter           │       45063.55│         45063.55│          15159.67│
│Payload Specialist│       34355.72│         34355.72│                 .│
│Plumber           │       40413.91│         40410.00│           4726.05│
│Scientist         │       36687.07│         36803.83│          10873.54│
│Scrientist        │       42530.65│         42530.65│                 .│
│Tailor            │       34586.79│         34586.79│           3728.98│
└──────────────────┴───────────────┴─────────────────┴──────────────────┘
```

Some values for the standard deviation are blank because there is only
one case with the respective occupation.

