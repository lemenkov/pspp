# SET

```
SET

(data input)
        /BLANKS={SYSMIS,'.',number}
        /DECIMAL={DOT,COMMA}
        /FORMAT=FMT_SPEC
        /EPOCH={AUTOMATIC,YEAR}
        /RIB={NATIVE,MSBFIRST,LSBFIRST}

(interaction)
        /MXERRS=MAX_ERRS
        /MXWARNS=MAX_WARNINGS
        /WORKSPACE=WORKSPACE_SIZE

(syntax execution)
        /LOCALE='LOCALE'
        /MXLOOPS=MAX_LOOPS
        /SEED={RANDOM,SEED_VALUE}
        /UNDEFINED={WARN,NOWARN}
        /FUZZBITS=FUZZBITS
        /SCALEMIN=COUNT

(data output)
        /CC{A,B,C,D,E}={'NPRE,PRE,SUF,NSUF','NPRE.PRE.SUF.NSUF'}
        /DECIMAL={DOT,COMMA}
        /FORMAT=FMT_SPEC
        /LEADZERO={ON,OFF}
        /MDISPLAY={TEXT,TABLES}
        /SMALL=NUMBER
        /WIB={NATIVE,MSBFIRST,LSBFIRST}

(output routing)
        /ERRORS={ON,OFF,TERMINAL,LISTING,BOTH,NONE}
        /MESSAGES={ON,OFF,TERMINAL,LISTING,BOTH,NONE}
        /PRINTBACK={ON,OFF,TERMINAL,LISTING,BOTH,NONE}
        /RESULTS={ON,OFF,TERMINAL,LISTING,BOTH,NONE}

(output driver options)
        /HEADERS={NO,YES,BLANK}
        /LENGTH={NONE,N_LINES}
        /WIDTH={NARROW,WIDTH,N_CHARACTERS}
        /TNUMBERS={VALUES,LABELS,BOTH}
        /TVARS={NAMES,LABELS,BOTH}
        /TLOOK={NONE,FILE}

(logging)
        /JOURNAL={ON,OFF} ['FILE_NAME']

(system files)
        /SCOMPRESSION={ON,OFF}

(miscellaneous)
        /SAFER=ON
        /LOCALE='STRING'

(macros)
        /MEXPAND={ON,OFF}
        /MPRINT={ON,OFF}
        /MITERATE=NUMBER
        /MNEST=NUMBER

(settings not yet implemented, but accepted and ignored)
        /BASETEXTDIRECTION={AUTOMATIC,RIGHTTOLEFT,LEFTTORIGHT}
        /BLOCK='C'
        /BOX={'XXX','XXXXXXXXXXX'}
        /CACHE={ON,OFF}
        /CELLSBREAK=NUMBER
        /COMPRESSION={ON,OFF}
        /CMPTRANS={ON,OFF}
        /HEADER={NO,YES,BLANK}
```

`SET` allows the user to adjust several parameters relating to PSPP's
execution.  Since there are many subcommands to this command, its
subcommands are examined in groups.

For subcommands that take boolean values, `ON` and `YES` are
synonymous, as are `OFF` and `NO`, when used as subcommand values.

The data input subcommands affect the way that data is read from data
files.  The data input subcommands are

* `BLANKS`  
  This is the value assigned to an item data item that is empty or
  contains only white space.  An argument of SYSMIS or '.'  causes
  the system-missing value to be assigned to null items.  This is the
  default.  Any real value may be assigned.

* <a name="decimal">`DECIMAL`</a>  
  This value may be set to `DOT` or `COMMA`.  Setting it to `DOT`
  causes the decimal point character to be `.` and the grouping
  character to be `,`.  Setting it to `COMMA` causes the decimal point
  character to be `,` and the grouping character to be `.`.  If the
  setting is `COMMA`, then `,` is not treated as a field separator in
  the [`DATA LIST`](data-list.md) command.  The default
  value is determined from the system locale.

* <a name="format">`FORMAT`</a>  
  Changes the default numeric [input/output
  format](../language/datasets/formats/index.md).  The default is
  initially `F8.2`.

* <a name="epoch">`EPOCH`</a>  
  Specifies the range of years used when a 2-digit year is read from a
  data file or used in a [date construction
  expression](../language/expressions/functions/time-and-date.md#constructing-dates).
  If a 4-digit year is specified for the epoch, then 2-digit years are
  interpreted starting from that year, known as the epoch.  If
  `AUTOMATIC` (the default) is specified, then the epoch begins 69
  years before the current date.

* <a name="rib">`RIB`</a>  
  PSPP extension to set the byte ordering (endianness) used for
  reading data in [`IB` or `PIB`
  format](../language/datasets/formats/binary-and-hex.md#ib-and-pib-formats).  In
  `MSBFIRST` ordering, the most-significant byte appears at the left
  end of a IB or PIB field.  In `LSBFIRST` ordering, the
  least-significant byte appears at the left end.  `NATIVE`, the
  default, is equivalent to `MSBFIRST` or `LSBFIRST` depending on the
  native format of the machine running PSPP.

Interaction subcommands affect the way that PSPP interacts with an
online user.  The interaction subcommands are

* `MXERRS`  
  The maximum number of errors before PSPP halts processing of the
  current command file.  The default is 50.

* `MXWARNS`  
  The maximum number of warnings + errors before PSPP halts
  processing the current command file.  The special value of zero
  means that all warning situations should be ignored.  No warnings
  are issued, except a single initial warning advising you that
  warnings will not be given.  The default value is 100.

Syntax execution subcommands control the way that PSPP commands
execute.  The syntax execution subcommands are

* `LOCALE`  
  Overrides the system locale for the purpose of reading and writing
  syntax and data files.  The argument should be a locale name in the
  general form `LANGUAGE_COUNTRY.ENCODING`, where `LANGUAGE` and
  `COUNTRY` are 2-character language and country abbreviations,
  respectively, and `ENCODING` is an [IANA character set
  name](http://www.iana.org/assignments/character-sets). Example
  locales are `en_US.UTF-8` (UTF-8 encoded English as spoken in the
  United States) and `ja_JP.EUC-JP` (EUC-JP encoded Japanese as spoken
  in Japan).

* <a name="mxloops">`MXLOOPS`</a>  
  The maximum number of iterations for an uncontrolled
  [`LOOP`](loop.md), and for any [loop in the matrix
  language](matrix.md#the-loop-and-break-commands).  The default
  `MXLOOPS` is 40.

* <a name="seed">`SEED`</a>  
  The initial pseudo-random number seed.  Set it to a real number or
  to `RANDOM`, to obtain an initial seed from the current time of day.

* `UNDEFINED`  
  Currently not used.

* <a name="fuzzbits">`FUZZBITS`</a>  
  The maximum number of bits of errors in the least-significant places
  to accept for rounding up a value that is almost halfway between two
  possibilities for rounding with the
  [RND](../language/expressions/functions/mathematical.md#rnd).  The
  default FUZZBITS is 6.

* <a name="scalemin">`SCALEMIN`</a>  
  The minimum number of distinct valid values for PSPP to assume that
  a variable has a scale [measurement
  level](../language/datasets/variables.md#measurement-level).

* `WORKSPACE`  
  The maximum amount of memory (in kilobytes) that PSPP uses to store
  data being processed.  If memory in excess of the workspace size is
  required, then PSPP starts to use temporary files to store the
  data.  Setting a higher value means that procedures run faster, but
  may cause other applications to run slower.  On platforms without
  virtual memory management, setting a very large workspace may cause
  PSPP to abort.

Data output subcommands affect the format of output data.  These
subcommands are

* `CCA`  
  `CCB`  
  `CCC`  
  `CCD`  
  `CCE`  
  Set up [custom currency
  formats](../language/datasets/formats/custom-currency.md).

* `DECIMAL`  
  The default `DOT` setting causes the decimal point character to be
  `.`.  A setting of `COMMA` causes the decimal point character to be
  `,`.

* `FORMAT`  
  Allows the default numeric [input/output
  format](../language/datasets/formats/index.md) to be specified.  The
  default is `F8.2`.

* <a name="leadzero">`LEADZERO`</a>  
  Controls whether numbers with magnitude less than one are displayed
  with a zero before the decimal point.  For example, with `SET
  LEADZERO=OFF`, which is the default, one-half is shown as 0.5, and
  with `SET LEADZERO=ON`, it is shown as .5.  This setting affects
  only the `F`, `COMMA`, and `DOT` formats.

* <a name="mdisplay">`MDISPLAY`</a>  
  Controls how the [`PRINT`](matrix.md#the-print-command) command
  within [`MATRIX`...`END MATRIX`](matrix.md) outputs matrices.  With
  the default `TEXT`, `PRINT` outputs matrices as text.  Change this
  setting to `TABLES` to instead output matrices as pivot tables.

* `SMALL`  
  This controls how PSPP formats small numbers in pivot tables, in
  cases where PSPP does not otherwise have a well-defined format for
  the numbers.  When such a number has a magnitude less than the
  value set here, PSPP formats the number in scientific notation;
  otherwise, it formats it in standard notation.  The default is
  0.0001.  Set a value of 0 to disable scientific notation.

* <a name="wib">`WIB`</a>  
  PSPP extension to set the byte ordering (endianness) used for
  writing data in [`IB` or `PIB`
  format](../language/datasets/formats/binary-and-hex.md#ib-and-pib-formats).
  In `MSBFIRST` ordering, the most-significant byte appears at the
  left end of a IB or PIB field.  In `LSBFIRST` ordering, the
  least-significant byte appears at the left end.  `NATIVE`, the
  default, is equivalent to `MSBFIRST` or `LSBFIRST` depending on the
  native format of the machine running PSPP.

In the PSPP text-based interface, the output routing subcommands
affect where output is sent.  The following values are allowed for each
of these subcommands:

* `OFF`  
  `NONE`  
  Discard this kind of output.

* `TERMINAL`  
  Write this output to the terminal, but not to listing files and
  other output devices.

* `LISTING`  
  Write this output to listing files and other output devices, but
  not to the terminal.

* `ON`  
  `BOTH`  
  Write this type of output to all output devices.

These output routing subcommands are:

* `ERRORS`  
  Applies to error and warning messages.  The default is `BOTH`.

* `MESSAGES`  
  Applies to notes.  The default is `BOTH`.

* `PRINTBACK`  
  Determines whether the syntax used for input is printed back as
  part of the output.  The default is `NONE`.

* `RESULTS`  
  Applies to everything not in one of the above categories, such as
  the results of statistical procedures.  The default is `BOTH`.

These subcommands have no effect on output in the PSPP GUI
environment.

Output driver option subcommands affect output drivers' settings.
These subcommands are:

* `HEADERS`  

* `LENGTH`  

* <a name="width">`WIDTH`</a>  

* `TNUMBERS`  
  The `TNUMBERS` option sets the way in which values are displayed in
  output tables.  The valid settings are `VALUES`, `LABELS` and
  `BOTH`.  If `TNUMBERS` is set to `VALUES`, then all values are
  displayed with their literal value (which for a numeric value is a
  number and for a string value an alphanumeric string).  If
  `TNUMBERS` is set to `LABELS`, then values are displayed using their
  assigned [value labels](value-labels.md), if any.  If the value has
  no label, then the literal value is used for display.  If `TNUMBERS`
  is set to `BOTH`, then values are displayed with both their label
  (if any) and their literal value in parentheses.

* <a name="tvars">`TVARS`</a>  
  The `TVARS` option sets the way in which variables are displayed in
  output tables.  The valid settings are `NAMES`, `LABELS` and `BOTH`.
  If `TVARS` is set to `NAMES`, then all variables are displayed using
  their names.  If `TVARS` is set to `LABELS`, then variables are
  displayed using their [variable label](variable-labels.md), if one
  has been set.  If no label has been set, then the name is used.  If
  `TVARS` is set to `BOTH`, then variables are displayed with both
  their label (if any) and their name in parentheses.

* <a name="tlook">`TLOOK`</a>  
  The `TLOOK` option sets the style used for subsequent table output.
  Specifying `NONE` makes PSPP use the default built-in style.
  Otherwise, specifying FILE makes PSPP search for an `.stt` or
  `.tlo` file in the same way as specifying `--table-look=FILE` the
  PSPP command line (*note Main Options::).

Logging subcommands affect logging of commands executed to external
files.  These subcommands are

* `JOURNAL`  
  `LOG`  
  These subcommands, which are synonyms, control the journal.  The
  default is `ON`, which causes commands entered interactively to be
  written to the journal file.  Commands included from syntax files
  that are included interactively and error messages printed by PSPP
  are also written to the journal file, prefixed by `>`.  `OFF`
  disables use of the journal.

  The journal is named `pspp.jnl` by default.  A different name may
  be specified.

System file subcommands affect the default format of system files
produced by PSPP.  These subcommands are

* <a name="scompression">`SCOMPRESSION</a>`  
  Whether system files created by `SAVE` or `XSAVE` are compressed by
  default.  The default is `ON`.

Security subcommands affect the operations that commands are allowed
to perform.  The security subcommands are

* <a name="safer">`SAFER`</a>  
  Setting this option disables the following operations:

     - The `ERASE` command.
     - The `HOST` command.
     - The `PERMISSIONS` command.
     - Pipes (file names beginning or ending with `|`).

  Be aware that this setting does not guarantee safety (commands can
  still overwrite files, for instance) but it is an improvement.
  When set, this setting cannot be reset during the same session, for
  obvious security reasons.

* <a name="locale">`LOCALE`</a>  
  This item is used to set the default character encoding.  The
  encoding may be specified either as an [IANA encoding name or
  alias](http://www.iana.org/assignments/character-sets), or as a
  locale name.  If given as a locale name, only the character encoding
  of the locale is relevant.

  System files written by PSPP use this encoding.  System files read
  by PSPP, for which the encoding is unknown, are interpreted using
  this encoding.

  The full list of valid encodings and locale names/alias are
  operating system dependent.  The following are all examples of
  acceptable syntax on common GNU/Linux systems.

  ```
  SET LOCALE='iso-8859-1'.

  SET LOCALE='ru_RU.cp1251'.

  SET LOCALE='japanese'.
  ```

  Contrary to intuition, this command does not affect any aspect of
  the system's locale.

The following subcommands affect the interpretation of macros.  For
more information, see [Macro Settings](define.md#macro-settings).

* <a name="mexpand">`MEXPAND`</a>  
  Controls whether macros are expanded.  The default is `ON`.

* <a name="mprint">`MPRINT`</a>  
  Controls whether the expansion of macros is included in output.
  This is separate from whether command syntax in general is included
  in output.  The default is `OFF`.

* <a name="miterate">`MITERATE`</a>  
  Limits the number of iterations executed in
  [`!DO`](define.md#macro-loops) loops within macros.  This does not
  affect other language constructs such as [`LOOP`…`END
  LOOP`](loop.md).  This must be set to a positive integer.  The
  default is 1000.

* <a name="mnest">`MNEST`</a>  
  Limits the number of levels of nested macro expansions.  This must
  be set to a positive integer.  The default is 50.

The following subcommands are not yet implemented, but PSPP accepts
them and ignores the settings:

* `BASETEXTDIRECTION`
* `BLOCK`
* `BOX`
* `CACHE`
* `CELLSBREAK`
* `COMPRESSION`
* `CMPTRANS`
* `HEADER`

