# DATASET commands

```
DATASET NAME NAME [WINDOW={ASIS,FRONT}].
DATASET ACTIVATE NAME [WINDOW={ASIS,FRONT}].
DATASET COPY NAME [WINDOW={MINIMIZED,HIDDEN,FRONT}].
DATASET DECLARE NAME [WINDOW={MINIMIZED,HIDDEN,FRONT}].
DATASET CLOSE {NAME,*,ALL}.
DATASET DISPLAY.
```

   The `DATASET` commands simplify use of multiple datasets within a
PSPP session.  They allow datasets to be created and destroyed.  At any
given time, most PSPP commands work with a single dataset, called the
active dataset.

   The `DATASET NAME` command gives the active dataset the specified name,
or if it already had a name, it renames it.  If another dataset already
had the given name, that dataset is deleted.

   The `DATASET ACTIVATE` command selects the named dataset, which must
already exist, as the active dataset.  Before switching the active
dataset, any pending transformations are executed, as if `EXECUTE` had
been specified.  If the active dataset is unnamed before switching, then
it is deleted and becomes unavailable after switching.

   The `DATASET COPY` command creates a new dataset with the specified
name, whose contents are a copy of the active dataset.  Any pending
transformations are executed, as if `EXECUTE` had been specified, before
making the copy.  If a dataset with the given name already exists, it is
replaced.  If the name is the name of the active dataset, then the
active dataset becomes unnamed.

   The `DATASET DECLARE` command creates a new dataset that is
initially "empty," that is, it has no dictionary or data.  If a
dataset with the given name already exists, this has no effect.  The
new dataset can be used with commands that support output to a
dataset, such as. [`AGGREGATE`](../data/aggregate.md).

   The `DATASET CLOSE` command deletes a dataset.  If the active dataset
is specified by name, or if `*` is specified, then the active dataset
becomes unnamed.  If a different dataset is specified by name, then it
is deleted and becomes unavailable.  Specifying `ALL` deletes all datasets
except for the active dataset, which becomes unnamed.

   The `DATASET DISPLAY` command lists all the currently defined datasets.

   Many `DATASET` commands accept an optional `WINDOW` subcommand.  In the
PSPPIRE GUI, the value given for this subcommand influences how the
dataset's window is displayed.  Outside the GUI, the `WINDOW` subcommand
has no effect.  The valid values are:

* `ASIS`  
  Do not change how the window is displayed.  This is the default for
  `DATASET NAME` and `DATASET ACTIVATE`.

* `FRONT`  
  Raise the dataset's window to the top.  Make it the default dataset
  for running syntax.

* `MINIMIZED`  
  Display the window "minimized" to an icon.  Prefer other datasets
  for running syntax.  This is the default for `DATASET COPY` and
  `DATASET DECLARE`.

* `HIDDEN`  
  Hide the dataset's window.  Prefer other datasets for running
  syntax.
