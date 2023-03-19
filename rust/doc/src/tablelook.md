# SPSS TableLook File Formats

SPSS has a concept called a TableLook to control the styling of pivot
tables in output.  SPSS 15 and earlier used `.tlo` files with a
special binary format to save TableLooks to disk; SPSS 16 and later
use `.stt` files in an XML format to save them.  Both formats expose
roughly the same features, although the older `.tlo` format does have
some features that `.stt` does not.

This chapter describes both formats.

## The `.stt` Format

The `.stt` file format is an XML file that contains a subset of the
SPV structure member format.  Its root element is a [`tableProperties`
element](spv/legacy-detail-xml.md#legacy-properties).

## The `.tlo` Format

A `.tlo` file has a custom binary format.  This section describes it
using the [binary format
conventions](spv/light-detail.md#binary-format-conventions) used for
SPV binary members.  There is one new convention: TLO files express
colors as `int32` values in which the low 8 bits are the red
component, the next 8 bits are green, and next 8 bits are blue, and
the high bits are zeros.

TLO files support various features that SPV files do not.  PSPP
implements the SPV feature set, so it mostly ignores the added TLO
features.  The details of this mapping are explained below.

At the top level, a TLO file consists of five sections.  The first
four are always present and the last one is optional:

```
TableLook =>
   PTTableLook[tl]
   PVSeparatorStyle[ss]
   PVCellStyle[cs]
   PVTextStyle[ts]
   V2Styles?
```

Each section is described below.

### `PTTableLook`

```
PTTableLook =>
   ff ff 00 00 "PTTableLook" (00|02)[version]
   int16[flags]
   00 00
   bool[nested-row-labels] 00
   bool[footnote-marker-subscripts] 00
   i54 i18
```

   In `PTTableLook`, `version` is 00 or 02.  The only difference is
that version 00 lacks [`V2Styles`](#v2styles) and that version 02
includes it.  Both TLO versions are seen in the wild.

`flags` is a bit-mapped field.  Its bits have the following meanings:

* 0x2: If set to 1, hide empty rows and columns; otherwise, show them.

* 0x4: If set to 1, use numeric footnote markers; otherwise, use
  alphabetic footnote markers.

* 0x8: If set to 1, print all layers; otherwise, print only the
  current layer.

* 0x10: If set to 1, scale the table to fit the page width; otherwise,
  break it horizontally if necessary.

* 0x20: If set to 1, scale the table to fit the page length;
  otherwise, break it vertically if necessary.

* 0x40: If set to 1, print each layer on a separate page (only if all
  layers are being printed); otherwise, paginate layers naturally.

* 0x80: If set to 1, print a continuation string at the top of a table
  that is split between pages.

* 0x100: If set to 1, print a continuation string at the bottom of a
  table that is split between pages.

When `nested-row-labels` is 1, row dimension labels appear nested;
otherwise, they are put into the upper-left corner of the pivot table.

When `footnote-marker-subscripts` is 1, footnote markers are shown as
subscripts; otherwise, they are shown as superscripts.

### `PVSeparatorStyle`

```
PVSeparatorStyle =>
   ff ff 00 00 "PVSeparatorStyle" 00
   Separator*4[sep1]
   03 80 00
   Separator*4[sep2]

Separator =>
   case(
       00 00
     | 01 00 int32[color] int16[style] int16[width]
   )[type]
```

`PVSeparatorStyle` contains eight `Separators`, in two groups.  Each
`Separator` represents a border between pivot table elements.  TLO and
SPV files have the same concepts for borders.  See [Light Member
Borders](spv/light-detail.md#borders), for the treatment of borders in
SPV files.

A `Separator`'s `type` is 00 if the border is not drawn, 01 otherwise.
For a border that is drawn, `color` is the color that it is drawn in.
`style` and `width` have the following meanings:

* `style` = 0 and 0 ≤ `width` ≤ 3  
  An increasingly thick single line.  SPV files only have three line
  thicknesses.  PSPP treats `width` 0 as a thin line, `width` 1 as a
  solid (normal width) line, and `width` 2 or 3 as a thick line.

* `style` = 1 and 0 ≤ `width` ≤ 1  
  A doubled line, composed of normal-width (0) or thick (1) lines.
  SPV files only have "normal" width double lines, so PSPP maps both
  variants the same way.

* `style` = 2  
  A dashed line.

The first group, `sep1`, represents the following borders within the
pivot table, by index:

0. Horizontal dimension rows
1. Vertical dimension rows
2. Horizontal category rows
3. Vertical category rows

The second group, `sep2`, represents the following borders within the
pivot table, by index:

0. Horizontal dimension columns
1. Vertical dimension columns
2. Horizontal category columns
3. Vertical category columns

### `PVCellStyle` and `PVTextStyle`

```
PVCellStyle =>
   ff ff 00 00 "PVCellStyle"
   AreaColor[title-color]

PVTextStyle =>
   ff ff 00 00 "PVTextStyle" 00
   AreaStyle[title-style] MostAreas*7[most-areas]

MostAreas =>
   06 80
   AreaColor[color] 08 80 00 AreaStyle[style]
```

These sections hold the styling and coloring for each of the 8 areas
in a pivot table.  They are conceptually similar to the
[Areas](spv/light-detail.md#areas) style information in SPV light
members.

The styling and coloring for the title area is split between
`PVCellStyle` and `PVTextStyle`: the former holds `title-color`, the
latter holds `title-style`.  The style for the remaining 7 areas is in
`most-areas` in `PVTextStyle`, in the following order: layers, corner,
row labels, column labels, data, caption, and footer.

```
AreaColor =>
   00 01 00 int32[color10] int32[color0] byte[shading] 00
```

`AreaColor` represents the background color of an area.  TLO files, but
not SPV files, describe backgrounds that are a shaded combination of two
colors: `shading` of 0 is pure `color0`, `shading` of 10 is pure
`color10`, and value in between mix pixels of the two different colors
in linear degree.  PSPP does not implement shading, so for 1 ≤ `shading`
≤ 9 it interpolates RGB values between colors to arrive at an
intermediate shade.

```
AreaStyle =>
   int16[valign] int16[halign] int16[decimal-offset]
   int16[left-margin] int16[right-margin] int16[top-margin] int16[bottom-margin]
   00 00 01 00
   int32[font-size] int16[stretch]
   00*2
   int32[rotation-angle]
   00*4
   int16[weight]
   00*2
   bool[italic] bool[underline] bool[strikethrough]
   int32[rtf-charset-number]
   byte[x]
   byte[font-name-len] byte*[font-name-len][font-name]
   int32[text-color]
   00*2
```

`AreaStyle` represents style properties of an area.

`valign` is 0 for top alignment, 1 for bottom alginment, 2 for
center.

`halign` is 0 for left alignment, 1 for right, 2 for center, 3 for
mixed, 4 for decimal.  For decimal alignment, `decimal-offset` is the
offset of the decimal point in 20ths of a point.

`left-margin`, `right-margin`, `top-margin`, and `bottom-margin` are
also measured in 20ths of a point.

`font-size` is negative 96ths of an inch, e.g.  9 point is -12 or
0xfffffff3.

`stretch` has something to do with font size or stretch.  The usual
value is 01 and values larger than that do weird things.  A reader can
safely ignore it.

`rotation-angle` is a font rotation angle.  A reader can safely
ignore it.

`weight` is 400 for a normal-weight font, 700 indicates bold.  (This
is a Windows API convention.)

`italic` and `underline` have the obvious meanings.  So does
`strikethrough`, which PSPP ignores.

`rtf-charset-number` is a character set number from RTF. A reader can
safely ignore it.

The meaning of `x` is unknown.  Values 12, 22, 31, and 32 have been
observed.

The `font-name` is the name of a font, such as `Arial`.  Only
US-ASCII characters have been observed here.

`text-color` is the color of the text itself.

## `V2Styles`

```
V2Styles =>
   Separator*11[sep3]
   byte[continuation-len] byte*[continuation-len][continuation]
   int32[min-col-width] int32[max-col-width]
   int32[min-row-height] int32[max-row-height]
```

This final, optional, part of the TLO file format contains some
additional style information.  It begins with `sep3`, which represents
the following borders within the pivot table, by index:

* 0: Title.
* 1...4: Left, right, top, and bottom inner frame.
* 5...8: Left, right, top, and bottom outer frame.
* 9, 10: Left and top of data area.

When `V2Styles` is absent, the inner frame borders default to a solid
line and the others listed above to no line.

`continuation` is the string that goes at the top or bottom of a
table broken across pages.  When `V2Styles` is absent, the default is
`(Cont.)`.

`min-col-width` is the minimum width that a column will be assigned
automatically.  `max-col-width` is the maximum width that a column
will be assigned to accommodate a long column label.  `min-row-width`
and `max-row-width` are a similar range for the width of row labels.
All of these measurements are in points.  When `V2Styles` is absent,
the defaults are 36 for `min-col-width` and `min-row-height`, 72 for
`max-col-width`, and 120 for `max-row-height`.

