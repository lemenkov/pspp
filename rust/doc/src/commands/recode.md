# RECODE

The `RECODE` command is used to transform existing values into other,
user specified values.  The general form is:

```
RECODE SRC_VARS
        (SRC_VALUE SRC_VALUE ... = DEST_VALUE)
        (SRC_VALUE SRC_VALUE ... = DEST_VALUE)
        (SRC_VALUE SRC_VALUE ... = DEST_VALUE) ...
         [INTO DEST_VARS].
```

Following the `RECODE` keyword itself comes `SRC_VARS`, a list of
variables whose values are to be transformed.  These variables must
all string or all numeric variables.

After the list of source variables, there should be one or more
"mappings".  Each mapping is enclosed in parentheses, and contains the
source values and a destination value separated by a single `=`.  The
source values are used to specify the values in the dataset which need
to change, and the destination value specifies the new value to which
they should be changed.  Each SRC_VALUE may take one of the following
forms:

* `NUMBER` (numeric source variables only)  
  Matches a number.

* `STRING` (string source variables only)  
  Matches a string enclosed in single or double quotes.

* `NUM1 THRU NUM2` (numeric source variables only)  
  Matches all values in the range between `NUM1` and `NUM2`, including
  both endpoints of the range.  `NUM1` should be less than `NUM2`.
  Open-ended ranges may be specified using `LO` or `LOWEST` for `NUM1`
  or `HI` or `HIGHEST` for `NUM2`.

* `MISSING`  
  Matches system missing and user missing values.

* `SYSMIS` (numeric source variables only)  
  Match system-missing values.

* `ELSE`  
  Matches any values that are not matched by any other `SRC_VALUE`.
  This should appear only as the last mapping in the command.

After the source variables comes an `=` and then the `DEST_VALUE`,
which may take any of the following forms:

* `NUMBER` (numeric destination variables only)  
  A literal numeric value to which the source values should be
  changed.

* `STRING` (string destination variables only)  
  A literal string value (enclosed in quotation marks) to which the
  source values should be changed.  This implies the destination
  variable must be a string variable.

* `SYSMIS` (numeric destination variables only)  
  The keyword `SYSMIS` changes the value to the system missing value.
  This implies the destination variable must be numeric.

* `COPY`  
  The special keyword `COPY` means that the source value should not be
  modified, but copied directly to the destination value.  This is
  meaningful only if `INTO DEST_VARS` is specified.

Mappings are considered from left to right.  Therefore, if a value is
matched by a `SRC_VALUE` from more than one mapping, the first
(leftmost) mapping which matches is considered.  Any subsequent
matches are ignored.

The clause `INTO DEST_VARS` is optional.  The behaviour of the command
is slightly different depending on whether it appears or not:

* Without `INTO DEST_VARS`, then values are recoded "in place".  This
  means that the recoded values are written back to the source variables
  from whence the original values came.  In this case, the DEST_VALUE
  for every mapping must imply a value which has the same type as the
  SRC_VALUE.  For example, if the source value is a string value, it is
  not permissible for DEST_VALUE to be `SYSMIS` or another forms which
  implies a numeric result.  It is also not permissible for DEST_VALUE
  to be longer than the width of the source variable.

  The following example recodes two numeric variables `x` and `y` in
  place.  0 becomes 99, the values 1 to 10 inclusive are unchanged,
  values 1000 and higher are recoded to the system-missing value, and
  all other values are changed to 999:

  ```
  RECODE x y
          (0 = 99)
          (1 THRU 10 = COPY)
          (1000 THRU HIGHEST = SYSMIS)
          (ELSE = 999).
  ```

* With `INTO DEST_VARS`, recoded values are written into the variables
  specified in `DEST_VARS`, which must therefore contain a list of
  valid variable names.  The number of variables in `DEST_VARS` must
  be the same as the number of variables in `SRC_VARS` and the
  respective order of the variables in `DEST_VARS` corresponds to the
  order of `SRC_VARS`.  That is to say, the recoded value whose
  original value came from the Nth variable in `SRC_VARS` is placed
  into the Nth variable in `DEST_VARS`.  The source variables are
  unchanged.  If any mapping implies a string as its destination
  value, then the respective destination variable must already exist,
  or have been declared using `STRING` or another transformation.
  Numeric variables however are automatically created if they don't
  already exist.

  The following example deals with two source variables, `a` and `b`
  which contain string values.  Hence there are two destination
  variables `v1` and `v2`.  Any cases where `a` or `b` contain the
  values `apple`, `pear` or `pomegranate` result in `v1` or `v2` being
  filled with the string `fruit` whilst cases with `tomato`, `lettuce`
  or `carrot` result in `vegetable`.  Other values produce the result
  `unknown`:

  ```
  STRING v1 (A20).
  STRING v2 (A20).

  RECODE a b
          ("apple" "pear" "pomegranate" = "fruit")
          ("tomato" "lettuce" "carrot" = "vegetable")
          (ELSE = "unknown")
          INTO v1 v2.
  ```

There is one special mapping, not mentioned above.  If the source
variable is a string variable then a mapping may be specified as
`(CONVERT)`.  This mapping, if it appears must be the last mapping
given and the `INTO DEST_VARS` clause must also be given and must not
refer to a string variable.  `CONVERT` causes a number specified as a
string to be converted to a numeric value.  For example it converts
the string `"3"` into the numeric value 3 (note that it does not
convert `three` into 3).  If the string cannot be parsed as a number,
then the system-missing value is assigned instead.  In the following
example, cases where the value of `x` (a string variable) is the empty
string, are recoded to 999 and all others are converted to the numeric
equivalent of the input value.  The results are placed into the
numeric variable `y`:

```
RECODE x ("" = 999) (CONVERT) INTO y.
```

It is possible to specify multiple recodings on a single command.
Introduce additional recodings with a slash (`/`) to separate them from
the previous recodings:

```
RECODE
    a (2 = 22) (ELSE = 99)
   /b (1 = 3) INTO z.
```

Here we have two recodings.  The first affects the source variable `a`
and recodes in-place the value 2 into 22 and all other values to 99.
The second recoding copies the values of `b` into the variable `z`,
changing any instances of 1 into 3.

