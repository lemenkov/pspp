# Invoking `pspp`

This chapter describes how to invoke `pspp`, PSPP's main command-line
user interface.

## Main Options

Here is a summary of all the options, grouped by type, followed by
explanations in the same order.

In the table, arguments to long options also apply to any
corresponding short options.

```
_Non-option arguments_
          SYNTAX-FILE

_Output options_
          -o, --output=OUTPUT-FILE
          -O OPTION=VALUE
          -O format=FORMAT
          -O device={terminal|listing}
          --no-output
          --table-look=FILE
          -e, --error-file=ERROR-FILE

_Language options_
          -I, --include=DIR
          -I-, --no-include
          -b, --batch
          -i, --interactive
          -r, --no-statrc
          -a, --algorithm={compatible|enhanced}
          -x, --syntax={compatible|enhanced}
          --syntax-encoding=ENCODING

_Informational options_
          -h, --help
          -V, --version

_Other options_
          -s, --safer
          --testing-mode
```

* `SYNTAX-FILE`  
  Read and execute the named syntax file.  If no syntax files are
  specified, PSPP prompts for commands.  If any syntax files are
  specified, PSPP by default exits after it runs them, but you may
  make it prompt for commands by specifying `-` as an additional
  syntax file.

* `-o OUTPUT-FILE`  
  Write output to OUTPUT-FILE.  PSPP has several different output
  drivers that support output in various formats (use `--help` to
  list the available formats).  Specify this option more than once to
  produce multiple output files, presumably in different formats.

  Use `-` as OUTPUT-FILE to write output to standard output.

  If no `-o` option is used, then PSPP writes text and CSV output to
  standard output and other kinds of output to whose name is based on
  the format, e.g. `pspp.pdf` for PDF output.

* `-O OPTION=VALUE`  
  Sets an option for the output file configured by a preceding `-o`.
  Most options are specific to particular output formats.  A few
  options that apply generically are listed below.

* `-O format=FORMAT`  
  PSPP uses the extension of the file name given on `-o` to select an
  output format.  Use this option to override this choice by
  specifying an alternate format, e.g. `-o pspp.out -O format=html`
  to write HTML to a file named `pspp.out`.  Use `--help` to list the
  available formats.

* `-O device={terminal|listing}`  
  Sets whether PSPP considers the output device configured by the
  preceding `-o` to be a terminal or a listing device.  This affects
  what output will be sent to the device, as configured by the
  [`SET`](../commands/utilities/set.md) command's output routing
  subcommands.  By default, output written to standard output is
  considered a terminal device and other output is considered a
  listing device.

* `--no-output`  
  Disables output entirely, if neither `-o` nor `-O` is also used.
  If one of those options is used, `--no-output` has no effect.

* `--table-look=FILE`  
  Reads a table style from FILE and applies it to all PSPP table
  output.  The file should be a TableLook `.stt` or `.tlo` file.
  PSPP searches for FILE in the current directory, then in
  `.pspp/looks` in the user's home directory, then in a `looks`
  subdirectory inside PSPP's data directory (usually
  `/usr/local/share/pspp`).  If PSPP cannot find FILE under the given
  name, it also tries adding a `.stt` extension.

  When this option is not specified, PSPP looks for `default.stt`
  using the algorithm above, and otherwise it falls back to a default
  built-in style.

  Using [`SET TLOOK`](../commands/utilities/set.md#tlook) in PSPP syntax
  overrides the style set on the command line.

* `-e ERROR-FILE`  
  `--error-file=ERROR-FILE`  
  Configures a file to receive PSPP error, warning, and note messages
  in plain text format.  Use `-` as ERROR-FILE to write messages to
  standard output.  The default error file is standard output in the
  absence of these options, but this is suppressed if an output
  device writes to standard output (or another terminal), to avoid
  printing every message twice.  Use `none` as ERROR-FILE to
  explicitly suppress the default.

* `-I DIR`  
  `--include=DIR`  
  Appends DIR to the set of directories searched by the
  [`INCLUDE`](../commands/utilities/include.md) and
  [`INSERT`](../commands/utilities/insert.md) commands.

* `-I-`, `--no-include`  
  Clears all directories from the include path, including directories
  inserted in the include path by default.  The default include path
  is `.` (the current directory), followed by `.pspp` in the user's
  home directory, followed by PSPP's system configuration directory
  (usually `/etc/pspp` or `/usr/local/etc/pspp`).

* `-b`, `--batch`  
  `-i`, `--interactive`  
  These options forces syntax files to be interpreted in batch mode or
  interactive mode, respectively, rather than the default "auto" mode.
  See [Syntax Variants](../language/basics/syntax-variants.md), for a
  description of the differences.

* `-r`, `--no-statrc`  
  By default, at startup PSPP searches for a file named `rc` in the
  include path (described above) and, if it finds one, runs the
  commands in it.  This option disables this behavior.

* `-a {enhanced|compatible}`  
  `--algorithm={enhanced|compatible}`  
  With `enhanced`, the default, PSPP uses the best implemented
  algorithms for statistical procedures.  With `compatible`, however,
  PSPP will in some cases use inferior algorithms to produce the same
  results as the proprietary program SPSS.

  Some commands have subcommands that override this setting on a per
  command basis.

* `-x {enhanced|compatible}`  
  `--syntax={enhanced|compatible}` 
  With `enhanced`, the default, PSPP accepts its own extensions
  beyond those compatible with the proprietary program SPSS. With
  `compatible`, PSPP rejects syntax that uses these extensions.

* `--syntax-encoding=ENCODING`  
  Specifies ENCODING as the encoding for syntax files named on the
  command line.  The ENCODING also becomes the default encoding for
  other syntax files read during the PSPP session by the
  [`INCLUDE`](../commands/utilities/include.md) and
  [`INSERT`](../commands/utilities/insert.md) commands.  See
  [`INSERT`](../commands/utilities/insert.md) for the accepted forms of
  ENCODING.

* `--help`  
  Prints a message describing PSPP command-line syntax and the
  available device formats, then exits.

* `-V`, `--version`  
  Prints a brief message listing PSPP's version, warranties you don't
  have, copying conditions and copyright, and e-mail address for bug
  reports, then exits.

* `-s`, `--safer`  
  Disables certain unsafe operations.  This includes the `ERASE` and
  `HOST` commands, as well as use of pipes as input and output files.

* `--testing-mode`  
  Invoke heuristics to assist with testing PSPP.  For use by `make
  check` and similar scripts.

## PDF, PostScript, SVG, and PNG Output Options

To produce output in PDF, PostScript, SVG, or PNG format, specify `-o
FILE` on the PSPP command line, optionally followed by any of the
options shown in the table below to customize the output format.

PDF, PostScript, and SVG use real units: each dimension among the
options listed below may have a suffix `mm` for millimeters, `in` for
inches, or `pt` for points.  Lacking a suffix, numbers below 50 are
assumed to be in inches and those above 50 are assumed to be in
millimeters.

PNG files are pixel-based, so dimensions in PNG output must
ultimately be measured in pixels.  For output to these files, PSPP
translates the specified dimensions to pixels at 72 pixels per inch.
For PNG output only, fonts are by default rendered larger than this, at
96 pixels per inch.

An SVG or PNG file can only hold a single page.  When PSPP outputs
more than one page to SVG or PNG, it creates multiple files.  It outputs
the second page to a file named with a `-2` suffix, the third with a
`-3` suffix, and so on.

* `-O format={pdf|ps|svg|png}`  
  Specify the output format.  This is only necessary if the file name
  given on `-o` does not end in `.pdf`, `.ps`, `.svg`, or `.png`.

* `-O paper-size=PAPER-SIZE`  
  Paper size, as a name (e.g. `a4`, `letter`) or measurements (e.g.
  `210x297`, `8.5x11in`).

  The default paper size is taken from the `PAPERSIZE` environment
  variable or the file indicated by the `PAPERCONF` environment
  variable, if either variable is set.  If not, and your system
  supports the `LC_PAPER` locale category, then the default paper
  size is taken from the locale.  Otherwise, if `/etc/papersize`
  exists, the default paper size is read from it.  As a last resort,
  A4 paper is assumed.

* `-O foreground-color=COLOR`  
  Sets COLOR as the default color for lines and text.  Use a CSS
  color format (e.g. `#RRGGBB`) or name (e.g. `black`) as COLOR.

* `-O orientation=ORIENTATION`  
  Either `portrait` or `landscape`.  Default: `portrait`.

* `-O left-margin=DIMENSION`  
  `-O right-margin=DIMENSION`  
  `-O top-margin=DIMENSION`  
  `-O bottom-margin=DIMENSION`  
  Sets the margins around the page.  See below for the allowed forms
  of DIMENSION.  Default: `0.5in`.

* `-O object-spacing=DIMENSION`  
  Sets the amount of vertical space between objects (such as headings
  or tables).

* `-O prop-font=FONT-NAME`  
  Sets the default font used for ordinary text.  Most systems support
  CSS-like font names such as "Sans Serif", but a wide range of
  system-specific fonts are likely to be supported as well.

  Default: proportional font `Sans Serif`.

* `-O font-size=FONT-SIZE`  
  Sets the size of the default fonts, in thousandths of a point.
  Default: 10000 (10 point).

* `-O trim=true`  
  This option makes PSPP trim empty space around each page of output,
  before adding the margins.  This can make the output easier to
  include in other documents.

* `-O outline=BOOLEAN`  
  For PDF output only, this option controls whether PSPP includes an
  outline in the output file.  PDF viewers usually display the
  outline as a side bar that allows for easy navigation of the file.
  The default is true unless `-O trim=true` is also specified.  (The
  Cairo graphics library that PSPP uses to produce PDF output has a
  bug that can cause a crash when outlines and trimming are used
  together.)

* `-O font-resolution=DPI`  
  Sets the resolution for font rendering, in dots per inch.  For PDF,
  PostScript, and SVG output, the default is 72 dpi, so that a
  10-point font is rendered with a height of 10 points.  For PNG
  output, the default is 96 dpi, so that a 10-point font is rendered
  with a height of 10 / 72 * 96 = 13.3 pixels.  Use a larger DPI to
  enlarge text output, or a smaller DPI to shrink it.

## Plain Text Output Options

PSPP can produce plain text output, drawing boxes using ASCII or Unicode
line drawing characters.  To produce plain text output, specify `-o
FILE` on the PSPP command line, optionally followed by options from the
table below to customize the output format.

Plain text output is encoded in UTF-8.

* `-O format=txt`  
  Specify the output format.  This is only necessary if the file name
  given on `-o` does not end in `.txt` or `.list`.

* `-O charts={TEMPLATE.png|none}`  
  Name for chart files included in output.  The value should be a
  file name that includes a single `#` and ends in `png`.  When a
  chart is output, the `#` is replaced by the chart number.  The
  default is the file name specified on `-o` with the extension
  stripped off and replaced by `-#.png`.

  Specify `none` to disable chart output.

* `-O foreground-color=COLOR`  
  `-O background-color=COLOR`  
  Sets COLOR as the color to be used for the background or foreground
  to be used for charts.  Color should be given in the format
  `#RRRRGGGGBBBB`, where RRRR, GGGG and BBBB are 4 character
  hexadecimal representations of the red, green and blue components
  respectively.  If charts are disabled, this option has no effect.

* `-O width=COLUMNS`  
  Width of a page, in columns.  If unspecified or given as `auto`, the
  default is the width of the terminal, for interactive output, or the
  [`WIDTH`](../commands/utilities/set.md#width) setting, for output to a
  file.

* `-O box={ascii|unicode}`  
  Sets the characters used for lines in tables.  If set to `ascii`,
  output uses use the characters `-`, `|`, and `+` for single-width
  lines and `=` and `#` for double-width lines.  If set to `unicode`
  then, output uses Unicode box drawing characters.  The default is
  `unicode` if the locale's character encoding is "UTF-8" or `ascii`
  otherwise.

* `-O emphasis={none|bold|underline}`  
  How to emphasize text.  Bold and underline emphasis are achieved
  with overstriking, which may not be supported by all the software
  to which you might pass the output.  Default: `none`.

## SPV Output Options

SPSS 16 and later write `.spv` files to represent the contents of its
output editor.  To produce output in `.spv` format, specify `-o FILE` on
the PSPP command line, optionally followed by any of the options shown
in the table below to customize the output format.

* `-O format=spv`  
  Specify the output format.  This is only necessary if the file name
  given on `-o` does not end in `.spv`.

* `-O paper-size=PAPER-SIZE`  
  `-O left-margin=DIMENSION`  
  `-O right-margin=DIMENSION`  
  `-O top-margin=DIMENSION`  
  `-O bottom-margin=DIMENSION`  
  `-O object-spacing=DIMENSION`  
  These have the same syntax and meaning as for [PDF
  output](#pdf-postscript-svg-and-png-output-options).

## TeX Output Options

If you want to publish statistical results in professional or academic
journals, you will probably want to provide results in TeX format.  To
do this, specify `-o FILE` on the PSPP command line where FILE is a file
name ending in `.tex`, or you can specify `-O format=tex`.

The resulting file can be directly processed using TeX or you can
manually edit the file to add commentary text.  Alternatively, you can
cut and paste desired sections to another TeX file.

## HTML Output Options

To produce output in HTML format, specify `-o FILE` on the PSPP command
line, optionally followed by any of the options shown in the table below
to customize the output format.

* `-O format=html`  
  Specify the output format.  This is only necessary if the file name
  given on `-o` does not end in `.html`.

* `-O charts={TEMPLATE.png|none}`  
  Sets the name used for chart files.  See [Plain Text Output
  Options](#plain-text-output-options), for details.

* `-O borders=BOOLEAN`  
  Decorate the tables with borders.  If set to false, the tables
  produced will have no borders.  The default value is true.

* `-O bare=BOOLEAN`  
  The HTML output driver ordinarily outputs a complete HTML document.
  If set to true, the driver instead outputs only what would normally
  be the contents of the `body` element.  The default value is false.

* `-O css=BOOLEAN`  
  Use cascading style sheets.  Cascading style sheets give an
  improved appearance and can be used to produce pages which fit a
  certain web site's style.  The default value is true.

## OpenDocument Output Options

To produce output as an OpenDocument text (ODT) document, specify `-o
FILE` on the PSPP command line.  If FILE does not end in `.odt`, you
must also specify `-O format=odt`.

ODT support is only available if your installation of PSPP was
compiled with the libxml2 library.

The OpenDocument output format does not have any configurable
options.

## Comma-Separated Value Output Options

To produce output in comma-separated value (CSV) format, specify `-o
FILE` on the PSPP command line, optionally followed by any of the
options shown in the table below to customize the output format.

* `-O format=csv`  
  Specify the output format.  This is only necessary if the file name
  given on `-o` does not end in `.csv`.

* `-O separator=FIELD-SEPARATOR`  
  Sets the character used to separate fields.  Default: a comma
  (`,`).

* `-O quote=QUALIFIER`  
  Sets QUALIFIER as the character used to quote fields that contain
  white space, the separator (or any of the characters in the
  separator, if it contains more than one character), or the quote
  character itself.  If QUALIFIER is longer than one character, only
  the first character is used; if QUALIFIER is the empty string, then
  fields are never quoted.

* `-O titles=BOOLEAN`  
  Whether table titles (brief descriptions) should be printed.
  Default: `on`.

* `-O captions=BOOLEAN`  
  Whether table captions (more extensive descriptions) should be
  printed.  Default: on.

   The CSV format used is an extension to that specified in RFC 4180:

* Tables  
  Each table row is output on a separate line, and each column is
  output as a field.  The contents of a cell that spans multiple rows
  or columns is output only for the top-left row and column; the rest
  are output as empty fields.

* Titles  
  When a table has a title and titles are enabled, the title is
  output just above the table as a single field prefixed by `Table:`.

* Captions  
  When a table has a caption and captions are enabled, the caption is
  output just below the table as a single field prefixed by
  `Caption:`.

* Footnotes  
  Within a table, footnote markers are output as bracketed letters
  following the cell's contents, e.g. `[a]`, `[b]`, ...  The
  footnotes themselves are output following the body of the table, as
  a separate two-column table introduced with a line that says
  `Footnotes:`.  Each row in the table represent one footnote: the
  first column is the marker, the second column is the text.

* Text  
  Text in output is printed as a field on a line by itself.  The
  TITLE and SUBTITLE produce similar output, prefixed by `Title:` or
  `Subtitle:`, respectively.

* Messages  
  Errors, warnings, and notes are printed the same way as text.

* Charts  
  Charts are not included in CSV output.

Successive output items are separated by a blank line.

