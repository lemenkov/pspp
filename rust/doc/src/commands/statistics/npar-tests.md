# NPAR TESTS

```
NPAR TESTS
     nonparametric test subcommands
     .
     .
     .

     [ /STATISTICS={DESCRIPTIVES} ]

     [ /MISSING={ANALYSIS, LISTWISE} {INCLUDE, EXCLUDE} ]

     [ /METHOD=EXACT [ TIMER [(N)] ] ]
```

`NPAR TESTS` performs nonparametric tests.  Nonparametric tests make
very few assumptions about the distribution of the data.  One or more
tests may be specified by using the corresponding subcommand.  If the
`/STATISTICS` subcommand is also specified, then summary statistics
are produces for each variable that is the subject of any test.

Certain tests may take a long time to execute, if an exact figure is
required.  Therefore, by default asymptotic approximations are used
unless the subcommand `/METHOD=EXACT` is specified.  Exact tests give
more accurate results, but may take an unacceptably long time to
perform.  If the `TIMER` keyword is used, it sets a maximum time,
after which the test is abandoned, and a warning message printed.  The
time, in minutes, should be specified in parentheses after the `TIMER`
keyword.  If the `TIMER` keyword is given without this figure, then a
default value of 5 minutes is used.

<!-- toc -->

## Binomial test

```
     [ /BINOMIAL[(P)]=VAR_LIST[(VALUE1[, VALUE2)] ] ]
```

The `/BINOMIAL` subcommand compares the observed distribution of a
dichotomous variable with that of a binomial distribution.  The variable
`P` specifies the test proportion of the binomial distribution.  The
default value of 0.5 is assumed if `P` is omitted.

If a single value appears after the variable list, then that value is
used as the threshold to partition the observed values.  Values less
than or equal to the threshold value form the first category.  Values
greater than the threshold form the second category.

If two values appear after the variable list, then they are used as
the values which a variable must take to be in the respective category.
Cases for which a variable takes a value equal to neither of the
specified values, take no part in the test for that variable.

If no values appear, then the variable must assume dichotomous
values.  If more than two distinct, non-missing values for a variable
under test are encountered then an error occurs.

If the test proportion is equal to 0.5, then a two tailed test is
reported.  For any other test proportion, a one tailed test is reported.
For one tailed tests, if the test proportion is less than or equal to
the observed proportion, then the significance of observing the observed
proportion or more is reported.  If the test proportion is more than the
observed proportion, then the significance of observing the observed
proportion or less is reported.  That is to say, the test is always
performed in the observed direction.

PSPP uses a very precise approximation to the gamma function to
compute the binomial significance.  Thus, exact results are reported
even for very large sample sizes.

## Chi-square Test

```
     [ /CHISQUARE=VAR_LIST[(LO,HI)] [/EXPECTED={EQUAL|F1, F2 ... FN}] ]
```

The `/CHISQUARE` subcommand produces a chi-square statistic for the
differences between the expected and observed frequencies of the
categories of a variable.  Optionally, a range of values may appear
after the variable list.  If a range is given, then non-integer values
are truncated, and values outside the specified range are excluded
from the analysis.

The `/EXPECTED` subcommand specifies the expected values of each
category.  There must be exactly one non-zero expected value, for each
observed category, or the `EQUAL` keyword must be specified.  You may
use the notation `N*F` to specify N consecutive expected categories all
taking a frequency of F.  The frequencies given are proportions, not
absolute frequencies.  The sum of the frequencies need not be 1.  If no
`/EXPECTED` subcommand is given, then equal frequencies are expected.

### Chi-square Example

A researcher wishes to investigate whether there are an equal number of
persons of each sex in a population.  The sample chosen for invesigation
is that from the `physiology.sav` dataset.  The null hypothesis for the
test is that the population comprises an equal number of males and
females.  The analysis is performed as shown below:

```
get file='physiology.sav'.

npar test
     /chisquare=sex.
```


There is only one test variable: sex.  The other variables in
the dataset are ignored.

In the output, shown below, the summary box shows that in the sample,
there are more males than females.  However the significance of
chi-square result is greater than 0.05—the most commonly accepted
p-value—and therefore there is not enough evidence to reject the null
hypothesis and one must conclude that the evidence does not indicate
that there is an imbalance of the sexes in the population.

```
             Sex of subject
┌──────┬──────────┬──────────┬────────┐
│Value │Observed N│Expected N│Residual│
├──────┼──────────┼──────────┼────────┤
│Male  │        22│     20.00│    2.00│
│Female│        18│     20.00│   ─2.00│
│Total │        40│          │        │
└──────┴──────────┴──────────┴────────┘

         Test Statistics
┌──────────────┬──────────┬──┬───────────┐
│              │Chi─square│df│Asymp. Sig.│
├──────────────┼──────────┼──┼───────────┤
│Sex of subject│       .40│ 1│       .527│
└──────────────┴──────────┴──┴───────────┘
```

## Cochran Q Test

```
     [ /COCHRAN = VAR_LIST ]
```

The Cochran Q test is used to test for differences between three or
more groups.  The data for `VAR_LIST` in all cases must assume exactly
two distinct values (other than missing values).

The value of Q is displayed along with its asymptotic significance
based on a chi-square distribution.

## Friedman Test

```
     [ /FRIEDMAN = VAR_LIST ]
```

The Friedman test is used to test for differences between repeated
measures when there is no indication that the distributions are normally
distributed.

A list of variables which contain the measured data must be given.
The procedure prints the sum of ranks for each variable, the test
statistic and its significance.

## Kendall's W Test

```
     [ /KENDALL = VAR_LIST ]
```

The Kendall test investigates whether an arbitrary number of related
samples come from the same population.  It is identical to the
Friedman test except that the additional statistic W, Kendall's
Coefficient of Concordance is printed.  It has the range \[0,1\]—a value
of zero indicates no agreement between the samples whereas a value of
unity indicates complete agreement.

## Kolmogorov-Smirnov Test

```
     [ /KOLMOGOROV-SMIRNOV ({NORMAL [MU, SIGMA], UNIFORM [MIN, MAX], POISSON [LAMBDA], EXPONENTIAL [SCALE] }) = VAR_LIST ]
```

The one sample Kolmogorov-Smirnov subcommand is used to test whether
or not a dataset is drawn from a particular distribution.  Four
distributions are supported: normal, uniform, Poisson and
exponential.

Ideally you should provide the parameters of the distribution against
which you wish to test the data.  For example, with the normal
distribution the mean (`MU`) and standard deviation (`SIGMA`) should
be given; with the uniform distribution, the minimum (`MIN`) and
maximum (`MAX`) value should be provided.  However, if the parameters
are omitted they are imputed from the data.  Imputing the parameters
reduces the power of the test so should be avoided if possible.

In the following example, two variables `score` and `age` are tested to
see if they follow a normal distribution with a mean of 3.5 and a
standard deviation of 2.0.
```
  NPAR TESTS
        /KOLMOGOROV-SMIRNOV (NORMAL 3.5 2.0) = score age.
```
If the variables need to be tested against different distributions,
then a separate subcommand must be used.  For example the following
syntax tests `score` against a normal distribution with mean of 3.5 and
standard deviation of 2.0 whilst `age` is tested against a normal
distribution of mean 40 and standard deviation 1.5.
```
  NPAR TESTS
        /KOLMOGOROV-SMIRNOV (NORMAL 3.5 2.0) = score
        /KOLMOGOROV-SMIRNOV (NORMAL 40 1.5) =  age.
```

The abbreviated subcommand `K-S` may be used in place of
`KOLMOGOROV-SMIRNOV`.

## Kruskal-Wallis Test

```
     [ /KRUSKAL-WALLIS = VAR_LIST BY VAR (LOWER, UPPER) ]
```

The Kruskal-Wallis test is used to compare data from an arbitrary
number of populations.  It does not assume normality.  The data to be
compared are specified by `VAR_LIST`.  The categorical variable
determining the groups to which the data belongs is given by `VAR`.
The limits `LOWER` and `UPPER` specify the valid range of `VAR`.  If
`UPPER` is smaller than `LOWER`, the PSPP will assume their values to
be reversed.  Any cases for which `VAR` falls outside `[LOWER, UPPER]`
are ignored.

The mean rank of each group as well as the chi-squared value and
significance of the test are printed.  The abbreviated subcommand `K-W`
may be used in place of `KRUSKAL-WALLIS`.

## Mann-Whitney U Test

```
     [ /MANN-WHITNEY = VAR_LIST BY var (GROUP1, GROUP2) ]
```

The Mann-Whitney subcommand is used to test whether two groups of
data come from different populations.  The variables to be tested should
be specified in `VAR_LIST` and the grouping variable, that determines to
which group the test variables belong, in `VAR`.  `VAR` may be either a
string or an alpha variable.  `GROUP1` and `GROUP2` specify the two values
of VAR which determine the groups of the test data.  Cases for which the
`VAR` value is neither `GROUP1` or `GROUP2` are ignored.

The value of the Mann-Whitney U statistic, the Wilcoxon W, and the
significance are printed.  You may abbreviated the subcommand
`MANN-WHITNEY` to `M-W`.


## McNemar Test

```
     [ /MCNEMAR VAR_LIST [ WITH VAR_LIST [ (PAIRED) ]]]
```

Use McNemar's test to analyse the significance of the difference
between pairs of correlated proportions.

If the `WITH` keyword is omitted, then tests for all combinations of
the listed variables are performed.  If the `WITH` keyword is given, and
the `(PAIRED)` keyword is also given, then the number of variables
preceding `WITH` must be the same as the number following it.  In this
case, tests for each respective pair of variables are performed.  If the
`WITH` keyword is given, but the `(PAIRED)` keyword is omitted, then
tests for each combination of variable preceding `WITH` against variable
following `WITH` are performed.

The data in each variable must be dichotomous.  If there are more
than two distinct variables an error will occur and the test will not be
run.

## Median Test

```
     [ /MEDIAN [(VALUE)] = VAR_LIST BY VARIABLE (VALUE1, VALUE2) ]
```

The median test is used to test whether independent samples come from
populations with a common median.  The median of the populations against
which the samples are to be tested may be given in parentheses
immediately after the `/MEDIAN` subcommand.  If it is not given, the
median is imputed from the union of all the samples.

The variables of the samples to be tested should immediately follow
the `=` sign.  The keyword `BY` must come next, and then the grouping
variable.  Two values in parentheses should follow.  If the first
value is greater than the second, then a 2-sample test is performed
using these two values to determine the groups.  If however, the first
variable is less than the second, then a k sample test is conducted
and the group values used are all values encountered which lie in the
range `[VALUE1,VALUE2]`.

## Runs Test

```
     [ /RUNS ({MEAN, MEDIAN, MODE, VALUE})  = VAR_LIST ]
```

The `/RUNS` subcommand tests whether a data sequence is randomly
ordered.

It works by examining the number of times a variable's value crosses
a given threshold.  The desired threshold must be specified within
parentheses.  It may either be specified as a number or as one of
`MEAN`, `MEDIAN` or `MODE`.  Following the threshold specification comes
the list of variables whose values are to be tested.

The subcommand shows the number of runs, the asymptotic significance
based on the length of the data.

## Sign Test

```
     [ /SIGN VAR_LIST [ WITH VAR_LIST [ (PAIRED) ]]]
```

The `/SIGN` subcommand tests for differences between medians of the
variables listed.  The test does not make any assumptions about the
distribution of the data.

If the `WITH` keyword is omitted, then tests for all combinations of
the listed variables are performed.  If the `WITH` keyword is given, and
the `(PAIRED)` keyword is also given, then the number of variables
preceding `WITH` must be the same as the number following it.  In this
case, tests for each respective pair of variables are performed.  If the
`WITH` keyword is given, but the `(PAIRED)` keyword is omitted, then
tests for each combination of variable preceding `WITH` against variable
following `WITH` are performed.

## Wilcoxon Matched Pairs Signed Ranks Test

```
     [ /WILCOXON VAR_LIST [ WITH VAR_LIST [ (PAIRED) ]]]
```

The `/WILCOXON` subcommand tests for differences between medians of
the variables listed.  The test does not make any assumptions about the
variances of the samples.  It does however assume that the distribution
is symmetrical.

If the `WITH` keyword is omitted, then tests for all combinations of
the listed variables are performed.  If the `WITH` keyword is given, and
the `(PAIRED)` keyword is also given, then the number of variables
preceding `WITH` must be the same as the number following it.  In this
case, tests for each respective pair of variables are performed.  If the
`WITH` keyword is given, but the `(PAIRED)` keyword is omitted, then
tests for each combination of variable preceding `WITH` against variable
following `WITH` are performed.

