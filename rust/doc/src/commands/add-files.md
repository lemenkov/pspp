# ADD FILES

```
ADD FILES

Per input file:
        /FILE={*,'FILE_NAME'}
        [/RENAME=(SRC_NAMES=TARGET_NAMES)...]
        [/IN=VAR_NAME]
        [/SORT]

Once per command:
        [/BY VAR_LIST[({D|A})] [VAR_LIST[({D|A})]...]]
        [/DROP=VAR_LIST]
        [/KEEP=VAR_LIST]
        [/FIRST=VAR_NAME]
        [/LAST=VAR_NAME]
        [/MAP]
```

`ADD FILES` adds cases from multiple input files.  The output, which
replaces the active dataset, consists all of the cases in all of the
input files.

`ADD FILES` shares the bulk of its syntax with other PSPP commands for
combining multiple data files (see [Common
Syntax](combining.md#common-syntax) for details).

When `BY` is not used, the output of `ADD FILES` consists of all the
cases from the first input file specified, followed by all the cases
from the second file specified, and so on.  When `BY` is used, the
output is additionally sorted on the `BY` variables.

When `ADD FILES` creates an output case, variables that are not part
of the input file from which the case was drawn are set to the
system-missing value for numeric variables or spaces for string
variables.

