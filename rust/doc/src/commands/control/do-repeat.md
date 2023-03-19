# DO REPEAT…END REPEAT

```
DO REPEAT dummy_name=expansion....
        ...
END REPEAT [PRINT].

expansion takes one of the following forms:
        var_list
        num_or_range...
        'string'...
        ALL

num_or_range takes one of the following forms:
        number
        num1 TO num2
```

`DO REPEAT` repeats a block of code, textually substituting different
variables, numbers, or strings into the block with each repetition.

Specify a dummy variable name followed by an equals sign (`=`) and
the list of replacements.  Replacements can be a list of existing or new
variables, numbers, strings, or `ALL` to specify all existing variables.
When numbers are specified, runs of increasing integers may be indicated
as `NUM1 TO NUM2`, so that `1 TO 5` is short for `1 2 3 4 5`.

Multiple dummy variables can be specified.  Each variable must have
the same number of replacements.

The code within `DO REPEAT` is repeated as many times as there are
replacements for each variable.  The first time, the first value for
each dummy variable is substituted; the second time, the second value
for each dummy variable is substituted; and so on.

Dummy variable substitutions work like macros.  They take place
anywhere in a line that the dummy variable name occurs.  This includes
command and subcommand names, so command and subcommand names that
appear in the code block should not be used as dummy variable
identifiers.  Dummy variable substitutions do not occur inside quoted
strings, comments, unquoted strings (such as the text on the `TITLE`
or `DOCUMENT` command), or inside `BEGIN DATA`...`END DATA`.

Substitution occurs only on whole words, so that, for example, a dummy
variable `PRINT` would not be substituted into the word `PRINTOUT`.

New variable names used as replacements are not automatically created
as variables, but only if used in the code block in a context that
would create them, e.g. on a `NUMERIC` or `STRING` command or on the
left side of a `COMPUTE` assignment.

Any command may appear within `DO REPEAT`, including nested `DO
REPEAT` commands.  If `INCLUDE` or `INSERT` appears within `DO
REPEAT`, the substitutions do not apply to the included file.

If `PRINT` is specified on `END REPEAT`, the commands after
substitutions are made should be printed to the listing file, prefixed
by a plus sign (`+`).  This feature is not yet implemented.

