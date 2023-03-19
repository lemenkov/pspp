# CTABLES

`CTABLES` has the following overall syntax.  At least one `TABLE`
subcommand is required:

```
CTABLES
  ...global subcommands...
  [/TABLE axis [BY axis [BY axis]]
   ...per-table subcommands...]...
```

where each axis may be empty or take one of the following forms:

```
variable
variable [{C | S}]
axis + axis
axis > axis
(axis)
axis [summary [string] [format]]
```

The following subcommands precede the first `TABLE` subcommand and
apply to all of the output tables.  All of these subcommands are
optional:

```
/FORMAT
    [MINCOLWIDTH={DEFAULT | width}]
    [MAXCOLWIDTH={DEFAULT | width}]
    [UNITS={POINTS | INCHES | CM}]
    [EMPTY={ZERO | BLANK | string}]
    [MISSING=string]
/VLABELS
    VARIABLES=variables
    DISPLAY={DEFAULT | NAME | LABEL | BOTH | NONE}
/SMISSING {VARIABLE | LISTWISE}
/PCOMPUTE &postcompute=EXPR(expression)
/PPROPERTIES &postcompute...
    [LABEL=string]
    [FORMAT=[summary format]...]
    [HIDESOURCECATS={NO | YES}
/WEIGHT VARIABLE=variable
/HIDESMALLCOUNTS COUNT=count
```

The following subcommands follow `TABLE` and apply only to the
previous `TABLE`.  All of these subcommands are optional:

```
/SLABELS
    [POSITION={COLUMN | ROW | LAYER}]
    [VISIBLE={YES | NO}]
/CLABELS {AUTO | {ROWLABELS|COLLABELS}={OPPOSITE|LAYER}}
/CATEGORIES VARIABLES=variables
    {[value, value...]
   | [ORDER={A | D}]
     [KEY={VALUE | LABEL | summary(variable)}]
     [MISSING={EXCLUDE | INCLUDE}]}
    [TOTAL={NO | YES} [LABEL=string] [POSITION={AFTER | BEFORE}]]
    [EMPTY={INCLUDE | EXCLUDE}]
/TITLES
    [TITLE=string...]
    [CAPTION=string...]
    [CORNER=string...]
```

The `CTABLES` (aka "custom tables") command produces
multi-dimensional tables from categorical and scale data.  It offers
many options for data summarization and formatting.

This section's examples use data from the 2008 (USA) National Survey
of Drinking and Driving Attitudes and Behaviors, a public domain data
set from the (USA) National Highway Traffic Administration and available
at <https://data.transportation.gov>.  PSPP includes this data set, with
a modified dictionary, as `examples/nhtsa.sav`.

<!-- toc -->

## Basics

The only required subcommand is `TABLE`, which specifies the variables
to include along each axis:

```
     /TABLE rows [BY columns [BY layers]]
```

In `TABLE`, each of `ROWS`, `COLUMNS`, and `LAYERS` is either empty or
an axis expression that specifies one or more variables.  At least one
must specify an axis expression.

## Categorical Variables

An axis expression that names a categorical variable divides the data
into cells according to the values of that variable.  When all the
variables named on `TABLE` are categorical, by default each cell
displays the number of cases that it contains, so specifying a single
variable yields a frequency table, much like the output of the
[`FREQUENCIES`](frequencies.md) command:

```
     CTABLES /TABLE=ageGroup.
```

```
         Custom Tables
┌───────────────────────┬─────┐
│                       │Count│
├───────────────────────┼─────┤
│Age group 15 or younger│    0│
│          16 to 25     │ 1099│
│          26 to 35     │  967│
│          36 to 45     │ 1037│
│          46 to 55     │ 1175│
│          56 to 65     │ 1247│
│          66 or older  │ 1474│
└───────────────────────┴─────┘
```

Specifying a row and a column categorical variable yields a
crosstabulation, much like the output of the
[`CROSSTABS`](crosstabs.md) command:

```
CTABLES /TABLE=ageGroup BY gender.
```

```
             Custom Tables
┌───────────────────────┬────────────┐
│                       │S3a. GENDER:│
│                       ├─────┬──────┤
│                       │ Male│Female│
│                       ├─────┼──────┤
│                       │Count│ Count│
├───────────────────────┼─────┼──────┤
│Age group 15 or younger│    0│     0│
│          16 to 25     │  594│   505│
│          26 to 35     │  476│   491│
│          36 to 45     │  489│   548│
│          46 to 55     │  526│   649│
│          56 to 65     │  516│   731│
│          66 or older  │  531│   943│
└───────────────────────┴─────┴──────┘
```

The `>` "nesting" operator nests multiple variables on a single axis,
e.g.:

```
CTABLES /TABLE likelihoodOfBeingStoppedByPolice BY ageGroup > gender.
```

```
                                 Custom Tables
┌─────────────────────────────────┬───────────────────────────────────────────┐
│                                 │  86. In the past year, have you hosted a  │
│                                 │  social event or party where alcohol was  │
│                                 │             served to adults?             │
│                                 ├─────────────────────┬─────────────────────┤
│                                 │         Yes         │          No         │
│                                 ├─────────────────────┼─────────────────────┤
│                                 │        Count        │        Count        │
├─────────────────────────────────┼─────────────────────┼─────────────────────┤
│Age    15 or      S3a.     Male  │                    0│                    0│
│group  younger    GENDER:  Female│                    0│                    0│
│      ───────────────────────────┼─────────────────────┼─────────────────────┤
│       16 to 25   S3a.     Male  │                  208│                  386│
│                  GENDER:  Female│                  202│                  303│
│      ───────────────────────────┼─────────────────────┼─────────────────────┤
│       26 to 35   S3a.     Male  │                  225│                  251│
│                  GENDER:  Female│                  242│                  249│
│      ───────────────────────────┼─────────────────────┼─────────────────────┤
│       36 to 45   S3a.     Male  │                  223│                  266│
│                  GENDER:  Female│                  240│                  307│
│      ───────────────────────────┼─────────────────────┼─────────────────────┤
│       46 to 55   S3a.     Male  │                  201│                  325│
│                  GENDER:  Female│                  282│                  366│
│      ───────────────────────────┼─────────────────────┼─────────────────────┤
│       56 to 65   S3a.     Male  │                  196│                  320│
│                  GENDER:  Female│                  279│                  452│
│      ───────────────────────────┼─────────────────────┼─────────────────────┤
│       66 or      S3a.     Male  │                  162│                  367│
│       older      GENDER:  Female│                  243│                  700│
└─────────────────────────────────┴─────────────────────┴─────────────────────┘
```

The `+` "stacking" operator allows a single output table to include
multiple data analyses.  With `+`, `CTABLES` divides the output table
into multiple "sections", each of which includes an analysis of the full
data set.  For example, the following command separately tabulates age
group and driving frequency by gender:

```
CTABLES /TABLE ageGroup + freqOfDriving BY gender.
```

```
                                 Custom Tables
┌────────────────────────────────────────────────────────────────┬────────────┐
│                                                                │S3a. GENDER:│
│                                                                ├─────┬──────┤
│                                                                │ Male│Female│
│                                                                ├─────┼──────┤
│                                                                │Count│ Count│
├────────────────────────────────────────────────────────────────┼─────┼──────┤
│Age group                                    15 or younger      │    0│     0│
│                                             16 to 25           │  594│   505│
│                                             26 to 35           │  476│   491│
│                                             36 to 45           │  489│   548│
│                                             46 to 55           │  526│   649│
│                                             56 to 65           │  516│   731│
│                                             66 or older        │  531│   943│
├────────────────────────────────────────────────────────────────┼─────┼──────┤
│ 1. How often do you usually drive a car or  Every day          │ 2305│  2362│
│other motor vehicle?                         Several days a week│  440│   834│
│                                             Once a week or less│  125│   236│
│                                             Only certain times │   58│    72│
│                                             a year             │     │      │
│                                             Never              │  192│   348│
└────────────────────────────────────────────────────────────────┴─────┴──────┘
```

When `+` and `>` are used together, `>` binds more tightly.  Use
parentheses to override operator precedence.  Thus:

```
CTABLES /TABLE hasConsideredReduction + hasBeenCriticized > gender.
CTABLES /TABLE (hasConsideredReduction + hasBeenCriticized) > gender.
```

```
                                 Custom Tables
┌───────────────────────────────────────────────────────────────────────┬─────┐
│                                                                       │Count│
├───────────────────────────────────────────────────────────────────────┼─────┤
│26. During the last 12 months, has there been a    Yes                 │  513│
│time when you felt you should cut down on your    ─────────────────────┼─────┤
│drinking?                                          No                  │ 3710│
├───────────────────────────────────────────────────────────────────────┼─────┤
│27. During the last 12 months, has there been a    Yes S3a.      Male  │  135│
│time when people criticized your drinking?             GENDER:   Female│   49│
│                                                  ─────────────────────┼─────┤
│                                                   No  S3a.      Male  │ 1916│
│                                                       GENDER:   Female│ 2126│
└───────────────────────────────────────────────────────────────────────┴─────┘

                                 Custom Tables
┌───────────────────────────────────────────────────────────────────────┬─────┐
│                                                                       │Count│
├───────────────────────────────────────────────────────────────────────┼─────┤
│26. During the last 12 months, has there been a    Yes S3a.      Male  │  333│
│time when you felt you should cut down on your         GENDER:   Female│  180│
│drinking?                                         ─────────────────────┼─────┤
│                                                   No  S3a.      Male  │ 1719│
│                                                       GENDER:   Female│ 1991│
├───────────────────────────────────────────────────────────────────────┼─────┤
│27. During the last 12 months, has there been a    Yes S3a.      Male  │  135│
│time when people criticized your drinking?             GENDER:   Female│   49│
│                                                  ─────────────────────┼─────┤
│                                                   No  S3a.      Male  │ 1916│
│                                                       GENDER:   Female│ 2126│
└───────────────────────────────────────────────────────────────────────┴─────┘
```


## Scalar Variables

For a categorical variable, `CTABLES` divides the table into a cell per
category.  For a scalar variable, `CTABLES` instead calculates a summary
measure, by default the mean, of the values that fall into a cell.  For
example, if the only variable specified is a scalar variable, then the
output is a single cell that holds the mean of all of the data:

```
CTABLES /TABLE age.
```

```
          Custom Tables
┌──────────────────────────┬────┐
│                          │Mean│
├──────────────────────────┼────┤
│D1. AGE: What is your age?│  48│
└──────────────────────────┴────┘
```

A scalar variable may nest with categorical variables.  The following
example shows the mean age of survey respondents across gender and
language groups:

```
CTABLES /TABLE gender > age BY region.
```

```
Custom Tables
┌─────────────────────────────────────┬───────────────────────────────────────┐
│                                     │Was this interview conducted in English│
│                                     │              or Spanish?              │
│                                     ├───────────────────┬───────────────────┤
│                                     │      English      │      Spanish      │
│                                     ├───────────────────┼───────────────────┤
│                                     │        Mean       │        Mean       │
├─────────────────────────────────────┼───────────────────┼───────────────────┤
│D1. AGE: What is   S3a.        Male  │                 46│                 37│
│your age?          GENDER:     Female│                 51│                 39│
└─────────────────────────────────────┴───────────────────┴───────────────────┘
```

The order of nesting of scalar and categorical variables affects
table labeling, but it does not affect the data displayed in the table.
The following example shows how the output changes when the nesting
order of the scalar and categorical variable are interchanged:

```
CTABLES /TABLE age > gender BY region.
```

```
                                 Custom Tables
┌─────────────────────────────────────┬───────────────────────────────────────┐
│                                     │Was this interview conducted in English│
│                                     │              or Spanish?              │
│                                     ├───────────────────┬───────────────────┤
│                                     │      English      │      Spanish      │
│                                     ├───────────────────┼───────────────────┤
│                                     │        Mean       │        Mean       │
├─────────────────────────────────────┼───────────────────┼───────────────────┤
│S3a.       Male   D1. AGE: What is   │                 46│                 37│
│GENDER:           your age?          │                   │                   │
│          ───────────────────────────┼───────────────────┼───────────────────┤
│           Female D1. AGE: What is   │                 51│                 39│
│                  your age?          │                   │                   │
└─────────────────────────────────────┴───────────────────┴───────────────────┘
```

Only a single scalar variable may appear in each section; that is, a
scalar variable may not nest inside a scalar variable directly or
indirectly.  Scalar variables may only appear on one axis within
`TABLE`.

## Overriding Measurement Level

By default, `CTABLES` uses a variable's measurement level to decide
whether to treat it as categorical or scalar.  Variables assigned the
nominal or ordinal measurement level are treated as categorical, and
scalar variables are treated as scalar.

When PSPP reads data from a file in an external format, such as a text
file, variables' measurement levels are often unknown.  If `CTABLES`
runs when a variable has an unknown measurement level, it makes an
initial pass through the data to [guess measurement
levels](../../language/datasets/variables.md).  Use the [`VARIABLE
LEVEL`](../../commands/variables/variable-level.md) command to set or
change a variable's measurement level.

To treat a variable as categorical or scalar only for one use on
`CTABLES`, add `[C]` or `[S]`, respectively, after the variable name.
The following example shows the output when variable
`monthDaysMin1drink` is analyzed as scalar (the default for its
measurement level) and as categorical:

```
CTABLES
    /TABLE monthDaysMin1drink BY gender
    /TABLE monthDaysMin1drink [C] BY gender.
```

```
                                 Custom Tables
┌────────────────────────────────────────────────────────────────┬────────────┐
│                                                                │S3a. GENDER:│
│                                                                ├────┬───────┤
│                                                                │Male│ Female│
│                                                                ├────┼───────┤
│                                                                │Mean│  Mean │
├────────────────────────────────────────────────────────────────┼────┼───────┤
│20. On how many of the thirty days in this typical month did you│   7│      5│
│have one or more alcoholic beverages to drink?                  │    │       │
└────────────────────────────────────────────────────────────────┴────┴───────┘

                                 Custom Tables
┌────────────────────────────────────────────────────────────────┬────────────┐
│                                                                │S3a. GENDER:│
│                                                                ├─────┬──────┤
│                                                                │ Male│Female│
│                                                                ├─────┼──────┤
│                                                                │Count│ Count│
├────────────────────────────────────────────────────────────────┼─────┼──────┤
│20. On how many of the thirty days in this typical month None   │  152│   258│
│did you have one or more alcoholic beverages to drink?   1      │  403│   653│
│                                                         2      │  284│   324│
│                                                         3      │  169│   215│
│                                                         4      │  178│   143│
│                                                         5      │  107│   106│
│                                                         6      │   67│    59│
│                                                         7      │   31│    11│
│                                                         8      │  101│    74│
│                                                         9      │    6│     4│
│                                                         10     │   95│    75│
│                                                         11     │    4│     0│
│                                                         12     │   58│    33│
│                                                         13     │    3│     2│
│                                                         14     │   13│     3│
│                                                         15     │   79│    58│
│                                                         16     │   10│     6│
│                                                         17     │    4│     2│
│                                                         18     │    5│     4│
│                                                         19     │    2│     0│
│                                                         20     │  105│    47│
│                                                         21     │    2│     0│
│                                                         22     │    3│     3│
│                                                         23     │    0│     3│
│                                                         24     │    3│     0│
│                                                         25     │   35│    25│
│                                                         26     │    1│     1│
│                                                         27     │    3│     3│
│                                                         28     │   13│     8│
│                                                         29     │    3│     3│
│                                                         Every  │  104│    43│
│                                                         day    │     │      │
└────────────────────────────────────────────────────────────────┴─────┴──────┘
```

## Data Summarization

The `CTABLES` command allows the user to control how the data are
summarized with "summary specifications", syntax that lists one or more
summary function names, optionally separated by commas, and which are
enclosed in square brackets following a variable name on the `TABLE`
subcommand.  When all the variables are categorical, summary
specifications can be given for the innermost nested variables on any
one axis.  When a scalar variable is present, only the scalar variable
may have summary specifications.

The following example includes a summary specification for column and
row percentages for categorical variables, and mean and median for a
scalar variable:

```
CTABLES
    /TABLE=age [MEAN, MEDIAN] BY gender
    /TABLE=ageGroup [COLPCT, ROWPCT] BY gender.
```

```
                    Custom Tables
┌──────────────────────────┬───────────────────────┐
│                          │      S3a. GENDER:     │
│                          ├───────────┬───────────┤
│                          │    Male   │   Female  │
│                          ├────┬──────┼────┬──────┤
│                          │Mean│Median│Mean│Median│
├──────────────────────────┼────┼──────┼────┼──────┤
│D1. AGE: What is your age?│  46│    45│  50│    52│
└──────────────────────────┴────┴──────┴────┴──────┘

                     Custom Tables
┌───────────────────────┬─────────────────────────────┐
│                       │         S3a. GENDER:        │
│                       ├──────────────┬──────────────┤
│                       │     Male     │    Female    │
│                       ├────────┬─────┼────────┬─────┤
│                       │Column %│Row %│Column %│Row %│
├───────────────────────┼────────┼─────┼────────┼─────┤
│Age group 15 or younger│     .0%│    .│     .0%│    .│
│          16 to 25     │   19.0%│54.0%│   13.1%│46.0%│
│          26 to 35     │   15.2%│49.2%│   12.7%│50.8%│
│          36 to 45     │   15.6%│47.2%│   14.2%│52.8%│
│          46 to 55     │   16.8%│44.8%│   16.8%│55.2%│
│          56 to 65     │   16.5%│41.4%│   18.9%│58.6%│
│          66 or older  │   17.0%│36.0%│   24.4%│64.0%│
└───────────────────────┴────────┴─────┴────────┴─────┘
```

A summary specification may override the default label and format by
appending a string or format specification or both (in that order) to
the summary function name.  For example:

```
CTABLES /TABLE=ageGroup [COLPCT 'Gender %' PCT5.0,
                         ROWPCT 'Age Group %' PCT5.0]
               BY gender.
```

```
                           Custom Tables
┌───────────────────────┬─────────────────────────────────────────┐
│                       │               S3a. GENDER:              │
│                       ├────────────────────┬────────────────────┤
│                       │        Male        │       Female       │
│                       ├────────┬───────────┼────────┬───────────┤
│                       │Gender %│Age Group %│Gender %│Age Group %│
├───────────────────────┼────────┼───────────┼────────┼───────────┤
│Age group 15 or younger│      0%│          .│      0%│          .│
│          16 to 25     │     19%│        54%│     13%│        46%│
│          26 to 35     │     15%│        49%│     13%│        51%│
│          36 to 45     │     16%│        47%│     14%│        53%│
│          46 to 55     │     17%│        45%│     17%│        55%│
│          56 to 65     │     16%│        41%│     19%│        59%│
│          66 or older  │     17%│        36%│     24%│        64%│
└───────────────────────┴────────┴───────────┴────────┴───────────┘
```

In addition to the standard formats, `CTABLES` allows the user to
specify the following special formats:

|Format|Description|Positive Example|Negative Example|
|:-----|:----------|-------:|-------:|
|`NEGPARENw.d`|Encloses negative numbers in parentheses.|42.96|(42.96)|
|`NEQUALw.d`|Adds a `N=` prefix.|N=42.96|N=-42.96|
|`PARENw.d`|Encloses all numbers in parentheses.|(42.96)|(-42.96)|
|`PCTPARENw.d`|Encloses all numbers in parentheses with a `%` suffix.|(42.96%)|(-42.96%)|

Parentheses provide a shorthand to apply summary specifications to
multiple variables.  For example, both of these commands:

```
CTABLES /TABLE=ageGroup[COLPCT] + membersOver16[COLPCT] BY gender.
CTABLES /TABLE=(ageGroup + membersOver16)[COLPCT] BY gender.
```

produce the same output shown below:

```
                                 Custom Tables
┌─────────────────────────────────────────────────────────────┬───────────────┐
│                                                             │  S3a. GENDER: │
│                                                             ├───────┬───────┤
│                                                             │  Male │ Female│
│                                                             ├───────┼───────┤
│                                                             │ Column│ Column│
│                                                             │   %   │   %   │
├─────────────────────────────────────────────────────────────┼───────┼───────┤
│Age group                                         15 or      │    .0%│    .0%│
│                                                  younger    │       │       │
│                                                  16 to 25   │  19.0%│  13.1%│
│                                                  26 to 35   │  15.2%│  12.7%│
│                                                  36 to 45   │  15.6%│  14.2%│
│                                                  46 to 55   │  16.8%│  16.8%│
│                                                  56 to 65   │  16.5%│  18.9%│
│                                                  66 or older│  17.0%│  24.4%│
├─────────────────────────────────────────────────────────────┼───────┼───────┤
│S1. Including yourself, how many members of this  None       │    .0%│    .0%│
│household are age 16 or older?                    1          │  21.4%│  35.0%│
│                                                  2          │  61.9%│  52.3%│
│                                                  3          │  11.0%│   8.2%│
│                                                  4          │   4.2%│   3.2%│
│                                                  5          │   1.1%│    .9%│
│                                                  6 or more  │    .4%│    .4%│
└─────────────────────────────────────────────────────────────┴───────┴───────┘
```

The following sections list the available summary functions.  After
each function's name is given its default label and format.  If no
format is listed, then the default format is the print format for the
variable being summarized.

### Summary Functions for Individual Cells

This section lists the summary functions that consider only an
individual cell in `CTABLES`.  Only one such summary function, `COUNT`,
may be applied to both categorical and scale variables:

* `COUNT` ("Count", F40.0)  
  The sum of weights in a cell.

  If `CATEGORIES` for one or more of the variables in a table include
  missing values (see [Per-Variable Category
  Options](#per-variable-category-options)), then some or all of the
  categories for a cell might be missing values.  `COUNT` counts data
  included in a cell regardless of whether its categories are missing.

The following summary functions apply only to scale variables or
totals and subtotals for categorical variables.  Be cautious about
interpreting the summary value in the latter case, because it is not
necessarily meaningful; however, the mean of a Likert scale, etc. may
have a straightforward interpreation.

* `MAXIMUM` ("Maximum")  
  The largest value.

* `MEAN` ("Mean")  
  The mean.

* `MEDIAN` ("Median")  
  The median value.

* `MINIMUM` ("Minimum")  
  The smallest value.

* `MISSING` ("Missing")  
  Sum of weights of user- and system-missing values.

* `MODE` ("Mode")  
  The highest-frequency value.  Ties are broken by taking the
  smallest mode.

* `PTILE` n ("Percentile n")  
  The Nth percentile, where 0 ≤ N ≤ 100.

* `RANGE` ("Range")  
  The maximum minus the minimum.

* `SEMEAN` ("Std Error of Mean")  
  The standard error of the mean.

* `STDDEV` ("Std Deviation")  
  The standard deviation.

* `SUM` ("Sum")  
  The sum.

* `TOTALN` ("Total N", F40.0)  
  The sum of weights in a cell.

  For scale data, `COUNT` and `TOTALN` are the same.

  For categorical data, `TOTALN` counts missing values in excluded
  categories, that is, user-missing values not in an explicit category
  list on `CATEGORIES` (see [Per-Variable Category
  Options](#per-variable-category-options)), or user-missing values
  excluded because `MISSING=EXCLUDE` is in effect on `CATEGORIES`, or
  system-missing values.  `COUNT` does not count these.

  See [Missing Values for Summary
  Variables](#missing-values-for-summary-variables), for details of
  how `CTABLES` summarizes missing values.

* `VALIDN` ("Valid N", F40.0)  
  The sum of valid count weights in included categories.

  For categorical variables, `VALIDN` does not count missing values
  regardless of whether they are in included categories via
  `CATEGORIES`.  `VALIDN` does not count valid values that are in
  excluded categories.  See [Missing Values for Summary
  Variables](#missing-values-for-summary-variables) for details.

* `VARIANCE` ("Variance")  
  The variance.

### Summary Functions for Groups of Cells

These summary functions summarize over multiple cells within an area of
the output chosen by the user and specified as part of the function
name.  The following basic AREAs are supported, in decreasing order of
size:

* `TABLE`  
  A "section".  Stacked variables divide sections of the output from
  each other.  sections may span multiple layers.

* `LAYER`  
  A section within a single layer.

* `SUBTABLE`  
  A "subtable", whose contents are the cells that pair an innermost
  row variable and an innermost column variable within a single
  layer.

The following shows how the output for the table expression
`hasBeenPassengerOfDesignatedDriver > hasBeenPassengerOfDrunkDriver BY
isLicensedDriver > hasHostedEventWithAlcohol + hasBeenDesignatedDriver
BY gender`[^1] is divided up into `TABLE`, `LAYER`, and `SUBTABLE`
areas.  Each unique value for Table ID is one section, and similarly
for Layer ID and Subtable ID. Thus, this output has two `TABLE` areas
(one for `isLicensedDriver` and one for `hasBeenDesignatedDriver`),
four `LAYER` areas (for those two variables, per layer), and 12
`SUBTABLE` areas.

```
                        Custom Tables
Male
┌─────────────────────────────────┬─────────────────┬──────┐
│                                 │     licensed    │desDrv│
│                                 ├────────┬────────┼───┬──┤
│                                 │   Yes  │   No   │   │  │
│                                 ├────────┼────────┤   │  │
│                                 │ hostAlc│ hostAlc│   │  │
│                                 ├────┬───┼────┬───┤   │  │
│                                 │ Yes│ No│ Yes│ No│Yes│No│
├─────────────────────────────────┼────┼───┼────┼───┼───┼──┤
│desPas Yes druPas Yes Table ID   │   1│  1│   1│  1│  2│ 2│
│                      Layer ID   │   1│  1│   1│  1│  2│ 2│
│                      Subtable ID│   1│  1│   2│  2│  3│ 3│
│                 ────────────────┼────┼───┼────┼───┼───┼──┤
│                  No  Table ID   │   1│  1│   1│  1│  2│ 2│
│                      Layer ID   │   1│  1│   1│  1│  2│ 2│
│                      Subtable ID│   1│  1│   2│  2│  3│ 3│
│      ───────────────────────────┼────┼───┼────┼───┼───┼──┤
│       No  druPas Yes Table ID   │   1│  1│   1│  1│  2│ 2│
│                      Layer ID   │   1│  1│   1│  1│  2│ 2│
│                      Subtable ID│   4│  4│   5│  5│  6│ 6│
│                 ────────────────┼────┼───┼────┼───┼───┼──┤
│                  No  Table ID   │   1│  1│   1│  1│  2│ 2│
│                      Layer ID   │   1│  1│   1│  1│  2│ 2│
│                      Subtable ID│   4│  4│   5│  5│  6│ 6│
└─────────────────────────────────┴────┴───┴────┴───┴───┴──┘
```

`CTABLES` also supports the following AREAs that further divide a
subtable or a layer within a section:

* `LAYERROW`  
  `LAYERCOL`  
  A row or column, respectively, in one layer of a section.

* `ROW`  
  `COL`  
  A row or column, respectively, in a subtable.

The following summary functions for groups of cells are available for
each AREA described above, for both categorical and scale variables:

* `areaPCT` or `areaPCT.COUNT` ("Area %", PCT40.1)  
  A percentage of total counts within AREA.

* `areaPCT.VALIDN` ("Area Valid N %", PCT40.1)  
  A percentage of total counts for valid values within AREA.

* `areaPCT.TOTALN` ("Area Total N %", PCT40.1)  
  A percentage of total counts for all values within AREA.

Scale variables and totals and subtotals for categorical variables
may use the following additional group cell summary function:

* `areaPCT.SUM` ("Area Sum %", PCT40.1)  
  Percentage of the sum of the values within AREA.


[^1]: This is not necessarily a meaningful table.  To make it easier to
read, short variable labels are used.

### Summary Functions for Adjusted Weights

If the `WEIGHT` subcommand specified an [effective weight
variable](#effective-weight), then the following summary functions use
its value instead of the dictionary weight variable.  Otherwise, they
are equivalent to the summary function without the `E`-prefix:

- `ECOUNT` ("Adjusted Count", F40.0)

- `ETOTALN` ("Adjusted Total N", F40.0)

- `EVALIDN` ("Adjusted Valid N", F40.0)

### Unweighted Summary Functions

The following summary functions with a `U`-prefix are equivalent to the
same ones without the prefix, except that they use unweighted counts:

- `UCOUNT` ("Unweighted Count", F40.0)

- `UareaPCT` or `UareaPCT.COUNT` ("Unweighted Area %", PCT40.1)

- `UareaPCT.VALIDN` ("Unweighted Area Valid N %", PCT40.1)

- `UareaPCT.TOTALN` ("Unweighted Area Total N %", PCT40.1)

- `UMEAN` ("Unweighted Mean")

- `UMEDIAN` ("Unweighted Median")

- `UMISSING` ("Unweighted Missing")

- `UMODE` ("Unweighted Mode")

- `UareaPCT.SUM` ("Unweighted Area Sum %", PCT40.1)

- `UPTILE` n ("Unweighted Percentile n")

- `USEMEAN` ("Unweighted Std Error of Mean")

- `USTDDEV` ("Unweighted Std Deviation")

- `USUM` ("Unweighted Sum")

- `UTOTALN` ("Unweighted Total N", F40.0)

- `UVALIDN` ("Unweighted Valid N", F40.0)

- `UVARIANCE` ("Unweighted Variance", F40.0)

## Statistics Positions and Labels

```
/SLABELS
    [POSITION={COLUMN | ROW | LAYER}]
    [VISIBLE={YES | NO}]
```

The `SLABELS` subcommand controls the position and visibility of
summary statistics for the `TABLE` subcommand that it follows.

`POSITION` sets the axis on which summary statistics appear.  With
POSITION=COLUMN, which is the default, each summary statistic appears in
a column.  For example:

```
CTABLES /TABLE=age [MEAN, MEDIAN] BY gender.
```

```
                    Custom Tables
+──────────────────────────+───────────────────────+
│                          │      S3a. GENDER:     │
│                          +───────────+───────────+
│                          │    Male   │   Female  │
│                          +────+──────+────+──────+
│                          │Mean│Median│Mean│Median│
+──────────────────────────+────+──────+────+──────+
│D1. AGE: What is your age?│  46│    45│  50│    52│
+──────────────────────────+────+──────+────+──────+
```


With `POSITION=ROW`, each summary statistic appears in a row, as shown
below:

```
CTABLES /TABLE=age [MEAN, MEDIAN] BY gender /SLABELS POSITION=ROW.
```

```
                  Custom Tables
+─────────────────────────────────+─────────────+
│                                 │ S3a. GENDER:│
│                                 +─────+───────+
│                                 │ Male│ Female│
+─────────────────────────────────+─────+───────+
│D1. AGE: What is your age? Mean  │   46│     50│
│                           Median│   45│     52│
+─────────────────────────────────+─────+───────+
```


`POSITION=LAYER` is also available to place each summary statistic in a
separate layer.

Labels for summary statistics are shown by default.  Use VISIBLE=NO
to suppress them.  Because unlabeled data can cause confusion, it should
only be considered if the meaning of the data is evident, as in a simple
case like this:

```
CTABLES /TABLE=ageGroup [TABLEPCT] /SLABELS VISIBLE=NO.
```

```
         Custom Tables
+───────────────────────+─────+
│Age group 15 or younger│  .0%│
│          16 to 25     │15.7%│
│          26 to 35     │13.8%│
│          36 to 45     │14.8%│
│          46 to 55     │16.8%│
│          56 to 65     │17.8%│
│          66 or older  │21.1%│
+───────────────────────+─────+
```


## Category Label Positions

```
/CLABELS {AUTO │ {ROWLABELS│COLLABELS}={OPPOSITE│LAYER}}
```

The `CLABELS` subcommand controls the position of category labels for
the `TABLE` subcommand that it follows.  By default, or if AUTO is
specified, category labels for a given variable nest inside the
variable's label on the same axis.  For example, the command below
results in age categories nesting within the age group variable on the
rows axis and gender categories within the gender variable on the
columns axis:

```
CTABLES /TABLE ageGroup BY gender.
```

```
             Custom Tables
+───────────────────────+────────────+
│                       │S3a. GENDER:│
│                       +─────+──────+
│                       │ Male│Female│
│                       +─────+──────+
│                       │Count│ Count│
+───────────────────────+─────+──────+
│Age group 15 or younger│    0│     0│
│          16 to 25     │  594│   505│
│          26 to 35     │  476│   491│
│          36 to 45     │  489│   548│
│          46 to 55     │  526│   649│
│          56 to 65     │  516│   731│
│          66 or older  │  531│   943│
+───────────────────────+─────+──────+
```


ROWLABELS=OPPOSITE or COLLABELS=OPPOSITE move row or column variable
category labels, respectively, to the opposite axis.  The setting
affects only the innermost variable or variables, which must be
categorical, on the given axis.  For example:

```
CTABLES /TABLE ageGroup BY gender /CLABELS ROWLABELS=OPPOSITE.
CTABLES /TABLE ageGroup BY gender /CLABELS COLLABELS=OPPOSITE.
```

```
                                Custom Tables
+─────+──────────────────────────────────────────────────────────────────────
│     │                                      S3a. GENDER:
│     +───────────────────────────────────────────+──────────────────────────
│     │                    Male                   │                   Female
│     +───────+─────+─────+─────+─────+─────+─────+───────+─────+─────+─────+
│     │ 15 or │16 to│26 to│36 to│46 to│56 to│66 or│ 15 or │16 to│26 to│36 to│
│     │younger│  25 │  35 │  45 │  55 │  65 │older│younger│  25 │  35 │  45 │
│     +───────+─────+─────+─────+─────+─────+─────+───────+─────+─────+─────+
│     │ Count │Count│Count│Count│Count│Count│Count│ Count │Count│Count│Count│
+─────+───────+─────+─────+─────+─────+─────+─────+───────+─────+─────+─────+
│Age  │      0│  594│  476│  489│  526│  516│  531│      0│  505│  491│  548│
│group│       │     │     │     │     │     │     │       │     │     │     │
+─────+───────+─────+─────+─────+─────+─────+─────+───────+─────+─────+─────+

+─────+─────────────────+
│     │                 │
│     +─────────────────+
│     │                 │
│     +─────+─────+─────+
│     │46 to│56 to│66 or│
│     │  55 │  65 │older│
│     +─────+─────+─────+
│     │Count│Count│Count│
+─────+─────+─────+─────+
│Age  │  649│  731│  943│
│group│     │     │     │
+─────+─────+─────+─────+

                Custom Tables
+──────────────────────────────+────────────+
│                              │S3a. GENDER:│
│                              +────────────+
│                              │    Count   │
+──────────────────────────────+────────────+
│Age group 15 or younger Male  │           0│
│                        Female│           0│
│         ─────────────────────+────────────+
│          16 to 25      Male  │         594│
│                        Female│         505│
│         ─────────────────────+────────────+
│          26 to 35      Male  │         476│
│                        Female│         491│
│         ─────────────────────+────────────+
│          36 to 45      Male  │         489│
│                        Female│         548│
│         ─────────────────────+────────────+
│          46 to 55      Male  │         526│
│                        Female│         649│
│         ─────────────────────+────────────+
│          56 to 65      Male  │         516│
│                        Female│         731│
│         ─────────────────────+────────────+
│          66 or older   Male  │         531│
│                        Female│         943│
+──────────────────────────────+────────────+
```


`ROWLABELS=LAYER` or `COLLABELS=LAYER` move the innermost row or column
variable category labels, respectively, to the layer axis.

Only one axis's labels may be moved, whether to the opposite axis or
to the layer axis.

### Effect on Summary Statistics

`CLABELS` primarily affects the appearance of tables, not the data
displayed in them.  However, `CTABLES` can affect the values displayed
for statistics that summarize areas of a table, since it can change the
definitions of these areas.

For example, consider the following syntax and output:

```
CTABLES /TABLE ageGroup BY gender [ROWPCT, COLPCT].
```

```
                     Custom Tables
+───────────────────────+─────────────────────────────+
│                       │         S3a. GENDER:        │
│                       +──────────────+──────────────+
│                       │     Male     │    Female    │
│                       +─────+────────+─────+────────+
│                       │Row %│Column %│Row %│Column %│
+───────────────────────+─────+────────+─────+────────+
│Age group 15 or younger│    .│     .0%│    .│     .0%│
│          16 to 25     │54.0%│   19.0%│46.0%│   13.1%│
│          26 to 35     │49.2%│   15.2%│50.8%│   12.7%│
│          36 to 45     │47.2%│   15.6%│52.8%│   14.2%│
│          46 to 55     │44.8%│   16.8%│55.2%│   16.8%│
│          56 to 65     │41.4%│   16.5%│58.6%│   18.9%│
│          66 or older  │36.0%│   17.0%│64.0%│   24.4%│
+───────────────────────+─────+────────+─────+────────+
```


Using `COLLABELS=OPPOSITE` changes the definitions of rows and columns,
so that column percentages display what were previously row percentages
and the new row percentages become meaningless (because there is only
one cell per row):

```
CTABLES
    /TABLE ageGroup BY gender [ROWPCT, COLPCT]
    /CLABELS COLLABELS=OPPOSITE.
```

```
                  Custom Tables
+──────────────────────────────+───────────────+
│                              │  S3a. GENDER: │
│                              +──────+────────+
│                              │ Row %│Column %│
+──────────────────────────────+──────+────────+
│Age group 15 or younger Male  │     .│       .│
│                        Female│     .│       .│
│         ─────────────────────+──────+────────+
│          16 to 25      Male  │100.0%│   54.0%│
│                        Female│100.0%│   46.0%│
│         ─────────────────────+──────+────────+
│          26 to 35      Male  │100.0%│   49.2%│
│                        Female│100.0%│   50.8%│
│         ─────────────────────+──────+────────+
│          36 to 45      Male  │100.0%│   47.2%│
│                        Female│100.0%│   52.8%│
│         ─────────────────────+──────+────────+
│          46 to 55      Male  │100.0%│   44.8%│
│                        Female│100.0%│   55.2%│
│         ─────────────────────+──────+────────+
│          56 to 65      Male  │100.0%│   41.4%│
│                        Female│100.0%│   58.6%│
│         ─────────────────────+──────+────────+
│          66 or older   Male  │100.0%│   36.0%│
│                        Female│100.0%│   64.0%│
+──────────────────────────────+──────+────────+
```


### Moving Categories for Stacked Variables

If `CLABELS` moves category labels from an axis with stacked
variables, the variables that are moved must have the same category
specifications (see [Per-Variable Category
Options](#per-variable-category-options)) and the same value labels.

The following shows both moving stacked category variables and
adapting to the changing definitions of rows and columns:

```
CTABLES /TABLE (likelihoodOfBeingStoppedByPolice
                + likelihoodOfHavingAnAccident) [COLPCT].
CTABLES /TABLE (likelihoodOfBeingStoppedByPolice
                + likelihoodOfHavingAnAccident) [ROWPCT]
  /CLABELS ROW=OPPOSITE.
```

```
                                 Custom Tables
+─────────────────────────────────────────────────────────────────────+───────+
│                                                                     │ Column│
│                                                                     │   %   │
+─────────────────────────────────────────────────────────────────────+───────+
│105b. How likely is it that drivers who have had too     Almost      │  10.2%│
│much to drink to drive safely will A. Get stopped by the certain     │       │
│police?                                                  Very likely │  21.8%│
│                                                         Somewhat    │  40.2%│
│                                                         likely      │       │
│                                                         Somewhat    │  19.0%│
│                                                         unlikely    │       │
│                                                         Very        │   8.9%│
│                                                         unlikely    │       │
+─────────────────────────────────────────────────────────────────────+───────+
│105b. How likely is it that drivers who have had too     Almost      │  15.9%│
│much to drink to drive safely will B. Have an accident?  certain     │       │
│                                                         Very likely │  40.8%│
│                                                         Somewhat    │  35.0%│
│                                                         likely      │       │
│                                                         Somewhat    │   6.2%│
│                                                         unlikely    │       │
│                                                         Very        │   2.0%│
│                                                         unlikely    │       │
+─────────────────────────────────────────────────────────────────────+───────+

                                 Custom Tables
+─────────────────────────────+────────+───────+─────────+──────────+─────────+
│                             │ Almost │  Very │ Somewhat│ Somewhat │   Very  │
│                             │ certain│ likely│  likely │ unlikely │ unlikely│
│                             +────────+───────+─────────+──────────+─────────+
│                             │  Row % │ Row % │  Row %  │   Row %  │  Row %  │
+─────────────────────────────+────────+───────+─────────+──────────+─────────+
│105b. How likely is it that  │   10.2%│  21.8%│    40.2%│     19.0%│     8.9%│
│drivers who have had too much│        │       │         │          │         │
│to drink to drive safely will│        │       │         │          │         │
│A. Get stopped by the police?│        │       │         │          │         │
│105b. How likely is it that  │   15.9%│  40.8%│    35.0%│      6.2%│     2.0%│
│drivers who have had too much│        │       │         │          │         │
│to drink to drive safely will│        │       │         │          │         │
│B. Have an accident?         │        │       │         │          │         │
+─────────────────────────────+────────+───────+─────────+──────────+─────────+
```


## Per-Variable Category Options

```
/CATEGORIES VARIABLES=variables
    {[value, value...]
   | [ORDER={A | D}]
     [KEY={VALUE | LABEL | summary(variable)}]
     [MISSING={EXCLUDE | INCLUDE}]}
    [TOTAL={NO | YES} [LABEL=string] [POSITION={AFTER | BEFORE}]]
    [EMPTY={INCLUDE | EXCLUDE}]
```

The `CATEGORIES` subcommand specifies, for one or more categorical
variables, the categories to include and exclude, the sort order for
included categories, and treatment of missing values.  It also controls
the totals and subtotals to display.  It may be specified any number of
times, each time for a different set of variables.  `CATEGORIES` applies
to the table produced by the `TABLE` subcommand that it follows.

`CATEGORIES` does not apply to scalar variables.

VARIABLES is required and must list the variables for the subcommand
to affect.

The syntax may specify the categories to include and their sort order
either explicitly or implicitly.  The following sections give the
details of each form of syntax, followed by information on totals and
subtotals and the `EMPTY` setting.

### Explicit Categories

To use `CTABLES` to explicitly specify categories to include, list the
categories within square brackets in the desired sort order.  Use spaces
or commas to separate values.  Categories not covered by the list are
excluded from analysis.

Each element of the list takes one of the following forms:

* `number`  
  `'string'`  
  A numeric or string category value, for variables that have the
  corresponding type.

* `'date'`  
  `'time'`  
  A date or time category value, for variables that have a date or
  time print format.

* `min THRU max`  
  `LO THRU max`  
  `min THRU HI`  
  A range of category values, where `min` and `max` each takes one of
  the forms above, in increasing order.

* `MISSING`  
  All user-missing values.  (To match individual user-missing values,
  specify their category values.)

* `OTHERNM`  
  Any non-missing value not covered by any other element of the list
  (regardless of where `OTHERNM` is placed in the list).

* `&postcompute`  
  A [computed category name](#computed-categories).

* `SUBTOTAL`  
  `HSUBTOTAL`  
     A [subtotal](#totals-and-subtotals).

If multiple elements of the list cover a given category, the last one
in the list takes precedence.

The following example syntax and output show how an explicit category
can limit the displayed categories:

```
CTABLES /TABLE freqOfDriving.
CTABLES /TABLE freqOfDriving /CATEGORIES VARIABLES=freqOfDriving [1, 2, 3].
```

```
                                 Custom Tables
+───────────────────────────────────────────────────────────────────────+─────+
│                                                                       │Count│
+───────────────────────────────────────────────────────────────────────+─────+
│ 1. How often do you usually drive a car or other  Every day           │ 4667│
│motor vehicle?                                     Several days a week │ 1274│
│                                                   Once a week or less │  361│
│                                                   Only certain times a│  130│
│                                                   year                │     │
│                                                   Never               │  540│
+───────────────────────────────────────────────────────────────────────+─────+

                                 Custom Tables
+───────────────────────────────────────────────────────────────────────+─────+
│                                                                       │Count│
+───────────────────────────────────────────────────────────────────────+─────+
│ 1. How often do you usually drive a car or other     Every day        │ 4667│
│motor vehicle?                                        Several days a   │ 1274│
│                                                      week             │     │
│                                                      Once a week or   │  361│
│                                                      less             │     │
+───────────────────────────────────────────────────────────────────────+─────+
```


### Implicit Categories

In the absence of an explicit list of categories, `CATEGORIES` allows
`KEY`, `ORDER`, and `MISSING` to specify how to select and sort
categories.

The `KEY` setting specifies the sort key.  By default, or with
`KEY=VALUE`, categories are sorted by default.  Categories may also be
sorted by value label, with `KEY=LABEL`, or by the value of a summary
function, e.g. `KEY=COUNT`.

By default, or with `ORDER=A`, categories are sorted in ascending
order.  Specify `ORDER=D` to sort in descending order.

User-missing values are excluded by default, or with
`MISSING=EXCLUDE`.  Specify `MISSING=INCLUDE` to include user-missing
values.  The system-missing value is always excluded.

The following example syntax and output show how `MISSING=INCLUDE`
causes missing values to be included in a category list.

```
CTABLES /TABLE freqOfDriving.
CTABLES /TABLE freqOfDriving
        /CATEGORIES VARIABLES=freqOfDriving MISSING=INCLUDE.
```

```
                                 Custom Tables
+───────────────────────────────────────────────────────────────────────+─────+
│                                                                       │Count│
+───────────────────────────────────────────────────────────────────────+─────+
│ 1. How often do you usually drive a car or other  Every day           │ 4667│
│motor vehicle?                                     Several days a week │ 1274│
│                                                   Once a week or less │  361│
│                                                   Only certain times a│  130│
│                                                   year                │     │
│                                                   Never               │  540│
+───────────────────────────────────────────────────────────────────────+─────+

                                 Custom Tables
+───────────────────────────────────────────────────────────────────────+─────+
│                                                                       │Count│
+───────────────────────────────────────────────────────────────────────+─────+
│ 1. How often do you usually drive a car or other  Every day           │ 4667│
│motor vehicle?                                     Several days a week │ 1274│
│                                                   Once a week or less │  361│
│                                                   Only certain times a│  130│
│                                                   year                │     │
│                                                   Never               │  540│
│                                                   Don't know          │    8│
│                                                   Refused             │   19│
+───────────────────────────────────────────────────────────────────────+─────+
```


### Totals and Subtotals

`CATEGORIES` also controls display of totals and subtotals.  By default,
or with `TOTAL=NO`, totals are not displayed.  Use `TOTAL=YES` to
display a total.  By default, the total is labeled "Total"; use
`LABEL="label"` to override it.

Subtotals are also not displayed by default.  To add one or more
subtotals, use an explicit category list and insert `SUBTOTAL` or
`HSUBTOTAL` in the position or positions where the subtotal should
appear.  The subtotal becomes an extra row or column or layer.
`HSUBTOTAL` additionally hides the categories that make up the
subtotal.  Either way, the default label is "Subtotal", use
`SUBTOTAL="label"` or `HSUBTOTAL="label"` to specify a custom label.

The following example syntax and output show how to use `TOTAL=YES`
and `SUBTOTAL`:

```
CTABLES
    /TABLE freqOfDriving
    /CATEGORIES VARIABLES=freqOfDriving [OTHERNM, SUBTOTAL='Valid Total',
                                         MISSING, SUBTOTAL='Missing Total']
                                        TOTAL=YES LABEL='Overall Total'.
```

```
                                 Custom Tables
+───────────────────────────────────────────────────────────────────────+─────+
│                                                                       │Count│
+───────────────────────────────────────────────────────────────────────+─────+
│ 1. How often do you usually drive a car or other  Every day           │ 4667│
│motor vehicle?                                     Several days a week │ 1274│
│                                                   Once a week or less │  361│
│                                                   Only certain times a│  130│
│                                                   year                │     │
│                                                   Never               │  540│
│                                                   Valid Total         │ 6972│
│                                                   Don't know          │    8│
│                                                   Refused             │   19│
│                                                   Missing Total       │   27│
│                                                   Overall Total       │ 6999│
+───────────────────────────────────────────────────────────────────────+─────+
```


By default, or with `POSITION=AFTER`, totals are displayed in the
output after the last category and subtotals apply to categories that
precede them.  With `POSITION=BEFORE`, totals come before the first
category and subtotals apply to categories that follow them.

Only categorical variables may have totals and subtotals.  Scalar
variables may be "totaled" indirectly by enabling totals and subtotals
on a categorical variable within which the scalar variable is
summarized.  For example, the following syntax produces a mean, count,
and valid count across all data by adding a total on the categorical
`region` variable, as shown:

```
CTABLES /TABLE=region > monthDaysMin1drink [MEAN, VALIDN]
    /CATEGORIES VARIABLES=region TOTAL=YES LABEL='All regions'.
```

```
                                 Custom Tables
+───────────────────────────────────────────────────────────+────+─────+──────+
│                                                           │    │     │ Valid│
│                                                           │Mean│Count│   N  │
+───────────────────────────────────────────────────────────+────+─────+──────+
│20. On how many of the thirty days in this  Region NE      │ 5.6│ 1409│   945│
│typical month did you have one or more             MW      │ 5.0│ 1654│  1026│
│alcoholic beverages to drink?                      S       │ 6.0│ 2390│  1285│
│                                                   W       │ 6.5│ 1546│   953│
│                                                   All     │ 5.8│ 6999│  4209│
│                                                   regions │    │     │      │
+───────────────────────────────────────────────────────────+────+─────+──────+
```


By default, PSPP uses the same summary functions for totals and
subtotals as other categories.  To summarize totals and subtotals
differently, specify the summary functions for totals and subtotals
after the ordinary summary functions inside a nested set of `[]`
following `TOTALS`.  For example, the following syntax displays `COUNT`
for individual categories and totals and `VALIDN` for totals, as shown:

```
CTABLES
    /TABLE isLicensedDriver [COUNT, TOTALS[COUNT, VALIDN]]
    /CATEGORIES VARIABLES=isLicensedDriver TOTAL=YES MISSING=INCLUDE.
```

```
                                 Custom Tables
+────────────────────────────────────────────────────────────────+─────+──────+
│                                                                │     │ Valid│
│                                                                │Count│   N  │
+────────────────────────────────────────────────────────────────+─────+──────+
│D7a. Are you a licensed driver; that is, do you have a Yes      │ 6379│      │
│valid driver's license?                                No       │  572│      │
│                                                       Don't    │    4│      │
│                                                       know     │     │      │
│                                                       Refused  │   44│      │
│                                                       Total    │ 6999│  6951│
+────────────────────────────────────────────────────────────────+─────+──────+
```


### Categories Without Values

Some categories might not be included in the data set being analyzed.
For example, our example data set has no cases in the "15 or younger"
age group.  By default, or with `EMPTY=INCLUDE`, PSPP includes these
empty categories in output tables.  To exclude them, specify
`EMPTY=EXCLUDE`.

For implicit categories, empty categories potentially include all the
values with value labels for a given variable; for explicit categories,
they include all the values listed individually and all values with
value labels that are covered by ranges or `MISSING` or `OTHERNM`.

The following example syntax and output show the effect of
`EMPTY=EXCLUDE` for the `membersOver16` variable, in which 0 is labeled
"None" but no cases exist with that value:

```
CTABLES /TABLE=membersOver16.
CTABLES /TABLE=membersOver16 /CATEGORIES VARIABLES=membersOver16 EMPTY=EXCLUDE.
```

```
                                 Custom Tables
+───────────────────────────────────────────────────────────────────────+─────+
│                                                                       │Count│
+───────────────────────────────────────────────────────────────────────+─────+
│S1. Including yourself, how many members of this household are None    │    0│
│age 16 or older?                                               1       │ 1586│
│                                                               2       │ 3031│
│                                                               3       │  505│
│                                                               4       │  194│
│                                                               5       │   55│
│                                                               6 or    │   21│
│                                                               more    │     │
+───────────────────────────────────────────────────────────────────────+─────+

                                 Custom Tables
+───────────────────────────────────────────────────────────────────────+─────+
│                                                                       │Count│
+───────────────────────────────────────────────────────────────────────+─────+
│S1. Including yourself, how many members of this household are 1       │ 1586│
│age 16 or older?                                               2       │ 3031│
│                                                               3       │  505│
│                                                               4       │  194│
│                                                               5       │   55│
│                                                               6 or    │   21│
│                                                               more    │     │
+───────────────────────────────────────────────────────────────────────+─────+
```


## Titles

```
/TITLES
    [TITLE=string...]
    [CAPTION=string...]
    [CORNER=string...]
```

The `TITLES` subcommand sets the title, caption, and corner text for
the table output for the previous `TABLE` subcommand.  Any number of
strings may be specified for each kind of text, with each string
appearing on a separate line in the output.  The title appears above the
table, the caption below the table, and the corner text appears in the
table's upper left corner.  By default, the title is "Custom Tables" and
the caption and corner text are empty.  With some table output styles,
the corner text is not displayed.

The strings provided in this subcommand may contain the following
macro-like keywords that PSPP substitutes at the time that it runs the
command:

* `)DATE`  
  The current date, e.g. MM/DD/YY. The format is locale-dependent.

* `)TIME`  
  The current time, e.g. HH:MM:SS. The format is locale-dependent.

* `)TABLE`  
  The expression specified on the `TABLE` command.  Summary and
  measurement level specifications are omitted, and variable labels
  are used in place of variable names.

## Table Formatting

```
/FORMAT
    [MINCOLWIDTH={DEFAULT | width}]
    [MAXCOLWIDTH={DEFAULT | width}]
    [UNITS={POINTS | INCHES | CM}]
    [EMPTY={ZERO | BLANK | string}]
    [MISSING=string]
```

The `FORMAT` subcommand, which must precede the first `TABLE`
subcommand, controls formatting for all the output tables.  `FORMAT` and
all of its settings are optional.

Use `MINCOLWIDTH` and `MAXCOLWIDTH` to control the minimum or maximum
width of columns in output tables.  By default, with `DEFAULT`, column
width varies based on content.  Otherwise, specify a number for either
or both of these settings.  If both are specified, `MAXCOLWIDTH` must be
greater than or equal to `MINCOLWIDTH`.  The default unit, or with
`UNITS=POINTS`, is points (1/72 inch), or specify `UNITS=INCHES` to use
inches or `UNITS=CM` for centimeters.  PSPP does not currently honor any
of these settings.

By default, or with `EMPTY=ZERO`, zero values are displayed in their
usual format.  Use `EMPTY=BLANK` to use an empty cell instead, or
`EMPTY="string"` to use the specified string.

By default, missing values are displayed as `.`, the same as in other
tables.  Specify `MISSING="string"` to instead use a custom string.

## Display of Variable Labels

```
/VLABELS
    VARIABLES=variables
    DISPLAY={DEFAULT | NAME | LABEL | BOTH | NONE}
```

The `VLABELS` subcommand, which must precede the first `TABLE`
subcommand, controls display of variable labels in all the output
tables.  `VLABELS` is optional.  It may appear multiple times to adjust
settings for different variables.

`VARIABLES` and `DISPLAY` are required.  The value of `DISPLAY`
controls how variable labels are displayed for the variables listed on
`VARIABLES`.  The supported values are:

* `DEFAULT`  
  Use the setting from [`SET TVARS`](../utilities/set.md#tvars)).

* `NAME`  
  Show only a variable name.

* `LABEL`  
  Show only a variable label.

* `BOTH`  
  Show variable name and label.

* `NONE`  
  Show nothing.

## Missing Value Treatment

The `TABLE` subcommand on `CTABLES` specifies two different kinds of
variables: variables that divide tables into cells (which are always
categorical) and variables being summarized (which may be categorical or
scale).  PSPP treats missing values differently in each kind of
variable, as described in the sections below.

### Missing Values for Cell-Defining Variables

For variables that divide tables into cells, per-variable category
options, as described in [Per-Variable Category
Options](#per-variable-category-options), determine which data is
analyzed.  If any of the categories for such a variable would exclude
a case, then that case is not included.

As an example, consider the following entirely artificial dataset, in
which `x` and `y` are categorical variables with missing value 9, and
`z` is scale:

```
   Data List
+─+─+─────────+
│x│y│    z    │
+─+─+─────────+
│1│1│        1│
│1│2│       10│
│1│9│      100│
│2│1│     1000│
│2│2│    10000│
│2│9│   100000│
│9│1│  1000000│
│9│2│ 10000000│
│9│9│100000000│
+─+─+─────────+
```


Using `x` and `y` to define cells, and summarizing `z`, by default
PSPP omits all the cases that have `x` or `y` (or both) missing:

```
CTABLES /TABLE x > y > z [SUM].
```

```
  Custom Tables
+─────────+─────+
│         │ Sum │
+─────────+─────+
│x 1 y 1 z│    1│
│     ────+─────+
│      2 z│   10│
│ ────────+─────+
│  2 y 1 z│ 1000│
│     ────+─────+
│      2 z│10000│
+─────────+─────+
```


If, however, we add `CATEGORIES` specifications to include missing
values for `y` or for `x` and `y`, the output table includes them, like
so:

```
CTABLES /TABLE x > y > z [SUM] /CATEGORIES VARIABLES=y MISSING=INCLUDE.
CTABLES /TABLE x > y > z [SUM] /CATEGORIES VARIABLES=x y MISSING=INCLUDE.
```

```
   Custom Tables
+─────────+──────+
│         │  Sum │
+─────────+──────+
│x 1 y 1 z│     1│
│     ────+──────+
│      2 z│    10│
│     ────+──────+
│      9 z│   100│
│ ────────+──────+
│  2 y 1 z│  1000│
│     ────+──────+
│      2 z│ 10000│
│     ────+──────+
│      9 z│100000│
+─────────+──────+

    Custom Tables
+─────────+─────────+
│         │   Sum   │
+─────────+─────────+
│x 1 y 1 z│        1│
│     ────+─────────+
│      2 z│       10│
│     ────+─────────+
│      9 z│      100│
│ ────────+─────────+
│  2 y 1 z│     1000│
│     ────+─────────+
│      2 z│    10000│
│     ────+─────────+
│      9 z│   100000│
│ ────────+─────────+
│  9 y 1 z│  1000000│
│     ────+─────────+
│      2 z│ 10000000│
│     ────+─────────+
│      9 z│100000000│
+─────────+─────────+
```


### Missing Values for Summary Variables

For summary variables, values that are valid and in included categories
are analyzed, and values that are missing or in excluded categories are
not analyzed, with the following exceptions:

- The `VALIDN` summary functions (`VALIDN`, `EVALIDN`, `UVALIDN`,
  `areaPCT.VALIDN`, and `UareaPCT.VALIDN`) only count valid values in
  included categories (not missing values in included categories).

- The `TOTALN` summary functions (`TOTALN`, `ETOTALN`, `UTOTALN`,
  `areaPCT.TOTALN`), and `UareaPCT.TOTALN` count all values (valid
  and missing) in included categories and missing (but not valid)
  values in excluded categories.

For categorical variables, system-missing values are never in included
categories.  For scale variables, there is no notion of included and
excluded categories, so all values are effectively included.

The following table provides another view of the above rules:

||`VALIDN`|other|`TOTALN`|
|:--|:--|:--|:--|
|Categorical variables:||||
|   Valid values in included categories|yes|yes|yes|
|   Missing values in included categories|--|yes|yes|
|   Missing values in excluded categories|--|--|yes|
|   Valid values in excluded categories|--|--|--|
|Scale variables:||||
|   Valid values|yes|yes|yes|
|   User- or system-missing values|--|yes|yes|


### Scale Missing Values

```
/SMISSING {VARIABLE | LISTWISE}
```

The `SMISSING` subcommand, which must precede the first `TABLE`
subcommand, controls treatment of missing values for scalar variables in
producing all the output tables.  `SMISSING` is optional.

With `SMISSING=VARIABLE`, which is the default, missing values are
excluded on a variable-by-variable basis.  With `SMISSING=LISTWISE`,
when stacked scalar variables are nested together with a categorical
variable, a missing value for any of the scalar variables causes the
case to be excluded for all of them.

As an example, consider the following dataset, in which `x` is a
categorical variable and `y` and `z` are scale:

```
   Data List
+─+─────+─────+
│x│  y  │  z  │
+─+─────+─────+
│1│    .│40.00│
│1│10.00│50.00│
│1│20.00│60.00│
│1│30.00│    .│
+─+─────+─────+
```


With the default missing-value treatment, `x`'s mean is 20, based on the
values 10, 20, and 30, and `y`'s mean is 50, based on 40, 50, and 60:

```
CTABLES /TABLE (y + z) > x.
```

```
Custom Tables
+─────+─────+
│     │ Mean│
+─────+─────+
│y x 1│20.00│
+─────+─────+
│z x 1│50.00│
+─────+─────+
```


By adding `SMISSING=LISTWISE`, only cases where `y` and `z` are both
non-missing are considered, so `x`'s mean becomes 15, as the average of
10 and 20, and `y`'s mean becomes 55, the average of 50 and 60:

```
CTABLES /SMISSING LISTWISE /TABLE (y + z) > x.
```

```
Custom Tables
+─────+─────+
│     │ Mean│
+─────+─────+
│y x 1│15.00│
+─────+─────+
│z x 1│55.00│
+─────+─────+
```


Even with `SMISSING=LISTWISE`, if `y` and `z` are separately nested with
`x`, instead of using a single `>` operator, missing values revert to
being considered on a variable-by-variable basis:

```
CTABLES /SMISSING LISTWISE /TABLE (y > x) + (z > x).
```

```
Custom Tables
+─────+─────+
│     │ Mean│
+─────+─────+
│y x 1│20.00│
+─────+─────+
│z x 1│50.00│
+─────+─────+
```


## Computed Categories

```
/PCOMPUTE &postcompute=EXPR(expression)
/PPROPERTIES &postcompute...
    [LABEL=string]
    [FORMAT=[summary format]...]
    [HIDESOURCECATS={NO | YES}
```

"Computed categories", also called "postcomputes", are categories
created using arithmetic on categories obtained from the data.  The
`PCOMPUTE` subcommand creates a postcompute, which may then be used on
`CATEGORIES` within an [explicit category
list](#explicit-categories).  Optionally, `PPROPERTIES` refines how
a postcompute is displayed.  The following sections provide the
details.

### PCOMPUTE

```
/PCOMPUTE &postcompute=EXPR(expression)
```

The `PCOMPUTE` subcommand, which must precede the first `TABLE`
command, defines computed categories.  It is optional and may be used
any number of times to define multiple postcomputes.

Each `PCOMPUTE` defines one postcompute.  Its syntax consists of a
name to identify the postcompute as a PSPP identifier prefixed by `&`,
followed by `=` and a postcompute expression enclosed in `EXPR(...)`.  A
postcompute expression consists of:

* `[category]`  
  This form evaluates to the summary statistic for category, e.g.
  `[1]` evaluates to the value of the summary statistic associated
  with category 1.  The category may be a number, a quoted string, or
  a quoted time or date value.  All of the categories for a given
  postcompute must have the same form.  The category must appear in
  all the `CATEGORIES` list in which the postcompute is used.

* `[min THRU max]`  
`[LO THRU max]`  
`[min THRU HI]`  
`MISSING`  
`OTHERNM`  
  These forms evaluate to the summary statistics for a category
  specified with the same syntax, as described in a previous section
  (see [Explicit Category List](#explicit-categories)).  The
  category must appear in all the `CATEGORIES` list in which the
  postcompute is used.

* `SUBTOTAL`  
  The summary statistic for the subtotal category.  This form is
  allowed only if the `CATEGORIES` lists that include this
  postcompute have exactly one subtotal.

* `SUBTOTAL[index]`  
  The summary statistic for subtotal category index, where 1 is the
  first subtotal, 2 is the second, and so on.  This form may be used
  for `CATEGORIES` lists with any number of subtotals.

* `TOTAL`  
  The summary statistic for the total.  The `CATEGORIES` lsits that
  include this postcompute must have a total enabled.

* `a + b`  
  `a - b`  
  `a * b`  
  `a / b`  
  `a ** b`
  These forms perform arithmetic on the values of postcompute
  expressions a and b.  The usual operator precedence rules apply.

* `number`  
  Numeric constants may be used in postcompute expressions.

* `(a)`  
  Parentheses override operator precedence.

A postcompute is not associated with any particular variable.
Instead, it may be referenced within `CATEGORIES` for any suitable
variable (e.g. only a string variable is suitable for a postcompute
expression that refers to a string category, only a variable with
subtotals for an expression that refers to subtotals, ...).

Normally a named postcompute is defined only once, but if a later
`PCOMPUTE` redefines a postcompute with the same name as an earlier one,
the later one take precedence.

The following syntax and output shows how `PCOMPUTE` can compute a
total over subtotals, summing the "Frequent Drivers" and "Infrequent
Drivers" subtotals to form an "All Drivers" postcompute.  It also
shows how to calculate and display a percentage, in this case the
percentage of valid responses that report never driving.  It uses
[`PPROPERTIES`](#pproperties) to display the latter in `PCT` format.

```
CTABLES
    /PCOMPUTE &all_drivers=EXPR([1 THRU 2] + [3 THRU 4])
    /PPROPERTIES &all_drivers LABEL='All Drivers'
    /PCOMPUTE &pct_never=EXPR([5] / ([1 THRU 2] + [3 THRU 4] + [5]) * 100)
    /PPROPERTIES &pct_never LABEL='% Not Drivers' FORMAT=COUNT PCT40.1
    /TABLE=freqOfDriving BY gender
    /CATEGORIES VARIABLES=freqOfDriving
                             [1 THRU 2, SUBTOTAL='Frequent Drivers',
                              3 THRU 4, SUBTOTAL='Infrequent Drivers',
                              &all_drivers, 5, &pct_never,
                              MISSING, SUBTOTAL='Not Drivers or Missing'].
```

```
                                 Custom Tables
+────────────────────────────────────────────────────────────────+────────────+
│                                                                │S3a. GENDER:│
│                                                                +─────+──────+
│                                                                │ Male│Female│
│                                                                +─────+──────+
│                                                                │Count│ Count│
+────────────────────────────────────────────────────────────────+─────+──────+
│ 1. How often do you usually drive a car or Every day           │ 2305│  2362│
│other motor vehicle?                        Several days a week │  440│   834│
│                                            Frequent Drivers    │ 2745│  3196│
│                                            Once a week or less │  125│   236│
│                                            Only certain times a│   58│    72│
│                                            year                │     │      │
│                                            Infrequent Drivers  │  183│   308│
│                                            All Drivers         │ 2928│  3504│
│                                            Never               │  192│   348│
│                                            % Not Drivers       │ 6.2%│  9.0%│
│                                            Don't know          │    3│     5│
│                                            Refused             │    9│    10│
│                                            Not Drivers or      │  204│   363│
│                                            Missing             │     │      │
+────────────────────────────────────────────────────────────────+─────+──────+
```


### PPROPERTIES

```
/PPROPERTIES &postcompute...
    [LABEL=string]
    [FORMAT=[summary format]...]
    [HIDESOURCECATS={NO | YES}
```

The `PPROPERTIES` subcommand, which must appear before `TABLE`, sets
properties for one or more postcomputes defined on prior `PCOMPUTE`
subcommands.  The subcommand syntax begins with the list of
postcomputes, each prefixed with `&` as specified on `PCOMPUTE`.

All of the settings on `PPROPERTIES` are optional.  Use `LABEL` to
set the label shown for the postcomputes in table output.  The default
label for a postcompute is the expression used to define it.

A postcompute always uses same summary functions as the variable
whose categories contain it, but `FORMAT` allows control over the format
used to display their values.  It takes a list of summary function names
and format specifiers.

By default, or with `HIDESOURCECATS=NO`, categories referred to by
computed categories are displayed like other categories.  Use
`HIDESOURCECATS=YES` to hide them.

The previous section provides an example for `PPROPERTIES`.

## Effective Weight

```
/WEIGHT VARIABLE=variable
```

The `WEIGHT` subcommand is optional and must appear before `TABLE`.
If it appears, it must name a numeric variable, known as the
"effective weight" or "adjustment weight".  The effective weight
variable stands in for the dictionary's [weight
variable](../../commands/selection/weight.md), if any, in most
calculations in `CTABLES`.  The only exceptions are the `COUNT`,
`TOTALN`, and `VALIDN` summary functions, which use the dictionary
weight instead.

Weights obtained from the PSPP dictionary are rounded to the nearest
integer at the case level.  Effective weights are not rounded.
Regardless of the weighting source, PSPP does not analyze cases with
zero, missing, or negative effective weights.


## Hiding Small Counts

```
/HIDESMALLCOUNTS COUNT=count
```

The `HIDESMALLCOUNTS` subcommand is optional.  If it specified, then
`COUNT`, `ECOUNT`, and `UCOUNT` values in output tables less than the
value of count are shown as `<count` instead of their true values.  The
value of count must be an integer and must be at least 2.

The following syntax and example shows how to use `HIDESMALLCOUNTS`:

```
CTABLES /HIDESMALLCOUNTS COUNT=10 /TABLE placeOfLastDrinkBeforeDrive.
```

```
                                 Custom Tables
+───────────────────────────────────────────────────────────────────────+─────+
│                                                                       │Count│
+───────────────────────────────────────────────────────────────────────+─────+
│37. Please think about the most recent occasion that   Other (list)    │<10  │
│you drove within two hours of drinking alcoholic       Your home       │  182│
│beverages. Where did you drink on that occasion?       Friend's home   │  264│
│                                                       Bar/Tavern/Club │  279│
│                                                       Restaurant      │  495│
│                                                       Work            │   21│
│                                                       Bowling alley   │<10  │
│                                                       Hotel/Motel     │<10  │
│                                                       Country Club/   │   17│
│                                                       Golf course     │     │
│                                                       Drank in the    │<10  │
│                                                       car/On the road │     │
│                                                       Sporting event  │   15│
│                                                       Movie theater   │<10  │
│                                                       Shopping/Store/ │<10  │
│                                                       Grocery store   │     │
│                                                       Wedding         │   15│
│                                                       Party at someone│   81│
│                                                       else's home     │     │
│                                                       Park/picnic     │   14│
│                                                       Party at your   │<10  │
│                                                       house           │     │
+───────────────────────────────────────────────────────────────────────+─────+
```
