# SPSS Viewer File Format

SPSS Viewer or `.spv` files, here called SPV files, are written by SPSS
16 and later to represent the contents of its output editor.  This
chapter documents the format, based on examination of a corpus of about
8,000 files from a variety of sources.  This description is detailed
enough to both read and write SPV files.

SPSS 15 and earlier versions instead use `.spo` files, which have a
completely different output format based on the Microsoft Compound
Document Format.  This format is not documented here.

An SPV file is a Zip archive that can be read with `zipinfo` and
`unzip` and similar programs.  The final member in the Zip archive is
the "manifest", a file named `META-INF/MANIFEST.MF`.  This structure
makes SPV files resemble Java "JAR" files (and ODF files), but whereas a
JAR manifest contains a sequence of colon-delimited key/value pairs, an
SPV manifest contains the string `allowPivoting=true`, without a
new-line.  PSPP uses this string to identify an SPV file; it is
invariant across the corpus.

> SPV files always begin with the 7-byte sequence 50 4b 03 04 14 00
> 08, but this is not a useful magic number because most Zip archives
> start the same way.
>
> Checking only for the presence of `META-INF/MANIFEST.MF` is also not
> a useful magic number because this file name also appears in every
> [Java JAR archive].
>
> SPSS writes `META-INF/MANIFEST.MF` to every SPV file, but it does
> not read it or even require it to exist, so using different
> contents, e.g. `allowPivoting=false`, has no effect.
>
> [Java JAR archive]: https://docs.oracle.com/javase/8/docs/technotes/guides/jar/jar.html

The rest of the members in an SPV file's Zip archive fall into two
categories: "structure" and "detail" members.  Structure member names
take the form with `outputViewerNUMBER.xml` or
`outputViewerNUMBER_heading.xml`, where `NUMBER` is an 10-digit decimal
number.  Each of these members represents some kind of output item (a
table, a heading, a block of text, etc.)  or a group of them.  The
member whose output goes at the beginning of the document is numbered
0, the next member in the output is numbered 1, and so on.

Structure members contain XML. This XML is sometimes self-contained,
but it often references detail members in the Zip archive, which are
named as follows:

* `PREFIX_table.xml` and `PREFIX_tableData.bin`  
  `PREFIX_lightTableData.bin`  
  The structure of a table plus its data.  Older SPV files pair a
  `PREFIX_table.xml` file that describes the table's structure with a
  binary `PREFIX_tableData.bin` file that gives its data.  Newer SPV
  files (the majority of those in the corpus) instead include a
  single `PREFIX_lightTableData.bin` file that incorporates both into
  a single binary format.

* `PREFIX_warning.xml` and `PREFIX_warningData.bin`  
  `PREFIX_lightWarningData.bin`  
  Same format used for tables, with a different name.

* `PREFIX_notes.xml` and `PREFIX_notesData.bin`  
  `PREFIX_lightNotesData.bin`  
  Same format used for tables, with a different name.

* `PREFIX_chartData.bin` and `PREFIX_chart.xml`  
  The structure of a chart plus its data.  Charts do not have a
  "light" format.

* `PREFIX_Imagegeneric.png`  
  `PREFIX_PastedObjectgeneric.png`  
  `PREFIX_imageData.bin`  
  A PNG image referenced by an `object` element (in the first two
  cases) or an `image` element (in the final case).  See [The `object`
  and `image` Elements](structure.md#the-object-and-image-elements),
  for details.

* `PREFIX_pmml.scf`  
  `PREFIX_stats.scf`  
  `PREFIX_model.xml`  
  Not yet investigated.  The corpus contains few examples.

The `PREFIX` in the names of the detail members is typically an
11-digit decimal number that increases for each item, tending to skip
values.  Older SPV files use different naming conventions for detail
members.  Structure member refer to detail members by name, and so
their exact names do not matter to readers as long as they are unique.

SPSS tolerates corrupted Zip archives that Zip reader libraries tend
to reject.  These can be fixed up with `zip -FF`.
