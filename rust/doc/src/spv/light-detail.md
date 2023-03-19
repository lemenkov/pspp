# Light Detail Member Format

This section describes the format of "light" detail `.bin` members.

<!-- toc -->

## Binary Format Conventions

These members have a binary format which we describe here in terms of a
context-free grammar using the following conventions:

* `NonTerminal ⇒ ...`  
  Nonterminals have CamelCaps names, and ⇒ indicates a production.
  The right-hand side of a production is often broken across multiple
  lines.  Break points are chosen for aesthetics only and have no
  semantic significance.

* `00, 01, ..., ff.`  
  A bytes with a fixed value, written as a pair of hexadecimal
  digits.

* `i0, i1, ..., i9, i10, i11, ...`  
  `ib0, ib1, ..., ib9, ib10, ib11, ...`  
  A 32-bit integer in little-endian or big-endian byte order,
  respectively, with a fixed value, written in decimal.  Prefixed by
  `i` for little-endian or `ib` for big-endian.

* `byte`  
  A byte.

* `bool`  
  A byte with value 0 or 1.

* `int16`  
  `be16`  
  A 16-bit unsigned integer in little-endian or big-endian byte
  order, respectively.

* `int32`  
  `be32`  
  A 32-bit unsigned integer in little-endian or big-endian byte
  order, respectively.

* `int64`  
  `be64`  
  A 64-bit unsigned integer in little-endian or big-endian byte
  order, respectively.

* `double`  
  A 64-bit IEEE floating-point number.

* `float`  
  A 32-bit IEEE floating-point number.

* `string`  
  `bestring`  
  A 32-bit unsigned integer, in little-endian or big-endian byte
  order, respectively, followed by the specified number of bytes of
  character data.  (The encoding is indicated by the Formats
  nonterminal.)

* `X?`  
  X is optional, e.g. 00?  is an optional zero byte.

* `X*N`  
  X is repeated N times, e.g. byte*10 for ten arbitrary bytes.

* `X[NAME]`  
  Gives X the specified NAME.  Names are used in textual
  explanations.  They are also used, also bracketed, to indicate
  counts, e.g. `int32[n] byte*[n]` for a 32-bit integer followed by
  the specified number of arbitrary bytes.

* `A | B`  
  Either A or B.

* `(X)`  
  Parentheses are used for grouping to make precedence clear,
  especially in the presence of |, e.g. in 00 (01 | 02 | 03) 00.

* `count(X)`  
  `becount(X)`  
  A 32-bit unsigned integer, in little-endian or big-endian byte
  order, respectively, that indicates the number of bytes in X,
  followed by X itself.

* `v1(X)`  
  In a version 1 `.bin` member, X; in version 3, nothing.  (The
  `.bin` header indicates the version.)

* `v3(X)`  
  In a version 3 `.bin` member, X; in version 1, nothing.

PSPP uses this grammar to parse light detail members.  See
`src/output/spv/light-binary.grammar` in the PSPP source tree for the
full grammar.

Little-endian byte order is far more common in this format, but a few
pieces of the format use big-endian byte order.

Light detail members express linear units in two ways: <a
name="pt">points (pt)</a>, at 72/inch, and <a
name="px">"device-independent pixels" (px)</a>, at 96/inch.  To
convert from pt to px, multiply by 1.33 and round up.  To convert from
px to pt, divide by 1.33 and round down.

A "light" detail member `.bin` consists of a number of sections
concatenated together, terminated by an optional byte 01:

```
Table =>
    Header Titles Footnotes
    Areas Borders PrintSettings TableSettings Formats
    Dimensions Axes Cells
    01?
```

## Header

An SPV light member begins with a 39-byte header:

```
Header =>
    01 00
    (i1 | i3)[version]
    bool[x0]
    bool[x1]
    bool[rotate-inner-column-labels]
    bool[rotate-outer-row-labels]
    bool[x2]
    int32[x3]
    int32[min-col-heading-width] int32[max-col-heading-width]
    int32[min-row-heading-width] int32[max-row-heading-width]
    int64[table-id]
```

`version` is a version number that affects the interpretation of some
of the other data in the member.  We will refer to "version 1" and
"version 3" later on and use `v1(...)` and `v3(...)` for
version-specific formatting (as described previously).

If `rotate-inner-column-labels` is 1, then column labels closest to
the data are rotated 90° counterclockwise; otherwise, they are shown in
the normal way.

If `rotate-outer-row-labels` is 1, then row labels farthest from the
data are rotated 90° counterclockwise; otherwise, they are shown in the
normal way.

`min-col-heading-width`, `max-col-heading-width`,
`min-row-heading-width`, and `max-row-heading-width` are measurements in
1/96 inch units (called "device independent pixel" units in Windows)
whose values influence column widths.  For the purpose of interpreting
these values, a table is divided into the three regions shown below:

```
+------------------+-------------------------------------------------+
|                  |                  column headings                |
|                  +-------------------------------------------------+
|      corner      |                                                 |
|       and        |                                                 |
|   row headings   |                      data                       |
|                  |                                                 |
|                  |                                                 |
+------------------+-------------------------------------------------+
```

`min-col-heading-width` and `max-col-heading-width` apply to the
columns in the column headings region.  `min-col-heading-width` is the
minimum width that any of these columns will be given automatically.  In
addition, `max-col-heading-width` is the maximum width that a column
will be assigned to accommodate a long label in the column headings
cells.  These columns will still be made wider to accommodate wide data
values in the data region.

`min-row-heading-width` is the minimum width that a column in the
corner and row headings region will be given automatically.
`max-col-heading-width` is the maximum width that a column in this
region will be assigned to accomodate a long label.  This region doesn't
include data, so data values don't affect column widths.

`table-id` is a binary version of the `tableId` attribute in the
structure member that refers to the detail member.  For example, if
`tableId` is `-4122591256483201023`, then `table-id` would be
0xc6c99d183b300001.

The meaning of the other variable parts of the header is not known.
A writer may safely use version 3, true for `x0`, false for `x1`, true
for `x2`, and 0x15 for `x3`.

## Titles

```
Titles =>
    Value[title] 01?
    Value[subtype] 01? 31
    Value[user-title] 01?
    (31 Value[corner-text] | 58)
    (31 Value[caption] | 58)
```

The `Titles` follow the Header and specify the table's title, caption,
and corner text.

The `user-title` reflects any user editing of the title text or
style.  The `title` is the title originally generated by the procedure.
Both of these are appropriate for presentation and localized to the
user's language.  For example, for a frequency table, `title` and
`user-title` normally name the variable and `c` is simply "Frequencies".

`subtype` is the same as the `subType` attribute in the [`table`
structure XML element](structure.md#the-table-element) that referred
to this member.

The `corner-text`, if present, is shown in the upper-left corner of
the table, above the row headings and to the left of the column
headings.  It is usually absent.  When row dimension labels are
displayed in the corner (see `show-row-labels-in-corner`), corner text
is hidden.

The `caption`, if present, is shown below the table.  `caption`
reflects user editing of the caption.

## Footnotes

```
Footnotes => int32[n-footnotes] Footnote*[n-footnotes]
Footnote => Value[text] (58 | 31 Value[marker]) int32[show]
```

Each footnote has `text` and an optional custom `marker` (such as
`*`).

The syntax for Value would allow footnotes (and their markers) to
reference other footnotes, but in practice this doesn't work.

`show` is a 32-bit signed integer.  It is positive to show the
footnote or negative to hide it.  Its magnitude is often 1, and in other
cases tends to be the number of references to the footnote.  It is safe
to write 1 to show a footnote and -1 to hide it.

## Areas

```
Areas => 00? Area*8
Area =>
    byte[index] 31
    string[typeface] float[size] int32[style] bool[underline]
    int32[halign] int32[valign]
    string[fg-color] string[bg-color]
    bool[alternate] string[alt-fg-color] string[alt-bg-color]
    v3(int32[left-margin] int32[right-margin] int32[top-margin] int32[bottom-margin])
```

Each `Area` represents the style for a different area of the table, in
the following order: title, caption, footer, corner, column labels,
row labels, data, and layers.

`index` is the 1-based index of the Area, i.e. 1 for the first `Area`,
through 8 for the final `Area`.

`typeface` is the string name of the font used in the area.  In the
corpus, this is `SansSerif` in over 99% of instances and `Times New
Roman` in the rest.

`size` is the size of the font, in [px](#px).  The most common size in
the corpus is 12 px.  Even though `size` has a floating-point type, in
the corpus its values are always integers.

`style` is a bit mask.  Bit 0 (with value 1) is set for bold, bit 1
(with value 2) is set for italic.

`underline` is 1 if the font is underlined, 0 otherwise.

`halign` specifies horizontal alignment: 0 for center, 2 for left, 4
for right, 61453 for decimal, 64173 for mixed.  Mixed alignment varies
according to type: string data is left-justified, numbers and most other
formats are right-justified.

`valign` specifies vertical alignment: 0 for center, 1 for top, 3 for
bottom.

`fg-color` and `bg-color` are the foreground color and background
color, respectively.  In the corpus, these are always `#000000` and
`#ffffff`, respectively.

`alternate` is 1 if rows should alternate colors, 0 if all rows
should be the same color.  When `alternate` is 1, `alt-fg-color` and
`alt-bg-color` specify the colors for the alternate rows; otherwise they
are empty strings.

`left-margin`, `right-margin`, `top-margin`, and `bottom-margin` are
measured in px.

## Borders

```
Borders =>
    count(
        ib1[endian]
        be32[n-borders] Border*[n-borders]
        bool[show-grid-lines]
        00 00 00)

Border =>
    be32[border-type]
    be32[stroke-type]
    be32[color]
```

`Borders` reflects how borders between regions are drawn.

The fixed value of `endian` can be used to validate the endianness.

`show-grid-lines` is 1 to draw grid lines, otherwise 0.

Each `Border` describes one kind of border.  `n-borders` seems to
always be 19.  Each `border-type` appears once (although in an
unpredictable order) and correspond to the following borders:

* 0: Title.
* 1...4: Left, top, right, and bottom outer frame.
* 5...8: Left, top, right, and bottom inner frame.
* 9, 10: Left and top of data area.
* 11, 12: Horizontal and vertical dimension rows.
* 13, 14: Horizontal and vertical dimension columns.
* 15, 16: Horizontal and vertical category rows.
* 17, 18: Horizontal and vertical category columns.

`stroke-type` describes how a border is drawn, as one of:

* 0: No line.
* 1: Solid line.
* 2: Dashed line.
* 3: Thick line.
* 4: Thin line.
* 5: Double line.

`color` is an RGB color.  Bits 24-31 are alpha, bits 16-23 are red,
8-15 are green, 0-7 are blue.  An alpha of 255 indicates an opaque
color, therefore opaque black is 0xff000000.

## Print Settings

```
PrintSettings =>
    count(
        ib1[endian]
        bool[all-layers]
        bool[paginate-layers]
        bool[fit-width]
        bool[fit-length]
        bool[top-continuation]
        bool[bottom-continuation]
        be32[n-orphan-lines]
        bestring[continuation-string])
```

`PrintSettings` reflects settings for printing.  The fixed value of
`endian` can be used to validate the endianness.

`all-layers` is 1 to print all layers, 0 to print only the layer
designated by `current-layer` in [`TableSettings`](#table-settings).

`paginate-layers` is 1 to print each layer at the start of a new
page, 0 otherwise.  (This setting is honored only `all-layers` is 1,
since otherwise only one layer is printed.)

`fit-width` and `fit-length` control whether the table is shrunk to
fit within a page's width or length, respectively.

`n-orphan-lines` is the minimum number of rows or columns to put in
one part of a table that is broken across pages.

If `top-continuation` is 1, then `continuation-string` is printed at
the top of a page when a table is broken across pages for printing;
similarly for `bottom-continuation` and the bottom of a page.  Usually,
`continuation-string` is empty.

## Table Settings

```
TableSettings =>
    count(
      v3(
        ib1[endian]
        be32[x5]
        be32[current-layer]
        bool[omit-empty]
        bool[show-row-labels-in-corner]
        bool[show-alphabetic-markers]
        bool[footnote-marker-superscripts]
        byte[x6]
        becount(
          Breakpoints[row-breaks] Breakpoints[column-breaks]
          Keeps[row-keeps] Keeps[column-keeps]
          PointKeeps[row-point-keeps] PointKeeps[column-point-keeps]
        )
        bestring[notes]
        bestring[table-look]
        )...)

Breakpoints => be32[n-breaks] be32*[n-breaks]

Keeps => be32[n-keeps] Keep*[n-keeps]
Keep => be32[offset] be32[n]

PointKeeps => be32[n-point-keeps] PointKeep*[n-point-keeps]
PointKeep => be32[offset] be32 be32
```

`TableSettings` reflects display settings.  The fixed value of
`endian` can be used to validate the endianness.

`current-layer` is the displayed layer.  Suppose there are \\(d\\)
layers, numbered 1 through \\(d\\) in the order given in the
[Dimensions](#dimensions), and that the displayed value of dimension
\\(i\\) is \\\(d_i, 0 \le x_i < n_i\\), where \\(n_i\\) is the number
of categories in dimension \\(i\\).  Then `current-layer` is the
\\(k\\) calculated by the following algorithm:

> let \\(k = 0\\).  
> for each \\(i\\) from \\(d\\) downto 1:  
> \\(\quad k = (n_i \times k) + x_i\\).

If `omit-empty` is 1, empty rows or columns (ones with nothing in any
cell) are hidden; otherwise, they are shown.

If `show-row-labels-in-corner` is 1, then row labels are shown in the
upper left corner; otherwise, they are shown nested.

If `show-alphabetic-markers` is 1, markers are shown as letters (e.g.
`a`, `b`, `c`, ...); otherwise, they are shown as numbers starting from
1.

When `footnote-marker-superscripts` is 1, footnote markers are shown
as superscripts, otherwise as subscripts.

The `Breakpoints` are rows or columns after which there is a page
break; for example, a row break of 1 requests a page break after the
second row.  Usually no breakpoints are specified, indicating that page
breaks should be selected automatically.

The `Keeps` are ranges of rows or columns to be kept together without
a page break; for example, a row Keep with `offset` 1 and `n` 10
requests that the 10 rows starting with the second row be kept
together.  Usually no `Keeps` are specified.

The `PointKeeps` seem to be generated automatically based on
user-specified Keeps.  They seems to indicate a conversion from rows or
columns to pixel or point offsets.

`notes` is a text string that contains user-specified notes.  It is
displayed when the user hovers the cursor over the table, like text in
the `title` attribute in HTML.  It is not printed.  It is usually empty.

`table-look` is the name of a SPSS "TableLook" table style, such as
"Default" or "Academic"; it is often empty.

`TableSettings` ends with an arbitrary number of null bytes.  A writer
may safely write 82 null bytes.

A writer may safely use 4 for `x5` and 0 for `x6`.

## Formats

```
Formats =>
    int32[n-widths] int32*[n-widths]
    string[locale]
    int32[current-layer]
    bool[x7] bool[x8] bool[x9]
    Y0
    CustomCurrency
    count(
      v1(X0?)
      v3(count(X1 count(X2)) count(X3)))
Y0 => int32[epoch] byte[decimal] byte[grouping]
CustomCurrency => int32[n-ccs] string*[n-ccs]
```

If `n-widths` is nonzero, then the accompanying integers are column
widths as manually adjusted by the user.

`locale` is a locale including an encoding, such as
`en_US.windows-1252` or `it_IT.windows-1252`.  (`locale` is often
duplicated in Y1, described below).

`epoch` is the year that starts the epoch.  A 2-digit year is
interpreted as belonging to the 100 years beginning at the epoch.  The
default epoch year is 69 years prior to the current year; thus, in 2017
this field by default contains 1948.  In the corpus, `epoch` ranges from
1943 to 1948, plus some contain -1.

`decimal` is the decimal point character.  The observed values are
`.` and `,`.

`grouping` is the grouping character.  Usually, it is `,` if
`decimal` is `.`, and vice versa.  Other observed values are `'`
(apostrophe), ` ` (space), and zero (presumably indicating that digits
should not be grouped).

`n-ccs` is observed as either 0 or 5.  When it is 5, the following
strings are [CCA through
CCE](../language/datasets/formats/custom-currency.md) format strings.
Most commonly these are all `-,,,` but other strings occur.

A writer may safely use false for `x7`, `x8`, and `x9`.

### X0

X0 only appears, optionally, in version 1 members.

```
X0 => byte*14 Y1 Y2
Y1 =>
    string[command] string[command-local]
    string[language] string[charset] string[locale]
    bool[x10] bool[include-leading-zero] bool[x12] bool[x13]
    Y0
Y2 => CustomCurrency byte[missing] bool[x17]
```

`command` describes the statistical procedure that generated the
output, in English.  It is not necessarily the literal syntax name of
the procedure: for example, NPAR TESTS becomes "Nonparametric Tests."
`command-local` is the procedure's name, translated into the output
language; it is often empty and, when it is not, sometimes the same as
`command`.

`include-leading-zero` is the
[`LEADZERO`](../commands/utilities/set.md#leadzero) setting for the
table, where false is `OFF` (the default) and true is `ON`.

`missing` is the character used to indicate that a cell contains a
missing value.  It is always observed as `.`.

A writer may safely use false for `x10` and `x17` and true for `x12`
and `x13`.

### X1

`X1` only appears in version 3 members.

```
X1 =>
    bool[x14]
    byte[show-title]
    bool[x16]
    byte[lang]
    byte[show-variables]
    byte[show-values]
    int32[x18] int32[x19]
    00*17
    bool[x20]
    bool[show-caption]
```

`lang` may indicate the language in use.  Some values seem to be 0:
en, 1: de, 2: es, 3: it, 5: ko, 6: pl, 8: zh-tw, 10: pt_BR, 11: fr.

`show-variables` determines how variables are displayed by default.
A value of 1 means to display variable names, 2 to display variable
labels when available, 3 to display both (name followed by label,
separated by a space).  The most common value is 0, which probably means
to use a global default.

`show-values` is a similar setting for values.  A value of 1 means to
display the value, 2 to display the value label when available, 3 to
display both.  Again, the most common value is 0, which probably means
to use a global default.

`show-title` is 1 to show the caption, 10 to hide it.

`show-caption` is true to show the caption, false to hide it.

A writer may safely use false for `x14`, false for `x16`, 0 for
`lang`, -1 for `x18` and `x19`, and false for `x20`.

### X2

`X2` only appears in version 3 members.

```
X2 =>
    int32[n-row-heights] int32*[n-row-heights]
    int32[n-style-map] StyleMap*[n-style-map]
    int32[n-styles] StylePair*[n-styles]
    count((i0 i0)?)
StyleMap => int64[cell-index] int16[style-index]
```

If present, `n-row-heights` and the accompanying integers are row
heights as manually adjusted by the user.

The rest of `X2` specifies styles for data cells.  At first glance
this is odd, because each data cell can have its own style embedded as
part of the data, but in practice `X2` specifies a style for a cell
only if that cell is empty (and thus does not appear in the data at
all).  Each StyleMap specifies the index of a blank cell, calculated
the same was as in the [Cells](#cells), along with a 0-based index
into the accompanying StylePair array.

A writer may safely omit the optional `i0 i0` inside the
`count(...)`.

### X3

`X3` only appears in version 3 members.

```
X3 =>
    01 00 byte[x21] 00 00 00
    Y1
    double[small] 01
    (string[dataset] string[datafile] i0 int32[date] i0)?
    Y2
    (int32[x22] i0 01?)?
```

`small` is a small real number.  In the corpus, it overwhelmingly
takes the value 0.0001, with zero occasionally seen.  Nonzero numbers
with format 40 (see [Value](#value)) whose magnitudes are smaller than
displayed in scientific notation.  (Thus, a `small` of zero prevents
scientific notation from being chosen.)

`dataset` is the name of the dataset analyzed to produce the output,
e.g. `DataSet1`, and `datafile` the name of the file it was read from,
e.g. `C:\Users\foo\bar.sav`.  The latter is sometimes the empty string.

`date` is a date, as seconds since the epoch, i.e. since January 1,
1970.  Pivot tables within an SPV file often have dates a few minutes
apart, so this is probably a creation date for the table rather than for
the file.

Sometimes `dataset`, `datafile`, and `date` are present and other
times they are absent.  The reader can distinguish by assuming that they
are present and then checking whether the presumptive `dataset` contains
a null byte (a valid string never will).

`x22` is usually 0 or 2000000.

A writer may safely use 4 for `x21` and omit `x22` and the other
optional bytes at the end.

### Encoding

Formats contains several indications of character encoding:

- `locale` in Formats itself.

- `locale` in Y1 (in version 1, Y1 is optionally nested inside X0; in
version 3, Y1 is nested inside X3).

- `charset` in version 3, in Y1.

- `lang` in X1, in version 3.

`charset`, if present, is a good indication of character encoding,
and in its absence the encoding suffix on `locale` in Formats will work.

`locale` in Y1 can be disregarded: it is normally the same as
`locale` in Formats, and it is only present if `charset` is also.

`lang` is not helpful and should be ignored for character encoding
purposes.

However, the corpus contains many examples of light members whose
strings are encoded in UTF-8 despite declaring some other character set.
Furthermore, the corpus contains several examples of light members in
which some strings are encoded in UTF-8 (and contain multibyte
characters) and other strings are encoded in another character set (and
contain non-ASCII characters).  PSPP treats any valid UTF-8 string as
UTF-8 and only falls back to the declared encoding for strings that are
not valid UTF-8.

The `pspp-output` program's `strings` command can help analyze the
encoding in an SPV light member.  Use `pspp-output --help-dev` to see
its usage.

## Dimensions

A pivot table presents multidimensional data.  A Dimension identifies
the categories associated with each dimension.

```
Dimensions => int32[n-dims] Dimension*[n-dims]
Dimension =>
    Value[name] DimProperties
    int32[n-categories] Category*[n-categories]
DimProperties =>
    byte[x1]
    byte[x2]
    int32[x3]
    bool[hide-dim-label]
    bool[hide-all-labels]
    01 int32[dim-index]
```

`name` is the name of the dimension, e.g. `Variables`, `Statistics`,
or a variable name.

The meanings of `x1` and `x3` are unknown.  `x1` is usually 0 but
many other values have been observed.  A writer may safely use 0 for
`x1` and 2 for `x3`.

`x2` is 0, 1, or 2.  For a pivot table with L layer dimensions, R row
dimensions, and C column dimensions, `x2` is 2 for the first L
dimensions, 0 for the next R dimensions, and 1 for the remaining C
dimensions.  This does not mean that the layer dimensions must be
presented first, followed by the row dimensions, followed by the column
dimensions--on the contrary, they are frequently in a different
order--but `x2` must follow this pattern to prevent the pivot table from
being misinterpreted.

If `hide-dim-label` is 00, the pivot table displays a label for the
dimension itself.  Because usually the group and category labels are
enough explanation, it is usually 01.

If `hide-all-labels` is 01, the pivot table omits all labels for the
dimension, including group and category labels.  It is usually 00.  When
`hide-all-labels` is 01, `hide-dim-label` is ignored.

`dim-index` is usually the 0-based index of the dimension, e.g. 0 for
the first dimension, 1 for the second, and so on.  Sometimes it is -1.
There is no visible difference.  A writer may safely use the 0-based
index.

## Categories

Categories are arranged in a tree.  Only the leaf nodes in the tree are
really categories; the others just serve as grouping constructs.

```
Category => Value[name] (Leaf | Group)
Leaf => 00 00 00 i2 int32[leaf-index] i0
Group =>
    bool[merge] 00 01 int32[x23]
    i-1 int32[n-subcategories] Category*[n-subcategories]
```

`name` is the name of the category (or group).

A Leaf represents a leaf category.  The Leaf's `leaf-index` is a
nonnegative integer unique within the Dimension and less than
`n-categories` in the Dimension.  If the user does not sort or rearrange
the categories, then `leaf-index` starts at 0 for the first Leaf in the
dimension and increments by 1 with each successive Leaf.  If the user
does sorts or rearrange the categories, then the order of categories in
the file reflects that change and `leaf-index` reflects the original
order.

A dimension can have no leaf categories at all.  A table that
contains such a dimension necessarily has no data at all.

A Group is a group of nested categories.  Usually a Group contains at
least one Category, so that `n-subcategories` is positive, but Groups
with zero subcategories have been observed.

If a Group's `merge` is 00, the most common value, then the group is
really a distinct group that should be represented as such in the visual
representation and user interface.  If `merge` is 01, the categories in
this group should be shown and treated as if they were direct children
of the group's containing group (or if it has no parent group, then
direct children of the dimension), and this group's name is irrelevant
and should not be displayed.  (Merged groups can be nested!)

Writers need not use merged groups.

A Group's `x23` appears to be `i2` when all of the categories within a
group are leaf categories that directly represent data values for a
variable (e.g. in a frequency table or crosstabulation, a group of
values in a variable being tabulated) and i0 otherwise.  A writer may
safely write a constant 0 in this field.

## Axes

After the dimensions come assignment of each dimension to one of the
axes: layers, rows, and columns.

```
Axes =>
    int32[n-layers] int32[n-rows] int32[n-columns]
    int32*[n-layers] int32*[n-rows] int32*[n-columns]
```

The values of `n-layers`, `n-rows`, and `n-columns` each specifies
the number of dimensions displayed in layers, rows, and columns,
respectively.  Any of them may be zero.  Their values sum to
`n-dimensions` from [`Dimensions`](#dimensions).

The following `n-dimensions` integers, in three groups, are a
permutation of the 0-based dimension numbers.  The first `n-layers`
integers specify each of the dimensions represented by layers, the next
`n-rows` integers specify the dimensions represented by rows, and the
final `n-columns` integers specify the dimensions represented by
columns.  When there is more than one dimension of a given kind, the
inner dimensions are given first.  (For the layer axis, this means that
the first dimension is at the bottom of the list and the last dimension
is at the top when the current layer is displayed.)

## Cells

The final part of an SPV light member contains the actual data.

```
Cells => int32[n-cells] Cell*[n-cells]
Cell => int64[index] v1(00?) Value
```

A Cell consists of an `index` and a Value.  Suppose there are \\(d\\)
dimensions, numbered 1 through \\(d\\) in the order given in the [`Dimensions`](#dimensions)
previously, and that dimension \\(i\\) has \\(n_i\\) categories.  Consider the cell
at coordinates \\(x_i, 1 \le i \le d\\), and note that \\(0 \le x_i < n_i\\).  Then
the index \\(k\\) is calculated by the following algorithm:


> let \\(k = 0\\).  
> for each \\(i\\) from 1 to \\(d\\):  
> \\(\quad k = (n_i \times k) + x_i\\)

For example, suppose there are 3 dimensions with 3, 4, and 5
categories, respectively.  The cell at coordinates (1, 2, 3) has index
\\(k = 5 \times (4 \times (3 \times 0 + 1) + 2) + 3 = 33\\).  Within a
given dimension, the index is the `leaf-index` in a Leaf.

## Value

`Value` is used throughout the SPV light member format.  It boils down to
a number or a string.

```
Value => 00? 00? 00? 00? RawValue
RawValue =>
    01 ValueMod int32[format] double[x]
  | 02 ValueMod int32[format] double[x]
    string[var-name] string[value-label] byte[show]
  | 03 string[local] ValueMod string[id] string[c] bool[fixed]
  | 04 ValueMod int32[format] string[value-label] string[var-name]
    byte[show] string[s]
  | 05 ValueMod string[var-name] string[var-label] byte[show]
  | 06 string[local] ValueMod string[id] string[c]
  | ValueMod string[template] int32[n-args] Argument*[n-args]
Argument =>
    i0 Value
  | int32[x] i0 Value*[x]      /* x > 0 */
```

There are several possible encodings, which one can distinguish by
the first nonzero byte in the encoding.

* `01`  
  The numeric value `x`, intended to be presented to the user
  formatted according to `format`, which is about the same as the
  [format described for system files](../system-file.md#format-types).
  The exception is that format 40 is not `MTIME` but instead
  approximately a synonym for `F` format with a different rule for
  whether a value is shown in scientific notation: a value in format
  40 is shown in scientific notation if and only if it is nonzero and
  its magnitude is less than [`small`](#formats).

  Most commonly, `format` has width 40 (the maximum).

  An `x` with the maximum negative double value `-DBL_MAX` represents
  the system-missing value `SYSMIS`. (`HIGHEST` and `LOWEST` have not
  been observed.)  See [System File
  Format](../system-file.md#introduction) for more about these special
  values.

* `02`  
  Similar to `01`, with the additional information that `x` is a
  value of variable `var-name` and has value label `value-label`.
  Both `var-name` and `value-label` can be the empty string, the
  latter very commonly.

  `show` determines whether to show the numeric value or the value
  label.  A value of 1 means to show the value, 2 to show the label,
  3 to show both, and 0 means to use the default specified in
  [`show-values`](#formats).

* `03`  
  A text string, in two forms: `c` is in English, and sometimes
  abbreviated or obscure, and `local` is localized to the user's
  locale.  In an English-language locale, the two strings are often
  the same, and in the cases where they differ, `local` is more
  appropriate for a user interface, e.g. `c` of "Not a PxP table for
  MCN..." versus `local` of "Computed only for a PxP table, where P
  must be greater than 1."

  `c` and `local` are always either both empty or both nonempty.

  `id` is a brief identifying string whose form seems to resemble a
  programming language identifier, e.g. `cumulative_percent` or
  `factor_14`.  It is not unique.

  `fixed` is:

    * `00` for text taken from user input, such as syntax fragment,
      expressions, file names, data set names.  `id` is always the
      empty string.

    * `01` for fixed text strings such as names of procedures or
      statistics.  `id` is sometimes empty.

* `04`  
  The string value `s`, intended to be presented to the user formatted
  according to `format`.  The format for a string is not too
  interesting, and the corpus contains many clearly invalid formats
  like `A16.39` or `A255.127` or `A134.1`, so readers should probably
  entirely disregard the format.  PSPP only checks `format` to
  distinguish AHEX format.

  `s` is a value of variable `var-name` and has value label
  `value-label`.  `var-name` is never empty but `value-label` is
  commonly empty.

  `show` has the same meaning as in the encoding for `02`.

* `05`  
  Variable `var-name` with variable label `var-label`.  In the
  corpus, `var-name` is rarely empty and `var-label` is often empty.

  `show` determines whether to show the variable name or the variable
  label.  A value of 1 means to show the name, 2 to show the label, 3
  to show both, and 0 means to use the default specified in
  [`show-variables`](#formats).

* `06`  
  Similar to type `03`, with `fixed` assumed to be true.

* otherwise  
  When the first byte of a `RawValue` is not one of the above, the
  `RawValue` starts with a `ValueMod`, whose syntax is described in
  the next section.  (A `ValueMod` always begins with byte 31 or 58.)

  This case is a template string, analogous to `printf`, followed by
  one or more `Argument`s, each of which has one or more values.  The
  template string is copied directly into the output except for the
  following special syntax:

  * `\%`  
    `\:`  
    `\[`  
    `\]`  
    Each of these expands to the character following `\\`, to
    escape characters that have special meaning in template
    strings.  These are effective inside and outside the `[...]`
    syntax forms described below.

  * `\n`  
    Expands to a new-line, inside or outside the `[...]` forms
    described below.

  * `^I`  
    Expands to a formatted version of argument `I`, which must have
    only a single value.  For example, `^1` expands to the first
    argument's `value`.

  * `[:A:]I`  
    Expands `A` for each of the values in `I`.  `A` should contain one
    or more `^J` conversions, which are drawn from the values for
    argument `I` in order.  Some examples from the corpus:

    * `[:^1:]1`  
      All of the values for the first argument, concatenated.

    * `[:^1\n:]1`  
      Expands to the values for the first argument, each followed by a
      new-line.

    * `[:^1 = ^2:]2`  
      Expands to `X = Y` where X is the second argument's first alue
      and Y is its second value.  (This would be used only if the
      argument has two values.  If there were more values, the second
      and third values would be directly concatenated, which would
      look funny.)

  * `[A:B:]I`  
    This extends the previous form so that the first values are
    expanded using `A` and later values are expanded using `B`.  For
    an unknown reason, within `A` the `^J` conversions are instead
    written as `%J`.  Some examples from the corpus:

    * `[%1:*^1:]1`  
      Expands to all of the values for the first argument,
      separated by `*`.

    * `[%1 = %2:, ^1 = ^2:]1`  
      Given appropriate values for the first argument, expands
      to `X = 1, Y = 2, Z = 3`.

    * `[%1:, ^1:]1`  
      Given appropriate values, expands to `1, 2, 3`.

  The template string is localized to the user's locale.

A writer may safely omit all of the optional 00 bytes at the
beginning of a Value, except that it should write a single 00 byte
before a templated Value.

## ValueMod

A `ValueMod` can specify special modifications to a Value.

```
ValueMod =>
    58
  | 31
    int32[n-refs] int16*[n-refs]
    int32[n-subscripts] string*[n-subscripts]
    v1(00 (i1 | i2) 00? 00? int32 00? 00?)
    v3(count(TemplateString StylePair))

TemplateString => count((count((i0 (58 | 31 55))?) (58 | 31 string[id]))?)

StylePair =>
    (31 FontStyle | 58)
    (31 CellStyle | 58)

FontStyle =>
    bool[bold] bool[italic] bool[underline] bool[show]
    string[fg-color] string[bg-color]
    string[typeface] byte[size]

CellStyle =>
    int32[halign] int32[valign] double[decimal-offset]
    int16[left-margin] int16[right-margin]
    int16[top-margin] int16[bottom-margin]
```

A `ValueMod` that begins with `31` specifies special modifications to
a `Value`.

Each of the `n-refs` integers is a reference to a
[`Footnote`](#footnotes) by a 0-based index.  Footnote markers are
shown appended to the main text of the `Value`, as superscripts or
subscripts.

The `subscripts`, if present, are strings to append to the main text
of the Value, as subscripts.  Each subscript text is a brief indicator,
e.g. `a` or `b`, with its meaning indicated by the table caption.  When
multiple subscripts are present, they are displayed separated by commas.

The `id` inside the `TemplateString`, if present, is a template string
for substitutions using the syntax explained previously.  It appears
to be an English-language version of the localized template string in
the Value in which the `Template` is nested.  A writer may safely omit
the optional fixed data in `TemplateString`.

`FontStyle` and `CellStyle`, if present, change the style for this
individual Value.  In `FontStyle`, `bold`, `italic`, and `underline`
control the particular style.  `show` is ordinarily 1; if it is 0, then
the cell data is not shown.  `fg-color` and `bg-color` are strings in
the format `#rrggbb`, e.g. `#ff0000` for red or `#ffffff` for white.
The empty string is occasionally observed also.  The `size` is a font
size in units of 1/128 inch.

In `CellStyle`, `halign` is 0 for center, 2 for left, 4 for right, 6
for decimal, 0xffffffad for mixed.  For decimal alignment,
`decimal-offset` is the decimal point's offset from the right side of
the cell, in [pt](#pt).  `valign` specifies vertical alignment: 0 for
center, 1 for top, 3 for bottom.  `left-margin`, `right-margin`,
`top-margin`, and `bottom-margin` are in pt.

