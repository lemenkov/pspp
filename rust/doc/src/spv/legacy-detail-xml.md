# Legacy Detail XML Member Format

The design of the detail XML format is not what one would end up with
for describing pivot tables.  This is because it is a special case of a
much more general format ("visualization XML" or "VizML") that can
describe a wide range of visualizations.  Most of this generality is
overkill for tables, and so we end up with a funny subset of a
general-purpose format.

An XML Schema for VizML is available, distributed with SPSS binaries,
under a nonfree license.  It contains documentation that is occasionally
helpful.

This section describes the detail XML format using the same notation
already used for the [structure XML format](structure.md).  See
`src/output/spv/detail-xml.grammar` in the PSPP source tree for the
full grammar that it uses for parsing.

The important elements of the detail XML format are:

- [Variables](#variable-elements).

- Assignment of variables to axes.  A variable can appear as columns,
  or rows, or layers.  The `faceting` element and its sub-elements
  describe this assignment.

- Styles and other annotations.

This description is not detailed enough to write legacy tables.
Instead, write tables in the light binary format.

<!-- toc -->

## The `visualization` Element

```
visualization
   :creator
   :date
   :lang
   :name
   :style[style_ref]=ref style
   :type
   :version
   :schemaLocation?
=> visualization_extension?
   userSource
   (sourceVariable | derivedVariable)+
   categoricalDomain?
   graph
   labelFrame[lf1]*
   container?
   labelFrame[lf2]*
   style+
   layerController?

extension[visualization_extension]
   :numRows=int?
   :showGridline=bool?
   :minWidthSet=(true)?
   :maxWidthSet=(true)?
=> EMPTY

userSource :missing=(listwise | pairwise)? => EMPTY

categoricalDomain => variableReference simpleSort

simpleSort :method[sort_method]=(custom) => categoryOrder

container :style=ref style => container_extension? location+ labelFrame*

extension[container_extension] :combinedFootnotes=(true) => EMPTY

layerController
   :source=(tableData)
   :target=ref label?
=> EMPTY
```

The `visualization` element is the root of detail XML member.  It has
the following attributes:

* `creator`  
  The version of the software that created this SPV file, as a string
  of the form `xxyyzz`, which represents software version xx.yy.zz,
  e.g. `160001` is version 16.0.1.  The corpus includes major
  versions 16 through 19.

* `date`  
  The date on the which the file was created, as a string of the form
  `YYYY-MM-DD`.

* `lang`  
  The locale used for output, in Windows format, which is similar to
  the format used in Unix with the underscore replaced by a hyphen,
  e.g. `en-US`, `en-GB`, `el-GR`, `sr-Cryl-RS`.

* `name`  
  The title of the pivot table, localized to the output language.

* `style`  
  The base style for the pivot table.  In every example in the
  corpus, the `style` element has no attributes other than `id`.

* `type`  
  A floating-point number.  The meaning is unknown.

* `version`  
  The visualization schema version number.  In the corpus, the value
  is one of 2.4, 2.5, 2.7, and 2.8.

The `userSource` element has no visible effect.

The `extension` element as a child of `visualization` has the
following attributes.

* `numRows`  
  An integer that presumably defines the number of rows in the
  displayed pivot table.

* `showGridline`  
  Always set to `false` in the corpus.

* `minWidthSet`  
* `maxWidthSet`  
  Always set to `true` in the corpus.

The `extension` element as a child of `container` has the following
attribute

* `combinedFootnotes`  
  Meaning unknown.

The `categoricalDomain` and `simpleSort` elements have no visible
effect.

The `layerController` element has no visible effect.

## Variable Elements

A "variable" in detail XML is a 1-dimensional array of data.  Each
element of the array may, independently, have string or numeric content.
All of the variables in a given detail XML member either have the same
number of elements or have zero elements.

Two different elements define variables and their content:

* `sourceVariable`  
  These variables' data comes from the associated `tableData.bin`
  member.

* `derivedVariable`  
  These variables are defined in terms of a mapping function from a
  source variable, or they are empty.

A variable named `cell` always exists.  This variable holds the data
displayed in the table.

Variables in detail XML roughly correspond to the dimensions in a
light detail member.  Each dimension has the following variables with
stylized names, where N is a number for the dimension starting from 0:

* `dimensionNcategories`  
  The dimension's leaf [categories](light-detail.md#categories).

* `dimensionNgroup0`  
  Present only if the dimension's categories are grouped, this
  variable holds the group labels for the categories.  Grouping is
  inferred through adjacent identical labels.  Categories that are
  not part of a group have empty-string data in this variable.

* `dimensionNgroup1`  
  Present only if the first-level groups are further grouped, this
  variable holds the labels for the second-level groups.  There can
  be additional variables with further levels of grouping.

* `dimensionN`  
  An empty variable.

   Determining the data for a (non-empty) variable is a multi-step
process:

1. Draw initial data from its source, for a `sourceVariable`, or from
   another named variable, for a `derivedVariable`.

2. Apply mappings from `valueMapEntry` elements within the
   `derivedVariable` element, if any.

3. Apply mappings from `relabel` elements within a `format` or
   `stringFormat` element in the `sourceVariable` or `derivedVariable`
   element, if any.

4. If the variable is a `sourceVariable` with a `labelVariable`
   attribute, and there were no mappings to apply in previous steps,
   then replace each element of the variable by the corresponding
   value in the label variable.

A single variable's data can be modified in two of the steps, if both
`valueMapEntry` and `relabel` are used.  The following example from
the corpus maps several integers to 2, then maps 2 in turn to the
string "Input":

```
<derivedVariable categorical="true" dependsOn="dimension0categories"
                 id="dimension0group0map" value="map(dimension0group0)">
  <stringFormat>
    <relabel from="2" to="Input"/>
    <relabel from="10" to="Missing Value Handling"/>
    <relabel from="14" to="Resources"/>
    <relabel from="0" to=""/>
    <relabel from="1" to=""/>
    <relabel from="13" to=""/>
  </stringFormat>
  <valueMapEntry from="2;3;5;6;7;8;9" to="2"/>
  <valueMapEntry from="10;11" to="10"/>
  <valueMapEntry from="14;15" to="14"/>
  <valueMapEntry from="0" to="0"/>
  <valueMapEntry from="1" to="1"/>
  <valueMapEntry from="13" to="13"/>
</derivedVariable>
```

### The `sourceVariable` Element

```
sourceVariable
   :id
   :categorical=(true)
   :source
   :domain=ref categoricalDomain?
   :sourceName
   :dependsOn=ref sourceVariable?
   :label?
   :labelVariable=ref sourceVariable?
=> variable_extension* (format | stringFormat)?
```

This element defines a variable whose data comes from the
`tableData.bin` member that corresponds to this `.xml`.

This element has the following attributes.

* `id`  
  An `id` is always present because this element exists to be
  referenced from other elements.

* `categorical`  
  Always set to `true`.

* `source`  
  Always set to `tableData`, the `source-name` in the corresponding
  `tableData.bin` member (see
  [Metadata](legacy-detail-binary.md#metadata)).

* `sourceName`  
  The name of a variable within the source, corresponding to the
  `variable-name` in the `tableData.bin` member (see [Numeric
  Data](legacy-detail-binary.md#numeric-data)).

* `label`  
  The variable label, if any.

* `labelVariable`  
  The `variable-name` of a variable whose string values correspond
  one-to-one with the values of this variable and are suitable for
  use as value labels.

* `dependsOn`  
  This attribute doesn't affect the display of a table.

### The `derivedVariable` Element

```
derivedVariable
   :id
   :categorical=(true)
   :value
   :dependsOn=ref sourceVariable?
=> variable_extension* (format | stringFormat)? valueMapEntry*
```

   Like `sourceVariable`, this element defines a variable whose values
can be used elsewhere in the visualization.  Instead of being read from
a data source, the variable's data are defined by a mathematical
expression.

   This element has the following attributes.

* `id`  
  An `id` is always present because this element exists to be
  referenced from other elements.

* `categorical`  
  Always set to `true`.

* `value`  
  An expression that defines the variable's value.  In theory this
  could be an arbitrary expression in terms of constants, functions,
  and other variables, e.g. (VAR1 + VAR2) / 2.  In practice, the
  corpus contains only the following forms of expressions:

  - `constant(0)`  
    `constant(VARIABLE)`  
    All zeros.  The reason why a variable is sometimes named is
    unknown.  Sometimes the "variable name" has spaces in it.

  - `map(VARIABLE)`  
    Transforms the values in the named VARIABLE using the
    `valueMapEntry`s contained within the element.

* `dependsOn`  
  This attribute doesn't affect the display of a table.

### The `valueMapEntry` Element

```
valueMapEntry :from :to => EMPTY
```

A `valueMapEntry` element defines a mapping from one or more values
of a source expression to a target value.  (In the corpus, the source
expression is always just the name of a variable.)  Each target value
requires a separate `valueMapEntry`.  If multiple source values map to
the same target value, they can be combined or separate.

In the corpus, all of the source and target values are integers.

`valueMapEntry` has the following attributes.

* `from`  
  A source value, or multiple source values separated by semicolons,
  e.g. `0` or `13;14;15;16`.

* `to`  
  The target value, e.g. `0`.

## The `extension` Element

This is a general-purpose "extension" element.  Readers that don't
understand a given extension should be able to safely ignore it.  The
attributes on this element, and their meanings, vary based on the
context.  Each known usage is described separately below.  The current
extensions use attributes exclusively, without any nested elements.

### `container` Parent Element

```
extension[container_extension] :combinedFootnotes=(true) => EMPTY
```

With `container` as its parent element, `extension` has the following
attributes.

* `combinedFootnotes`  
     Always set to `true` in the corpus.

### `sourceVariable` and `derivedVariable` Parent Element

```
extension[variable_extension] :from :helpId => EMPTY
```

With `sourceVariable` or `derivedVariable` as its parent element,
`extension` has the following attributes.  A given parent element
often contains several `extension` elements that specify the meaning
of the source data's variables or sources, e.g.

```
<extension from="0" helpId="corrected_model"/>
<extension from="3" helpId="error"/>
<extension from="4" helpId="total_9"/>
<extension from="5" helpId="corrected_total"/>
```

More commonly they are less helpful, e.g.

```
<extension from="0" helpId="notes"/>
<extension from="1" helpId="notes"/>
<extension from="2" helpId="notes"/>
<extension from="5" helpId="notes"/>
<extension from="6" helpId="notes"/>
<extension from="7" helpId="notes"/>
<extension from="8" helpId="notes"/>
<extension from="12" helpId="notes"/>
<extension from="13" helpId="no_help"/>
<extension from="14" helpId="notes"/>
```

* `from`  
  An integer or a name like "dimension0".

* `helpId`  
  An identifier.

## The `graph` Element

```
graph
   :cellStyle=ref style
   :style=ref style
=> location+ coordinates faceting facetLayout interval

coordinates => EMPTY
```

`graph` has the following attributes.

* `cellStyle`  
  `style`  
  Each of these is the `id` of a [`style`
  element](#the-style-element).  The former is the default style for
  individual cells, the latter for the entire table.

## The `location` Element

```
location
   :part=(height | width | top | bottom | left | right)
   :method=(sizeToContent | attach | fixed | same)
   :min=dimension?
   :max=dimension?
   :target=ref (labelFrame | graph | container)?
   :value?
=> EMPTY
```

Each instance of this element specifies where some part of the table
frame is located.  All the examples in the corpus have four instances
of this element, one for each of the parts `height`, `width`, `left`,
and `top`.  Some examples in the corpus add a fifth for part `bottom`,
even though it is not clear how all of `top`, `bottom`, and `height`
can be honored at the same time.  In any case, `location` seems to
have little importance in representing tables; a reader can safely
ignore it.

* `part`  
  The part of the table being located.

* `method`  
  How the location is determined:

  * `sizeToContent`  
    Based on the natural size of the table.  Observed only for
    parts `height` and `width`.

  * `attach`  
    Based on the location specified in `target`.  Observed only
    for parts `top` and `bottom`.

  * `fixed`  
    Using the value in `value`.  Observed only for parts `top`,
    `bottom`, and `left`.

  * `same`  
    Same as the specified `target`.  Observed only for part
    `left`.

* `min`  
  Minimum size.  Only observed with value `100pt`.  Only observed for
  part `width`.

* `target`  
  Required when `method` is `attach` or `same`, not observed
  otherwise.  This identifies an element to attach to.  Observed with
  the ID of `title`, `footnote`, `graph`, and other elements.

* `value`  
  Required when `method` is `fixed`, not observed otherwise.
  Observed values are `0%`, `0px`, `1px`, and `3px` on parts `top`
  and `left`, and `100%` on part `bottom`.

## The `faceting` Element

```
faceting => layer[layers1]* cross layer[layers2]*

cross => (unity | nest) (unity | nest)

unity => EMPTY

nest => variableReference[vars]+

variableReference :ref=ref (sourceVariable | derivedVariable) => EMPTY

layer
   :variable=ref (sourceVariable | derivedVariable)
   :value
   :visible=bool?
   :method[layer_method]=(nest)?
   :titleVisible=bool?
=> EMPTY
```

The `faceting` element describes the row, column, and layer structure
of the table.  Its `cross` child determines the row and column
structure, and each `layer` child (if any) represents a layer.  Layers
may appear before or after `cross`.

The `cross` element describes the row and column structure of the
table.  It has exactly two children, the first of which describes the
table's columns and the second the table's rows.  Each child is a `nest`
element if the table has any dimensions along the axis in question,
otherwise a `unity` element.

A `nest` element contains of one or more dimensions listed from
innermost to outermost, each represented by `variableReference` child
elements.  Each variable in a dimension is listed in order.  See
[Variable Elements](#variable-elements), for information on the
variables that comprise a dimension.

A `nest` can contain a single dimension, e.g.:

```
<nest>
  <variableReference ref="dimension0categories"/>
  <variableReference ref="dimension0group0"/>
  <variableReference ref="dimension0"/>
</nest>
```
A `nest` can contain multiple dimensions, e.g.:

```
<nest>
  <variableReference ref="dimension1categories"/>
  <variableReference ref="dimension1group0"/>
  <variableReference ref="dimension1"/>
  <variableReference ref="dimension0categories"/>
  <variableReference ref="dimension0"/>
</nest>
```

A `nest` may have no dimensions, in which case it still has one
`variableReference` child, which references a `derivedVariable` whose
`value` attribute is `constant(0)`.  In the corpus, such a
`derivedVariable` has `row` or `column`, respectively, as its `id`.
This is equivalent to using a `unity` element in place of `nest`.

A `variableReference` element refers to a variable through its `ref`
attribute.

Each `layer` element represents a dimension, e.g.:

```
<layer value="0" variable="dimension0categories" visible="true"/>
<layer value="dimension0" variable="dimension0" visible="false"/>
```

`layer` has the following attributes.

* `variable`  
  Refers to a `sourceVariable` or `derivedVariable` element.

* `value`  
  The value to select.  For a category variable, this is always `0`;
  for a data variable, it is the same as the `variable` attribute.

* `visible`  
  Whether the layer is visible.  Generally, category layers are
  visible and data layers are not, but sometimes this attribute is
  omitted.

* `method`  
  When present, this is always `nest`.

## The `facetLayout` Element

```
facetLayout => tableLayout setCellProperties[scp1]*
               facetLevel+ setCellProperties[scp2]*

tableLayout
   :verticalTitlesInCorner=bool
   :style=ref style?
   :fitCells=(ticks both)?
=> EMPTY
```
The `facetLayout` element and its descendants control styling for the
table.

Its `tableLayout` child has the following attributes

* `verticalTitlesInCorner`  
   If true, in the absence of corner text, row headings will be
   displayed in the corner.

* `style`  
   Refers to a `style` element.

* `fitCells`  
  Meaning unknown.

### The `facetLevel` Element

```
facetLevel :level=int :gap=dimension? => axis

axis :style=ref style => label? majorTicks

majorTicks
   :labelAngle=int
   :length=dimension
   :style=ref style
   :tickFrameStyle=ref style
   :labelFrequency=int?
   :stagger=bool?
=> gridline?

gridline
   :style=ref style
   :zOrder=int
=> EMPTY
```

Each `facetLevel` describes a `variableReference` or `layer`, and a
table has one `facetLevel` element for each such element.  For example,
an SPV detail member that contains four `variableReference` elements and
two `layer` elements will contain six `facetLevel` elements.

In the corpus, `facetLevel` elements and the elements that they
describe are always in the same order.  The correspondence may also be
observed in two other ways.  First, one may use the `level` attribute,
described below.  Second, in the corpus, a `facetLevel` always has an
`id` that is the same as the `id` of the element it describes with
`_facetLevel` appended.  One should not formally rely on this, of
course, but it is usefully indicative.

* `level`  
  A 1-based index into the `variableReference` and `layer` elements,
  e.g. a `facetLayout` with a `level` of 1 describes the first
  `variableReference` in the SPV detail member, and in a member with
  four `variableReference` elements, a `facetLayout` with a `level`
  of 5 describes the first `layer` in the member.

* `gap`  
  Always observed as `0pt`.

Each `facetLevel` contains an `axis`, which in turn may contain a
[`label`](#the-label-element) for the `facetLevel` and does contain a
`majorTicks` element.

* `labelAngle`  
  Normally 0.  The value -90 causes inner column or outer row labels
  to be rotated vertically.

* `style`  
* `tickFrameStyle`  
  Each refers to a `style` element.  `style` is the style of the tick
  labels, `tickFrameStyle` the style for the frames around the
  labels.

## The `label` Element

```
label
   :style=ref style
   :textFrameStyle=ref style?
   :purpose=(title | subTitle | subSubTitle | layer | footnote)?
=> text+ | descriptionGroup

descriptionGroup
   :target=ref faceting
   :separator?
=> (description | text)+

description :name=(variable | value) => EMPTY

text
   :usesReference=int?
   :definesReference=int?
   :position=(subscript | superscript)?
   :style=ref style
=> TEXT
```

This element represents a label on some aspect of the table.

* `style`  
  `textFrameStyle`  
  Each of these refers to a `style` element.  `style` is the style of
  the label text, `textFrameStyle` the style for the frame around the
  label.

* `purpose`  
  The kind of entity being labeled.

A `descriptionGroup` concatenates one or more elements to form a
label.  Each element can be a `text` element, which contains literal
text, or a `description` element that substitutes a value or a variable
name.

* `target`  
  The `id` of an element being described.  In the corpus, this is
  always `faceting`.

* `separator`  
  A string to separate the description of multiple groups, if the
  `target` has more than one.  In the corpus, this is always a
  new-line.

Typical contents for a `descriptionGroup` are a value by itself:
```
<description name="value"/>
```
or a variable and its value, separated by a colon:
```
<description name="variable"/><text>:</text><description name="value"/>
```

A `description` is like a macro that expands to some property of the
target of its parent `descriptionGroup`.  The `name` attribute specifies
the property.

## The `setCellProperties` Element

```
setCellProperties
   :applyToConverse=bool?
=> (setStyle | setFrameStyle | setFormat | setMetaData)* union[union_]?
```

The `setCellProperties` element sets style properties of cells or row
or column labels.

Interpreting `setCellProperties` requires answering two questions:
which cells or labels to style, and what styles to use.

### Which Cells?

```
union => intersect+

intersect => where+ | intersectWhere | alternating | EMPTY

where
   :variable=ref (sourceVariable | derivedVariable)
   :include
=> EMPTY

intersectWhere
   :variable=ref (sourceVariable | derivedVariable)
   :variable2=ref (sourceVariable | derivedVariable)
=> EMPTY

alternating => EMPTY
```

When `union` is present with `intersect` children, each of those
children specifies a group of cells that should be styled, and the total
group is all those cells taken together.  When `union` is absent, every
cell is styled.  One attribute on `setCellProperties` affects the choice
of cells:

* `applyToConverse`  
  If true, this inverts the meaning of the cell selection: the
  selected cells are the ones _not_ designated.  This is confusing,
  given the additional restrictions of `union`, but in the corpus
  `applyToConverse` is never present along with `union`.

An `intersect` specifies restrictions on the cells to be matched.
Each `where` child specifies which values of a given variable to
include.  The attributes of `intersect` are:

* `variable`  
  Refers to a variable, e.g. `dimension0categories`.  Only
  "categories" variables make sense here, but other variables, e.g.
  `dimension0group0map`, are sometimes seen.  The reader may ignore
  these.

* `include`  
  A value, or multiple values separated by semicolons, e.g. `0` or
  `13;14;15;16`.

PSPP ignores `setCellProperties` when `intersectWhere` is present.

### What Styles?

```
setStyle
   :target=ref (labeling | graph | interval | majorTicks)
   :style=ref style
=> EMPTY

setMetaData :target=ref graph :key :value => EMPTY

setFormat
   :target=ref (majorTicks | labeling)
   :reset=bool?
=> format | numberFormat | stringFormat+ | dateTimeFormat | elapsedTimeFormat

setFrameStyle
   :style=ref style
   :target=ref majorTicks
=> EMPTY
```

The `set*` children of `setCellProperties` determine the styles to
set.

When `setCellProperties` contains a `setFormat` whose `target`
references a `labeling` element, or if it contains a `setStyle` that
references a `labeling` or `interval` element, the `setCellProperties`
sets the style for table cells.  The format from the `setFormat`, if
present, replaces the cells' format.  The style from the `setStyle` that
references `labeling`, if present, replaces the label's font and cell
styles, except that the background color is taken instead from the
`interval`'s style, if present.

When `setCellProperties` contains a `setFormat` whose `target`
references a `majorTicks` element, or if it contains a `setStyle` whose
`target` references a `majorTicks`, or if it contains a `setFrameStyle`
element, the `setCellProperties` sets the style for row or column
labels.  In this case, the `setCellProperties` always contains a single
`where` element whose `variable` designates the variable whose labels
are to be styled.  The format from the `setFormat`, if present, replaces
the labels' format.  The style from the `setStyle` that references
`majorTicks`, if present, replaces the labels' font and cell styles,
except that the background color is taken instead from the
`setFrameStyle`'s style, if present.

When `setCellProperties` contains a `setStyle` whose `target`
references a `graph` element, and one that references a `labeling`
element, and the `union` element contains `alternating`, the
`setCellProperties` sets the alternate foreground and background colors
for the data area.  The foreground color is taken from the style
referenced by the `setStyle` that targets the `graph`, the background
color from the `setStyle` for `labeling`.

A reader may ignore a `setCellProperties` that only contains
`setMetaData`, as well as `setMetaData` within other
`setCellProperties`.

A reader may ignore a `setCellProperties` whose only `set*` child is
a `setStyle` that targets the `graph` element.

### The `setStyle` Element

```
setStyle
   :target=ref (labeling | graph | interval | majorTicks)
   :style=ref style
=> EMPTY
```

This element associates a style with the target.

* `target`  
  The `id` of an element whose style is to be set.

* `style`  
  The `id` of a `style` element that identifies the style to set on
  the target.

## The `setFormat` Element

```
setFormat
   :target=ref (majorTicks | labeling)
   :reset=bool?
=> format | numberFormat | stringFormat+ | dateTimeFormat | elapsedTimeFormat
```

This element sets the format of the target, "format" in this case
meaning the SPSS print format for a variable.

The details of this element vary depending on the schema version, as
declared in the root [`visualization`
element](#the-visualization-element)'s `version` attribute.  A reader
can interpret the content without knowing the schema version.

The `setFormat` element itself has the following attributes.

* `target`  
  Refers to an element whose style is to be set.

* `reset`  
  If this is `true`, this format replaces the target's previous
  format.  If it is `false`, the modifies the previous format.

### The `numberFormat` Element

```
numberFormat
   :minimumIntegerDigits=int?
   :maximumFractionDigits=int?
   :minimumFractionDigits=int?
   :useGrouping=bool?
   :scientific=(onlyForSmall | whenNeeded | true | false)?
   :small=real?
   :prefix?
   :suffix?
=> affix*
```

Specifies a format for displaying a number.  The available options
are a superset of those available from PSPP print formats.  PSPP chooses
a print format type for a `numberFormat` as follows:

1. If `scientific` is `true`, uses `E` format.

2. If `prefix` is `$`, uses `DOLLAR` format.

3. If `suffix` is `%`, uses `PCT` format.

4. If `useGrouping` is `true`, uses `COMMA` format.

5. Otherwise, uses `F` format.

For translating to a print format, PSPP uses `maximumFractionDigits`
as the number of decimals, unless that attribute is missing or out of
the range \[0,15\], in which case it uses 2 decimals.

* `minimumIntegerDigits`  
  Minimum number of digits to display before the decimal point.
  Always observed as `0`.

* `maximumFractionDigits`  
  `minimumFractionDigits`  
  Maximum or minimum, respectively, number of digits to display after
  the decimal point.  The observed values of each attribute range
  from 0 to 9.

* `useGrouping`  
  Whether to use the grouping character to group digits in large
  numbers.

* `scientific`  
  This attribute controls when and whether the number is formatted in
  scientific notation.  It takes the following values:

  * `onlyForSmall`  
    Use scientific notation only when the number's magnitude is
    smaller than the value of the `small` attribute.

  * `whenNeeded`  
    Use scientific notation when the number will not otherwise fit
    in the available space.

  * `true`  
    Always use scientific notation.  Not observed in the corpus.

  * `false`  
    Never use scientific notation.  A number that won't otherwise
    fit will be replaced by an error indication (see the
    `errorCharacter` attribute).  Not observed in the corpus.

* `small`  
  Only present when the `scientific` attribute is `onlyForSmall`,
  this is a numeric magnitude below which the number will be
  formatted in scientific notation.  The values `0` and `0.0001` have
  been observed.  The value `0` seems like a pathological choice,
  since no real number has a magnitude less than 0; perhaps in
  practice such a choice is equivalent to setting `scientific` to
  `false`.

* `prefix`  
  `suffix`  
  Specifies a prefix or a suffix to apply to the formatted number.
  Only `suffix` has been observed, with value `%`.

### The `stringFormat` Element

```
stringFormat => relabel* affix*

relabel :from=real :to => EMPTY
```

The `stringFormat` element specifies how to display a string.  By
default, a string is displayed verbatim, but `relabel` can change it.

The `relabel` element appears as a child of `stringFormat` (and of
`format`, when it is used to format strings).  It specifies how to
display a given value.  It is used to implement value labels and to
display the system-missing value in a human-readable way.  It has the
following attributes:

* `from`  
  The value to map.  In the corpus this is an integer or the
  system-missing value `-1.797693134862316E300`.

* `to`  
  The string to display in place of the value of `from`.  In the
  corpus this is a wide variety of value labels; the system-missing
  value is mapped to `.`.

### The `dateTimeFormat` Element

```
dateTimeFormat
   :baseFormat[dt_base_format]=(date | time | dateTime)
   :separatorChars?
   :mdyOrder=(dayMonthYear | monthDayYear | yearMonthDay)?
   :showYear=bool?
   :yearAbbreviation=bool?
   :showQuarter=bool?
   :quarterPrefix?
   :quarterSuffix?
   :showMonth=bool?
   :monthFormat=(long | short | number | paddedNumber)?
   :showWeek=bool?
   :weekPadding=bool?
   :weekSuffix?
   :showDayOfWeek=bool?
   :dayOfWeekAbbreviation=bool?
   :dayPadding=bool?
   :dayOfMonthPadding=bool?
   :hourPadding=bool?
   :minutePadding=bool?
   :secondPadding=bool?
   :showDay=bool?
   :showHour=bool?
   :showMinute=bool?
   :showSecond=bool?
   :showMillis=bool?
   :dayType=(month | year)?
   :hourFormat=(AMPM | AS_24 | AS_12)?
=> affix*
```

This element appears only in [schema
version](#the-visualization-eleemnt) 2.5 and earlier.

Data to be formatted in date formats is stored as strings in legacy
data, in the format `yyyy-mm-ddTHH:MM:SS.SSS` and must be parsed and
reformatted by the reader.

The following attribute is required.

* `baseFormat`  
  Specifies whether a date and time are both to be displayed, or just
  one of them.

Many of the attributes' meanings are obvious.  The following seem to
be worth documenting.

* `separatorChars`  
  Exactly four characters.  In order, these are used for: decimal
  point, grouping, date separator, time separator.  Always `.,-:`.

* `mdyOrder`  
  Within a date, the order of the days, months, and years.
  `dayMonthYear` is the only observed value, but one would expect
  that `monthDayYear` and `yearMonthDay` to be reasonable as well.

* `showYear`  
* `yearAbbreviation`  
  Whether to include the year and, if so, whether the year should be
  shown abbreviated, that is, with only 2 digits.  Each is `true` or
  `false`; only values of `true` and `false`, respectively, have been
  observed.

* `showMonth`  
* `monthFormat`  
  Whether to include the month (`true` or `false`) and, if so, how to
  format it.  `monthFormat` is one of the following:

  - `long`  
    The full name of the month, e.g. in an English locale,
    `September`.

  - `short`  
    The abbreviated name of the month, e.g. in an English locale,
    `Sep`.

  - `number`  
    The number representing the month, e.g. 9 for September.

  - `paddedNumber`  
    A two-digit number representing the month, e.g. 09 for September.

  Only values of `true` and `short`, respectively, have been observed.

* `dayType`  
  This attribute is always `month` in the corpus, specifying that the
  day of the month is to be displayed; a value of `year` is supposed
  to indicate that the day of the year, where 1 is January 1, is to be
  displayed instead.

* `hourFormat`  
  `hourFormat`, if present, is one of:

  - `AMPM`  
    The time is displayed with an `am` or `pm` suffix, e.g.
    `10:15pm`.

  - `AS_24`  
    The time is displayed in a 24-hour format, e.g. `22:15`.

    This is the only value observed in the corpus.

  - `AS_12`  
    The time is displayed in a 12-hour format, without
    distinguishing morning or evening, e.g. `10;15`.

  `hourFormat` is sometimes present for `elapsedTime` formats, which
  is confusing since a time duration does not have a concept of AM or
  PM. This might indicate a bug in the code that generated the XML in
  the corpus, or it might indicate that `elapsedTime` is sometimes
  used to format a time of day.

For a `baseFormat` of `date`, PSPP chooses a print format type based
on the following rules:

1. If `showQuarter` is true: `QYR`.

2. Otherwise, if `showWeek` is true: `WKYR`.

3. Otherwise, if `mdyOrder` is `dayMonthYear`:

   a. If `monthFormat` is `number` or `paddedNumber`: `EDATE`.

   b. Otherwise: `DATE`.

4. Otherwise, if `mdyOrder` is `yearMonthDay`: `SDATE`.

5. Otherwise, `ADATE`.

For a `baseFormat` of `dateTime`, PSPP uses `YMDHMS` if `mdyOrder` is
`yearMonthDay` and `DATETIME` otherwise.  For a `baseFormat` of `time`,
PSPP uses `DTIME` if `showDay` is true, otherwise `TIME` if `showHour`
is true, otherwise `MTIME`.

For a `baseFormat` of `date`, the chosen width is the minimum for the
format type, adding 2 if `yearAbbreviation` is false or omitted.  For
other base formats, the chosen width is the minimum for its type, plus 3
if `showSecond` is true, plus 4 more if `showMillis` is also true.
Decimals are 0 by default, or 3 if `showMillis` is true.

### The `elapsedTimeFormat` Element

```
elapsedTimeFormat
   :baseFormat[dt_base_format]=(date | time | dateTime)
   :dayPadding=bool?
   :hourPadding=bool?
   :minutePadding=bool?
   :secondPadding=bool?
   :showYear=bool?
   :showDay=bool?
   :showHour=bool?
   :showMinute=bool?
   :showSecond=bool?
   :showMillis=bool?
=> affix*
```

This element specifies the way to display a time duration.

Data to be formatted in elapsed time formats is stored as strings in
legacy data, in the format `H:MM:SS.SSS`, with additional hour digits as
needed for long durations, and must be parsed and reformatted by the
reader.

The following attribute is required.

* `baseFormat`  
  Specifies whether a day and a time are both to be displayed, or
  just one of them.

The remaining attributes specify exactly how to display the elapsed
time.

For `baseFormat` of `time`, PSPP converts this element to print
format type `DTIME`; otherwise, if `showHour` is true, to `TIME`;
otherwise, to `MTIME`.  The chosen width is the minimum for the chosen
type, adding 3 if `showSecond` is true, adding 4 more if `showMillis` is
also true.  Decimals are 0 by default, or 3 if `showMillis` is true.

### The `format` Element

```
format
   :baseFormat[f_base_format]=(date | time | dateTime | elapsedTime)?
   :errorCharacter?
   :separatorChars?
   :mdyOrder=(dayMonthYear | monthDayYear | yearMonthDay)?
   :showYear=bool?
   :showQuarter=bool?
   :quarterPrefix?
   :quarterSuffix?
   :yearAbbreviation=bool?
   :showMonth=bool?
   :monthFormat=(long | short | number | paddedNumber)?
   :dayPadding=bool?
   :dayOfMonthPadding=bool?
   :showWeek=bool?
   :weekPadding=bool?
   :weekSuffix?
   :showDayOfWeek=bool?
   :dayOfWeekAbbreviation=bool?
   :hourPadding=bool?
   :minutePadding=bool?
   :secondPadding=bool?
   :showDay=bool?
   :showHour=bool?
   :showMinute=bool?
   :showSecond=bool?
   :showMillis=bool?
   :dayType=(month | year)?
   :hourFormat=(AMPM | AS_24 | AS_12)?
   :minimumIntegerDigits=int?
   :maximumFractionDigits=int?
   :minimumFractionDigits=int?
   :useGrouping=bool?
   :scientific=(onlyForSmall | whenNeeded | true | false)?
   :small=real?
   :prefix?
   :suffix?
   :tryStringsAsNumbers=bool?
   :negativesOutside=bool?
=> relabel* affix*
```

This element is the union of all of the more-specific format
elements.  It is interpreted in the same way as one of those format
elements, using `baseFormat` to determine which kind of format to use.

There are a few attributes not present in the more specific formats:

* `tryStringsAsNumbers`  
  When this is `true`, it is supposed to indicate that string values
  should be parsed as numbers and then displayed according to numeric
  formatting rules.  However, in the corpus it is always `false`.

* `negativesOutside`  
  If true, the negative sign should be shown before the prefix; if
  false, it should be shown after.

### The `affix` Element

```
affix
   :definesReference=int
   :position=(subscript | superscript)
   :suffix=bool
   :value
=> EMPTY
```

This defines a suffix (or, theoretically, a prefix) for a formatted
value.  It is used to insert a reference to a footnote.  It has the
following attributes:

* `definesReference`  
  This specifies the footnote number as a natural number: 1 for the
  first footnote, 2 for the second, and so on.

* `position`  
  Position for the footnote label.  Always `superscript`.

* `suffix`  
  Whether the affix is a suffix (`true`) or a prefix (`false`).
  Always `true`.

* `value`  
  The text of the suffix or prefix.  Typically a letter, e.g. `a` for
  footnote 1, `b` for footnote 2, ...  The corpus contains other
  values: `*`, `**`, and a few that begin with at least one comma:
  `,b`, `,c`, `,,b`, and `,,c`.

## The `interval` Element

```
interval :style=ref style => labeling footnotes?

labeling
   :style=ref style?
   :variable=ref (sourceVariable | derivedVariable)
=> (formatting | format | footnotes)*

formatting :variable=ref (sourceVariable | derivedVariable) => formatMapping*

formatMapping :from=int => format?

footnotes
   :superscript=bool?
   :variable=ref (sourceVariable | derivedVariable)
=> footnoteMapping*

footnoteMapping :definesReference=int :from=int :to => EMPTY
```

The `interval` element and its descendants determine the basic
formatting and labeling for the table's cells.  These basic styles are
overridden by more specific styles set using
[`setCellProperties`](#the-setcellproperties-element).

The `style` attribute of `interval` itself may be ignored.

The `labeling` element may have a single `formatting` child.  If
present, its `variable` attribute refers to a variable whose values are
format specifiers as numbers, e.g.  value 0x050802 for F8.2.  However,
the numbers are not actually interpreted that way.  Instead, each number
actually present in the variable's data is mapped by a `formatMapping`
child of `formatting` to a `format` that specifies how to display it.

The `labeling` element may also have a `footnotes` child element.
The `variable` attribute of this element refers to a variable whose
values are comma-delimited strings that list the 1-based indexes of
footnote references.  (Cells without any footnote references are numeric
0 instead of strings.)

Each `footnoteMapping` child of the `footnotes` element defines the
footnote marker to be its `to` attribute text for the footnote whose
1-based index is given in its `definesReference` attribute.

## The `style` Element

```
style
   :color=color?
   :color2=color?
   :labelAngle=real?
   :border-bottom=(solid | thick | thin | double | none)?
   :border-top=(solid | thick | thin | double | none)?
   :border-left=(solid | thick | thin | double | none)?
   :border-right=(solid | thick | thin | double | none)?
   :border-bottom-color?
   :border-top-color?
   :border-left-color?
   :border-right-color?
   :font-family?
   :font-size?
   :font-weight=(regular | bold)?
   :font-style=(regular | italic)?
   :font-underline=(none | underline)?
   :margin-bottom=dimension?
   :margin-left=dimension?
   :margin-right=dimension?
   :margin-top=dimension?
   :textAlignment=(left | right | center | decimal | mixed)?
   :labelLocationHorizontal=(positive | negative | center)?
   :labelLocationVertical=(positive | negative | center)?
   :decimal-offset=dimension?
   :size?
   :width?
   :visible=bool?
=> EMPTY
```
A `style` element has an effect only when it is referenced by another
element to set some aspect of the table's style.  Most of the attributes
are self-explanatory.  The rest are described below.

* `color`  
  In some cases, the text color; in others, the background color.

* `color2`  
  Not used.

* `labelAngle`  
  Normally 0.  The value -90 causes inner column or outer row labels
  to be rotated vertically.

* `labelLocationHorizontal`  
  Not used.

* `labelLocationVertical`  
  The value `positive` corresponds to vertically aligning text to the
  top of a cell, `negative` to the bottom, `center` to the middle.

## The `labelFrame` Element

```
labelFrame :style=ref style => location+ label? paragraph?

paragraph :hangingIndent=dimension? => EMPTY
```

A `labelFrame` element specifies content and style for some aspect of
a table.  Only `labelFrame` elements that have a `label` child are
important.  The `purpose` attribute in the `label` determines what the
`labelFrame` affects:

* `title`  
  The table's title and its style.

* `subTitle`  
  The table's caption and its style.

* `footnote`  
  The table's footnotes and the style for the footer area.

* `layer`  
  The style for the layer area.

* `subSubTitle`  
  Ignored.

The `style` attribute references the style to use for the area.

The `label`, if present, specifies the text to put into the title or
caption or footnotes.  For footnotes, the label has two `text` children
for every footnote, each of which has a `usesReference` attribute
identifying the 1-based index of a footnote.  The first, third, fifth,
... `text` child specifies the content for a footnote; the second,
fourth, sixth, ... child specifies the marker.  Content tends to end in
a new-line, which the reader may wish to trim; similarly, markers tend
to end in `.`.

The `paragraph`, if present, may be ignored, since it is always
empty.

## Legacy Properties

The detail XML format has features for styling most of the aspects of a
table.  It also inherits defaults for many aspects from structure XML,
which has the following `tableProperties` element:

```
tableProperties
   :name?
=> generalProperties footnoteProperties cellFormatProperties borderProperties printingProperties

generalProperties
   :hideEmptyRows=bool?
   :maximumColumnWidth=dimension?
   :maximumRowWidth=dimension?
   :minimumColumnWidth=dimension?
   :minimumRowWidth=dimension?
   :rowDimensionLabels=(inCorner | nested)?
=> EMPTY

footnoteProperties
   :markerPosition=(superscript | subscript)?
   :numberFormat=(alphabetic | numeric)?
=> EMPTY

cellFormatProperties => cell_style+

any[cell_style]
   :alternatingColor=color?
   :alternatingTextColor=color?
=> style

style
   :color=color?
   :color2=color?
   :font-family?
   :font-size?
   :font-style=(regular | italic)?
   :font-weight=(regular | bold)?
   :font-underline=(none | underline)?
   :labelLocationVertical=(positive | negative | center)?
   :margin-bottom=dimension?
   :margin-left=dimension?
   :margin-right=dimension?
   :margin-top=dimension?
   :textAlignment=(left | right | center | decimal | mixed)?
   :decimal-offset=dimension?
=> EMPTY

borderProperties => border_style+

any[border_style]
   :borderStyleType=(none | solid | dashed | thick | thin | double)?
   :color=color?
=> EMPTY

printingProperties
   :printAllLayers=bool?
   :rescaleLongTableToFitPage=bool?
   :rescaleWideTableToFitPage=bool?
   :windowOrphanLines=int?
   :continuationText?
   :continuationTextAtBottom=bool?
   :continuationTextAtTop=bool?
   :printEachLayerOnSeparatePage=bool?
=> EMPTY
```

The `name` attribute appears only in [standalone `.stt`
files](../tablelook.md#the-tlo-format).

