# ADD DOCUMENT

```
ADD DOCUMENT
    'line one' 'line two' ... 'last line' .
```

`ADD DOCUMENT` adds one or more lines of descriptive commentary to
the active dataset.  Documents added in this way are saved to system
files.  They can be viewed using `SYSFILE INFO` or `DISPLAY DOCUMENTS`.
They can be removed from the active dataset with `DROP DOCUMENTS`.

Each line of documentary text must be enclosed in quotation marks, and
may not be more than 80 bytes long.  See also
[`DOCUMENT`](document.md).

