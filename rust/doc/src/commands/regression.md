# REGRESSION

The `REGRESSION` procedure fits linear models to data via least-squares
estimation.  The procedure is appropriate for data which satisfy those
assumptions typical in linear regression:

- The data set contains \\(n\\) observations of a dependent variable,
  say \\(y_1,...,y_n\\), and \\(n\\) observations of one or more
  explanatory variables.  Let \\(x_{11}, x_{12}, ..., x_{1n}\\\)
  denote the \\(n\\) observations of the first explanatory variable;
  \\(x_{21},...,x_{2n}\\) denote the \\(n\\) observations of the
  second explanatory variable; \\(x_{k1},...,x_{kn}\\) denote the
  \\(n\\) observations of the kth explanatory variable.

- The dependent variable \\(y\\) has the following relationship to the
  explanatory variables: \\(y_i = b_0 + b_1 x_{1i} + ... + b_k
  x_{ki} + z_i\\) where \\(b_0, b_1, ..., b_k\\) are unknown
  coefficients, and \\(z_1,...,z_n\\) are independent, normally
  distributed "noise" terms with mean zero and common variance.  The
  noise, or "error" terms are unobserved.  This relationship is called
  the "linear model".

   The `REGRESSION` procedure estimates the coefficients
\\(b_0,...,b_k\\) and produces output relevant to inferences for the
linear model.

## Syntax

```
REGRESSION
        /VARIABLES=VAR_LIST
        /DEPENDENT=VAR_LIST
        /STATISTICS={ALL, DEFAULTS, R, COEFF, ANOVA, BCOV, CI[CONF, TOL]}
        { /ORIGIN | /NOORIGIN }
        /SAVE={PRED, RESID}
```

The `REGRESSION` procedure reads the active dataset and outputs
statistics relevant to the linear model specified by the user.

The `VARIABLES` subcommand, which is required, specifies the list of
variables to be analyzed.  Keyword `VARIABLES` is required.  The
`DEPENDENT` subcommand specifies the dependent variable of the linear
model.  The `DEPENDENT` subcommand is required.  All variables listed
in the `VARIABLES` subcommand, but not listed in the `DEPENDENT`
subcommand, are treated as explanatory variables in the linear model.

All other subcommands are optional:

The `STATISTICS` subcommand specifies which statistics are to be
displayed.  The following keywords are accepted:

* `ALL`  
  All of the statistics below.
* `R`  
  The ratio of the sums of squares due to the model to the total sums
  of squares for the dependent variable.
* `COEFF`  
  A table containing the estimated model coefficients and their
  standard errors.
* `CI (CONF)`  
  This item is only relevant if `COEFF` has also been selected.  It
  specifies that the confidence interval for the coefficients should
  be printed.  The optional value `CONF`, which must be in
  parentheses, is the desired confidence level expressed as a
  percentage.
* `ANOVA`  
  Analysis of variance table for the model.
* `BCOV`  
  The covariance matrix for the estimated model coefficients.
* `TOL`  
  The variance inflation factor and its reciprocal.  This has no
  effect unless `COEFF` is also given.
* `DEFAULT`  
  The same as if `R`, `COEFF`, and `ANOVA` had been selected.  This is
  what you get if the `/STATISTICS` command is not specified, or if it
  is specified without any parameters.

The `ORIGIN` and `NOORIGIN` subcommands are mutually exclusive.
`ORIGIN` indicates that the regression should be performed through the
origin.  You should use this option if, and only if you have reason to
believe that the regression does indeed pass through the origin -- that
is to say, the value b_0 above, is zero.  The default is `NOORIGIN`.

The `SAVE` subcommand causes PSPP to save the residuals or predicted
values from the fitted model to the active dataset.  PSPP will store the
residuals in a variable called `RES1` if no such variable exists, `RES2`
if `RES1` already exists, `RES3` if `RES1` and `RES2` already exist,
etc.  It will choose the name of the variable for the predicted values
similarly, but with `PRED` as a prefix.  When `SAVE` is used, PSPP
ignores `TEMPORARY`, treating temporary transformations as permanent.

## Example

The following PSPP syntax will generate the default output and save the
predicted values and residuals to the active dataset.

```
title 'Demonstrate REGRESSION procedure'.
data list / v0 1-2 (A) v1 v2 3-22 (10).
begin data.
b  7.735648 -23.97588
b  6.142625 -19.63854
a  7.651430 -25.26557
c  6.125125 -16.57090
a  8.245789 -25.80001
c  6.031540 -17.56743
a  9.832291 -28.35977
c  5.343832 -16.79548
a  8.838262 -29.25689
b  6.200189 -18.58219
end data.
list.
regression /variables=v0 v1 v2 /statistics defaults /dependent=v2
           /save pred resid /method=enter.

```
