# DOCUMENT

```
DOCUMENT DOCUMENTARY_TEXT.
```

`DOCUMENT` adds one or more lines of descriptive commentary to the
active dataset.  Documents added in this way are saved to system
files.  They can be viewed using `SYSFILE INFO` or [`DISPLAY
DOCUMENTS`](display-documents.md).  They can be removed from the
active dataset with [`DROP DOCUMENTS`](drop-documents.md).

Specify the text of the document following the `DOCUMENT` keyword.  It
is interpreted literally—any quotes or other punctuation marks are
included in the file.  You can extend the documentary text over as
many lines as necessary, including blank lines to separate paragraphs.
Lines are truncated at 80 bytes.  Don't forget to terminate the
command with a dot at the end of a line.  See also [ADD
DOCUMENT](add-document.md).

