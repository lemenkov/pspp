# INSERT

```
INSERT [FILE=]'FILE_NAME'
   [CD={NO,YES}]
   [ERROR={CONTINUE,STOP}]
   [SYNTAX={BATCH,INTERACTIVE}]
   [ENCODING={LOCALE, 'CHARSET_NAME'}].
```

`INSERT` is similar to [`INCLUDE`](include.md) but more flexible.  It
causes the command processor to read a file as if it were embedded in
the current command file.

If `CD=YES` is specified, then before including the file, the current
directory becomes the directory of the included file.  The default
setting is `CD=NO`.  This directory remains current until it is
changed explicitly (with the `CD` command, or a subsequent `INSERT`
command with the `CD=YES` option).  It does not revert to its original
setting even after the included file is finished processing.

If `ERROR=STOP` is specified, errors encountered in the inserted file
causes processing to immediately cease.  Otherwise processing continues
at the next command.  The default setting is `ERROR=CONTINUE`.

If `SYNTAX=INTERACTIVE` is specified then the syntax contained in the
included file must conform to [interactive syntax
conventions](../../language/basics/syntax-variants.md).  The default
setting is `SYNTAX=BATCH`.

`ENCODING` optionally specifies the character set used by the
included file.  Its argument, which is not case-sensitive, must be in
one of the following forms:

* `LOCALE`  
  The encoding used by the system locale, or as overridden by [`SET
  LOCALE`](../utilities/set.md#locale).  On GNU/Linux and other
  Unix-like systems, environment variables, e.g. `LANG` or `LC_ALL`,
  determine the system locale.

* `'CHARSET_NAME'`  
  An [IANA character set
  name](http://www.iana.org/assignments/character-sets).  Some
  examples are `ASCII` (United States), `ISO-8859-1` (western Europe),
  `EUC-JP` (Japan), and `windows-1252` (Windows).  Not all systems
  support all character sets.

* `Auto,ENCODING`  
  Automatically detects whether a syntax file is encoded in a Unicode
  encoding such as UTF-8, UTF-16, or UTF-32.  If it is not, then PSPP
  generally assumes that the file is encoded in `ENCODING` (an IANA
  character set name).  However, if `ENCODING` is UTF-8, and the
  syntax file is not valid UTF-8, PSPP instead assumes that the file
  is encoded in `windows-1252`.

  For best results, `ENCODING` should be an ASCII-compatible encoding
  (the most common locale encodings are all ASCII-compatible),
  because encodings that are not ASCII compatible cannot be
  automatically distinguished from UTF-8.

* `Auto`  
  `Auto,Locale`  
  Automatic detection, as above, with the default encoding taken from
  the system locale or the setting on `SET LOCALE`.

When `ENCODING` is not specified, the default is taken from the
`--syntax-encoding` command option, if it was specified, and otherwise
it is `Auto`.

