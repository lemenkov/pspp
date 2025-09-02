# LOOP…END LOOP

```
LOOP [INDEX_VAR=START TO END [BY INCR]] [IF CONDITION].
        ...
END LOOP [IF CONDITION].
```

`LOOP` iterates a group of commands.  A number of termination options
are offered.

Specify `INDEX_VAR` to make that variable count from one value to
another by a particular increment.  `INDEX_VAR` must be a pre-existing
numeric variable.  `START`, `END`, and `INCR` are numeric
[expressions](../language/expressions/index.md).
[expressions](../language/expressions/index.md).

During the first iteration, `INDEX_VAR` is set to the value of
`START`.  During each successive iteration, `INDEX_VAR` is increased
by the value of `INCR`.  If `END > START`, then the loop terminates
when `INDEX_VAR > END`; otherwise it terminates when `INDEX_VAR <
END`.  If `INCR` is not specified then it defaults to +1 or -1 as
appropriate.

If `END > START` and `INCR < 0`, or if `END < START` and `INCR > 0`,
then the loop is never executed.  `INDEX_VAR` is nevertheless set to
the value of start.

Modifying `INDEX_VAR` within the loop is allowed, but it has no effect
on the value of `INDEX_VAR` in the next iteration.

Specify a boolean expression for the condition on `LOOP` to cause the
loop to be executed only if the condition is true.  If the condition
is false or missing before the loop contents are executed the first
time, the loop contents are not executed at all.

If index and condition clauses are both present on `LOOP`, the index
variable is always set before the condition is evaluated.  Thus, a
condition that makes use of the index variable will always see the index
value to be used in the next execution of the body.

Specify a boolean expression for the condition on `END LOOP` to cause
the loop to terminate if the condition is true after the enclosed code
block is executed.  The condition is evaluated at the end of the loop,
not at the beginning, so that the body of a loop with only a condition
on `END LOOP` will always execute at least once.

If the index clause is not present, then the global
[`MXLOOPS`](set.md#mxloops) setting, which defaults to
40, limits the number of iterations.

[`BREAK`](break.md) also terminates `LOOP` execution.

Loop index variables are by default reset to system-missing from one
case to another, not left, unless a scratch variable is used as index.
When loops are nested, this is usually undesired behavior, which can
be corrected with [`LEAVE`](leave.md) or by using a [scratch
variable](../language/datasets/scratch-variables.md) as the loop
index.

When `LOOP` or `END LOOP` is specified following
[`TEMPORARY`](temporary.md), the
[`LAG`](../language/expressions/functions/miscellaneous.md) function
may not be used.

