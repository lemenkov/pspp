# Attributes of Variables

Each variable has a number of attributes, including:

* Name  
  An [identifier](../basics/tokens.md), up to 64 bytes long.  Each
  variable must have a different name.

  User-defined variable names may not begin with `$`.

  A variable name can with `.`, but it should not, because such an
  identifier will be misinterpreted when it is the final token on a
  line: `FOO.`  is divided into two separate
  [tokens](../basics/tokens.md), `FOO` and `.`, indicating
  end-of-command.

  A variable name can end with `_`, but it should not, because some
  PSPP procedures reserve those names for special purposes.

  Variable names are not case-sensitive.  PSPP capitalizes variable
  names on output the same way they were capitalized at their point of
  definition in the input.

* Type  
  Numeric or string.

* Width (string variables only)  
  String variables with a width of 8 characters or fewer are called
  "short string variables", and wider ones are called "long string
  variables".  In a few contexts, long string variables are not
  allowed.

* Position  
  Variables in the dictionary are arranged in a specific order.
  [`DISPLAY`](../../commands/variables/display.md) can show this
  order.

* Initialization  
  Either reinitialized to 0 or spaces for each case, or left at its
  existing value.  Use [`LEAVE`](../../commands/variables/leave.md) to
  avoid reinitializing a variable.

* Missing values  
  Optionally, up to three values, or a range of values, or a specific
  value plus a range, can be specified as "user-missing values".
  There is also a "system-missing value" that is assigned to an
  observation when there is no other obvious value for that
  observation.  Observations with missing values are automatically
  excluded from analyses.  User-missing values are actual data values,
  while the system-missing value is not a value at all.  See [Handling
  Missing Values](../../language/basics/missing-values.md) for more
  information on missing values.  The [`MISSING
  VALUES`](../../commands/variables/missing-values.md) command sets
  missing values.

* Variable label  
  A string that describes the variable.  The [`VARIABLE
  LABELS`](../../commands/variables/variable-labels.md) command sets
  variable labels.

* Value label  
  Optionally, these associate each possible value of the variable with
  a string.  The [`VALUE
  LABELS`](../../commands/variables/value-labels.md) and [`ADD VALUE
  LABELS`](../../commands/variables/add-value-labels.md) commands set
  value labels.

* Print format  
  Display width, format, and (for numeric variables) number of decimal
  places.  This attribute does not affect how data are stored, just
  how they are displayed.  See [Input and Output
  Formats](../../language/datasets/formats/index.md) for details.  The
  [`FORMATS`](../../commands/variables/formats.md) and [`PRINT
  FORMATS`](../../commands/variables/print-formats.md) commands set
  print formats.

* Write format  
  Similar to print format, but used by the
  [`WRITE`](../../commands/data-io/write.md) command.  The
  [`FORMATS`](../../commands/variables/formats.md) and [`WRITE
  FORMATS`](../../commands/variables/write-formats.md) commands set
  write formats.

* <a name="measurement-level">Measurement level</a>  
  One of the following:

  - *Nominal*: Each value of a nominal variable represents a distinct
    category.  The possible categories are finite and often have value
    labels.  The order of categories is not significant.  Political
    parties, US states, and yes/no choices are nominal.  Numeric and
    string variables can be nominal.

  - *Ordinal*: Ordinal variables also represent distinct categories, but
    their values are arranged according to some natural order.  Likert
    scales, e.g. from strongly disagree to strongly agree, are
    ordinal.  Data grouped into ranges, e.g. age groups or income
    groups, are ordinal.  Both numeric and string variables can be
    ordinal.  String values are ordered alphabetically, so letter
    grades from A to F will work as expected, but `poor`,
    `satisfactory`, `excellent` will not.

  - *Scale*: Scale variables are ones for which differences and ratios
    are meaningful.  These are often values which have a natural unit
    attached, such as age in years, income in dollars, or distance in
    miles.  Only numeric variables are scalar.

  The [`VARIABLE LEVEL`](../../commands/variables/variable-level.md)
  command sets measurement levels.

  Variables created by `COMPUTE` and similar transformations,
  obtained from external sources, etc., initially have an unknown
  measurement level.  Any procedure that reads the data will then
  assign a default measurement level.  PSPP can assign some defaults
  without reading the data:

  - Nominal, if it's a string variable.

  - Nominal, if the variable has a WKDAY or MONTH print format.

  - Scale, if the variable has a DOLLAR, CCA through CCE, or time
    or date print format.

  Otherwise, PSPP reads the data and decides based on its
  distribution:

  - Nominal, if all observations are missing.

  - Scale, if one or more valid observations are noninteger or
    negative.

  - Scale, if no valid observation is less than 10.

  - Scale, if the variable has 24 or more unique valid values.  The
    value 24 is the default.  Use [`SET
    SCALEMIN`](../../commands/utilities/set.md#scalemin) to change the
    default.

  Finally, if none of the above is true, PSPP assigns the variable a
  nominal measurement level.

* Custom attributes  
  User-defined associations between names and values.  The [`VARIABLE
  ATTRIBUTE`](../../commands/variables/variable-attribute.md) command
  sets variable atributes.

* Role  
  The intended role of a variable for use in dialog boxes in graphical
  user interfaces.  The [`VARIABLE
  ROLE`](../../commands/variables/variable-role.md) command sets
  variable roles.

