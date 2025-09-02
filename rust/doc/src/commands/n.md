# N OF CASES

```
N [OF CASES] NUM_OF_CASES [ESTIMATED].
```

`N OF CASES` limits the number of cases processed by any procedures
that follow it in the command stream.  `N OF CASES 100`, for example,
tells PSPP to disregard all cases after the first 100.

When `N OF CASES` is specified after [`TEMPORARY`](temporary.md), it
affects only the next procedure.  Otherwise, cases beyond the limit
specified are not processed by any later procedure.

If the limit specified on `N OF CASES` is greater than the number of
cases in the active dataset, it has no effect.

When `N OF CASES` is used along with `SAMPLE` or `SELECT IF`, the
case limit is applied to the cases obtained after sampling or case
selection, regardless of how `N OF CASES` is placed relative to `SAMPLE`
or `SELECT IF` in the command file.  Thus, the commands `N OF CASES 100`
and `SAMPLE .5` both randomly sample approximately half of the active
dataset's cases, then select the first 100 of those sampled, regardless
of their order in the command file.

`N OF CASES` with the `ESTIMATED` keyword gives an estimated number of
cases before `DATA LIST` or another command to read in data.
`ESTIMATED` never limits the number of cases processed by procedures.
PSPP currently does not use case count estimates.

