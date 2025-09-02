# WEIGHT

```
WEIGHT BY VAR_NAME.
WEIGHT OFF.
```

`WEIGHT` assigns cases varying weights, changing the frequency
distribution of the active dataset.  Execution of `WEIGHT` is delayed
until data have been read.

If a variable name is specified, `WEIGHT` causes the values of that
variable to be used as weighting factors for subsequent statistical
procedures.  Use of keyword `BY` is optional but recommended.
Weighting variables must be numeric.  [Scratch
variables](../language/datasets/scratch-variables.md) may not be
variables](../language/datasets/scratch-variables.md) may not be
used for weighting.

When `OFF` is specified, subsequent statistical procedures weight all
cases equally.

A positive integer weighting factor `W` on a case yields the same
statistical output as would replicating the case `W` times.  A
weighting factor of 0 is treated for statistical purposes as if the
case did not exist in the input.  Weighting values need not be
integers, but negative and system-missing values for the weighting
variable are interpreted as weighting factors of 0.  User-missing
values are not treated specially.

When `WEIGHT` is specified after [`TEMPORARY`](temporary.md), it
affects only the next procedure.

`WEIGHT` does not cause cases in the active dataset to be replicated
in memory.

## Example

One could define a dataset containing an inventory of stock items.  It
would be reasonable to use a string variable for a description of the
item, and a numeric variable for the number in stock, like in the
syntax below.

```
data list notable list /item (a16) quantity (f8.0).
begin   data
nuts    345
screws  10034
washers 32012
bolts   876
end data.

echo 'Unweighted frequency table'.
frequencies /variables = item /format=dfreq.

weight by quantity.

echo 'Weighted frequency table'.
frequencies /variables = item /format=dfreq.
```

One analysis which most surely would be of interest is the relative
amounts or each item in stock.  However without setting a weight
variable, [`FREQUENCIES`](frequencies.md) does not tell
us what we want to know, since there is only one case for each stock
item.  The output below shows the difference between the weighted and
unweighted frequency tables.

```
Unweighted frequency table

                          item
┌─────────────┬─────────┬───────┬─────────────┬──────────────────┐
│             │Frequency│Percent│Valid Percent│Cumulative Percent│
├─────────────┼─────────┼───────┼─────────────┼──────────────────┤
│Valid bolts  │        1│  25.0%│        25.0%│             25.0%│
│      nuts   │        1│  25.0%│        25.0%│             50.0%│
│      screws │        1│  25.0%│        25.0%│             75.0%│
│      washers│        1│  25.0%│        25.0%│            100.0%│
├─────────────┼─────────┼───────┼─────────────┼──────────────────┤
│Total        │        4│ 100.0%│             │                  │
└─────────────┴─────────┴───────┴─────────────┴──────────────────┘

Weighted frequency table

                          item
┌─────────────┬─────────┬───────┬─────────────┬──────────────────┐
│             │Frequency│Percent│Valid Percent│Cumulative Percent│
├─────────────┼─────────┼───────┼─────────────┼──────────────────┤
│Valid washers│    32012│  74.0%│        74.0%│             74.0%│
│      screws │    10034│  23.2%│        23.2%│             97.2%│
│      bolts  │      876│   2.0%│         2.0%│             99.2%│
│      nuts   │      345│    .8%│          .8%│            100.0%│
├─────────────┼─────────┼───────┼─────────────┼──────────────────┤
│Total        │    43267│ 100.0%│             │                  │
└─────────────┴─────────┴───────┴─────────────┴──────────────────┘
```
