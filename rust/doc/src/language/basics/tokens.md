# Tokens

PSPP divides most syntax file lines into series of short chunks called
"tokens".  Tokens are then grouped to form commands, each of which
tells PSPP to take some action—read in data, write out data, perform a
statistical procedure, etc.  Each type of token is described below.

## Identifiers

Identifiers are names that typically specify variables, commands, or
subcommands.  The first character in an identifier must be a letter,
`#`, or `@`.  The remaining characters in the identifier must be
letters, digits, or one of the following special characters:

```
. _ $ # @
```

Identifiers may be any length, but only the first 64 bytes are
significant.  Identifiers are not case-sensitive: `foobar`,
`Foobar`, `FooBar`, `FOOBAR`, and `FoObaR` are different
representations of the same identifier.

Some identifiers are reserved.  Reserved identifiers may not be
used in any context besides those explicitly described in this
manual.  The reserved identifiers are:

```
ALL AND BY EQ GE GT LE LT NE NOT OR TO WITH
```

## Keywords

Keywords are a subclass of identifiers that form a fixed part of
command syntax.  For example, command and subcommand names are
keywords.  Keywords may be abbreviated to their first 3 characters
if this abbreviation is unambiguous.  (Unique abbreviations of 3 or
more characters are also accepted: `FRE`, `FREQ`, and `FREQUENCIES`
are equivalent when the last is a keyword.)

Reserved identifiers are always used as keywords.  Other
identifiers may be used both as keywords and as user-defined
identifiers, such as variable names.

## Numbers

Numbers are expressed in decimal.  A decimal point is optional.
Numbers may be expressed in scientific notation by adding `e` and a
base-10 exponent, so that `1.234e3` has the value 1234.  Here are
some more examples of valid numbers:

```
-5  3.14159265359  1e100  -.707  8945.
```

Negative numbers are expressed with a `-` prefix.  However, in
situations where a literal `-` token is expected, what appears to
be a negative number is treated as `-` followed by a positive
number.

No white space is allowed within a number token, except for
horizontal white space between `-` and the rest of the number.

The last example above, `8945.` is interpreted as two tokens, `8945`
and `.`, if it is the last token on a line (see [Forming
Commands](commands.md)).

## Strings

Strings are literal sequences of characters enclosed in pairs of
single quotes (`'`) or double quotes (`"`).  To include the
character used for quoting in the string, double it, e.g. `'it''s
an apostrophe'`.  White space and case of letters are significant
inside strings.

Strings can be concatenated using `+`, so that `"a" + 'b' + 'c'` is
equivalent to `'abc'`.  So that a long string may be broken across
lines, a line break may precede or follow, or both precede and
follow, the `+`.  (However, an entirely blank line preceding or
following the `+` is interpreted as ending the current command.)

Strings may also be expressed as hexadecimal character values by
prefixing the initial quote character by `x` or `X`.  Regardless of
the syntax file or active dataset's encoding, the hexadecimal
digits in the string are interpreted as Unicode characters in UTF-8
encoding.

> Individual Unicode code points may also be expressed by specifying
the hexadecimal code point number in single or double quotes
preceded by `u` or `U`.  For example, Unicode code point U+1D11E,
the musical G clef character, could be expressed as `U'1D11E'`.
Invalid Unicode code points (above U+10FFFF or in between U+D800
and U+DFFF) are not allowed.

When strings are concatenated with `+`, each segment's prefix is
considered individually.  For example, `'The G clef symbol is:' +
u"1d11e" + "."` inserts a G clef symbol in the middle of an
otherwise plain text string.

## Punctuators and Operators

These tokens are the punctuators and operators:

```
, / = ( ) + - * / ** < <= <> > >= ~= & | .
```

Most of these appear within the syntax of commands, but the period
(`.`) punctuator is used only at the end of a command.  It is a
punctuator only as the last character on a line (except white
space).  When it is the last non-space character on a line, a
period is not treated as part of another token, even if it would
otherwise be part of, e.g., an identifier or a floating-point
number.

