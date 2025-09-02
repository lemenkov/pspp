# SORT CASES

```
SORT CASES BY VAR_LIST[({D|A}] [ VAR_LIST[({D|A}] ] ...
```

`SORT CASES` sorts the active dataset by the values of one or more
variables.

Specify `BY` and a list of variables to sort by.  By default,
variables are sorted in ascending order.  To override sort order,
specify `(D)` or `(DOWN)` after a list of variables to get descending
order, or `(A)` or `(UP)` for ascending order.  These apply to all the
listed variables up until the preceding `(A)`, `(D)`, `(UP)` or
`(DOWN)`.

`SORT CASES` performs a stable sort, meaning that records with equal
values of the sort variables have the same relative order before and
after sorting.  Thus, re-sorting an already sorted file does not
affect the ordering of cases.

`SORT CASES` is a procedure.  It causes the data to be read.

`SORT CASES` attempts to sort the entire active dataset in main
memory.  If workspace is exhausted, it falls back to a merge sort
algorithm which creates numerous temporary files.

`SORT CASES` may not be specified following `TEMPORARY`.

## Example

In the syntax below, the data from the file `physiology.sav` is sorted
by two variables, viz sex in descending order and temperature in
ascending order.

```
get file='physiology.sav'.
sort cases by sex (D) temperature(A).
list.
```

   In the output below, you can see that all the cases with a sex of
`1` (female) appear before those with a sex of `0` (male).  This is
because they have been sorted in descending order.  Within each sex,
the data is sorted on the temperature variable, this time in ascending
order.

```
           Data List
┌───┬──────┬──────┬───────────┐
│sex│height│weight│temperature│
├───┼──────┼──────┼───────────┤
│  1│  1606│  56.1│      34.56│
│  1│   179│  56.3│      35.15│
│  1│  1609│  55.4│      35.46│
│  1│  1606│  56.0│      36.06│
│  1│  1607│  56.3│      36.26│
│  1│  1604│  56.0│      36.57│
│  1│  1604│  56.6│      36.81│
│  1│  1606│  56.3│      36.88│
│  1│  1604│  57.8│      37.32│
│  1│  1598│  55.6│      37.37│
│  1│  1607│  55.9│      37.84│
│  1│  1605│  54.5│      37.86│
│  1│  1603│  56.1│      38.80│
│  1│  1604│  58.1│      38.85│
│  1│  1605│  57.7│      38.98│
│  1│  1709│  55.6│      39.45│
│  1│  1604│ -55.6│      39.72│
│  1│  1601│  55.9│      39.90│
│  0│  1799│  90.3│      32.59│
│  0│  1799│  89.0│      33.61│
│  0│  1799│  90.6│      34.04│
│  0│  1801│  90.5│      34.42│
│  0│  1802│  87.7│      35.03│
│  0│  1793│  90.1│      35.11│
│  0│  1801│  92.1│      35.98│
│  0│  1800│  89.5│      36.10│
│  0│  1645│  92.1│      36.68│
│  0│  1698│  90.2│      36.94│
│  0│  1800│  89.6│      37.02│
│  0│  1800│  88.9│      37.03│
│  0│  1801│  88.9│      37.12│
│  0│  1799│  90.4│      37.33│
│  0│  1903│  91.5│      37.52│
│  0│  1799│  90.9│      37.53│
│  0│  1800│  91.0│      37.60│
│  0│  1799│  90.4│      37.68│
│  0│  1801│  91.7│      38.98│
│  0│  1801│  90.9│      39.03│
│  0│  1799│  89.3│      39.77│
│  0│  1884│  88.6│      39.97│
└───┴──────┴──────┴───────────┘
```

`SORT CASES` affects only the active file.  It does not have any
effect upon the `physiology.sav` file itself.  For that, you would
have to rewrite the file using the [`SAVE`](save.md) command.
