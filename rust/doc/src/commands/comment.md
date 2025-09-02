# COMMENT

```
Comment commands:
    COMMENT comment text ... .
    *comment text ... .

Comments within a line of syntax:
    FREQUENCIES /VARIABLES=v0 v1 v2.  /* All our categorical variables.
```

`COMMENT` is ignored.  It is used to provide information to the
author and other readers of the PSPP syntax file.

`COMMENT` can extend over any number of lines.  It ends at a dot at
the end of a line or a blank line.  The comment may contain any
characters.

PSPP also supports comments within a line of syntax, introduced with
`/*`.  These comments end at the first `*/` or at the end of the line,
whichever comes first.  A line that contains just this kind of comment
is considered blank and ends the current command.

