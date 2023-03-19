# TEMPORARY

```
TEMPORARY.
```

`TEMPORARY` is used to make the effects of transformations following
its execution temporary.  These transformations affect only the
execution of the next procedure or procedure-like command.  Their
effects are not be saved to the active dataset.

The only specification on `TEMPORARY` is the command name.

`TEMPORARY` may not appear within a `DO IF` or `LOOP` construct.  It
may appear only once between procedures and procedure-like commands.

Scratch variables cannot be used following `TEMPORARY`.

## Example

In the syntax below, there are two `COMPUTE` transformation.  One of
them immediately follows a `TEMPORARY` command, and therefore affects
only the next procedure, which in this case is the first
`DESCRIPTIVES` command.

```
data list notable /x 1-2.
begin data.
 2
 4
10
15
20
24
end data.

compute x=x/2.

temporary.
compute x=x+3.

descriptives x.
descriptives x.
```

The data read by the first `DESCRIPTIVES` procedure are 4, 5, 8, 10.5,
13, 15.  The data read by the second `DESCRIPTIVES` procedure are 1,
2, 5, 7.5, 10, 12.  This is because the second `COMPUTE`
transformation has no effect on the second `DESCRIPTIVES` procedure.
You can check these figures in the following output.

```
                Descriptive Statistics
┌────────────────────┬─┬────┬───────┬───────┬───────┐
│                    │N│Mean│Std Dev│Minimum│Maximum│
├────────────────────┼─┼────┼───────┼───────┼───────┤
│x                   │6│9.25│   4.38│      4│     15│
│Valid N (listwise)  │6│    │       │       │       │
│Missing N (listwise)│0│    │       │       │       │
└────────────────────┴─┴────┴───────┴───────┴───────┘

           Descriptive Statistics
┌────────────────────┬─┬────┬───────┬───────┬───────┐
│                    │N│Mean│Std Dev│Minimum│Maximum│
├────────────────────┼─┼────┼───────┼───────┼───────┤
│x                   │6│6.25│   4.38│      1│     12│
│Valid N (listwise)  │6│    │       │       │       │
│Missing N (listwise)│0│    │       │       │       │
└────────────────────┴─┴────┴───────┴───────┴───────┘
```
