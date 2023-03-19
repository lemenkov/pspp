# VARIABLE ATTRIBUTE

`VARIABLE ATTRIBUTE` adds, modifies, or removes user-defined attributes
associated with variables in the active dataset.  Custom variable
attributes are not interpreted by PSPP, but they are saved as part of
system files and may be used by other software that reads them.

```
VARIABLE ATTRIBUTE
         VARIABLES=VAR_LIST
         ATTRIBUTE=NAME('VALUE') [NAME('VALUE')]...
         ATTRIBUTE=NAME[INDEX]('VALUE') [NAME[INDEX]('VALUE')]...
         DELETE=NAME [NAME]...
         DELETE=NAME[INDEX] [NAME[INDEX]]...
```

The required `VARIABLES` subcommand must come first.  Specify the
variables to which the following `ATTRIBUTE` or `DELETE` subcommand
should apply.

Use the `ATTRIBUTE` subcommand to add or modify custom variable
attributes.  Specify the name of the attribute as an
[identifier](../../language/basics/tokens.md), followed by the desired
value, in parentheses, as a quoted string.  The specified attributes
are then added or modified in the variables specified on `VARIABLES`.
Attribute names that begin with `$` are reserved for PSPP's internal
use, and attribute names that begin with `@` or `$@` are not displayed
by most PSPP commands that display other attributes.  Other attribute
names are not treated specially.

Attributes may also be organized into arrays.  To assign to an array
element, add an integer array index enclosed in square brackets (`[`
and `]`) between the attribute name and value.  Array indexes start at
1, not 0.  An attribute array that has a single element (number 1) is
not distinguished from a non-array attribute.

Use the `DELETE` subcommand to delete an attribute from the variable
specified on `VARIABLES`.  Specify an attribute name by itself to
delete an entire attribute, including all array elements for attribute
arrays.  Specify an attribute name followed by an array index in
square brackets to delete a single element of an attribute array.  In
the latter case, all the array elements numbered higher than the
deleted element are shifted down, filling the vacated position.

To associate custom attributes with the entire active dataset, instead
of with particular variables, use [`DATAFILE
ATTRIBUTE`](../../commands/data-io/datafile-attribute.md) instead.

`VARIABLE ATTRIBUTE` takes effect immediately.  It is not affected by
conditional and looping structures such as `DO IF` or `LOOP`.

