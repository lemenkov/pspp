# Syntax Diagrams

The syntax of PSPP commands is presented in this manual with syntax
diagrams.

A syntax diagram is a series of definitions of "nonterminals".  Each
nonterminal is defined its name, then `::=`, then what the nonterminal
consists of.  If a nonterminal has multiple definitions, then any of
them is acceptable.  If the definition is empty, then one possible
expansion of that nonterminal is nothing.  Otherwise, the definition
consists of a series of nonterminals and "terminals".  The latter
represent single tokens and consist of:

- `KEYWORD`  
  Any word written in uppercase is that literal syntax keyword.

- `number`  
  A real number.

- `integer`  
  An integer number.

- `string`  
  A string.

- `var-name`  
  A single variable name.

- `=`, `/`, `+`, `-`, etc.  
  Operators and punctuators.

- `.`  
  The end of the command.  This is not necessarily an actual dot in
  the syntax file (see [Forming Commands](basics/commands.md)).

Some nonterminals are very common, so they are defined here in English
for clarity:

- `var-list`  
  A list of one or more variable names or the keyword `ALL`.

- `expression`  
  An [expression](../language/expressions/index.md).

The first nonterminal defined in a syntax diagram for a command is
the entire syntax for that command.

