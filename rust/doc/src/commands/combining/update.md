# UPDATE

```
UPDATE

Per input file:
        /FILE={*,'FILE_NAME'}
        [/RENAME=(SRC_NAMES=TARGET_NAMES)...]
        [/IN=VAR_NAME]
        [/SORT]

Once per command:
        /BY VAR_LIST[({D|A})] [VAR_LIST[({D|A})]]...
        [/DROP=VAR_LIST]
        [/KEEP=VAR_LIST]
        [/MAP]
```

`UPDATE` updates a "master file" by applying modifications from one
or more "transaction files".

`UPDATE` shares the bulk of its syntax with other PSPP commands for
combining multiple data files (see [Common
Syntax](index.md#common-syntax) for details).

At least two `FILE` subcommands must be specified.  The first `FILE`
subcommand names the master file, and the rest name transaction files.
Every input file must either be sorted on the variables named on the
`BY` subcommand, or the `SORT` subcommand must be used just after the
`FILE` subcommand for that input file.

`UPDATE` uses the variables specified on the `BY` subcommand, which
is required, to attempt to match each case in a transaction file with a
case in the master file:

- When a match is found, then the values of the variables present in
  the transaction file replace those variables' values in the new
  active file.  If there are matching cases in more than more
  transaction file, PSPP applies the replacements from the first
  transaction file, then from the second transaction file, and so on.
  Similarly, if a single transaction file has cases with duplicate
  `BY` values, then those are applied in order to the master file.

  When a variable in a transaction file has a missing value or when a
  string variable's value is all blanks, that value is never used to
  update the master file.

- If a case in the master file has no matching case in any transaction
  file, then it is copied unchanged to the output.

- If a case in a transaction file has no matching case in the master
  file, then it causes a new case to be added to the output,
  initialized from the values in the transaction file.

