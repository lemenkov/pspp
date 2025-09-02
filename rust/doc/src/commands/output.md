# OUTPUT

In the syntax below, the characters `[` and `]` are literals.  They
must appear in the syntax to be interpreted:

```
OUTPUT MODIFY
     /SELECT TABLES
     /TABLECELLS SELECT = [ CLASS... ]
     FORMAT = FMT_SPEC.
```

`OUTPUT` changes the appearance of the tables in which results are
printed.  In particular, it can be used to set the format and precision
to which results are displayed.

After running this command, the default table appearance parameters
will have been modified and each new output table generated uses the new
parameters.

Following `/TABLECELLS SELECT =` a list of cell classes must appear,
enclosed in square brackets.  This list determines the classes of values
should be selected for modification.  Each class can be:

* `RESIDUAL`: Residual values.  Default: `F40.2`.

* `CORRELATION`: Correlations.  Default: `F40.3`.

* `PERCENT`: Percentages.  Default: `PCT40.1`.

* `SIGNIFICANCE`: Significance of tests (p-values).  Default: `F40.3`.

* `COUNT`: Counts or sums of weights.  For a weighted data set, the
  default is the weight variable's print format.  For an unweighted
  data set, the default is `F40.0`.

For most other numeric values that appear in tables, [`SET
FORMAT`](set.md#format)) may be used to specify the format.

`FMT_SPEC` must be a valid [output
format](../language/datasets/formats/index.md).  Not all possible
format](../language/datasets/formats/index.md).  Not all possible
formats are meaningful for all classes.

