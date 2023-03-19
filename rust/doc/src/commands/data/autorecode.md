# AUTORECODE

```
AUTORECODE VARIABLES=SRC_VARS INTO DEST_VARS
        [ /DESCENDING ]
        [ /PRINT ]
        [ /GROUP ]
        [ /BLANK = {VALID, MISSING} ]
```

The `AUTORECODE` procedure considers the N values that a variable
takes on and maps them onto values 1...N on a new numeric variable.

Subcommand `VARIABLES` is the only required subcommand and must come
first.  Specify `VARIABLES`, an equals sign (`=`), a list of source
variables, `INTO`, and a list of target variables.  There must the
same number of source and target variables.  The target variables must
not already exist.

`AUTORECODE` ordinarily assigns each increasing non-missing value of a
source variable (for a string, this is based on character code
comparisons) to consecutive values of its target variable.  For
example, the smallest non-missing value of the source variable is
recoded to value 1, the next smallest to 2, and so on.  If the source
variable has user-missing values, they are recoded to consecutive
values just above the non-missing values.  For example, if a source
variables has seven distinct non-missing values, then the smallest
missing value would be recoded to 8, the next smallest to 9, and so
on.

Use `DESCENDING` to reverse the sort order for non-missing values, so
that the largest non-missing value is recoded to 1, the second-largest
to 2, and so on.  Even with `DESCENDING`, user-missing values are
still recoded in ascending order just above the non-missing values.

The system-missing value is always recoded into the system-missing
variable in target variables.

If a source value has a value label, then that value label is retained
for the new value in the target variable.  Otherwise, the source value
itself becomes each new value's label.

Variable labels are copied from the source to target variables.

`PRINT` is currently ignored.

The `GROUP` subcommand is relevant only if more than one variable is
to be recoded.  It causes a single mapping between source and target
values to be used, instead of one map per variable.  With `GROUP`,
user-missing values are taken from the first source variable that has
any user-missing values.

If `/BLANK=MISSING` is given, then string variables which contain
only whitespace are recoded as SYSMIS. If `/BLANK=VALID` is specified
then they are allocated a value like any other.  `/BLANK` is not
relevant to numeric values.  `/BLANK=VALID` is the default.

`AUTORECODE` is a procedure.  It causes the data to be read.

## Example

In the file `personnel.sav`, the variable occupation is a string
variable.  Except for data of a purely commentary nature, string
variables are generally a bad idea.  One reason is that data entry
errors are easily overlooked.  This has happened in `personnel.sav`;
one entry which should read "Scientist" has been mistyped as
"Scrientist".  The syntax below shows how to correct this error in the
`DO IF` clause[^1], which then uses `AUTORECODE` to create a new numeric
variable which takes recoded values of occupation.  Finally, we remove
the old variable and rename the new variable to the name of the old
variable:

[^1]: One must use care when correcting such data input errors rather
than simply marking them as missing.  For example, if an occupation
has been entered "Barister", did the person mean "Barrister" or
"Barista"?

```
get file='personnel.sav'.

* Correct a typing error in the original file.
do if occupation = "Scrientist".
 compute occupation = "Scientist".
end if.

autorecode
   variables = occupation into occ
   /blank = missing.

* Delete the old variable.
delete variables occupation.

* Rename the new variable to the old variable's name.
rename variables (occ = occupation).

* Inspect the new variable.
display dictionary /variables=occupation.
```


Notice, in the output below, how the new variable has been
automatically allocated value labels which correspond to the strings
of the old variable.  This means that in future analyses the
descriptive strings are reported instead of the numeric values.

```
                                   Variables
+----------+--------+--------------+-----+-----+---------+----------+---------+
|          |        |  Measurement |     |     |         |   Print  |  Write  |
|Name      |Position|     Level    | Role|Width|Alignment|  Format  |  Format |
+----------+--------+--------------+-----+-----+---------+----------+---------+
|occupation|       6|Unknown       |Input|    8|Right    |F2.0      |F2.0     |
+----------+--------+--------------+-----+-----+---------+----------+---------+

            Value Labels
+---------------+------------------+
|Variable Value |       Label      |
+---------------+------------------+
|occupation 1   |Artist            |
|           2   |Baker             |
|           3   |Barrister         |
|           4   |Carpenter         |
|           5   |Cleaner           |
|           6   |Cook              |
|           7   |Manager           |
|           8   |Mathematician     |
|           9   |Painter           |
|           10  |Payload Specialist|
|           11  |Plumber           |
|           12  |Scientist         |
|           13  |Tailor            |
+---------------+------------------+
```
