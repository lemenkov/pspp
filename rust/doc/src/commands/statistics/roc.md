# ROC

```
ROC
        VAR_LIST BY STATE_VAR (STATE_VALUE)
        /PLOT = { CURVE [(REFERENCE)], NONE }
        /PRINT = [ SE ] [ COORDINATES ]
        /CRITERIA = [ CUTOFF({INCLUDE,EXCLUDE}) ]
          [ TESTPOS ({LARGE,SMALL}) ]
          [ CI (CONFIDENCE) ]
          [ DISTRIBUTION ({FREE, NEGEXPO }) ]
        /MISSING={EXCLUDE,INCLUDE}
```

The `ROC` command is used to plot the receiver operating
characteristic curve of a dataset, and to estimate the area under the
curve.  This is useful for analysing the efficacy of a variable as a
predictor of a state of nature.

The mandatory `VAR_LIST` is the list of predictor variables.  The
variable `STATE_VAR` is the variable whose values represent the actual
states, and `STATE_VALUE` is the value of this variable which represents
the positive state.

The optional subcommand `PLOT` is used to determine if and how the
`ROC` curve is drawn.  The keyword `CURVE` means that the `ROC` curve
should be drawn, and the optional keyword `REFERENCE`, which should be
enclosed in parentheses, says that the diagonal reference line should be
drawn.  If the keyword `NONE` is given, then no `ROC` curve is drawn.
By default, the curve is drawn with no reference line.

The optional subcommand `PRINT` determines which additional tables
should be printed.  Two additional tables are available.  The `SE`
keyword says that standard error of the area under the curve should be
printed as well as the area itself.  In addition, a p-value for the null
hypothesis that the area under the curve equals 0.5 is printed.  The
`COORDINATES` keyword says that a table of coordinates of the `ROC`
curve should be printed.

The `CRITERIA` subcommand has four optional parameters:

- The `TESTPOS` parameter may be `LARGE` or `SMALL`.  `LARGE` is the
  default, and says that larger values in the predictor variables are
  to be considered positive.  `SMALL` indicates that smaller values
  should be considered positive.

- The `CI` parameter specifies the confidence interval that should be
  printed.  It has no effect if the `SE` keyword in the `PRINT`
  subcommand has not been given.

- The `DISTRIBUTION` parameter determines the method to be used when
  estimating the area under the curve.  There are two possibilities,
  viz: `FREE` and `NEGEXPO`.  The `FREE` method uses a non-parametric
  estimate, and the `NEGEXPO` method a bi-negative exponential
  distribution estimate.  The `NEGEXPO` method should only be used
  when the number of positive actual states is equal to the number of
  negative actual states.  The default is `FREE`.

- The `CUTOFF` parameter is for compatibility and is ignored.

The `MISSING` subcommand determines whether user missing values are to
be included or excluded in the analysis.  The default behaviour is to
exclude them.  Cases are excluded on a listwise basis; if any of the
variables in `VAR_LIST` or if the variable `STATE_VAR` is missing,
then the entire case is excluded.

