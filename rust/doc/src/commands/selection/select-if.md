# SELECT IF

```
SELECT IF EXPRESSION.
```

`SELECT IF` selects cases for analysis based on the value of
EXPRESSION.  Cases not selected are permanently eliminated from the
active dataset, unless [`TEMPORARY`](temporary.md) is in effect.

Specify a [boolean
expression](../../language/expressions/index.md#boolean-values).  If
the expression is true for a particular case, the case is analyzed.
If the expression is false or missing, then the case is deleted from
the data stream.

Place `SELECT IF` early in the command file.  Cases that are deleted
early can be processed more efficiently in time and space.  Once cases
have been deleted from the active dataset using `SELECT IF` they
cannot be re-instated.  If you want to be able to re-instate cases,
then use [`FILTER`](filter.md) instead.

When `SELECT IF` is specified following [`TEMPORARY`](temporary.md),
the [`LAG`](../../language/expressions/functions/miscellaneous.md)
function may not be used.

## Example

A shop steward is interested in the salaries of younger personnel in a
firm.  The file `personnel.sav` provides the salaries of all the
workers and their dates of birth.  The syntax below shows how `SELECT
IF` can be used to limit analysis only to those persons born after
December 31, 1999.

```
get file = 'personnel.sav'.

echo 'Salaries of all personnel'.
descriptives salary.

echo 'Salaries of personnel born after December 31 1999'.
select if dob > date.dmy (31,12,1999).
descriptives salary.
```

From the output shown below, one can see that there are 56 persons
listed in the dataset, and 17 of them were born after December 31,
1999.

```
Salaries of all personnel

               Descriptive Statistics
┌────────────────────────┬──┬────────┬───────┬───────┬───────┐
│                        │ N│  Mean  │Std Dev│Minimum│Maximum│
├────────────────────────┼──┼────────┼───────┼───────┼───────┤
│Annual salary before tax│56│40028.97│8721.17│$23,451│$57,044│
│Valid N (listwise)      │56│        │       │       │       │
│Missing N (listwise)    │ 0│        │       │       │       │
└────────────────────────┴──┴────────┴───────┴───────┴───────┘

Salaries of personnel born after December 31 1999

               Descriptive Statistics
┌────────────────────────┬──┬────────┬───────┬───────┬───────┐
│                        │ N│  Mean  │Std Dev│Minimum│Maximum│
├────────────────────────┼──┼────────┼───────┼───────┼───────┤
│Annual salary before tax│17│31828.59│4454.80│$23,451│$39,504│
│Valid N (listwise)      │17│        │       │       │       │
│Missing N (listwise)    │ 0│        │       │       │       │
└────────────────────────┴──┴────────┴───────┴───────┴───────┘
```

Note that the `personnel.sav` file from which the data were read is
unaffected.  The transformation affects only the active file.

