# Data Screening and Transformation

Once data has been entered, it is often desirable, or even necessary,
to transform it in some way before performing analysis upon it.  At
the very least, it's good practice to check for errors.

## Identifying incorrect data

Data from real sources is rarely error free.  PSPP has a number of
procedures which can be used to help identify data which might be
incorrect.

The [`DESCRIPTIVES`](../../commands/descriptives.md) command is used
to generate simple linear statistics for a dataset.  It is also useful
for identifying potential problems in the data.  The example file
`physiology.sav` contains a number of physiological measurements of a
sample of healthy adults selected at random.  However, the data entry
clerk made a number of mistakes when entering the data.  The following
example illustrates the use of `DESCRIPTIVES` to screen this data and
identify the erroneous values:

```
PSPP> get file='/usr/local/share/pspp/examples/physiology.sav'.
PSPP> descriptives sex, weight, height.
```

For this example, PSPP produces the following output:

```
                  Descriptive Statistics
┌─────────────────────┬──┬───────┬───────┬───────┬───────┐
│                     │ N│  Mean │Std Dev│Minimum│Maximum│
├─────────────────────┼──┼───────┼───────┼───────┼───────┤
│Sex of subject       │40│    .45│    .50│Male   │Female │
│Weight in kilograms  │40│  72.12│  26.70│  ─55.6│   92.1│
│Height in millimeters│40│1677.12│ 262.87│    179│   1903│
│Valid N (listwise)   │40│       │       │       │       │
│Missing N (listwise) │ 0│       │       │       │       │
└─────────────────────┴──┴───────┴───────┴───────┴───────┘
```


The most interesting column in the output is the minimum value.  The
weight variable has a minimum value of less than zero, which is clearly
erroneous.  Similarly, the height variable's minimum value seems to be
very low.  In fact, it is more than 5 standard deviations from the mean,
and is a seemingly bizarre height for an adult person.

We can look deeper into these discrepancies by issuing an additional
`EXAMINE` command:

```
PSPP> examine height, weight /statistics=extreme(3).
```

This command produces the following additional output (in part):

```
                   Extreme Values
┌───────────────────────────────┬───────────┬─────┐
│                               │Case Number│Value│
├───────────────────────────────┼───────────┼─────┤
│Height in millimeters Highest 1│         14│ 1903│
│                              2│         15│ 1884│
│                              3│         12│ 1802│
│                     ──────────┼───────────┼─────┤
│                      Lowest  1│         30│  179│
│                              2│         31│ 1598│
│                              3│         28│ 1601│
├───────────────────────────────┼───────────┼─────┤
│Weight in kilograms   Highest 1│         13│ 92.1│
│                              2│          5│ 92.1│
│                              3│         17│ 91.7│
│                     ──────────┼───────────┼─────┤
│                      Lowest  1│         38│─55.6│
│                              2│         39│ 54.5│
│                              3│         33│ 55.4│
└───────────────────────────────┴───────────┴─────┘
```

From this new output, you can see that the lowest value of height is 179
(which we suspect to be erroneous), but the second lowest is 1598 which
we know from `DESCRIPTIVES` is within 1 standard deviation from the
mean.  Similarly, the lowest value of weight is negative, but its second
lowest value is plausible.  This suggests that the two extreme values
are outliers and probably represent data entry errors.

The output also identifies the case numbers for each extreme value,
so we can see that cases 30 and 38 are the ones with the erroneous
values.

## Dealing with suspicious data

If possible, suspect data should be checked and re-measured.  However,
this may not always be feasible, in which case the researcher may
decide to disregard these values.  PSPP has a feature for [missing
values](../../language/basics/missing-values.md), whereby data can
assume the special value 'SYSMIS', and will be disregarded in future
analysis.  You can set the two suspect values to the `SYSMIS` value
using the [`RECODE`](../../commands/recode.md) command.

```
PSPP> recode height (179 = SYSMIS).
PSPP> recode weight (LOWEST THRU 0 = SYSMIS).
```

The first command says that for any observation which has a height value
of 179, that value should be changed to the SYSMIS value.  The second
command says that any weight values of zero or less should be changed to
SYSMIS. From now on, they will be ignored in analysis.

If you now re-run the `DESCRIPTIVES` or `EXAMINE` commands from the
previous section, you will see a data summary with more plausible
parameters.  You will also notice that the data summaries indicate the
two missing values.

## Inverting negatively coded variables

Data entry errors are not the only reason for wanting to recode data.
The sample file `hotel.sav` comprises data gathered from a customer
satisfaction survey of clients at a particular hotel.  The following
commands load the file and display its variables and associated data:

```
PSPP> get file='/usr/local/share/pspp/examples/hotel.sav'.
PSPP> display dictionary.
```

It yields the following output:

```
                                   Variables
┌────┬────────┬─────────────┬────────────┬─────┬─────┬─────────┬──────┬───────┐
│    │        │             │ Measurement│     │     │         │ Print│ Write │
│Name│Position│    Label    │    Level   │ Role│Width│Alignment│Format│ Format│
├────┼────────┼─────────────┼────────────┼─────┼─────┼─────────┼──────┼───────┤
│v1  │       1│I am         │Ordinal     │Input│    8│Right    │F8.0  │F8.0   │
│    │        │satisfied    │            │     │     │         │      │       │
│    │        │with the     │            │     │     │         │      │       │
│    │        │level of     │            │     │     │         │      │       │
│    │        │service      │            │     │     │         │      │       │
│v2  │       2│The value for│Ordinal     │Input│    8│Right    │F8.0  │F8.0   │
│    │        │money was    │            │     │     │         │      │       │
│    │        │good         │            │     │     │         │      │       │
│v3  │       3│The staff    │Ordinal     │Input│    8│Right    │F8.0  │F8.0   │
│    │        │were slow in │            │     │     │         │      │       │
│    │        │responding   │            │     │     │         │      │       │
│v4  │       4│My concerns  │Ordinal     │Input│    8│Right    │F8.0  │F8.0   │
│    │        │were dealt   │            │     │     │         │      │       │
│    │        │with in an   │            │     │     │         │      │       │
│    │        │efficient    │            │     │     │         │      │       │
│    │        │manner       │            │     │     │         │      │       │
│v5  │       5│There was too│Ordinal     │Input│    8│Right    │F8.0  │F8.0   │
│    │        │much noise in│            │     │     │         │      │       │
│    │        │the rooms    │            │     │     │         │      │       │
└────┴────────┴─────────────┴────────────┴─────┴─────┴─────────┴──────┴───────┘

                              Value Labels
┌────────────────────────────────────────────────────┬─────────────────┐
│Variable Value                                      │      Label      │
├────────────────────────────────────────────────────┼─────────────────┤
│I am satisfied with the level of service           1│Strongly Disagree│
│                                                   2│Disagree         │
│                                                   3│No Opinion       │
│                                                   4│Agree            │
│                                                   5│Strongly Agree   │
├────────────────────────────────────────────────────┼─────────────────┤
│The value for money was good                       1│Strongly Disagree│
│                                                   2│Disagree         │
│                                                   3│No Opinion       │
│                                                   4│Agree            │
│                                                   5│Strongly Agree   │
├────────────────────────────────────────────────────┼─────────────────┤
│The staff were slow in responding                  1│Strongly Disagree│
│                                                   2│Disagree         │
│                                                   3│No Opinion       │
│                                                   4│Agree            │
│                                                   5│Strongly Agree   │
├────────────────────────────────────────────────────┼─────────────────┤
│My concerns were dealt with in an efficient manner 1│Strongly Disagree│
│                                                   2│Disagree         │
│                                                   3│No Opinion       │
│                                                   4│Agree            │
│                                                   5│Strongly Agree   │
├────────────────────────────────────────────────────┼─────────────────┤
│There was too much noise in the rooms              1│Strongly Disagree│
│                                                   2│Disagree         │
│                                                   3│No Opinion       │
│                                                   4│Agree            │
│                                                   5│Strongly Agree   │
└────────────────────────────────────────────────────┴─────────────────┘
```

The output shows that all of the variables v1 through v5 are measured
on a 5 point Likert scale, with 1 meaning "Strongly disagree" and 5
meaning "Strongly agree".  However, some of the questions are positively
worded (v1, v2, v4) and others are negatively worded (v3, v5).  To
perform meaningful analysis, we need to recode the variables so that
they all measure in the same direction.  We could use the `RECODE`
command, with syntax such as:

```
recode v3 (1 = 5) (2 = 4) (4 = 2) (5 = 1).
```

However an easier and more elegant way uses the
[`COMPUTE`](../../commands/compute.md) command.  Since the variables
are Likert variables in the range (1 ... 5), subtracting their value
from 6 has the effect of inverting them:

```
compute VAR = 6 - VAR.
```

The following section uses this technique to recode the
variables v3 and v5.  After applying `COMPUTE` for both variables, all
subsequent commands will use the inverted values.

## Testing data consistency

A sensible check to perform on survey data is the calculation of
reliability.  This gives the statistician some confidence that the
questionnaires have been completed thoughtfully.  If you examine the
labels of variables v1, v3 and v4, you will notice that they ask very
similar questions.  One would therefore expect the values of these
variables (after recoding) to closely follow one another, and we can
test that with the [`RELIABILITY`](../../commands/reliability.md)
command.  The following example shows a PSPP session where the user
recodes negatively scaled variables and then requests reliability
statistics for v1, v3, and v4.

```
PSPP> get file='/usr/local/share/pspp/examples/hotel.sav'.
PSPP> compute v3 = 6 - v3.
PSPP> compute v5 = 6 - v5.
PSPP> reliability v1, v3, v4.
```

This yields the following output:

```
Scale: ANY

Case Processing Summary
┌────────┬──┬───────┐
│Cases   │ N│Percent│
├────────┼──┼───────┤
│Valid   │17│ 100.0%│
│Excluded│ 0│    .0%│
│Total   │17│ 100.0%│
└────────┴──┴───────┘

    Reliability Statistics
┌────────────────┬──────────┐
│Cronbach's Alpha│N of Items│
├────────────────┼──────────┤
│             .81│         3│
└────────────────┴──────────┘
```

As a rule of thumb, many statisticians consider a value of Cronbach's
Alpha of 0.7 or higher to indicate reliable data.

Here, the value is 0.81, which suggests a high degree of reliability
among variables v1, v3 and v4, so the data and the recoding that we
performed are vindicated.

## Testing for normality

Many statistical tests rely upon certain properties of the data.  One
common property, upon which many linear tests depend, is that of
normality -- the data must have been drawn from a normal distribution.
It is necessary then to ensure normality before deciding upon the test
procedure to use.  One way to do this uses the `EXAMINE` command.

In the following example, a researcher was examining the failure
rates of equipment produced by an engineering company.  The file
`repairs.sav` contains the mean time between failures (mtbf) of some
items of equipment subject to the study.  Before performing linear
analysis on the data, the researcher wanted to ascertain that the data
is normally distributed.

```
PSPP> get file='/usr/local/share/pspp/examples/repairs.sav'.
PSPP> examine mtbf /statistics=descriptives.
```

This produces the following output:

```
                                  Descriptives
┌──────────────────────────────────────────────────────────┬─────────┬────────┐
│                                                          │         │  Std.  │
│                                                          │Statistic│  Error │
├──────────────────────────────────────────────────────────┼─────────┼────────┤
│Mean time between        Mean                             │     8.78│    1.10│
│failures (months)       ──────────────────────────────────┼─────────┼────────┤
│                         95% Confidence Interval Lower    │     6.53│        │
│                         for Mean                Bound    │         │        │
│                                                 Upper    │    11.04│        │
│                                                 Bound    │         │        │
│                        ──────────────────────────────────┼─────────┼────────┤
│                         5% Trimmed Mean                  │     8.20│        │
│                        ──────────────────────────────────┼─────────┼────────┤
│                         Median                           │     8.29│        │
│                        ──────────────────────────────────┼─────────┼────────┤
│                         Variance                         │    36.34│        │
│                        ──────────────────────────────────┼─────────┼────────┤
│                         Std. Deviation                   │     6.03│        │
│                        ──────────────────────────────────┼─────────┼────────┤
│                         Minimum                          │     1.63│        │
│                        ──────────────────────────────────┼─────────┼────────┤
│                         Maximum                          │    26.47│        │
│                        ──────────────────────────────────┼─────────┼────────┤
│                         Range                            │    24.84│        │
│                        ──────────────────────────────────┼─────────┼────────┤
│                         Interquartile Range              │     6.03│        │
│                        ──────────────────────────────────┼─────────┼────────┤
│                         Skewness                         │     1.65│     .43│
│                        ──────────────────────────────────┼─────────┼────────┤
│                         Kurtosis                         │     3.41│     .83│
└──────────────────────────────────────────────────────────┴─────────┴────────┘
```

A normal distribution has a skewness and kurtosis of zero.  The
skewness of mtbf in the output above makes it clear that the mtbf
figures have a lot of positive skew and are therefore not drawn from a
normally distributed variable.  Positive skew can often be compensated
for by applying a logarithmic transformation, as in the following
continuation of the example:

```
PSPP> compute mtbf_ln = ln (mtbf).
PSPP> examine mtbf_ln /statistics=descriptives.
```

which produces the following additional output:

```
                                Descriptives
┌────────────────────────────────────────────────────┬─────────┬──────────┐
│                                                    │Statistic│Std. Error│
├────────────────────────────────────────────────────┼─────────┼──────────┤
│mtbf_ln Mean                                        │     1.95│       .13│
│       ─────────────────────────────────────────────┼─────────┼──────────┤
│        95% Confidence Interval for Mean Lower Bound│     1.69│          │
│                                         Upper Bound│     2.22│          │
│       ─────────────────────────────────────────────┼─────────┼──────────┤
│        5% Trimmed Mean                             │     1.96│          │
│       ─────────────────────────────────────────────┼─────────┼──────────┤
│        Median                                      │     2.11│          │
│       ─────────────────────────────────────────────┼─────────┼──────────┤
│        Variance                                    │      .49│          │
│       ─────────────────────────────────────────────┼─────────┼──────────┤
│        Std. Deviation                              │      .70│          │
│       ─────────────────────────────────────────────┼─────────┼──────────┤
│        Minimum                                     │      .49│          │
│       ─────────────────────────────────────────────┼─────────┼──────────┤
│        Maximum                                     │     3.28│          │
│       ─────────────────────────────────────────────┼─────────┼──────────┤
│        Range                                       │     2.79│          │
│       ─────────────────────────────────────────────┼─────────┼──────────┤
│        Interquartile Range                         │      .88│          │
│       ─────────────────────────────────────────────┼─────────┼──────────┤
│        Skewness                                    │     ─.37│       .43│
│       ─────────────────────────────────────────────┼─────────┼──────────┤
│        Kurtosis                                    │      .01│       .83│
└────────────────────────────────────────────────────┴─────────┴──────────┘
```

The `COMPUTE` command in the first line above performs the logarithmic
transformation: ``` compute mtbf_ln = ln (mtbf).  ``` Rather than
redefining the existing variable, this use of `COMPUTE` defines a new
variable mtbf_ln which is the natural logarithm of mtbf.  The final
command in this example calls `EXAMINE` on this new variable.  The
results show that both the skewness and kurtosis for mtbf_ln are very
close to zero.  This provides some confidence that the mtbf_ln
variable is normally distributed and thus safe for linear analysis.
In the event that no suitable transformation can be found, then it
would be worth considering an appropriate non-parametric test instead
of a linear one.  See [`NPAR TESTS`](../../commands/npar-tests.md),
for information about non-parametric tests.

