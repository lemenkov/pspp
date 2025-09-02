# CORRELATIONS

```
CORRELATIONS
     /VARIABLES = VAR_LIST [ WITH VAR_LIST ]
     [
      .
      .
      .
      /VARIABLES = VAR_LIST [ WITH VAR_LIST ]
      /VARIABLES = VAR_LIST [ WITH VAR_LIST ]
     ]

     [ /PRINT={TWOTAIL, ONETAIL} {SIG, NOSIG} ]
     [ /STATISTICS=DESCRIPTIVES XPROD ALL]
     [ /MISSING={PAIRWISE, LISTWISE} {INCLUDE, EXCLUDE} ]
```

The `CORRELATIONS` procedure produces tables of the Pearson
correlation coefficient for a set of variables.  The significance of the
coefficients are also given.

At least one `VARIABLES` subcommand is required.  If you specify the
`WITH` keyword, then a non-square correlation table is produced.  The
variables preceding `WITH`, are used as the rows of the table, and the
variables following `WITH` are used as the columns of the table.  If no
`WITH` subcommand is specified, then `CORRELATIONS` produces a square,
symmetrical table using all variables.

The `MISSING` subcommand determines the handling of missing
variables.  If `INCLUDE` is set, then user-missing values are included
in the calculations, but system-missing values are not.  If `EXCLUDE` is
set, which is the default, user-missing values are excluded as well as
system-missing values.

If `LISTWISE` is set, then the entire case is excluded from analysis
whenever any variable specified in any `/VARIABLES` subcommand contains
a missing value.  If `PAIRWISE` is set, then a case is considered
missing only if either of the values for the particular coefficient are
missing.  The default is `PAIRWISE`.

The `PRINT` subcommand is used to control how the reported
significance values are printed.  If the `TWOTAIL` option is used, then
a two-tailed test of significance is printed.  If the `ONETAIL` option
is given, then a one-tailed test is used.  The default is `TWOTAIL`.

If the `NOSIG` option is specified, then correlation coefficients
with significance less than 0.05 are highlighted.  If `SIG` is
specified, then no highlighting is performed.  This is the default.

The `STATISTICS` subcommand requests additional statistics to be
displayed.  The keyword `DESCRIPTIVES` requests that the mean, number of
non-missing cases, and the non-biased estimator of the standard
deviation are displayed.  These statistics are displayed in a separated
table, for all the variables listed in any `/VARIABLES` subcommand.  The
`XPROD` keyword requests cross-product deviations and covariance
estimators to be displayed for each pair of variables.  The keyword
`ALL` is the union of `DESCRIPTIVES` and `XPROD`.

