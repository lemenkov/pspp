# DEFINE…!ENDDEFINE

<!-- toc -->

## Overview

```
DEFINE macro_name([argument[/argument]...])
...body...
!ENDDEFINE.
```

Each argument takes the following form:
```
{!arg_name= | !POSITIONAL}
[!DEFAULT(default)]
[!NOEXPAND]
{!TOKENS(count) | !CHAREND('token') | !ENCLOSE('start' | 'end') | !CMDEND}
```

The following directives may be used within body:
```
!OFFEXPAND
!ONEXPAND
```

The following functions may be used within the body:
```
!BLANKS(count)
!CONCAT(arg...)
!EVAL(arg)
!HEAD(arg)
!INDEX(haystack, needle)
!LENGTH(arg)
!NULL
!QUOTE(arg)
!SUBSTR(arg, start[, count])
!TAIL(arg)
!UNQUOTE(arg)
!UPCASE(arg)
```

The body may also include the following constructs:
```
!IF (condition) !THEN true-expansion !ENDIF
!IF (condition) !THEN true-expansion !ELSE false-expansion !ENDIF

!DO !var = start !TO end [!BY step]
  body
!DOEND
!DO !var !IN (expression)
  body
!DOEND

!LET !var = expression
```

## Introduction

The DEFINE command creates a "macro", which is a name for a fragment of
PSPP syntax called the macro's "body".  Following the DEFINE command,
syntax may "call" the macro by name any number of times.  Each call
substitutes, or "expands", the macro's body in place of the call, as if
the body had been written in its place.

The following syntax defines a macro named `!vars` that expands to
the variable names `v1 v2 v3`.  The macro's name begins with `!`, which
is optional for macro names.  The `()` following the macro name are
required:

```
DEFINE !vars()
v1 v2 v3
!ENDDEFINE.
```

Here are two ways that `!vars` might be called given the preceding
definition:

```
DESCRIPTIVES !vars.
FREQUENCIES /VARIABLES=!vars.
```

With macro expansion, the above calls are equivalent to the
following:

```
DESCRIPTIVES v1 v2 v3.
FREQUENCIES /VARIABLES=v1 v2 v3.
```

The `!vars` macro expands to a fixed body.  Macros may have more
sophisticated contents:

- Macro "[arguments](#macro-arguments)" that are substituted into the
  body whenever they are named.  The values of a macro's arguments are
  specified each time it is called.

- Macro "[functions](#macro-functions)", expanded when the macro is
  called.

- [`!IF` constructs](#macro-conditional-expansion), for conditional expansion.

- Two forms of [`!DO` construct](#macro-loops), for looping over a
  numerical range or a collection of tokens.

- [`!LET` constructs](#macro-variable-assignment), for assigning to
  macro variables.

Many identifiers associated with macros begin with `!`, a character
not normally allowed in identifiers.  These identifiers are reserved
only for use with macros, which helps keep them from being confused with
other kinds of identifiers.

The following sections provide more details on macro syntax and
semantics.

## Macro Bodies

As previously shown, a macro body may contain a fragment of a PSPP
command (such as a variable name).  A macro body may also contain full
PSPP commands.  In the latter case, the macro body should also contain
the command terminators.

Most PSPP commands may occur within a macro.  The `DEFINE` command
itself is one exception, because the inner `!ENDDEFINE` ends the outer
macro definition.  For compatibility, `BEGIN DATA`...`END DATA.`
should not be used within a macro.

The body of a macro may call another macro.  The following shows one
way that could work:

```
DEFINE !commands()
DESCRIPTIVES !vars.
FREQUENCIES /VARIABLES=!vars.
!ENDDEFINE.

* Initially define the 'vars' macro to analyze v1...v3.
DEFINE !vars() v1 v2 v3 !ENDDEFINE.
!commands

* Redefine 'vars' macro to analyze different variables.
DEFINE !vars() v4 v5 !ENDDEFINE.
!commands
```

The `!commands` macro would be easier to use if it took the variables
to analyze as an argument rather than through another macro.  The
following section shows how to do that.

## Macro Arguments

This section explains how to use macro arguments.  As an initial
example, the following syntax defines a macro named `!analyze` that
takes all the syntax up to the first command terminator as an argument:

```
DEFINE !analyze(!POSITIONAL !CMDEND)
DESCRIPTIVES !1.
FREQUENCIES /VARIABLES=!1.
!ENDDEFINE.
```

When `!analyze` is called, it expands to a pair of analysis commands
with each `!1` in the body replaced by the argument.  That is, these
calls:

```
!analyze v1 v2 v3.
!analyze v4 v5.
```

act like the following:

```
DESCRIPTIVES v1 v2 v3.
FREQUENCIES /VARIABLES=v1 v2 v3.
DESCRIPTIVES v4 v5.
FREQUENCIES /VARIABLES=v4 v5.
```

Macros may take any number of arguments, described within the
parentheses in the DEFINE command.  Arguments come in two varieties
based on how their values are specified when the macro is called:

- A "positional" argument has a required value that follows the
  macro's name.  Use the `!POSITIONAL` keyword to declare a
  positional argument.

  When a macro is called, the positional argument values appear in
  the same order as their definitions, before any keyword argument
  values.

  References to a positional argument in a macro body are numbered:
  `!1` is the first positional argument, `!2` the second, and so on.
  In addition, `!*` expands to all of the positional arguments'
  values, separated by spaces.

  The following example uses a positional argument:

  ```
  DEFINE !analyze(!POSITIONAL !CMDEND)
  DESCRIPTIVES !1.
  FREQUENCIES /VARIABLES=!1.
  !ENDDEFINE.

  !analyze v1 v2 v3.
  !analyze v4 v5.
  ```

- A "keyword" argument has a name.  In the macro call, its value is
  specified with the syntax `name=value`.  The names allow keyword
  argument values to take any order in the call.

  In declaration and calls, a keyword argument's name may not begin
  with `!`, but references to it in the macro body do start with a
  leading `!`.

  The following example uses a keyword argument that defaults to ALL
  if the argument is not assigned a value:

  ```
  DEFINE !analyze_kw(vars=!DEFAULT(ALL) !CMDEND)
  DESCRIPTIVES !vars.
  FREQUENCIES /VARIABLES=!vars.
  !ENDDEFINE.

  !analyze_kw vars=v1 v2 v3.  /* Analyze specified variables.
  !analyze_kw.                /* Analyze all variables.
  ```

If a macro has both positional and keyword arguments, then the
positional arguments must come first in the DEFINE command, and their
values also come first in macro calls.  A keyword argument may be
omitted by leaving its keyword out of the call, and a positional
argument may be omitted by putting a command terminator where it would
appear.  (The latter case also omits any following positional
arguments and all keyword arguments, if there are any.)  When an
argument is omitted, a default value is used: either the value
specified in `!DEFAULT(value)`, or an empty value otherwise.

Each argument declaration specifies the form of its value:

* `!TOKENS(count)`  
  Exactly `count` tokens, e.g. `!TOKENS(1)` for a single token.  Each
  identifier, number, quoted string, operator, or punctuator is a
  token (see [Tokens](../../language/basics/tokens.md) for details).

  The following variant of `!analyze_kw` accepts only a single
  variable name (or `ALL`) as its argument:

  ```
  DEFINE !analyze_one_var(!POSITIONAL !TOKENS(1))
  DESCRIPTIVES !1.
  FREQUENCIES /VARIABLES=!1.
  !ENDDEFINE.

  !analyze_one_var v1.
  ```

* `!CHAREND('TOKEN')`  
  Any number of tokens up to `TOKEN`, which should be an operator or
  punctuator token such as `/` or `+`.  The `TOKEN` does not become
  part of the value.

  With the following variant of `!analyze_kw`, the variables must be
  following by `/`:

  ```
  DEFINE !analyze_parens(vars=!CHARNED('/'))
  DESCRIPTIVES !vars.
  FREQUENCIES /VARIABLES=!vars.
  !ENDDEFINE.

  !analyze_parens vars=v1 v2 v3/.
  ```

* `!ENCLOSE('START','END')`  
  Any number of tokens enclosed between `START` and `END`, which
  should each be operator or punctuator tokens.  For example, use
  `!ENCLOSE('(',')')` for a value enclosed within parentheses.  (Such
  a value could never have right parentheses inside it, even paired
  with left parentheses.)  The start and end tokens are not part of
  the value.

  With the following variant of `!analyze_kw`, the variables must be
  specified within parentheses:

  ```
  DEFINE !analyze_parens(vars=!ENCLOSE('(',')'))
  DESCRIPTIVES !vars.
  FREQUENCIES /VARIABLES=!vars.
  !ENDDEFINE.

  !analyze_parens vars=(v1 v2 v3).
  ```

* `!CMDEND`  
  Any number of tokens up to the end of the command.  This should be
  used only for the last positional parameter, since it consumes all
  of the tokens in the command calling the macro.

  The following variant of `!analyze_kw` takes all the variable names
  up to the end of the command as its argument:

  ```
  DEFINE !analyze_kw(vars=!CMDEND)
  DESCRIPTIVES !vars.
  FREQUENCIES /VARIABLES=!vars.
  !ENDDEFINE.

  !analyze_kw vars=v1 v2 v3.
  ```

By default, when an argument's value contains a macro call, the call
is expanded each time the argument appears in the macro's body.  The
[`!NOEXPAND` keyword](#controlling-macro-expansion) in an argument
declaration suppresses this expansion.

## Controlling Macro Expansion

Multiple factors control whether macro calls are expanded in different
situations.  At the highest level, `SET MEXPAND` controls whether
macro calls are expanded.  By default, it is enabled.  [`SET
MEXPAND`](../utilities/set.md#mexpand), for details.

A macro body may contain macro calls.  By default, these are expanded.
If a macro body contains `!OFFEXPAND` or `!ONEXPAND` directives, then
`!OFFEXPAND` disables expansion of macro calls until the following
`!ONEXPAND`.

A macro argument's value may contain a macro call.  These macro calls
are expanded, unless the argument was declared with the `!NOEXPAND`
keyword.

The argument to a macro function is a special context that does not
expand macro calls.  For example, if `!vars` is the name of a macro,
then `!LENGTH(!vars)` expands to 5, as does `!LENGTH(!1)` if
positional argument 1 has value `!vars`.  To expand macros in these
cases, use the [`!EVAL` macro function](#eval),
e.g. `!LENGTH(!EVAL(!vars))` or `!LENGTH(!EVAL(!1))`.

These rules apply to macro calls, not to uses within a macro body of
macro functions, macro arguments, and macro variables created by `!DO`
or `!LET`, which are always expanded.

`SET MEXPAND` may appear within the body of a macro, but it will not
affect expansion of the macro that it appears in.  Use `!OFFEXPAND`
and `!ONEXPAND` instead.

## Macro Functions

Macro bodies may manipulate syntax using macro functions.  Macro
functions accept tokens as arguments and expand to sequences of
characters.

The arguments to macro functions have a restricted form.  They may
only be a single token (such as an identifier or a string), a macro
argument, or a call to a macro function.  Thus, the following are valid
macro arguments:

- `x`
- `5.0`
- `x`
- `!1`
- `"5 + 6"`
- `!CONCAT(x,y)`

and the following are not (because they are each multiple tokens):

- `x y`
- `5+6`

Macro functions expand to sequences of characters.  When these
character strings are processed further as character strings,
e.g. with `!LENGTH`, any character string is valid.  When they are
interpreted as PSPP syntax, e.g. when the expansion becomes part of a
command, they need to be valid for that purpose.  For example,
`!UNQUOTE("It's")` will yield an error if the expansion `It's` becomes
part of a PSPP command, because it contains unbalanced single quotes,
but `!LENGTH(!UNQUOTE("It's"))` expands to 4.

The following macro functions are available.

* `!BLANKS(count)`  
     Expands to COUNT unquoted spaces, where COUNT is a nonnegative
     integer.  Outside quotes, any positive number of spaces are
     equivalent; for a quoted string of spaces, use
     `!QUOTE(!BLANKS(COUNT))`.

     In the examples below, `_` stands in for a space to make the
     results visible.

     ```
     !BLANKS(0)                  ↦ empty
     !BLANKS(1)                  ↦ _
     !BLANKS(2)                  ↦ __
     !QUOTE(!BLANKS(5))          ↦ '_____'
     ```

     |Call|Expansion|
     |:-----|:--------|
     |`!BLANKS(0)`|(empty)|
     |`!BLANKS(1)`|`_`|
     |`!BLANKS(2)`|`__`|
     |`!QUOTE(!BLANKS(5))|`'_____'`|

* `!CONCAT(arg...)`  
     Expands to the concatenation of all of the arguments.  Before
     concatenation, each quoted string argument is unquoted, as if
     `!UNQUOTE` were applied.  This allows for "token pasting",
     combining two (or more) tokens into a single one:

     |Call|Expansion|
     |:-----|:--------|
     |`!CONCAT(x, y)`|`xy`|
     |`!CONCAT('x', 'y')`|`xy`|
     |`!CONCAT(12, 34)`|`1234`|
     |`!CONCAT(!NULL, 123)`|`123`|

     `!CONCAT` is often used for constructing a series of similar
     variable names from a prefix followed by a number and perhaps a
     suffix.  For example:

     |Call|Expansion|
     |:-----|:--------|
     |`!CONCAT(x, 0)`|`x0`|
     |`!CONCAT(x, 0, y)`|`x0y`|

     An identifier token must begin with a letter (or `#` or `@`), which
     means that attempting to use a number as the first part of an
     identifier will produce a pair of distinct tokens rather than a
     single one.  For example:

     |Call|Expansion|
     |:-----|:--------|
     |`!CONCAT(0, x)`|`0 x`|
     |`!CONCAT(0, x, y)`|`0 xy`|

* <a name="eval">`!EVAL(arg)`</a>  
     Expands macro calls in ARG.  This is especially useful if ARG is
     the name of a macro or a macro argument that expands to one,
     because arguments to macro functions are not expanded by default
     (see [Controlling Macro Expansion](#controlling-macro-expansion)).

     The following examples assume that `!vars` is a macro that expands
     to `a b c`:

     |Call|Expansion|
     |:-----|:--------|
     |`!vars`|`a b c`|
     |`!QUOTE(!vars)`|`'!vars'`|
     |`!EVAL(!vars)`|`a b c`|
     |`!QUOTE(!EVAL(!vars))`|`'a b c'`|

     These examples additionally assume that argument `!1` has value
     `!vars`:

     |Call|Expansion|
     |:-----|:--------|
     |`!1`|`a b c`|
     |`!QUOTE(!1)`|`'!vars'`|
     |`!EVAL(!1)`|`a b c`|
     |`!QUOTE(!EVAL(!1))`|`'a b c'`|

* `!HEAD(arg)`  
  `!TAIL(arg)`  
     `!HEAD` expands to just the first token in an unquoted version of
     ARG, and `!TAIL` to all the tokens after the first.

     |Call|Expansion|
     |:-----|:--------|
     |`!HEAD('a b c')`|`a`|
     |`!HEAD('a')`|`a`|
     |`!HEAD(!NULL)`|(empty)|
     |`!HEAD('')`|(empty)|
     |`!TAIL('a b c')`|`b c`|
     |`!TAIL('a')`|(empty)|
     |`!TAIL(!NULL)`|(empty)|
     |`!TAIL('')`|(empty)|

* `!INDEX(haystack, needle)`  
     Looks for NEEDLE in HAYSTACK.  If it is present, expands to the
     1-based index of its first occurrence; if not, expands to 0.

     |Call|Expansion|
     |:-----|:--------|
     |`!INDEX(banana, an)`|`2`|
     |`!INDEX(banana, nan)`|`3`|
     |`!INDEX(banana, apple)`|`0`|
     |`!INDEX("banana", nan)`|`4`|
     |`!INDEX("banana", "nan")`|`0`|
     |`!INDEX(!UNQUOTE("banana"), !UNQUOTE("nan"))`|`3`|

* `!LENGTH(arg)`  
     Expands to a number token representing the number of characters in
     ARG.

     |Call|Expansion|
     |:-----|:--------|
     |`!LENGTH(123)`|`3`|
     |`!LENGTH(123.00)`|`6`|
     |`!LENGTH( 123 )`|`3`|
     |`!LENGTH("123")`|`5`|
     |`!LENGTH(xyzzy)`|`5`|
     |`!LENGTH("xyzzy")`|`7`|
     |`!LENGTH("xy""zzy")`|`9`|
     |`!LENGTH(!UNQUOTE("xyzzy"))`|`5`|
     |`!LENGTH(!UNQUOTE("xy""zzy"))`|`6`|
     |`!LENGTH(!1)`|`5` (if `!1` is `a b c`)|
     |`!LENGTH(!1)`|`0` (if `!1` is empty)|
     |`!LENGTH(!NULL)`|`0`|

* `!NULL`  
     Expands to an empty character sequence.

     |Call|Expansion|
     |:-----|:--------|
     |`!NULL`|(empty)|
     |`!QUOTE(!NULL)`|`''`|

* `!QUOTE(arg)`  
  `!UNQUOTE(arg)`  
     The `!QUOTE` function expands to its argument surrounded by
     apostrophes, doubling any apostrophes inside the argument to make
     sure that it is valid PSPP syntax for a string.  If the argument
     was already a quoted string, `!QUOTE` expands to it unchanged.

     Given a quoted string argument, the `!UNQUOTED` function expands to
     the string's contents, with the quotes removed and any doubled
     quote marks reduced to singletons.  If the argument was not a
     quoted string, `!UNQUOTE` expands to the argument unchanged.

     |Call|Expansion|
     |:-----|:--------|
     |`!QUOTE(123.0)`|`'123.0'`|
     |`!QUOTE( 123 )`|`'123'`|
     |`!QUOTE('a b c')`|`'a b c'`|
     |`!QUOTE("a b c")`|`"a b c"`|
     |`!QUOTE(!1)`|`'a ''b'' c'` (if `!1` is `a 'b' c`)|
     |`!UNQUOTE(123.0)`|`123.0`|
     |`!UNQUOTE( 123 )`|`123`|
     |`!UNQUOTE('a b c')`|`a b c`|
     |`!UNQUOTE("a b c")`|`a b c`|
     |`!UNQUOTE(!1)`|`a 'b' c` (if `!1` is `a 'b' c`)|
     |`!QUOTE(!UNQUOTE(123.0))`|`'123.0'`|
     |`!QUOTE(!UNQUOTE( 123 ))`|`'123'`|
     |`!QUOTE(!UNQUOTE('a b c'))`|`'a b c'`|
     |`!QUOTE(!UNQUOTE("a b c"))`|`'a b c'`|
     |`!QUOTE(!UNQUOTE(!1))`|`'a ''b'' c'` (if `!1` is `a 'b' c`)|

* `!SUBSTR(arg, start[, count])`  
     Expands to a substring of ARG starting from 1-based position START.
     If COUNT is given, it limits the number of characters in the
     expansion; if it is omitted, then the expansion extends to the end
     of ARG.

     ```
     |Call|Expansion|
     |:-----|:--------|
     |`!SUBSTR(banana, 3)`|`nana`|
     |`!SUBSTR(banana, 3, 3)`|`nan`|
     |`!SUBSTR("banana", 1, 3)`|error (`"ba` is not a valid token)|
     |`!SUBSTR(!UNQUOTE("banana"), 3)`|`nana`|
     |`!SUBSTR("banana", 3, 3)`|`ana`|
     |`!SUBSTR(banana, 3, 0)`|(empty)|
     |`!SUBSTR(banana, 3, 10)`|`nana`|
     |`!SUBSTR(banana, 10, 3)`|(empty)|
     ```

* `!UPCASE(arg)`  
     Expands to an unquoted version of ARG with all letters converted to
     uppercase.

     |Call|Expansion|
     |:-----|:--------|
     |`!UPCASE(freckle)`|`FRECKLE`|
     |`!UPCASE('freckle')`|`FRECKLE`|
     |`!UPCASE('a b c')`|`A B C`|
     |`!UPCASE('A B C')`|`A B C`|

## Macro Expressions

Macro expressions are used in conditional expansion and loops, which are
described in the following sections.  A macro expression may use the
following operators, listed in descending order of operator precedence:

* `()`  
  Parentheses override the default operator precedence.

* `!EQ !NE !GT !LT !GE !LE = ~= <> > < >= <=`  
  Relational operators compare their operands and yield a Boolean
  result, either `0` for false or `1` for true.

  These operators always compare their operands as strings.  This can
  be surprising when the strings are numbers because, e.g., `1 < 1.0`
  and `10 < 2` both evaluate to `1` (true).

  Comparisons are case sensitive, so that `a = A` evaluates to `0`
  (false).

* `!NOT ~`  
  `!AND &`  
  `!OR |`  
  Logical operators interpret their operands as Boolean values, where
  quoted or unquoted `0` is false and anything else is true, and
  yield a Boolean result, either `0` for false or `1` for true.

Macro expressions do not include any arithmetic operators.

An operand in an expression may be a single token (including a macro
argument name) or a macro function invocation.  Either way, the
expression evaluator unquotes the operand, so that `1 = '1'` is true.

## Macro Conditional Expansion

The `!IF` construct may be used inside a macro body to allow for
conditional expansion.  It takes the following forms:

```
!IF (EXPRESSION) !THEN TRUE-EXPANSION !IFEND
!IF (EXPRESSION) !THEN TRUE-EXPANSION !ELSE FALSE-EXPANSION !IFEND
```

When `EXPRESSION` evaluates to true, the macro processor expands
`TRUE-EXPANSION`; otherwise, it expands `FALSE-EXPANSION`, if it is
present.  The macro processor considers quoted or unquoted `0` to be
false, and anything else to be true.

## Macro Loops

The body of a macro may include two forms of loops: loops over numerical
ranges and loops over tokens.  Both forms expand a "loop body" multiple
times, each time setting a named "loop variable" to a different value.
The loop body typically expands the loop variable at least once.

The [`MITERATE` setting](../utilities/set.md#miterate) limits the number of
iterations in a loop.  This is a safety measure to ensure that macro
expansion terminates.  PSPP issues a warning when the `MITERATE` limit is
exceeded.

### Loops Over Ranges

```
!DO !VAR = START !TO END [!BY STEP]
  BODY
!DOEND
```

A loop over a numerical range has the form shown above.  `START`,
`END`, and `STEP` (if included) must be expressions with numeric
values.  The macro processor accepts both integers and real numbers.
The macro processor expands `BODY` for each numeric value from `START`
to `END`, inclusive.

The default value for `STEP` is 1.  If `STEP` is positive and `FIRST >
LAST`, or if `STEP` is negative and `FIRST < LAST`, then the macro
processor doesn't expand the body at all.  `STEP` may not be zero.

### Loops Over Tokens

```
!DO !VAR !IN (EXPRESSION)
  BODY
!DOEND
```

A loop over tokens takes the form shown above.  The macro processor
evaluates `EXPRESSION` and expands `BODY` once per token in the
result, substituting the token for `!VAR` each time it appears.

## Macro Variable Assignment

The `!LET` construct evaluates an expression and assigns the result to a
macro variable.  It may create a new macro variable or change the value
of one created by a previous `!LET` or `!DO`, but it may not change the
value of a macro argument.  `!LET` has the following form:

```
!LET !VAR = EXPRESSION
```

If `EXPRESSION` is more than one token, it must be enclosed in
parentheses.

## Macro Settings

Some macro behavior is controlled through the
[`SET`](../utilities/set.md) command.  This section describes these
settings.

Any `SET` command that changes these settings within a macro body only
takes effect following the macro.  This is because PSPP expands a
macro's entire body at once, so that `SET` inside the body only
executes afterwards.

The [`MEXPAND`](../utilities/set.md#mexpand) setting controls whether
macros will be expanded at all.  By default, macro expansion is on.
To avoid expansion of macros called within a macro body, use
[`!OFFEXPAND` and `!ONEXPAND`](#controlling-macro-expansion).

When [`MPRINT`](../utilities/set.md#mprint) is turned on, PSPP outputs
an expansion of each macro called.  This feature can be useful for
debugging macro definitions.  For reading the expanded version, keep
in mind that macro expansion removes comments and standardizes white
space.

[`MNEST`](../utilities/set.md#mnest) limits the depth of expansion of
macro calls, that is, the nesting level of macro expansion.  The
default is 50.  This is mainly useful to avoid infinite expansion in
the case of a macro that calls itself.

[`MITERATE`](../utilities/set.md#miterate) limits the number of
iterations in a `!DO` construct.  The default is 1000.

## Additional Notes

### Calling Macros from Macros

If the body of macro A includes a call to macro B, the call can use
macro arguments (including `!*`) and macro variables as part of
arguments to B. For `!TOKENS` arguments, the argument or variable name
counts as one token regardless of the number that it expands into; for
`!CHAREND` and `!ENCLOSE` arguments, the delimiters come only from the
call, not the expansions; and `!CMDEND` ends at the calling command, not
any end of command within an argument or variable.

Macro functions are not supported as part of the arguments in a macro
call.  To get the same effect, use `!LET` to define a macro variable,
then pass the macro variable to the macro.

When macro A calls macro B, the order of their `DEFINE` commands
doesn't matter, as long as macro B has been defined when A is called.

### Command Terminators

Macros and command terminators require care.  Macros honor the syntax
differences between [interactive and batch
syntax](../../language/basics/syntax-variants.md), which means that
the interpretation of a macro can vary depending on the syntax mode in
use.  We assume here that interactive mode is in use, in which `.` at
the end of a line is the primary way to end a command.

The `DEFINE` command needs to end with `.` following the `!ENDDEFINE`.
The macro body may contain `.` if it is intended to expand to whole
commands, but using `.` within a macro body that expands to just
syntax fragments (such as a list of variables) will cause syntax
errors.

Macro directives such as `!IF` and `!DO` do not end with `.`.

### Expansion Contexts

PSPP does not expand macros within comments, whether introduced within
a line by `/*` or as a separate [`COMMENT` or
`*`](../utilities/comment.md) command.  (SPSS does expand macros in
`COMMENT` and `*`.)

Macros do not expand within quoted strings.

Macros are expanded in the [`TITLE`](../utilities/title.md) and
[`SUBTITLE`](../utilities/subtitle.md) commands as long as their
arguments are not quoted strings.

### PRESERVE and RESTORE

Some macro bodies might use the [`SET`](../utilities/set.md) command
to change certain settings.  When this is the case, consider using the
[`PRESERVE` and `RESTORE`](../utilities/preserve.md) commands to save
and then restore these settings.

