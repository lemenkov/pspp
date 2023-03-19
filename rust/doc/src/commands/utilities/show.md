# SHOW

```
SHOW
        [ALL]
        [BLANKS]
        [CC]
        [CCA]
        [CCB]
        [CCC]
        [CCD]
        [CCE]
        [COPYING]
        [DECIMAL]
        [DIRECTORY]
        [ENVIRONMENT]
        [FORMAT]
        [FUZZBITS]
        [LENGTH]
        [MEXPAND]
        [MPRINT]
        [MITERATE]
        [MNEST]
        [MXERRS]
        [MXLOOPS]
        [MXWARNS]
        [N]
        [SCOMPRESSION]
        [SYSTEM]
        [TEMPDIR]
        [UNDEFINED]
        [VERSION]
        [WARRANTY]
        [WEIGHT]
        [WIDTH]
```

`SHOW` displays PSPP's settings and status.  Parameters that can be
changed using [`SET`](set.md), can be examined using `SHOW` using the
subcommand with the same name.  `SHOW` supports the following
additional subcommands:

* `ALL`  
  Show all settings.
* `CC`  
  Show all custom currency settings (`CCA` through `CCE`).
* `DIRECTORY`  
  Shows the current working directory.
* `ENVIRONMENT`  
  Shows the operating system details.
* `N`  
  Reports the number of cases in the active dataset.  The reported
  number is not weighted.  If no dataset is defined, then `Unknown`
  is reported.
* `SYSTEM`  
  Shows information about how PSPP was built.  This information is
  useful in bug reports.
* `TEMPDIR`  
  Shows the path of the directory where temporary files are stored.
* `VERSION`  
  Shows the version of this installation of PSPP.
* `WARRANTY`  
  Show details of the lack of warranty for PSPP.
* `COPYING` or `LICENSE`  
  Display the terms of [PSPP's copyright licence](../../license.md).

Specifying `SHOW` without any subcommands is equivalent to `SHOW
ALL`.

