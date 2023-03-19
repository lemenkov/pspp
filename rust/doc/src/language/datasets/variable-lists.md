# Variable Lists

To refer to a set of variables, list their names one after another.
Optionally, their names may be separated by commas.  To include a
range of variables from the dictionary in the list, write the name of
the first and last variable in the range, separated by `TO`.  For
instance, if the dictionary contains six variables with the names
`ID`, `X1`, `X2`, `GOAL`, `MET`, and `NEXTGOAL`, in that order, then
`X2 TO MET` would include variables `X2`, `GOAL`, and `MET`.

   Commands that define variables, such as `DATA LIST`, give `TO` an
alternate meaning.  With these commands, `TO` define sequences of
variables whose names end in consecutive integers.  The syntax is two
identifiers that begin with the same root and end with numbers,
separated by `TO`.  The syntax `X1 TO X5` defines 5 variables, named
`X1`, `X2`, `X3`, `X4`, and `X5`.  The syntax `ITEM0008 TO ITEM0013`
defines 6 variables, named `ITEM0008`, `ITEM0009`, `ITEM0010`,
`ITEM0011`, `ITEM0012`, and `ITEM00013`.  The syntaxes `QUES001 TO
QUES9` and `QUES6 TO QUES3` are invalid.

   After a set of variables has been defined with `DATA LIST` or
another command with this method, the same set can be referenced on
later commands using the same syntax.

