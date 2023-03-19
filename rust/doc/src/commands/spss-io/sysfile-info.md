# SYSFILE INFO

```
SYSFILE INFO FILE='FILE_NAME' [ENCODING='ENCODING'].
```

`SYSFILE INFO` reads the dictionary in an SPSS system file, SPSS/PC+
system file, or SPSS portable file, and displays the information in
its dictionary.

Specify a file name or file handle.  `SYSFILE INFO` reads that file
and displays information on its dictionary.

PSPP automatically detects the encoding of string data in the file,
when possible.  The character encoding of old SPSS system files cannot
always be guessed correctly, and SPSS/PC+ system files do not include
any indication of their encoding.  Specify the `ENCODING` subcommand
with an IANA character set name as its string argument to override the
default, or specify `ENCODING='DETECT'` to analyze and report possibly
valid encodings for the system file.  The `ENCODING` subcommand is a
PSPP extension.

`SYSFILE INFO` does not affect the current active dataset.

