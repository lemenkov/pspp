# DATAFILE ATTRIBUTE

```
DATAFILE ATTRIBUTE
         ATTRIBUTE=NAME('VALUE') [NAME('VALUE')]...
         ATTRIBUTE=NAME[INDEX]('VALUE') [NAME[INDEX]('VALUE')]...
         DELETE=NAME [NAME]...
         DELETE=NAME[INDEX] [NAME[INDEX]]...
```

   `DATAFILE ATTRIBUTE` adds, modifies, or removes user-defined
attributes associated with the active dataset.  Custom data file
attributes are not interpreted by PSPP, but they are saved as part of
system files and may be used by other software that reads them.

   Use the `ATTRIBUTE` subcommand to add or modify a custom data file
attribute.  Specify the name of the attribute, followed by the desired
value, in parentheses, as a quoted string.  Attribute names that begin
with `$` are reserved for PSPP's internal use, and attribute names
that begin with `@` or `$@` are not displayed by most PSPP commands
that display other attributes.  Other attribute names are not treated
specially.

   Attributes may also be organized into arrays.  To assign to an array
element, add an integer array index enclosed in square brackets (`[` and
`]`) between the attribute name and value.  Array indexes start at 1,
not 0.  An attribute array that has a single element (number 1) is not
distinguished from a non-array attribute.

   Use the `DELETE` subcommand to delete an attribute.  Specify an
attribute name by itself to delete an entire attribute, including all
array elements for attribute arrays.  Specify an attribute name followed
by an array index in square brackets to delete a single element of an
attribute array.  In the latter case, all the array elements numbered
higher than the deleted element are shifted down, filling the vacated
position.

   To associate custom attributes with particular variables, instead
of with the entire active dataset, use [`VARIABLE
ATTRIBUTE`](../variables/variable-attribute.md) instead.

   `DATAFILE ATTRIBUTE` takes effect immediately.  It is not affected by
conditional and looping structures such as `DO IF` or `LOOP`.

