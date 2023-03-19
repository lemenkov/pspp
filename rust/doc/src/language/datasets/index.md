# Datasets

PSPP works with data organized into "datasets".  A dataset consists of a
set of "variables", which taken together are said to form a
"dictionary", and one or more "cases", each of which has one value for
each variable.

   At any given time PSPP has exactly one distinguished dataset,
called the "active dataset".  Most PSPP commands work only with the
active dataset.  In addition to the active dataset, PSPP also supports
any number of additional open datasets.  The [`DATASET`
commands](../../commands/data-io/dataset.md) can choose a new active
dataset from among those that are open, as well as create and destroy
datasets.
