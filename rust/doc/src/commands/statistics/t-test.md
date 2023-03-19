# T-TEST

```
T-TEST
        /MISSING={ANALYSIS,LISTWISE} {EXCLUDE,INCLUDE}
        /CRITERIA=CI(CONFIDENCE)


(One Sample mode.)
        TESTVAL=TEST_VALUE
        /VARIABLES=VAR_LIST


(Independent Samples mode.)
        GROUPS=var(VALUE1 [, VALUE2])
        /VARIABLES=VAR_LIST


(Paired Samples mode.)
        PAIRS=VAR_LIST [WITH VAR_LIST [(PAIRED)] ]
```

The `T-TEST` procedure outputs tables used in testing hypotheses
about means.  It operates in one of three modes:
- [One Sample mode](#one-sample-mode).
- [Independent Samples mode](#independent-samples-mode).
- [Paired Samples mode](#paired-samples-mode).

Each of these modes are described in more detail below.  There are two
optional subcommands which are common to all modes.

The `/CRITERIA` subcommand tells PSPP the confidence interval used in
the tests.  The default value is 0.95.

The `MISSING` subcommand determines the handling of missing
variables.  If `INCLUDE` is set, then user-missing values are included
in the calculations, but system-missing values are not.  If `EXCLUDE` is
set, which is the default, user-missing values are excluded as well as
system-missing values.  This is the default.

If `LISTWISE` is set, then the entire case is excluded from analysis
whenever any variable specified in the `/VARIABLES`, `/PAIRS` or
`/GROUPS` subcommands contains a missing value.  If `ANALYSIS` is set,
then missing values are excluded only in the analysis for which they
would be needed.  This is the default.

## One Sample Mode

The `TESTVAL` subcommand invokes the One Sample mode.  This mode is used
to test a population mean against a hypothesized mean.  The value given
to the `TESTVAL` subcommand is the value against which you wish to test.
In this mode, you must also use the `/VARIABLES` subcommand to tell PSPP
which variables you wish to test.

### Example

A researcher wishes to know whether the weight of persons in a
population is different from the national average.  The samples are
drawn from the population under investigation and recorded in the file
`physiology.sav`.  From the Department of Health, she knows that the
national average weight of healthy adults is 76.8kg.  Accordingly the
`TESTVAL` is set to 76.8.  The null hypothesis therefore is that the
mean average weight of the population from which the sample was drawn is
76.8kg.

   As previously noted, one sample in the dataset contains a weight
value which is clearly incorrect.  So this is excluded from the
analysis using the `SELECT` command.

```
GET FILE='physiology.sav'.

SELECT IF (weight > 0).

T-TEST TESTVAL = 76.8
   /VARIABLES = weight.
```

The output below shows that the mean of our sample differs from the
test value by -1.40kg.  However the significance is very high (0.610).
So one cannot reject the null hypothesis, and must conclude there is
not enough evidence to suggest that the mean weight of the persons in
our population is different from 76.8kg.

```
                 One─Sample Statistics
┌───────────────────┬──┬─────┬──────────────┬─────────┐
│                   │ N│ Mean│Std. Deviation│S.E. Mean│
├───────────────────┼──┼─────┼──────────────┼─────────┤
│Weight in kilograms│39│75.40│         17.08│     2.73│
└───────────────────┴──┴─────┴──────────────┴─────────┘

                                One─Sample Test
┌──────────────┬──────────────────────────────────────────────────────────────┐
│              │                       Test Value = 76.8                      │
│              ├────┬──┬────────────┬────────────┬────────────────────────────┤
│              │    │  │            │            │ 95% Confidence Interval of │
│              │    │  │            │            │       the Difference       │
│              │    │  │  Sig. (2─  │    Mean    ├──────────────┬─────────────┤
│              │  t │df│   tailed)  │ Difference │     Lower    │    Upper    │
├──────────────┼────┼──┼────────────┼────────────┼──────────────┼─────────────┤
│Weight in     │─.51│38│        .610│       ─1.40│         ─6.94│         4.13│
│kilograms     │    │  │            │            │              │             │
└──────────────┴────┴──┴────────────┴────────────┴──────────────┴─────────────┘
```

## Independent Samples Mode

The `GROUPS` subcommand invokes Independent Samples mode or 'Groups'
mode.  This mode is used to test whether two groups of values have the
same population mean.  In this mode, you must also use the `/VARIABLES`
subcommand to tell PSPP the dependent variables you wish to test.

The variable given in the `GROUPS` subcommand is the independent
variable which determines to which group the samples belong.  The values
in parentheses are the specific values of the independent variable for
each group.  If the parentheses are omitted and no values are given, the
default values of 1.0 and 2.0 are assumed.

If the independent variable is numeric, it is acceptable to specify
only one value inside the parentheses.  If you do this, cases where the
independent variable is greater than or equal to this value belong to
the first group, and cases less than this value belong to the second
group.  When using this form of the `GROUPS` subcommand, missing values
in the independent variable are excluded on a listwise basis, regardless
of whether `/MISSING=LISTWISE` was specified.

### Example

A researcher wishes to know whether within a population, adult males are
taller than adult females.  The samples are drawn from the population
under investigation and recorded in the file `physiology.sav`.

As previously noted, one sample in the dataset contains a height value
which is clearly incorrect.  So this is excluded from the analysis
using the `SELECT` command.

```
get file='physiology.sav'.

select if (height >= 200).

t-test /variables = height
       /groups = sex(0,1).
```

The null hypothesis is that both males and females are on average of
equal height.


From the output, shown below, one can clearly see that the _sample_
mean height is greater for males than for females.  However in order
to see if this is a significant result, one must consult the T-Test
table.

The T-Test table contains two rows; one for use if the variance of
the samples in each group may be safely assumed to be equal, and the
second row if the variances in each group may not be safely assumed to
be equal.

In this case however, both rows show a 2-tailed significance less
than 0.001 and one must therefore reject the null hypothesis and
conclude that within the population the mean height of males and of
females are unequal.

```
                         Group Statistics
┌────────────────────────────┬──┬───────┬──────────────┬─────────┐
│                      Group │ N│  Mean │Std. Deviation│S.E. Mean│
├────────────────────────────┼──┼───────┼──────────────┼─────────┤
│Height in millimeters Male  │22│1796.49│         49.71│    10.60│
│                      Female│17│1610.77│         25.43│     6.17│
└────────────────────────────┴──┴───────┴──────────────┴─────────┘

                          Independent Samples Test
┌─────────────────────┬──────────┬──────────────────────────────────────────
│                     │ Levene's │
│                     │ Test for │
│                     │ Equality │
│                     │    of    │
│                     │ Variances│              T─Test for Equality of Means
│                     ├────┬─────┼─────┬─────┬───────┬──────────┬──────────┐
│                     │    │     │     │     │       │          │          │
│                     │    │     │     │     │       │          │          │
│                     │    │     │     │     │       │          │          │
│                     │    │     │     │     │       │          │          │
│                     │    │     │     │     │  Sig. │          │          │
│                     │    │     │     │     │  (2─  │   Mean   │Std. Error│
│                     │  F │ Sig.│  t  │  df │tailed)│Difference│Difference│
├─────────────────────┼────┼─────┼─────┼─────┼───────┼──────────┼──────────┤
│Height in   Equal    │ .97│ .331│14.02│37.00│   .000│    185.72│     13.24│
│millimeters variances│    │     │     │     │       │          │          │
│            assumed  │    │     │     │     │       │          │          │
│            Equal    │    │     │15.15│32.71│   .000│    185.72│     12.26│
│            variances│    │     │     │     │       │          │          │
│            not      │    │     │     │     │       │          │          │
│            assumed  │    │     │     │     │       │          │          │
└─────────────────────┴────┴─────┴─────┴─────┴───────┴──────────┴──────────┘

┌─────────────────────┬─────────────┐
│                     │             │
│                     │             │
│                     │             │
│                     │             │
│                     │             │
│                     ├─────────────┤
│                     │     95%     │
│                     │  Confidence │
│                     │ Interval of │
│                     │     the     │
│                     │  Difference │
│                     ├──────┬──────┤
│                     │ Lower│ Upper│
├─────────────────────┼──────┼──────┤
│Height in   Equal    │158.88│212.55│
│millimeters variances│      │      │
│            assumed  │      │      │
│            Equal    │160.76│210.67│
│            variances│      │      │
│            not      │      │      │
│            assumed  │      │      │
└─────────────────────┴──────┴──────┘
```

## Paired Samples Mode

The `PAIRS` subcommand introduces Paired Samples mode.  Use this mode
when repeated measures have been taken from the same samples.  If the
`WITH` keyword is omitted, then tables for all combinations of variables
given in the `PAIRS` subcommand are generated.  If the `WITH` keyword is
given, and the `(PAIRED)` keyword is also given, then the number of
variables preceding `WITH` must be the same as the number following it.
In this case, tables for each respective pair of variables are
generated.  In the event that the `WITH` keyword is given, but the
`(PAIRED)` keyword is omitted, then tables for each combination of
variable preceding `WITH` against variable following `WITH` are
generated.

