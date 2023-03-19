# Legacy Detail Member Binary Format

Whereas the light binary format represents everything about a given
pivot table, the legacy binary format conceptually consists of a number
of named sources, each of which consists of a number of named variables,
each of which is a 1-dimensional array of numbers or strings or a mix.
Thus, the legacy binary member format is quite simple.

This section uses the same context-free grammar notation as in the
previous section, with the following additions:

* `vAF(X)`  
  In a version 0xaf legacy member, `X`; in other versions, nothing.
  (The legacy member header indicates the version; see below.)

* `vB0(X)`  
  In a version 0xb0 legacy member, `X`; in other versions, nothing.

A legacy detail member `.bin` has the following overall format:

```
LegacyBinary =>
    00 byte[version] int16[n-sources] int32[member-size]
    Metadata*[n-sources]
    #Data*[n-sources]
    #Strings?
```

`version` is a version number that affects the interpretation of some
of the other data in the member.  Versions 0xaf and 0xb0 are known.  We
will refer to "version 0xaf" and "version 0xb0" members later on.

A legacy member consists of `n-sources` data sources, each of which
has Metadata and Data.

`member-size` is the size of the legacy binary member, in bytes.

The Data and Strings above are commented out because the Metadata has
some oddities that mean that the Data sometimes seems to start at an
unexpected place.  The following section goes into detail.

<!-- toc -->

## Metadata

```
Metadata =>
    int32[n-values] int32[n-variables] int32[data-offset]
    vAF(byte*28[source-name])
    vB0(byte*64[source-name] int32[x])
```

A data source has `n-variables` variables, each with `n-values` data
values.

`source-name` is a 28- or 64-byte string padded on the right with
0-bytes.  The names that appear in the corpus are very generic: usually
`tableData` for pivot table data or `source0` for chart data.

A given Metadata's `data-offset` is the offset, in bytes, from the
beginning of the member to the start of the corresponding Data.  This
allows programs to skip to the beginning of the data for a particular
source.  In every case in the corpus, the Data follow the Metadata in
the same order, but it is important to use `data-offset` instead of
reading sequentially through the file because of the exception described
below.

One SPV file in the corpus has legacy binary members with version
0xb0 but a 28-byte `source-name` field (and only a single source).  In
practice, this means that the 64-byte `source-name` used in version 0xb0
has a lot of 0-bytes in the middle followed by the `variable-name` of
the following Data.  As long as a reader treats the first 0-byte in the
`source-name` as terminating the string, it can properly interpret these
members.

The meaning of `x` in version 0xb0 is unknown.

## Numeric Data

```
Data => Variable*[n-variables]
Variable => byte*288[variable-name] double*[n-values]
```

Data follow the `Metadata` in the legacy binary format, with sources
in the same order (but readers should use the `data-offset` in
`Metadata` records, rather than reading sequentially).  Each Variable
begins with a `variable-name` that generally indicates its role in the
pivot table, e.g. "cell", "cellFormat", "dimension0categories",
"dimension0group0", followed by the numeric data, one double per
datum.  A double with the maximum negative double `-DBL_MAX`
represents the system-missing value `SYSMIS`.

## String Data

```
Strings => SourceMaps[maps] Labels

SourceMaps => int32[n-maps] SourceMap*[n-maps]

SourceMap => string[source-name] int32[n-variables] VariableMap*[n-variables]
VariableMap => string[variable-name] int32[n-data] DatumMap*[n-data]
DatumMap => int32[value-idx] int32[label-idx]

Labels => int32[n-labels] Label*[n-labels]
Label => int32[frequency] string[label]
```

Each variable may include a mix of numeric and string data values.
If a legacy binary member contains any string data, `Strings` is present;
otherwise, it ends just after the last Data element.

The string data overlays the numeric data.  When a variable includes
any string data, its Variable represents the string values with a
`SYSMIS` or NaN placeholder.  (Not all such values need be
placeholders.)

Each `SourceMap` provides a mapping between `SYSMIS` or NaN values in
source `source-name` and the string data that they represent.
`n-variables` is the number of variables in the source that include
string data.  More precisely, it is the 1-based index of the last
variable in the source that includes any string data; thus, it would
be 4 if there are 5 variables and only the fourth one includes string
data.

A `VariableMap` repeats its variable's name, but variables are always
present in the same order as the source, starting from the first
variable, without skipping any even if they have no string values.
Each `VariableMap` contains `DatumMap` nonterminals, each of which
maps from a 0-based index within its variable's data to a 0-based
label index, e.g.  pair `value-idx` = 2, `label-idx` = 3, means that
the third data value (which must be `SYSMIS` or NaN) is to be replaced
by the string of the fourth Label.

The labels themselves follow the pairs.  The valuable part of each
label is the string `label`.  Each label also includes a `frequency`
that reports the number of `DatumMaps` that reference it (although
this is not useful).

