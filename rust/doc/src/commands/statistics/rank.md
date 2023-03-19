# RANK

```
RANK
        [VARIABLES=] VAR_LIST [{A,D}] [BY VAR_LIST]
        /TIES={MEAN,LOW,HIGH,CONDENSE}
        /FRACTION={BLOM,TUKEY,VW,RANKIT}
        /PRINT[={YES,NO}
        /MISSING={EXCLUDE,INCLUDE}

        /RANK [INTO VAR_LIST]
        /NTILES(k) [INTO VAR_LIST]
        /NORMAL [INTO VAR_LIST]
        /PERCENT [INTO VAR_LIST]
        /RFRACTION [INTO VAR_LIST]
        /PROPORTION [INTO VAR_LIST]
        /N [INTO VAR_LIST]
        /SAVAGE [INTO VAR_LIST]
```

The `RANK` command ranks variables and stores the results into new
variables.

The `VARIABLES` subcommand, which is mandatory, specifies one or more
variables whose values are to be ranked.  After each variable, `A` or
`D` may appear, indicating that the variable is to be ranked in
ascending or descending order.  Ascending is the default.  If a `BY`
keyword appears, it should be followed by a list of variables which are
to serve as group variables.  In this case, the cases are gathered into
groups, and ranks calculated for each group.

The `TIES` subcommand specifies how tied values are to be treated.
The default is to take the mean value of all the tied cases.

The `FRACTION` subcommand specifies how proportional ranks are to be
calculated.  This only has any effect if `NORMAL` or `PROPORTIONAL` rank
functions are requested.

The `PRINT` subcommand may be used to specify that a summary of the
rank variables created should appear in the output.

The function subcommands are `RANK`, `NTILES`, `NORMAL`, `PERCENT`,
`RFRACTION`, `PROPORTION`, and `SAVAGE`.  Any number of function
subcommands may appear.  If none are given, then the default is `RANK`.
The `NTILES` subcommand must take an integer specifying the number of
partitions into which values should be ranked.  Each subcommand may be
followed by the `INTO` keyword and a list of variables which are the
variables to be created and receive the rank scores.  There may be as
many variables specified as there are variables named on the
`VARIABLES` subcommand.  If fewer are specified, then the variable
names are automatically created.

The `MISSING` subcommand determines how user missing values are to be
treated.  A setting of `EXCLUDE` means that variables whose values are
user-missing are to be excluded from the rank scores.  A setting of
`INCLUDE` means they are to be included.  The default is `EXCLUDE`.

