# Custom Currency Formats

The custom currency formats are closely related to the basic numeric
formats, but they allow users to customize the output format.  The SET
command configures custom currency formats, using the syntax

```
SET CCX="STRING".
```

where X is `A`, `B`, `C`, `D`, or `E`, and `STRING` is no more than 16
characters long.

   `STRING` must contain exactly three commas or exactly three periods
(but not both), except that a single quote character may be used to
"escape" a following comma, period, or single quote.  If three commas
are used, commas are used for grouping in output, and a period is used
as the decimal point.  Uses of periods reverses these roles.

   The commas or periods divide `STRING` into four fields, called the
"negative prefix", "prefix", "suffix", and "negative suffix",
respectively.  The prefix and suffix are added to output whenever
space is available.  The negative prefix and negative suffix are
always added to a negative number when the output includes a nonzero
digit.

   The following syntax shows how custom currency formats could be used
to reproduce basic numeric formats:

```
SET CCA="-,,,".  /* Same as COMMA.
SET CCB="-...".  /* Same as DOT.
SET CCC="-,$,,". /* Same as DOLLAR.
SET CCD="-,,%,". /* Like PCT, but groups with commas.
```

   Here are some more examples of custom currency formats.  The final
example shows how to use a single quote to escape a delimiter:

```
SET CCA=",EUR,,-".   /* Euro.
SET CCB="(,USD ,,)". /* US dollar.
SET CCC="-.R$..".    /* Brazilian real.
SET CCD="-,, NIS,".  /* Israel shekel.
SET CCE="-.Rp'. ..". /* Indonesia Rupiah.
```

These formats would yield the following output:

|Format    |` 3145.59`         |`-3145.59`|
|:---------|------------------:|---------------:|
|`CCA12.2` |  ` EUR3,145.59`   |  `EUR3,145.59-`|
|`CCB14.2` |  `  USD 3,145.59` |  `(USD 3,145.59)`|
|`CCC11.2` |  ` R$3.145,59`    |  `-R$3.145,59`|
|`CCD13.2` |  ` 3,145.59 NIS`  |  `-3,145.59 NIS`|
|`CCE10.0` |  ` Rp. 3.146`     |  `-Rp. 3.146`|

   The default for all the custom currency formats is `-,,,`, equivalent
to `COMMA` format.

