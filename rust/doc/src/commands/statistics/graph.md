# GRAPH

```
GRAPH
        /HISTOGRAM [(NORMAL)]= VAR
        /SCATTERPLOT [(BIVARIATE)] = VAR1 WITH VAR2 [BY VAR3]
        /BAR = {SUMMARY-FUNCTION(VAR1) | COUNT-FUNCTION} BY VAR2 [BY VAR3]
        [ /MISSING={LISTWISE, VARIABLE} [{EXCLUDE, INCLUDE}] ]
       [{NOREPORT,REPORT}]
```

`GRAPH` produces a graphical plots of data.  Only one of the
subcommands `HISTOGRAM`, `BAR` or `SCATTERPLOT` can be specified, i.e.
only one plot can be produced per call of `GRAPH`.  The `MISSING` is
optional.

## Scatterplot

The subcommand `SCATTERPLOT` produces an xy plot of the data.  `GRAPH`
uses `VAR3`, if specified, to determine the colours and/or
markers for the plot.  The following is an example for producing a
scatterplot.

```
GRAPH
        /SCATTERPLOT = height WITH weight BY gender.
```

This example produces a scatterplot where `height` is plotted versus
`weight`.  Depending on the value of `gender`, the colour of the
datapoint is different.  With this plot it is possible to analyze
gender differences for `height` versus `weight` relation.

## Histogram

The subcommand `HISTOGRAM` produces a histogram.  Only one variable is
allowed for the histogram plot.  The keyword `NORMAL` may be specified
in parentheses, to indicate that the ideal normal curve should be
superimposed over the histogram.  For an alternative method to produce
histograms, see [EXAMINE](examine.md).  The following example produces
a histogram plot for the variable `weight`.

```
GRAPH
        /HISTOGRAM = weight.
```

## Bar Chart

The subcommand `BAR` produces a bar chart.  This subcommand requires
that a `COUNT-FUNCTION` be specified (with no arguments) or a
`SUMMARY-FUNCTION` with a variable VAR1 in parentheses.  Following the
summary or count function, the keyword `BY` should be specified and
then a catagorical variable, `VAR2`.  The values of `VAR2` determine
the labels of the bars to be plotted.  A second categorical variable
`VAR3` may be specified, in which case a clustered (grouped) bar chart
is produced.

Valid count functions are:

* `COUNT`  
  The weighted counts of the cases in each category.
* `PCT`  
  The weighted counts of the cases in each category expressed as a
  percentage of the total weights of the cases.
* `CUFREQ`  
  The cumulative weighted counts of the cases in each category.
* `CUPCT`  
  The cumulative weighted counts of the cases in each category
  expressed as a percentage of the total weights of the cases.

The summary function is applied to `VAR1` across all cases in each
category.  The recognised summary functions are:

* `SUM`  
  The sum.
* `MEAN`  
  The arithmetic mean.
* `MAXIMUM`  
  The maximum value.
* `MINIMUM`  
  The minimum value.

The following examples assume a dataset which is the results of a
survey.  Each respondent has indicated annual income, their sex and city
of residence.  One could create a bar chart showing how the mean income
varies between of residents of different cities, thus:
```
GRAPH  /BAR  = MEAN(INCOME) BY CITY.
```

This can be extended to also indicate how income in each city differs
between the sexes.
```
GRAPH  /BAR  = MEAN(INCOME) BY CITY BY SEX.
```

One might also want to see how many respondents there are from each
city.  This can be achieved as follows:
```
GRAPH  /BAR  = COUNT BY CITY.
```

The [FREQUENCIES](frequencies.md) and [CROSSTABS](crosstabs.md)
commands can also produce bar charts.

