# LOGISTIC REGRESSION

```
LOGISTIC REGRESSION [VARIABLES =] DEPENDENT_VAR WITH PREDICTORS
     [/CATEGORICAL = CATEGORICAL_PREDICTORS]
     [{/NOCONST | /ORIGIN | /NOORIGIN }]
     [/PRINT = [SUMMARY] [DEFAULT] [CI(CONFIDENCE)] [ALL]]
     [/CRITERIA = [BCON(MIN_DELTA)] [ITERATE(MAX_INTERATIONS)]
                  [LCON(MIN_LIKELIHOOD_DELTA)] [EPS(MIN_EPSILON)]
                  [CUT(CUT_POINT)]]
     [/MISSING = {INCLUDE|EXCLUDE}]
```

Bivariate Logistic Regression is used when you want to explain a
dichotomous dependent variable in terms of one or more predictor
variables.

The minimum command is
```
LOGISTIC REGRESSION y WITH x1 x2 ... xN.
```

Here, `y` is the dependent variable, which must be dichotomous and
`x1` through `xN` are the predictor variables whose coefficients the
procedure estimates.

By default, a constant term is included in the model.  Hence, the
full model is $${\bf y} = b_0 + b_1 {\bf x_1} + b_2 {\bf x_2} + \dots +
b_n {\bf x_n}.$$

Predictor variables which are categorical in nature should be listed
on the `/CATEGORICAL` subcommand.  Simple variables as well as
interactions between variables may be listed here.

If you want a model without the constant term b_0, use the keyword
`/ORIGIN`.  `/NOCONST` is a synonym for `/ORIGIN`.

An iterative Newton-Raphson procedure is used to fit the model.  The
`/CRITERIA` subcommand is used to specify the stopping criteria of the
procedure, and other parameters.  The value of `CUT_POINT` is used in the
classification table.  It is the threshold above which predicted values
are considered to be 1.  Values of `CUT_POINT` must lie in the range
\[0,1\].  During iterations, if any one of the stopping criteria are
satisfied, the procedure is considered complete.  The stopping criteria
are:

- The number of iterations exceeds `MAX_ITERATIONS`.  The default value
  of `MAX_ITERATIONS` is 20.
- The change in the all coefficient estimates are less than
  `MIN_DELTA`.  The default value of `MIN_DELTA` is 0.001.
- The magnitude of change in the likelihood estimate is less than
  `MIN_LIKELIHOOD_DELTA`.  The default value of `MIN_LIKELIHOOD_DELTA`
  is zero.  This means that this criterion is disabled.
- The differential of the estimated probability for all cases is less
  than `MIN_EPSILON`.  In other words, the probabilities are close to
  zero or one.  The default value of `MIN_EPSILON` is 0.00000001.

The `PRINT` subcommand controls the display of optional statistics.
Currently there is one such option, `CI`, which indicates that the
confidence interval of the odds ratio should be displayed as well as its
value.  `CI` should be followed by an integer in parentheses, to
indicate the confidence level of the desired confidence interval.

The `MISSING` subcommand determines the handling of missing
variables.  If `INCLUDE` is set, then user-missing values are included
in the calculations, but system-missing values are not.  If `EXCLUDE` is
set, which is the default, user-missing values are excluded as well as
system-missing values.  This is the default.

