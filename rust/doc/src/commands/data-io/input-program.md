# INPUT PROGRAM…END INPUT PROGRAM

```
INPUT PROGRAM.
... input commands ...
END INPUT PROGRAM.
```

   `INPUT PROGRAM`...`END INPUT PROGRAM` specifies a complex input
program.  By placing data input commands within `INPUT PROGRAM`, PSPP
programs can take advantage of more complex file structures than
available with only `DATA LIST`.

   The first sort of extended input program is to simply put multiple
`DATA LIST` commands within the `INPUT PROGRAM`.  This will cause all of
the data files to be read in parallel.  Input will stop when end of file
is reached on any of the data files.

   Transformations, such as conditional and looping constructs, can also
be included within `INPUT PROGRAM`.  These can be used to combine input
from several data files in more complex ways.  However, input will still
stop when end of file is reached on any of the data files.

   To prevent `INPUT PROGRAM` from terminating at the first end of
file, use the `END` subcommand on `DATA LIST`.  This subcommand takes
a variable name, which should be a numeric [scratch
variable](../../language/datasets/scratch-variables.md).  (It need not
be a scratch variable but otherwise the results can be surprising.)
The value of this variable is set to 0 when reading the data file, or
1 when end of file is encountered.

   Two additional commands are useful in conjunction with `INPUT
PROGRAM`.  `END CASE` is the first.  Normally each loop through the
`INPUT PROGRAM` structure produces one case.  `END CASE` controls
exactly when cases are output.  When `END CASE` is used, looping from
the end of `INPUT PROGRAM` to the beginning does not cause a case to be
output.

   `END FILE` is the second.  When the `END` subcommand is used on `DATA
LIST`, there is no way for the `INPUT PROGRAM` construct to stop
looping, so an infinite loop results.  `END FILE`, when executed, stops
the flow of input data and passes out of the `INPUT PROGRAM` structure.

   `INPUT PROGRAM` must contain at least one `DATA LIST` or `END FILE`
command.

## Example 1: Read two files in parallel to the end of the shorter

The following example reads variable `X` from file `a.txt` and
variable `Y` from file `b.txt`.  If one file is shorter than the other
then the extra data in the longer file is ignored.

```
INPUT PROGRAM.
    DATA LIST NOTABLE FILE='a.txt'/X 1-10.
    DATA LIST NOTABLE FILE='b.txt'/Y 1-10.
END INPUT PROGRAM.
LIST.
```

## Example 2: Read two files in parallel, supplementing the shorter

The following example also reads variable `X` from `a.txt` and
variable `Y` from `b.txt`.  If one file is shorter than the other then
it continues reading the longer to its end, setting the other variable
to system-missing.

```
INPUT PROGRAM.
    NUMERIC #A #B.

    DO IF NOT #A.
        DATA LIST NOTABLE END=#A FILE='a.txt'/X 1-10.
    END IF.
    DO IF NOT #B.
        DATA LIST NOTABLE END=#B FILE='b.txt'/Y 1-10.
    END IF.
    DO IF #A AND #B.
        END FILE.
    END IF.
    END CASE.
END INPUT PROGRAM.
LIST.
```

## Example 3: Concatenate two files (version 1)

The following example reads data from file `a.txt`, then from `b.txt`,
and concatenates them into a single active dataset.

```
INPUT PROGRAM.
    NUMERIC #A #B.

    DO IF #A.
        DATA LIST NOTABLE END=#B FILE='b.txt'/X 1-10.
        DO IF #B.
            END FILE.
        ELSE.
            END CASE.
        END IF.
    ELSE.
        DATA LIST NOTABLE END=#A FILE='a.txt'/X 1-10.
        DO IF NOT #A.
            END CASE.
        END IF.
    END IF.
END INPUT PROGRAM.
LIST.
```

## Example 4: Concatenate two files (version 2)

This is another way to do the same thing as Example 3.

```
INPUT PROGRAM.
    NUMERIC #EOF.

    LOOP IF NOT #EOF.
        DATA LIST NOTABLE END=#EOF FILE='a.txt'/X 1-10.
        DO IF NOT #EOF.
            END CASE.
        END IF.
    END LOOP.

    COMPUTE #EOF = 0.
    LOOP IF NOT #EOF.
        DATA LIST NOTABLE END=#EOF FILE='b.txt'/X 1-10.
        DO IF NOT #EOF.
            END CASE.
        END IF.
    END LOOP.

    END FILE.
END INPUT PROGRAM.
LIST.
```

## Example 5: Generate random variates

The follows example creates a dataset that consists of 50 random
variates between 0 and 10.

```
INPUT PROGRAM.
    LOOP #I=1 TO 50.
        COMPUTE X=UNIFORM(10).
        END CASE.
    END LOOP.
    END FILE.
END INPUT PROGRAM.
LIST /FORMAT=NUMBERED.
```
