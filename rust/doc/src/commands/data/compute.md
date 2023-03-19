# COMPUTE

```
COMPUTE VARIABLE = EXPRESSION.
   or
COMPUTE vector(INDEX) = EXPRESSION.
```

`COMPUTE` assigns the value of an expression to a target variable.
For each case, the expression is evaluated and its value assigned to
the target variable.  Numeric and string variables may be assigned.
When a string expression's width differs from the target variable's
width, the string result of the expression is truncated or padded with
spaces on the right as necessary.  The expression and variable types
must match.

For numeric variables only, the target variable need not already
exist.  Numeric variables created by `COMPUTE` are assigned an `F8.2`
output format.  String variables must be declared before they can be
used as targets for `COMPUTE`.

The target variable may be specified as an element of a
[vector](../../commands/variables/vector.md).  In this case, an
expression `INDEX` must be specified in parentheses following the vector
name.  The expression `INDEX` must evaluate to a numeric value that,
after rounding down to the nearest integer, is a valid index for the
named vector.

Using `COMPUTE` to assign to a variable specified on
[`LEAVE`](../../commands/variables/leave.md) resets the variable's
left state.  Therefore, `LEAVE` should be specified following
`COMPUTE`, not before.

`COMPUTE` is a transformation.  It does not cause the active dataset
to be read.

When `COMPUTE` is specified following
[`TEMPORARY`](../selection/temporary.md), the
[`LAG`](../../language/expressions/functions/miscellaneous.md)
function may not be used.

## Example

The dataset `physiology.sav` contains the height and weight of
persons.  For some purposes, neither height nor weight alone is of
interest.  Epidemiologists are often more interested in the "body mass
index" which can sometimes be used as a predictor for clinical
conditions.  The body mass index is defined as the weight of the
person in kilograms divided by the square of the person's height in
metres.[^1]

[^1]: Since BMI is a quantity with a ratio scale and has units, the
term "index" is a misnomer, but that is what it is called.

```
get file='physiology.sav'.

* height is in mm so we must divide by 1000 to get metres.
compute bmi = weight / (height/1000)**2.
variable label bmi "Body Mass Index".

descriptives /weight height bmi.
```

This syntax shows how you can use `COMPUTE` to generate a new variable
called bmi and have every case's value calculated from the existing
values of weight and height.  It also shows how you can [add a
label](../../commands/variables/variable-labels.md) to this new
variable, so that a more descriptive label appears in subsequent
analyses, and this can be seen in the output from the `DESCRIPTIVES`
command, below.

The expression which follows the `=` sign can be as complicated as
necessary.  See [Expressions](../../language/expressions/index.md) for
a full description of the language accepted.

```
                  Descriptive Statistics
┌─────────────────────┬──┬───────┬───────┬───────┬───────┐
│                     │ N│  Mean │Std Dev│Minimum│Maximum│
├─────────────────────┼──┼───────┼───────┼───────┼───────┤
│Weight in kilograms  │40│  72.12│  26.70│  ─55.6│   92.1│
│Height in millimeters│40│1677.12│ 262.87│    179│   1903│
│Body Mass Index      │40│  67.46│ 274.08│ ─21.62│1756.82│
│Valid N (listwise)   │40│       │       │       │       │
│Missing N (listwise) │ 0│       │       │       │       │
└─────────────────────┴──┴───────┴───────┴───────┴───────┘
```
