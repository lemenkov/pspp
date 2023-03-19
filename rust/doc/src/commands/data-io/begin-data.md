# BEGIN DATA…END DATA

```
BEGIN DATA.
...
END DATA.
```

`BEGIN DATA` and `END DATA` can be used to embed raw ASCII data in a
PSPP syntax file.  [`DATA LIST`](data-list.md) or another input
procedure must be used before `BEGIN DATA`.  `BEGIN DATA` and `END
DATA` must be used together.  `END DATA` must appear by itself on a
single line, with no leading white space and exactly one space between
the words `END` and `DATA`.
