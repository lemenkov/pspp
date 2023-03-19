# DO IF…END IF

```
DO IF condition.
        ...
[ELSE IF condition.
        ...
]...
[ELSE.
        ...]
END IF.
```

`DO IF` allows one of several sets of transformations to be executed,
depending on user-specified conditions.

If the specified boolean expression evaluates as true, then the block
of code following `DO IF` is executed.  If it evaluates as missing,
then none of the code blocks is executed.  If it is false, then the
boolean expression on the first `ELSE IF`, if present, is tested in
turn, with the same rules applied.  If all expressions evaluate to
false, then the `ELSE` code block is executed, if it is present.

When `DO IF` or `ELSE IF` is specified following
[`TEMPORARY`](../../commands/selection/temporary.md), the
[`LAG`](../../language/expressions/functions/miscellaneous.md)
function may not be used.

