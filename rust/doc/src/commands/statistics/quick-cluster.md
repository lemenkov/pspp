# QUICK CLUSTER

```
QUICK CLUSTER VAR_LIST
      [/CRITERIA=CLUSTERS(K) [MXITER(MAX_ITER)] CONVERGE(EPSILON) [NOINITIAL]]
      [/MISSING={EXCLUDE,INCLUDE} {LISTWISE, PAIRWISE}]
      [/PRINT={INITIAL} {CLUSTER}]
      [/SAVE[=[CLUSTER[(MEMBERSHIP_VAR)]] [DISTANCE[(DISTANCE_VAR)]]]
```

The `QUICK CLUSTER` command performs k-means clustering on the
dataset.  This is useful when you wish to allocate cases into clusters
of similar values and you already know the number of clusters.

The minimum specification is `QUICK CLUSTER` followed by the names of
the variables which contain the cluster data.  Normally you will also
want to specify `/CRITERIA=CLUSTERS(K)` where `K` is the number of
clusters.  If this is not specified, then `K` defaults to 2.

If you use `/CRITERIA=NOINITIAL` then a naive algorithm to select the
initial clusters is used.  This will provide for faster execution but
less well separated initial clusters and hence possibly an inferior
final result.

`QUICK CLUSTER` uses an iterative algorithm to select the clusters
centers.  The subcommand `/CRITERIA=MXITER(MAX_ITER)` sets the maximum
number of iterations.  During classification, PSPP will continue
iterating until until `MAX_ITER` iterations have been done or the
convergence criterion (see below) is fulfilled.  The default value of
MAX_ITER is 2.

If however, you specify `/CRITERIA=NOUPDATE` then after selecting the
initial centers, no further update to the cluster centers is done.  In
this case, `MAX_ITER`, if specified, is ignored.

The subcommand `/CRITERIA=CONVERGE(EPSILON)` is used to set the
convergence criterion.  The value of convergence criterion is
`EPSILON` times the minimum distance between the _initial_ cluster
centers.  Iteration stops when the mean cluster distance between one
iteration and the next is less than the convergence criterion.  The
default value of `EPSILON` is zero.

The `MISSING` subcommand determines the handling of missing
variables.  If `INCLUDE` is set, then user-missing values are considered
at their face value and not as missing values.  If `EXCLUDE` is set,
which is the default, user-missing values are excluded as well as
system-missing values.

If `LISTWISE` is set, then the entire case is excluded from the
analysis whenever any of the clustering variables contains a missing
value.  If `PAIRWISE` is set, then a case is considered missing only if
all the clustering variables contain missing values.  Otherwise it is
clustered on the basis of the non-missing values.  The default is
`LISTWISE`.

The `PRINT` subcommand requests additional output to be printed.  If
`INITIAL` is set, then the initial cluster memberships will be printed.
If `CLUSTER` is set, the cluster memberships of the individual cases are
displayed (potentially generating lengthy output).

You can specify the subcommand `SAVE` to ask that each case's cluster
membership and the euclidean distance between the case and its cluster
center be saved to a new variable in the active dataset.  To save the
cluster membership use the `CLUSTER` keyword and to save the distance
use the `DISTANCE` keyword.  Each keyword may optionally be followed by
a variable name in parentheses to specify the new variable which is to
contain the saved parameter.  If no variable name is specified, then
PSPP will create one.

