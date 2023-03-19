# Hypothesis Testing

One of the most fundamental purposes of statistical analysis is
hypothesis testing.  Researchers commonly need to test hypotheses about
a set of data.  For example, she might want to test whether one set of
data comes from the same distribution as another, or whether the mean of
a dataset significantly differs from a particular value.  This section
presents just some of the possible tests that PSPP offers.

The researcher starts by making a "null hypothesis".  Often this is a
hypothesis which he suspects to be false.  For example, if he suspects
that A is greater than B he will state the null hypothesis as A = B.[^1]

[^1]: This example assumes that it is already proven that B is not
greater than A.

The "p-value" is a recurring concept in hypothesis testing.  It is
the highest acceptable probability that the evidence implying a null
hypothesis is false, could have been obtained when the null hypothesis
is in fact true.  Note that this is not the same as "the probability of
making an error" nor is it the same as "the probability of rejecting a
hypothesis when it is true".

## Testing for differences of means

A common statistical test involves hypotheses about means.  The `T-TEST`
command is used to find out whether or not two separate subsets have the
same mean.

A researcher suspected that the heights and core body temperature of
persons might be different depending upon their sex.  To investigate
this, he posed two null hypotheses based on the data from
`physiology.sav` previously encountered:

   - The mean heights of males and females in the population are equal.

   - The mean body temperature of males and females in the population
     are equal.

For the purposes of the investigation the researcher decided to use a
p-value of 0.05.

In addition to the T-test, the `T-TEST` command also performs the
Levene test for equal variances.  If the variances are equal, then a
more powerful form of the T-test can be used.  However if it is unsafe
to assume equal variances, then an alternative calculation is necessary.
PSPP performs both calculations.

For the height variable, the output shows the significance of the
Levene test to be 0.33 which means there is a 33% probability that the
Levene test produces this outcome when the variances are equal.  Had the
significance been less than 0.05, then it would have been unsafe to
assume that the variances were equal.  However, because the value is
higher than 0.05 the homogeneity of variances assumption is safe and the
"Equal Variances" row (the more powerful test) can be used.  Examining
this row, the two tailed significance for the height t-test is less than
0.05, so it is safe to reject the null hypothesis and conclude that the
mean heights of males and females are unequal.

For the temperature variable, the significance of the Levene test is
0.58 so again, it is safe to use the row for equal variances.  The equal
variances row indicates that the two tailed significance for temperature
is 0.20.  Since this is greater than 0.05 we must reject the null
hypothesis and conclude that there is insufficient evidence to suggest
that the body temperature of male and female persons are different.

   The syntax for this analysis is:

```
PSPP> get file='/usr/local/share/pspp/examples/physiology.sav'.
PSPP> recode height (179 = SYSMIS).
PSPP> t-test group=sex(0,1) /variables = height temperature.
```

PSPP produces the following output for this syntax:

```
                                Group Statistics
┌───────────────────────────────────────────┬──┬───────┬─────────────┬────────┐
│                                           │  │       │     Std.    │  S.E.  │
│                                     Group │ N│  Mean │  Deviation  │  Mean  │
├───────────────────────────────────────────┼──┼───────┼─────────────┼────────┤
│Height in millimeters                Male  │22│1796.49│        49.71│   10.60│
│                                     Female│17│1610.77│        25.43│    6.17│
├───────────────────────────────────────────┼──┼───────┼─────────────┼────────┤
│Internal body temperature in degrees Male  │22│  36.68│         1.95│     .42│
│Celcius                              Female│18│  37.43│         1.61│     .38│
└───────────────────────────────────────────┴──┴───────┴─────────────┴────────┘

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
├─────────────────────┼────┼─────┼─────┼─────┼───────┼──────────┼──────────┤
│Internal    Equal    │ .31│ .581│─1.31│38.00│   .198│      ─.75│       .57│
│body        variances│    │     │     │     │       │          │          │
│temperature assumed  │    │     │     │     │       │          │          │
│in degrees  Equal    │    │     │─1.33│37.99│   .190│      ─.75│       .56│
│Celcius     variances│    │     │     │     │       │          │          │
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
├─────────────────────┼──────┼──────┤
│Internal    Equal    │ ─1.91│   .41│
│body        variances│      │      │
│temperature assumed  │      │      │
│in degrees  Equal    │ ─1.89│   .39│
│Celcius     variances│      │      │
│            not      │      │      │
│            assumed  │      │      │
└─────────────────────┴──────┴──────┘
```

The `T-TEST` command tests for differences of means.  Here, the height
variable's two tailed significance is less than 0.05, so the null
hypothesis can be rejected.  Thus, the evidence suggests there is a
difference between the heights of male and female persons.  However
the significance of the test for the temperature variable is greater
than 0.05 so the null hypothesis cannot be rejected, and there is
insufficient evidence to suggest a difference in body temperature.

## Linear Regression

Linear regression is a technique used to investigate if and how a
variable is linearly related to others.  If a variable is found to be
linearly related, then this can be used to predict future values of that
variable.

In the following example, the service department of the company wanted
to be able to predict the time to repair equipment, in order to
improve the accuracy of their quotations.  It was suggested that the
time to repair might be related to the time between failures and the
duty cycle of the equipment.  The p-value of 0.1 was chosen for this
investigation.  In order to investigate this hypothesis, the
[`REGRESSION`](../../commands/statistics/regression.md) command was
used.  This command not only tests if the variables are related, but
also identifies the potential linear relationship.

A first attempt includes `duty_cycle`:

```
PSPP> get file='/usr/local/share/pspp/examples/repairs.sav'.
PSPP> regression /variables = mtbf duty_cycle /dependent = mttr.
```

This attempt yields the following output (in part):

```
                  Coefficients (Mean time to repair (hours) )
┌────────────────────────┬─────────────────────┬───────────────────┬─────┬────┐
│                        │    Unstandardized   │    Standardized   │     │    │
│                        │     Coefficients    │    Coefficients   │     │    │
│                        ├─────────┬───────────┼───────────────────┤     │    │
│                        │    B    │ Std. Error│        Beta       │  t  │Sig.│
├────────────────────────┼─────────┼───────────┼───────────────────┼─────┼────┤
│(Constant)              │    10.59│       3.11│                .00│ 3.40│.002│
│Mean time between       │     3.02│        .20│                .95│14.88│.000│
│failures (months)       │         │           │                   │     │    │
│Ratio of working to non─│    ─1.12│       3.69│               ─.02│ ─.30│.763│
│working time            │         │           │                   │     │    │
└────────────────────────┴─────────┴───────────┴───────────────────┴─────┴────┘
```

The coefficients in the above table suggest that the formula
\\(\textrm{MTTR} = 9.81 + 3.1 \times \textrm{MTBF} + 1.09 \times
\textrm{DUTY\_CYCLE}\\) can be used to predict the time to repair.
However, the significance value for the `DUTY_CYCLE` coefficient is
very high, which would make this an unsafe predictor.  For this
reason, the test was repeated, but omitting the `duty_cycle` variable:

```
PSPP> regression /variables = mtbf /dependent = mttr.
```

This second try produces the following output (in part):

```
                  Coefficients (Mean time to repair (hours) )
┌───────────────────────┬──────────────────────┬───────────────────┬─────┬────┐
│                       │    Unstandardized    │    Standardized   │     │    │
│                       │     Coefficients     │    Coefficients   │     │    │
│                       ├─────────┬────────────┼───────────────────┤     │    │
│                       │    B    │ Std. Error │        Beta       │  t  │Sig.│
├───────────────────────┼─────────┼────────────┼───────────────────┼─────┼────┤
│(Constant)             │     9.90│        2.10│                .00│ 4.71│.000│
│Mean time between      │     3.01│         .20│                .94│15.21│.000│
│failures (months)      │         │            │                   │     │    │
└───────────────────────┴─────────┴────────────┴───────────────────┴─────┴────┘
```

This time, the significance of all coefficients is no higher than
0.06, suggesting that at the 0.06 level, the formula \\(\textrm{MTTR} = 10.5 +
3.11 \times \textrm{MTBF}\\) is a reliable predictor of the time to repair.

