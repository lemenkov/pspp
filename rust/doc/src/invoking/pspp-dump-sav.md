# Invoking `pspp-dump-sav`

`pspp-dump-sav` is a command-line utility accompanying PSPP.  It is not
installed by default, so it may be missing from your PSPP installation.
It reads one or more SPSS system files and prints their contents.  The
output format is useful for debugging system file readers and writers
and for discovering how to interpret unknown or poorly understood
records.  End users may find the output useful for providing the PSPP
developers information about system files that PSPP does not accurately
read.

Synopsis:

```
pspp-dump-sav [-d[MAXCASES] | --data[=MAXCASES]] FILE...

pspp-dump-sav --help | -h

pspp-dump-sav --version | -v
```

The following options are accepted:

* `-d[MAXCASES]`  
  `--data[=MAXCASES]`  
  By default, `pspp-dump-sav` does not print any of the data in a
  system file, only the file headers.  Specify this option to print
  the data as well.  If MAXCASES is specified, then it limits the
  number of cases printed.

* `-h`, `--help`  
  Prints a usage message on stdout and exits.

* `-v`, `--version`  
  Prints version information on stdout and exits.

Some errors that prevent files from being interpreted successfully
cause `pspp-dump-sav` to exit without reading any additional files
given on the command line.

