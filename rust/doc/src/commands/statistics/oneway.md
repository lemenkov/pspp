# ONEWAY

```
ONEWAY
        [/VARIABLES = ] VAR_LIST BY VAR
        /MISSING={ANALYSIS,LISTWISE} {EXCLUDE,INCLUDE}
        /CONTRAST= VALUE1 [, VALUE2] ... [,VALUEN]
        /STATISTICS={DESCRIPTIVES,HOMOGENEITY}
        /POSTHOC={BONFERRONI, GH, LSD, SCHEFFE, SIDAK, TUKEY, ALPHA ([VALUE])}
```

The `ONEWAY` procedure performs a one-way analysis of variance of
variables factored by a single independent variable.  It is used to
compare the means of a population divided into more than two groups.

The dependent variables to be analysed should be given in the
`VARIABLES` subcommand.  The list of variables must be followed by the
`BY` keyword and the name of the independent (or factor) variable.

You can use the `STATISTICS` subcommand to tell PSPP to display
ancillary information.  The options accepted are:
- `DESCRIPTIVES`: Displays descriptive statistics about the groups
factored by the independent variable.
- `HOMOGENEITY`: Displays the Levene test of Homogeneity of Variance for
the variables and their groups.

The `CONTRAST` subcommand is used when you anticipate certain
differences between the groups.  The subcommand must be followed by a
list of numerals which are the coefficients of the groups to be tested.
The number of coefficients must correspond to the number of distinct
groups (or values of the independent variable).  If the total sum of the
coefficients are not zero, then PSPP will display a warning, but will
proceed with the analysis.  The `CONTRAST` subcommand may be given up to
10 times in order to specify different contrast tests.  The `MISSING`
subcommand defines how missing values are handled.  If `LISTWISE` is
specified then cases which have missing values for the independent
variable or any dependent variable are ignored.  If `ANALYSIS` is
specified, then cases are ignored if the independent variable is missing
or if the dependent variable currently being analysed is missing.  The
default is `ANALYSIS`.  A setting of `EXCLUDE` means that variables
whose values are user-missing are to be excluded from the analysis.  A
setting of `INCLUDE` means they are to be included.  The default is
`EXCLUDE`.

Using the `POSTHOC` subcommand you can perform multiple pairwise
comparisons on the data.  The following comparison methods are
available:
- `LSD`: Least Significant Difference.
- `TUKEY`: Tukey Honestly Significant Difference.
- `BONFERRONI`: Bonferroni test.
- `SCHEFFE`: Scheffé's test.
- `SIDAK`: Sidak test.
- `GH`: The Games-Howell test.

Use the optional syntax `ALPHA(VALUE)` to indicate that `ONEWAY` should
perform the posthoc tests at a confidence level of VALUE.  If
`ALPHA(VALUE)` is not specified, then the confidence level used is 0.05.

