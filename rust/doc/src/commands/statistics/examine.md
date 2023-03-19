#EXAMINE

```
EXAMINE
        VARIABLES= VAR1 [VAR2] ... [VARN]
           [BY FACTOR1 [BY SUBFACTOR1]
             [ FACTOR2 [BY SUBFACTOR2]]
             ...
             [ FACTOR3 [BY SUBFACTOR3]]
            ]
        /STATISTICS={DESCRIPTIVES, EXTREME[(N)], ALL, NONE}
        /PLOT={BOXPLOT, NPPLOT, HISTOGRAM, SPREADLEVEL[(T)], ALL, NONE}
        /CINTERVAL P
        /COMPARE={GROUPS,VARIABLES}
        /ID=IDENTITY_VARIABLE
        /{TOTAL,NOTOTAL}
        /PERCENTILE=[PERCENTILES]={HAVERAGE, WAVERAGE, ROUND, AEMPIRICAL, EMPIRICAL }
        /MISSING={LISTWISE, PAIRWISE} [{EXCLUDE, INCLUDE}]
       [{NOREPORT,REPORT}]
```

`EXAMINE` is used to perform exploratory data analysis.  In
particular, it is useful for testing how closely a distribution
follows a normal distribution, and for finding outliers and extreme
values.

The `VARIABLES` subcommand is mandatory.  It specifies the dependent
variables and optionally variables to use as factors for the analysis.
Variables listed before the first `BY` keyword (if any) are the
dependent variables.  The dependent variables may optionally be followed
by a list of factors which tell PSPP how to break down the analysis for
each dependent variable.

Following the dependent variables, factors may be specified.  The
factors (if desired) should be preceded by a single `BY` keyword.  The
format for each factor is `FACTORVAR [BY SUBFACTORVAR]`.  Each unique
combination of the values of `FACTORVAR` and `SUBFACTORVAR` divide the
dataset into "cells".  Statistics are calculated for each cell and for
the entire dataset (unless `NOTOTAL` is given).

The `STATISTICS` subcommand specifies which statistics to show.
`DESCRIPTIVES` produces a table showing some parametric and
non-parametrics statistics.  `EXTREME` produces a table showing the
extremities of each cell.  A number in parentheses determines how many
upper and lower extremities to show.  The default number is 5.

The subcommands `TOTAL` and `NOTOTAL` are mutually exclusive.  If
`TOTAL` appears, then statistics for the entire dataset as well as for
each cell are produced.  If `NOTOTAL` appears, then statistics are
produced only for the cells (unless no factor variables have been
given).  These subcommands have no effect if there have been no factor
variables specified.

The `PLOT` subcommand specifies which plots are to be produced if
any.  Available plots are `HISTOGRAM`, `NPPLOT`, `BOXPLOT` and
`SPREADLEVEL`.  The first three can be used to visualise how closely
each cell conforms to a normal distribution, whilst the spread vs. level
plot can be useful to visualise how the variance differs between
factors.  Boxplots show you the outliers and extreme values.[^1]

[^1]: `HISTOGRAM` uses Sturges' rule to determine the number of bins,
as approximately \\(1 + \log2(n)\\), where \\(n\\) is the number of
samples.  ([`FREQUENCIES`](frequencies.md) uses a different algorithm
to find the bin size.)

The `SPREADLEVEL` plot displays the interquartile range versus the
median.  It takes an optional parameter `T`, which specifies how the
data should be transformed prior to plotting.  The given value `T` is
a power to which the data are raised.  For example, if `T` is given as
2, then the square of the data is used.  Zero, however is a special
value.  If `T` is 0 or is omitted, then data are transformed by taking
its natural logarithm instead of raising to the power of `T`.

When one or more plots are requested, `EXAMINE` also performs the
Shapiro-Wilk test for each category.  There are however a number of
provisos:
- All weight values must be integer.
- The cumulative weight value must be in the range \[3, 5000\].

The `COMPARE` subcommand is only relevant if producing boxplots, and
it is only useful there is more than one dependent variable and at least
one factor.  If `/COMPARE=GROUPS` is specified, then one plot per
dependent variable is produced, each of which contain boxplots for all
the cells.  If `/COMPARE=VARIABLES` is specified, then one plot per cell
is produced, each containing one boxplot per dependent variable.  If the
`/COMPARE` subcommand is omitted, then PSPP behaves as if
`/COMPARE=GROUPS` were given.

The `ID` subcommand is relevant only if `/PLOT=BOXPLOT` or
`/STATISTICS=EXTREME` has been given.  If given, it should provide the
name of a variable which is to be used to labels extreme values and
outliers.  Numeric or string variables are permissible.  If the `ID`
subcommand is not given, then the case number is used for labelling.

The `CINTERVAL` subcommand specifies the confidence interval to use
in calculation of the descriptives command.  The default is 95%.

The `PERCENTILES` subcommand specifies which percentiles are to be
calculated, and which algorithm to use for calculating them.  The
default is to calculate the 5, 10, 25, 50, 75, 90, 95 percentiles using
the `HAVERAGE` algorithm.

The `TOTAL` and `NOTOTAL` subcommands are mutually exclusive.  If
`NOTOTAL` is given and factors have been specified in the `VARIABLES`
subcommand, then statistics for the unfactored dependent variables are
produced in addition to the factored variables.  If there are no factors
specified then `TOTAL` and `NOTOTAL` have no effect.

The following example generates descriptive statistics and histograms
for two variables `score1` and `score2`.  Two factors are given: `gender`
and `gender BY culture`.  Therefore, the descriptives and histograms are
generated for each distinct value of `gender` _and_ for each distinct
combination of the values of `gender` and `race`.  Since the `NOTOTAL`
keyword is given, statistics and histograms for `score1` and `score2`
covering the whole dataset are not produced.

```
EXAMINE score1 score2 BY
        gender
        gender BY culture
        /STATISTICS = DESCRIPTIVES
        /PLOT = HISTOGRAM
        /NOTOTAL.
```

Here is a second example showing how `EXAMINE` may be used to find
extremities.

```
EXAMINE height weight BY
        gender
        /STATISTICS = EXTREME (3)
        /PLOT = BOXPLOT
        /COMPARE = GROUPS
        /ID = name.
```

In this example, we look at the height and weight of a sample of
individuals and how they differ between male and female.  A table
showing the 3 largest and the 3 smallest values of height and weight for
each gender, and for the whole dataset as are shown.  In addition, the
`/PLOT` subcommand requests boxplots.  Because `/COMPARE = GROUPS` was
specified, boxplots for male and female are shown in juxtaposed in the
same graphic, allowing us to easily see the difference between the
genders.  Since the variable `name` was specified on the `ID` subcommand,
values of the `name` variable are used to label the extreme values.

> ⚠️ If you specify many dependent variables or factor variables for
which there are many distinct values, then `EXAMINE` will produce a
very large quantity of output.
