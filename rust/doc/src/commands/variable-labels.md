# VARIABLE LABELS

Each variable can have a "label" to supplement its name.  Whereas a
variable name is a concise, easy-to-type mnemonic for the variable, a
label may be longer and more descriptive.

```
VARIABLE LABELS
        VARIABLE 'LABEL'
        [VARIABLE 'LABEL']...
```

`VARIABLE LABELS` associates explanatory names with variables.  This
name, called a "variable label", is displayed by statistical
procedures.

Specify each variable followed by its label as a quoted string.
Variable-label pairs may be separated by an optional slash `/`.

If a listed variable already has a label, the new one replaces it.
Specifying an empty string as the label, e.g. `''`, removes a label.

