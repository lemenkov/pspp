# Structure Member Format

A structure member lays out the high-level structure for a group of
output items such as heading, tables, and charts.  Structure members do
not include the details of tables and charts but instead refer to them
by their member names.

Structure members' XML files claim conformance with a collection of
XML Schemas.  These schemas are distributed, under a nonfree license,
with SPSS binaries.  Fortunately, the schemas are not necessary to
understand the structure members.  The schemas can even be deceptive
because they document elements and attributes that are not in the corpus
and do not document elements and attributes that are commonly found in
the corpus.

Structure members use a different XML namespace for each schema, but
these namespaces are not entirely consistent.  In some SPV files, for
example, the `viewer-tree` schema is associated with namespace
`http://xml.spss.com/spss/viewer-tree` and in others with
`http://xml.spss.com/spss/viewer/viewer-tree` (note the additional
`viewer/`).  Under either name, the schema URIs are not resolvable to
obtain the schemas themselves.

One may ignore all of the above in interpreting a structure member.
The actual XML has a simple and straightforward form that does not
require a reader to take schemas or namespaces into account.  A
structure member's root is `heading` element, which contains `heading`
or `container` elements (or a mix), forming a tree.  In turn,
`container` holds a `label` and one more child, usually `text` or
`table`.

The following sections document the elements found in structure
members in a context-free grammar-like fashion.  Consider the following
example, which specifies the attributes and content for the `container`
element:

```
container
   :visibility=(visible | hidden)
   :page-break-before=(always)?
   :text-align=(left | center)?
   :width=dimension
=> label (table | container_text | graph | model | object | image | tree)
```

Each attribute specification begins with `:` followed by the
attribute's name.  If the attribute's value has an easily specified
form, then `=` and its description follows the name.  Finally, if the
attribute is optional, the specification ends with `?`.  The following
value specifications are defined:

* `(A | B | ...)`  
  One of the listed literal strings.  If only one string is listed,
  it is the only acceptable value.  If `OTHER` is listed, then any
  string not explicitly listed is also accepted.

* `bool`  
  Either `true` or `false`.

* `dimension`  
  A floating-point number followed by a unit, e.g. `10pt`.  Units in
  the corpus include `in` (inch), `pt` (points, 72/inch), `px`
  ("device-independent pixels", 96/inch), and `cm`.  If the unit is
  omitted then points should be assumed.  The number and unit may be
  separated by white space.

  The corpus also includes localized names for units.  A reader must
  understand these to properly interpret the dimension:

  * inch: `인치`, `pol.`, `cala`, `cali`
  * point: `пт`
  * centimeter: `см`

* `real`  
  A floating-point number.

* `int`  
  An integer.

* `color`  
  A color in one of the forms `#RRGGBB` or `RRGGBB`, or the string
  `transparent`, or one of the standard Web color names.

* `ref`  
  `ref ELEMENT`  
  `ref(ELEM1 | ELEM2 | ...)`  
  The name from the `id` attribute in some element.  If one or more
  elements are named, the name must refer to one of those elements,
  otherwise any element is acceptable.

All elements have an optional `id` attribute.  If present, its value
must be unique.  In practice many elements are assigned `id` attributes
that are never referenced.

The content specification for an element supports the following
syntax:

* `ELEMENT`  
  An element.

* `A B`  
  A followed by B.

* `A | B | C`  
  One of A or B or C.

* `A?`  
  Zero or one instances of A.

* `A*`  
  Zero or more instances of A.

* `B+`  
  One or more instances of A.

* `(SUBEXPRESSION)`  
  Grouping for a subexpression.

* `EMPTY`  
  No content.

* `TEXT`  
  Text and CDATA.

Element and attribute names are sometimes suffixed by another name in
square brackets to distinguish different uses of the same name.  For
example, structure XML has two `text` elements, one inside `container`,
the other inside `pageParagraph`.  The former is defined as
`text[container_text]` and referenced as `container_text`, the latter
defined as `text[pageParagraph_text]` and referenced as
`pageParagraph_text`.

This language is used in the PSPP source code for parsing structure
and detail XML members.  Refer to `src/output/spv/structure-xml.grammar`
and `src/output/spv/detail-xml.grammar` for the full grammars.

The following example shows the contents of a typical structure
member for a DESCRIPTIVES procedure.  A real structure member is not
indented.  This example also omits most attributes, all XML namespace
information, and the CSS from the embedded HTML:

```
<?xml version="1.0" encoding="utf-8"?>
<heading>
  <label>Output</label>
  <heading commandName="Descriptives">
    <label>Descriptives</label>
    <container>
      <label>Title</label>
      <text commandName="Descriptives" type="title">
        <html lang="en">
<![CDATA[<head><style type="text/css">...</style></head><BR>Descriptives]]>
        </html>
      </text>
    </container>
    <container visibility="hidden">
      <label>Notes</label>
      <table commandName="Descriptives" subType="Notes" type="note">
        <tableStructure>
          <dataPath>00000000001_lightNotesData.bin</dataPath>
        </tableStructure>
      </table>
    </container>
    <container>
      <label>Descriptive Statistics</label>
      <table commandName="Descriptives" subType="Descriptive Statistics"
             type="table">
        <tableStructure>
          <dataPath>00000000002_lightTableData.bin</dataPath>
        </tableStructure>
      </table>
    </container>
  </heading>
</heading>
```

<!-- toc -->

## The `heading` Element

```
heading[root_heading]
   :creator-version?
   :creator?
   :creation-date-time?
   :lockReader=bool?
   :schemaLocation?
=> label pageSetup? (container | heading)*

heading
   :creator-version?
   :commandName?
   :visibility[heading_visibility]=(collapsed)?
   :locale?
   :olang?
=> label (container | heading)*
```

A `heading` represents a tree of content that appears in an output
viewer window.  It contains a `label` text string that is shown in the
outline view ordinarily followed by content containers or further nested
(sub)-sections of output.  Unlike heading elements in HTML and other
common document formats, which precede the content that they head,
`heading` contains the elements that appear below the heading.

The root of a structure member is a special `heading`.  The direct
children of the root `heading` elements in all structure members in an
SPV file are siblings.  That is, the root `heading` in all of the
structure members conceptually represent the same node.  The root
heading's `label` is ignored (see [the `label`
element](#the-label-element)).  The root heading in the first
structure member in the Zip file may contain a `pageSetup` element.

The schema implies that any `heading` may contain a sequence of any
number of `heading` and `container` elements.  This does not work for
the root `heading` in practice, which must actually contain exactly one
`container` or `heading` child element.  Furthermore, if the root
heading's child is a `heading`, then the structure member's name must
end in `_heading.xml`; if it is a `container` child, then it must not.

The following attributes have been observed on both document root and
nested `heading` elements.

* `creator-version`  
  The version of the software that created this SPV file.  A string
  of the form `xxyyzzww` represents software version xx.yy.zz.ww,
  e.g. `21000001` is version 21.0.0.1.  Trailing pairs of zeros are
  sometimes omitted, so that `21`, `210000`, and `21000000` are all
  version 21.0.0.0 (and the corpus contains all three of those
  forms).

The following attributes have been observed on document root `heading`
elements only:

* `creator`  
  The directory in the file system of the software that created this
  SPV file.

* `creation-date-time`  
  The date and time at which the SPV file was written, in a
  locale-specific format, e.g. `Friday, May 16, 2014 6:47:37 PM PDT`
  or `lunedì 17 marzo 2014 3.15.48 CET` or even `Friday, December 5,
  2014 5:00:19 o'clock PM EST`.

* `lockReader`  
  Whether a reader should be allowed to edit the output.  The
  possible values are `true` and `false`.  The value `false` is by
  far the most common.

* `schemaLocation`  
  This is actually an XML Namespace attribute.  A reader may ignore
  it.

The following attributes have been observed only on nested `heading`
elements:

* `commandName`  
  A locale-invariant identifier for the command that produced the
  output, e.g. `Frequencies`, `T-Test`, `Non Par Corr`.

* `visibility`  
  If this attribute is absent, the heading's content is expanded in
  the outline view.  If it is set to `collapsed`, it is collapsed.
  (This attribute is never present in a root `heading` because the
  root node is always expanded when a file is loaded, even though the
  UI can be used to collapse it interactively.)

* `locale`  
  The locale used for output, in Windows format, which is similar to
  the format used in Unix with the underscore replaced by a hyphen,
  e.g. `en-US`, `en-GB`, `el-GR`, `sr-Cryl-RS`.

* `olang`  
  The output language, e.g. `en`, `it`, `es`, `de`, `pt-BR`.

## The `label` Element

```
label => TEXT
```

Every `heading` and `container` holds a `label` as its first child.
The label text is what appears in the outline pane of the GUI's viewer
window.  PSPP also puts it into the outline of PDF output.  The label
text doesn't appear in the output itself.

The text in `label` describes what it labels, often by naming the
statistical procedure that was executed, e.g. "Frequencies" or "T-Test".
Labels are often very generic, especially within a `container`, e.g.
"Title" or "Warnings" or "Notes".  Label text is localized according to
the output language, e.g. in Italian a frequency table procedure is
labeled "Frequenze".

The user can edit labels to be anything they want.  The corpus
contains a few examples of empty labels, ones that contain no text,
probably as a result of user editing.

The root `heading` in an SPV file has a `label`, like every
`heading`.  It normally contains "Output" but its content is disregarded
anyway.  The user cannot edit it.

## The `container` Element

```
container
   :visibility=(visible | hidden)
   :page-break-before=(always)?
   :text-align=(left | center)?
   :width=dimension
=> label (table | container_text | graph | model | object | image | tree)
```

A `container` serves to contain and label a `table`, `text`, or other
kind of item.

This element has the following attributes.

* `visibility`  
  Whether the container's content is displayed.  "Notes" tables are
  often hidden; other data is usually visible.

* `text-align`  
  Alignment of text within the container.  Observed with nested
  `table` and `text` elements.

* `width`  
  The width of the container, e.g. `1097px`.

All of the elements that nest inside `container` (except the `label`)
have the following optional attribute.

* `commandName`  
  As on the `heading` element.  The corpus contains one example of
  where `commandName` is present but set to the empty string.

## The `text` Element (Inside `container`)

```
text[container_text]
  :type[text_type]=(title | log | text | page-title)
  :commandName?
  :creator-version?
=> html
```
This `text` element is nested inside a `container`.  There is a
different `text` element that is nested inside a `pageParagraph`.

This element has the following attributes.

* `commandName`  
  See [the `container` element](#the-container-element).  For output
  not specific to a command, this is simply `log`.

* `type`  
  The semantics of the text.

* `creator-version`  
  As on the `heading` element.

## The `html` Element

```
html :lang=(en) => TEXT
```

The element contains an HTML document as text (or, in practice, as
CDATA). In some cases, the document starts with `<html>` and ends with
`</html>`; in others the `html` element is implied.  Generally the HTML
includes a `head` element with a CSS stylesheet.  The HTML body often
begins with `<BR>`.

The HTML document uses only the following elements:

* `html`  
  Sometimes, the document is enclosed with `<html>`...`</html>`.

* `br`  
  The HTML body often begins with `<BR>` and may contain it as well.

* `b`  
  `i`  
  `u`  
  Styling.

* `font`  
  The attributes `face`, `color`, and `size` are observed.  The value
  of `color` takes one of the forms `#RRGGBB` or `rgb (R, G, B)`.
  The value of `size` is a number between 1 and 7, inclusive.

The CSS in the corpus is simple.  To understand it, a parser only
needs to be able to skip white space, `<!--`, and `-->`, and parse style
only for `p` elements.  Only the following properties matter:

* `color`  
  In the form `RRGGBB`, e.g.  `000000`, with no leading `#`.

* `font-weight`  
  Either `bold` or `normal`.

* `font-style`  
  Either `italic` or `normal`.

* `text-decoration`  
  Either `underline` or `normal`.

* `font-family`  
  A font name, commonly `Monospaced` or `SansSerif`.

* `font-size`  
  Values claim to be in points, e.g. `14pt`, but the values are
  actually in "device-independent pixels" (px), at 96/inch.

This element has the following attributes.

* `lang`  
  This always contains `en` in the corpus.

## The `table` Element

```
table
   :VDPId?
   :ViZmlSource?
   :activePageId=int?
   :commandName
   :creator-version?
   :displayFiltering=bool?
   :maxNumCells=int?
   :orphanTolerance=int?
   :rowBreakNumber=int?
   :subType
   :tableId
   :tableLookId?
   :type[table_type]=(table | note | warning)
=> tableProperties? tableStructure

tableStructure => path? dataPath csvPath?
```

This element has the following attributes.

* `commandName`  
  See [the `container` element](#the-container-element).

* `type`  
  One of `table`, `note`, or `warning`.

* `subType`  
  The locale-invariant command ID for the particular kind of output
  that this table represents in the procedure.  This can be the same
  as `commandName` e.g. `Frequencies`, or different, e.g. `Case
  Processing Summary`.  Generic subtypes `Notes` and `Warnings` are
  often used.

* `tableId`  
  A number that uniquely identifies the table within the SPV file,
  typically a large negative number such as `-4147135649387905023`.

* `creator-version`  
  As on the `heading` element.  In the corpus, this is only present
  for version 21 and up and always includes all 8 digits.

See [Legacy Properties](legacy-detail-xml.md#legacy-properties), for
details on the `tableProperties` element.

## The `graph` Element

```
graph
   :VDPId?
   :ViZmlSource?
   :commandName?
   :creator-version?
   :dataMapId?
   :dataMapURI?
   :editor?
   :refMapId?
   :refMapURI?
   :csvFileIds?
   :csvFileNames?
=> dataPath? path csvPath?
```

This element represents a graph.  The `dataPath` and `path` elements
name the Zip members that give the details of the graph.  Normally, both
elements are present; there is only one counterexample in the corpus.

`csvPath` only appears in one SPV file in the corpus, for two graphs.
In these two cases, `dataPath`, `path`, and `csvPath` all appear.  These
`csvPath` name Zip members with names of the form `NUMBER_csv.bin`,
where `NUMBER` is a many-digit number and the same as the `csvFileIds`.
The named Zip members are CSV text files (despite the `.bin` extension).
The CSV files are encoded in UTF-8 and begin with a U+FEFF byte-order
marker.

## The `model` Element

```
model
   :PMMLContainerId?
   :PMMLId
   :StatXMLContainerId
   :VDPId
   :auxiliaryViewName
   :commandName
   :creator-version
   :mainViewName
=> ViZml? dataPath? path | pmmlContainerPath statsContainerPath

pmmlContainerPath => TEXT

statsContainerPath => TEXT

ViZml :viewName? => TEXT
```

This element represents a model.  The `dataPath` and `path` elements
name the Zip members that give the details of the model.  Normally, both
elements are present; there is only one counterexample in the corpus.

The details are unexplored.  The `ViZml` element contains base-64
encoded text, that decodes to a binary format with some embedded text
strings, and `path` names an Zip member that contains XML.
Alternatively, `pmmlContainerPath` and `statsContainerPath` name Zip
members with `.scf` extension.

## The `object` and `image` Elements

```
object
   :commandName?
   :type[object_type]=(unknown)?
   :uri
=> EMPTY

image
   :commandName?
   :VDPId
=> dataPath
```

These two elements represent an image in PNG format.  They are
equivalent and the corpus contains examples of both.  The only
difference is the syntax: for `object`, the `uri` attribute names the
Zip member that contains a PNG file; for `image`, the text of the inner
`dataPath` element names the Zip member.

PSPP writes `object` in output but there is no strong reason to
choose this form.

The corpus only contains PNG image files.

## The `tree` Element

```
tree
   :commandName
   :creator-version
   :name
   :type
=> dataPath path
```

This element represents a tree.  The `dataPath` and `path` elements
name the Zip members that give the details of the tree.  The details are
unexplored.

## Path Elements

```
dataPath => TEXT

path => TEXT

csvPath => TEXT
```

These element contain the name of the Zip members that hold details
for a container.  For tables:

- When a "light" format is used, only `dataPath` is present, and it
  names a `.bin` member of the Zip file that has `light` in its name,
  e.g. `0000000001437_lightTableData.bin`.  See [Light Detail Member
  Format](light-detail.md) for light format details.

- When the legacy format is used, both are present.  In this case,
  `dataPath` names a Zip member with a legacy binary format that
  contains relevant data (see [Legacy Detail Member Binary
  Format](legacy-detail-binary.md)), and `path` names a Zip member
  that uses an XML format (see [Legacy Detail Member XML Member
  Format](legacy-detail-xml.md)).

Graphs normally follow the legacy approach described above.  The
corpus contains one example of a graph with `path` but not `dataPath`.
The reason is unexplored.

Models use `path` but not `dataPath`.  See [`graph`
element](#the-graph-element), for more information.

These elements have no attributes.

## The `pageSetup` Element

```
pageSetup
   :initial-page-number=int?
   :chart-size=(as-is | full-height | half-height | quarter-height | OTHER)?
   :margin-left=dimension?
   :margin-right=dimension?
   :margin-top=dimension?
   :margin-bottom=dimension?
   :paper-height=dimension?
   :paper-width=dimension?
   :reference-orientation?
   :space-after=dimension?
=> pageHeader pageFooter

pageHeader => pageParagraph?

pageFooter => pageParagraph?

pageParagraph => pageParagraph_text
```

The `pageSetup` element has the following attributes.

* `initial-page-number`  
     The page number to put on the first page of printed output.
     Usually `1`.

* `chart-size`  
     One of the listed, self-explanatory chart sizes, `quarter-height`,
     or a localization (!)  of one of these (e.g. `dimensione attuale`,
     `Wie vorgegeben`).

* `margin-left`  
* `margin-right`  
* `margin-top`  
* `margin-bottom`  
     Margin sizes, e.g. `0.25in`.

* `paper-height`  
* `paper-width`  
     Paper sizes.

* `reference-orientation`  
     Indicates the orientation of the output page.  Either `0deg`
     (portrait) or `90deg` (landscape),

* `space-after`  
     The amount of space between printed objects, typically `12pt`.

## The `text` Element (Inside `pageParagraph`)

```
text[pageParagraph_text] :type=(title | text) => TEXT
```

This `text` element is nested inside a `pageParagraph`.  There is a
different `text` element that is nested inside a `container`.

The element is either empty, or contains CDATA that holds almost-XHTML
text: in the corpus, either an `html` or `p` element.  It is
_almost_-XHTML because the `html` element designates the default
namespace as `http://xml.spss.com/spss/viewer/viewer-tree` instead of
an XHTML namespace, and because the CDATA can contain substitution
variables.  The following variables are supported:

* `&[Date]`  
  `&[Time]`  
  The current date or time in the preferred format for the locale.

* `&[Head1]`  
  `&[Head2]`  
  `&[Head3]`  
  `&[Head4]`  
  First-, second-, third-, or fourth-level heading.

* `&[PageTitle]`  
  The page title.

* `&[Filename]`  
  Name of the output file.

* `&[Page]`  
  The page number.

Typical contents (indented for clarity):

```
<html xmlns="http://xml.spss.com/spss/viewer/viewer-tree">
    <head></head>
    <body>
        <p style="text-align:right; margin-top: 0">Page &[Page]</p>
    </body>
</html>
```

This element has the following attributes.

* `type`  
  Always `text`.

