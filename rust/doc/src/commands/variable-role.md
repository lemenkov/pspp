# VARIABLE ROLE

```
VARIABLE ROLE
        /ROLE VAR_LIST
        [/ROLE VAR_LIST]...
```

`VARIABLE ROLE` sets the intended role of a variable for use in dialog
boxes in graphical user interfaces.  Each `ROLE` specifies one of the
following roles for the variables that follow it:

* `INPUT`  
  An input variable, such as an independent variable.

* `TARGET`  
  An output variable, such as an dependent variable.

* `BOTH`  
  A variable used for input and output.

* `NONE`  
  No role assigned.  (This is a variable's default role.)

* `PARTITION`  
  Used to break the data into groups for testing.

* `SPLIT`  
  No meaning except for certain third party software.  (This role's
  meaning is unrelated to `SPLIT FILE`.)

The PSPPIRE GUI does not yet use variable roles.
