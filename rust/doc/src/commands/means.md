# MEANS

```
MEANS [TABLES =]
      {VAR_LIST}
        [ BY {VAR_LIST} [BY {VAR_LIST} [BY {VAR_LIST} ... ]]]

      [ /{VAR_LIST}
         [ BY {VAR_LIST} [BY {VAR_LIST} [BY {VAR_LIST} ... ]]] ]

      [/CELLS = [MEAN] [COUNT] [STDDEV] [SEMEAN] [SUM] [MIN] [MAX] [RANGE]
        [VARIANCE] [KURT] [SEKURT]
        [SKEW] [SESKEW] [FIRST] [LAST]
        [HARMONIC] [GEOMETRIC]
        [DEFAULT]
        [ALL]
        [NONE] ]

      [/MISSING = [INCLUDE] [DEPENDENT]]
```

You can use the `MEANS` command to calculate the arithmetic mean and
similar statistics, either for the dataset as a whole or for categories
of data.

The simplest form of the command is
```
MEANS V.
```
which calculates the mean, count and standard deviation for V.  If you
specify a grouping variable, for example
```
MEANS V BY G.
```
then the means, counts and standard deviations for V after having been
grouped by G are calculated.  Instead of the mean, count and standard
deviation, you could specify the statistics in which you are interested:
```
MEANS X Y BY G
      /CELLS = HARMONIC SUM MIN.
```
This example calculates the harmonic mean, the sum and the minimum
values of X and Y grouped by G.

The `CELLS` subcommand specifies which statistics to calculate.  The
available statistics are:
- `MEAN`: The arithmetic mean.
- `COUNT`: The count of the values.
- `STDDEV`: The standard deviation.
- `SEMEAN`: The standard error of the mean.
- `SUM`: The sum of the values.
- `MIN`: The minimum value.
- `MAX`: The maximum value.
- `RANGE`: The difference between the maximum and minimum values.
- `VARIANCE`: The variance.
- `FIRST`: The first value in the category.
- `LAST`: The last value in the category.
- `SKEW`: The skewness.
- `SESKEW`: The standard error of the skewness.
- `KURT`: The kurtosis
- `SEKURT`: The standard error of the kurtosis.
- `HARMONIC`: The harmonic mean.
- `GEOMETRIC`: The geometric mean.

In addition, three special keywords are recognized:
- `DEFAULT`: This is the same as `MEAN COUNT STDDEV`.
- `ALL`: All of the above statistics are calculated.
- `NONE`: No statistics are calculated (only a summary is shown).

More than one "table" can be specified in a single command.  Each
table is separated by a `/`.  For example

```
     MEANS TABLES =
           c d e BY x
           /a b BY x y
           /f BY y BY z.
```

has three tables (the `TABLE =` is optional).  The first table has
three dependent variables `c`, `d`, and `e` and a single categorical
variable `x`.  The second table has two dependent variables `a` and
`b`, and two categorical variables `x` and `y`.  The third table has a
single dependent variable `f` and a categorical variable formed by the
combination of `y` and `Z`.

By default values are omitted from the analysis only if missing
values (either system missing or user missing) for any of the variables
directly involved in their calculation are encountered.  This behaviour
can be modified with the `/MISSING` subcommand.  Three options are
possible: `TABLE`, `INCLUDE` and `DEPENDENT`.

`/MISSING = INCLUDE` says that user missing values, either in the
dependent variables or in the categorical variables should be taken at
their face value, and not excluded.

`/MISSING = DEPENDENT` says that user missing values, in the
dependent variables should be taken at their face value, however cases
which have user missing values for the categorical variables should be
omitted from the calculation.

## Example

The dataset in `repairs.sav` contains the mean time between failures
(mtbf) for a sample of artifacts produced by different factories and
trialed under different operating conditions.  Since there are four
combinations of categorical variables, by simply looking at the list
of data, it would be hard to how the scores vary for each category.
The syntax below shows one way of tabulating the mtbf in a way which
is easier to understand.

```
get file='repairs.sav'.

means tables = mtbf
      by factory by environment.
```

The results are shown below.  The figures shown indicate the mean,
standard deviation and number of samples in each category.  These
figures however do not indicate whether the results are statistically
significant.  For that, you would need to use the procedures `ONEWAY`,
`GLM` or `T-TEST` depending on the hypothesis being tested.

```
                    Case Processing Summary
┌────────────────────────────┬───────────────────────────────┐
│                            │             Cases             │
│                            ├──────────┬─────────┬──────────┤
│                            │ Included │ Excluded│   Total  │
│                            ├──┬───────┼─┬───────┼──┬───────┤
│                            │ N│Percent│N│Percent│ N│Percent│
├────────────────────────────┼──┼───────┼─┼───────┼──┼───────┤
│mtbf * factory * environment│30│ 100.0%│0│    .0%│30│ 100.0%│
└────────────────────────────┴──┴───────┴─┴───────┴──┴───────┘

                                Report
┌────────────────────────────────────────────┬─────┬──┬──────────────┐
│Manufacturing facility Operating Environment│ Mean│ N│Std. Deviation│
├────────────────────────────────────────────┼─────┼──┼──────────────┤
│0                      Temperate            │ 7.26│ 9│          2.57│
│                       Tropical             │ 7.47│ 7│          2.68│
│                       Total                │ 7.35│16│          2.53│
├────────────────────────────────────────────┼─────┼──┼──────────────┤
│1                      Temperate            │13.38│ 6│          7.77│
│                       Tropical             │ 8.20│ 8│          8.39│
│                       Total                │10.42│14│          8.26│
├────────────────────────────────────────────┼─────┼──┼──────────────┤
│Total                  Temperate            │ 9.71│15│          5.91│
│                       Tropical             │ 7.86│15│          6.20│
│                       Total                │ 8.78│30│          6.03│
└────────────────────────────────────────────┴─────┴──┴──────────────┘
```

PSPP does not limit the number of variables for which you can
calculate statistics, nor number of categorical variables per layer,
nor the number of layers.  However, running `MEANS` on a large number
of variables, or with categorical variables containing a large number
of distinct values, may result in an extremely large output, which
will not be easy to interpret.  So you should consider carefully which
variables to select for participation in the analysis.

