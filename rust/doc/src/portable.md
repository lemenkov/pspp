# Portable File Format

These days, most computers use the same internal data formats for
integer and floating-point data, if one ignores little differences
like big- versus little-endian byte ordering.  This has not always
been true, particularly in the 1960s or 1970s, when the portable file
format originated as a way to exchange data between systems with
incompatible data formats.

At the time, even bytes being 8 bits each was not a given.  For that
reason, the portable file format is a text format, because text files
could be exchanged portably among systems slightly more freely.  On
the other hand, character encoding was not standardized, so exchanging
data in portable file format required recoding it from the origin
system's character encoding to the destination's.

Some contemporary systems represented text files as sequences of
fixed-length (typically 80-byte) records, without new-line sequences.
These operating systems padded lines shorter lines with spaces and
truncated longer lines.  To tolerate files copied from such systems,
which might drop spaces at the ends of lines, the portable file format
treats lines less than 80 bytes long as padded with spaces to that
length.

The portable file format self-identifies the character encoding on the
system that produced it at the very beginning, in the
[header](#portable-file-header).  Since portable files are normally
recoded when they are transported from one system to another, this
identification can be wrong on its face: a file that was started in
EBCDIC, and is then recoded to ASCII, will still say `EBCDIC SPSS PORT
FILE` at the beginning, just in ASCII instead of EBCDIC.

The portable file header also contains a table of all of the
characters that it supports.  Readers use this to translate each byte
of the file into its local encoding.  Like the rest of the portable
file, the character table is recoded when the file is moved to a
system with a different character set so that it remains correct, or
at least consistent with the rest of the file.

The portable file format is mostly obsolete.  [System
files](system-file.md) are a better alternative.

<!-- toc -->

## Sources

The information in this chapter is drawn from documentation and source
code, including:

* `pff.tar.Z`, a Fortran program from the 1980s that reads and writes
  portable files.  This program contains translation tables from the
  portable character set to EBCDIC and to ASCII.

* <a name="document">A document</a>, now lost, that describes portable
  file syntax.

It is further informed by a <a name="corpus">corpus</a> of about 1,400
portable files.  The plausible creation dates in the corpus range from
1986 to 2025, in addition to 131 files with alleged creation dates
between 1900 and 1907 and 21 files with an invalid creation date.

[document]: #document
[corpus]: #corpus

## Portable File Characters

Portable files are arranged as a series of lines of 80 characters each.
Each line is terminated by a carriage-return, line-feed sequence
("new-lines").  New-lines are only used to avoid line length limits
imposed by some OSes; they are not meaningful.

Most lines in portable files are exactly 80 characters long.  The
only exception is a line that ends in one or more spaces, in which the
spaces may optionally be omitted.  Thus, a portable file reader must act
as though a line shorter than 80 characters is padded to that length
with spaces.

The file must be terminated with a `Z` character.  In addition, if
the final line in the file does not have exactly 80 characters, then it
is padded on the right with `Z` characters.  (The file contents may be
in any character set; the file contains a description of its own
character set, as explained in the next section.  Therefore, the `Z`
character is not necessarily an ASCII `Z`.)

For the rest of the description of the portable file format,
new-lines and the trailing `Z`s will be ignored, as if they did not
exist, because they are not an important part of understanding the file
contents.

## Portable File Structure

Every portable file consists of the following records, in sequence:

- Splash strings.

- Version and date info.

- Product identification.

- Author identification (optional).

- Subproduct identification (optional).

- Variable count.

- Case weight variable (optional).

- Variables.  Each variable record may optionally be followed by a
  missing value record and a variable label record.

- Value labels (optional).

- Documents (optional).

- Data.

Most records are identified by a single-character tag code.  The file
header and version info record do not have a tag.

Other than these single-character codes, there are three types of
fields in a portable file: floating-point, integer, and string.
Floating-point fields have the following format:

- Zero or more leading spaces.

- Optional asterisk (`*`), which indicates a missing value.  The
  asterisk must be followed by a single character, generally a period
  (`.`), but it appears that other characters may also be possible.
  This completes the specification of a missing value.

- Optional minus sign (`-`) to indicate a negative number.

- A whole number, consisting of one or more base-30 digits: `0`
  through `9` plus capital letters `A` through `T`.

- Optional fraction, consisting of a radix point (`.`) followed by
  one or more base-30 digits.

- Optional exponent, consisting of a plus or minus sign (`+` or `-`)
  followed by one or more base-30 digits.

- A forward slash (`/`).

Integer fields take a form identical to floating-point fields, but
they may not contain a fraction.

String fields take the form of a integer field having value N,
followed by exactly N characters, which are the string content.

> Strings longer than 255 bytes exist in the [corpus].

## Splash Strings

Every portable file begins with 200 bytes of splash strings that serve
to identify the file's type and its original character set.  The 200
bytes are divided into five 40-byte sections, each of which is
supposed to represent the string `<CHARSET> SPSS PORT FILE` in a
different character set encoding[^0], where `<CHARSET>` is the name of
the character set used in the file, e.g. `ASCII` or `EBCDIC`.  Each
string is padded on the right with spaces in its respective character
set.

[^0]: The strings are supposed to be in EBCDIC, 7-bit ASCII, CDC 6-bit
  ASCII, 6-bit ASCII, and Honeywell 6-bit ASCII.  (It is somewhat
  astonishing that anyone considered the possibility of 6-bit "ASCII",
  or that there were at least three incompatible version of it.)

It appears that these strings exist only to inform those who might
view the file on a screen, letting them know what character set the
file is in regardless of how they are viewing it, and that they are
not parsed by SPSS products.  Thus, they can be safely ignored.  It is
reasonable to simply write out `ASCII SPSS PORT FILE` five times, each
time padded to 40 bytes.

## Translation Table

The splash strings are followed by a 256-byte character set translation table.
This segment describes a mapping from the character set used
in the portable file to a "portable character set" that does not
correspond to any known single-byte character set or code page.  Each
byte in the table reports the byte value that corresponds to the
character represented by its position.  The following section lists
the character at each position.

> For example, position 0x4a (decimal 74) in the portable character
set is uppercase letter A (as shown in the table in the following
section), so the 75th byte in the table is the value that represents
`A` in the file.

Any real character set will not necessarily include all of the
characters in the portable character set.  In the translation table,
omitted characters are written as digit `0`[^10].

[^10]: Character `0`, not NUL or byte zero.

> For example, in practice, all of the control character positions are
always written as `0`.

The following section describes how the translation table is supposed
to act based on looking at the [sources](#sources), and then the
section after that describes what it actually contains in practice.

### Theory

The table below shows the portable character set.  The columns in the
table are:

* "Pos", a position within the portable character set, in hex, from 00
  to FF.

* "EBCDIC", the translation for the given position to EBCDIC, as
  written in `pff.tar.Z`.

* "ASCII", the translation for the given position to ASCII, as written
  in `pff.tar.Z`.

* "Unicode", a suggestion for the best translation from this position to
  Unicode.

* "Notes", which links to additional information for some characters.

In addition to the [sources](#sources) previously cited, some of the
information below is drawn from [RFC 183], from 1971.  This RFC shows
many of the "EBCDIC" hex codes in `pff.tar.Z` as corresponding to the
descriptions in the document, even though no known EBCDIC codepage
contains those characters with those codes.

[RFC 183]: https://www.rfc-editor.org/rfc/rfc183.pdf

| Pos | EBCDIC | ASCII | Unicode |  | Notes
| -:  | :----- | :---- | :------ | :-------- | :----------
| 00 | 00 | — | — | — | [^1]
| 01 | 01 | — | — | — | [^1]
| 02 | 02 | — | — | — | [^1]
| 03 | 03 | — | — | — | [^1]
| 04 | 04 | — | — | — | [^1]
| 05 | 05 | — | U+0009 CHARACTER TABULATION | — | [^1]
| 06 | 06 | — |  — | — | [^1]
| 07 | 07 | — |  — | — | [^1]
| 08 | 08 | — |  — | — | [^1]
| 09 | 09 | — |  — | — | [^1]
| 0A | 0A | — |  — | — | [^1]
| 0B | 0B | — | U+000B LINE TABULATION | — | [^1]
| 0C | 0C | — | U+000C FORM FEED | — | [^1]
| 0D | 0D | — | U+000D CARRIAGE RETURN | — | [^1]
| 0E | 0E | — | — | — | [^1]
| 0F | 0F | — | — | — | [^1]
| 10 | 10 | — | — | — | [^1]
| 11 | 11 | — | — | — | [^1]
| 12 | 12 | — | — | — | [^1]
| 13 | 13 | — | — | — | [^1]
| 14 | 3C | — | — | — | [^1]
| 15 | 15 | — | U+000A LINE FEED | — | [^1]
| 16 | 16 | — | U+0008 BACKSPACE | — | [^1]
| 17 | 17 | — | — | — | [^1]
| 18 | 18 | — | — | — | [^1]
| 19 | 19 | — | — | — | [^1]
| 1A | 1A | — | — | — | [^1]
| 1B | 1B | — | — | — | [^1]
| 1C | 1C | — | — | — | [^1]
| 1D | 1D | — | — | — | [^1]
| 1E | 1E | — | — | — | [^1]
| 1F | 2A | — | — | — | [^1]
| 20 | 20 | — | — | — | [^1]
| 21 | 21 | — | — | — | [^1]
| 22 | 22 | — | — | — | [^1]
| 23 | 23 | — | — | — | [^1]
| 24 | 2B | — | — | — | [^1]
| 25 | 25 | — | U+000A LINE FEED | — | [^1]
| 26 | 26 | — | — | — | [^1]
| 27 | 27 | — | — | — | [^1]
| 28 | 1F | — | — | — | [^1]
| 29 | 24 | — | — | — | [^1]
| 2A | 14 | — | — | — | [^1]
| 2B | 2D | — | — | — | [^1]
| 2C | 2E | — | — | — | [^1]
| 2D | 2F | — | U+0007 BELL | — | [^1]
| 2E | 32 | — | — | — | [^1]
| 2F | 33 | — | — | — | [^1]
| 30 | 34 | — | — | — | [^1]
| 31 | 35 | — | — | — | [^1]
| 32 | 36 | — | — | — | [^1]
| 33 | 37 | — | — | — | [^1]
| 34 | 38 | — | — | — | [^1]
| 35 | 39 | — | — | — | [^1]
| 36 | 3A | — | — | — | [^1]
| 37 | 3B | — | — | — | [^1]
| 38 | 3D | — | — | — | [^1]
| 39 | 3F | — | — | — | [^1]
| 3A | 28 | — | — | — | [^1]
| 3B | 29 | — | — | — | [^1]
| 3C | 2C | — | — | — | [^1]
| 3D | — | — | — | — | [^8]
| 3E | — | — | — | — | [^8]
| 3F | — | — | — | — | [^8]
| 40 | F0 | 30 | U+0030 DIGIT ZERO | `0` |
| ... |
| 49 | F9 | 39 | U+0039 DIGIT NINE | `9` |
| 4A | C1 | 41 | U+0041 LATIN CAPITAL LETTER A | `A` |
| ... |
| 52 | C9 | 49 | U+0049 LATIN CAPITAL LETTER I | `I` |
| 53 | D1 | 4A | U+004A LATIN CAPITAL LETTER J | `J` |
| ... |
| 5B | D9 | 52 | U+0052 LATIN CAPITAL LETTER R | `R` |
| 5C | E2 | 53 | U+0053 LATIN CAPITAL LETTER S | `S` |
| ... |
| 63 | E9 | 5A | U+005A LATIN CAPITAL LETTER Z | `Z` |
| 64 | 81 | 61 | U+0061 LATIN SMALL LETTER A | `a` |
| ... |
| 7D | 89 | 69 | U+0069 LATIN SMALL LETTER I | `i` |
| 64 | 91 | 6A | U+006A LATIN SMALL LETTER J | `j` |
| ... |
| 7D | 99 | 72 | U+0072 LATIN SMALL LETTER R | `r` |
| 64 | A2 | 73 | U+0073 LATIN SMALL LETTER S | `s` |
| ... |
| 7D | A9 | 7A | U+007A LATIN SMALL LETTER Z | `z` |
| 7E | 40 | 20 | U+0020 SPACE | ` ` |
| 7F | 4B | 2E | U+002E FULL STOP | `.` |
| 80 | 4C | 3C | U+003C LESS-THAN SIGN | `<` |
| 81 | 4D | 28 | U+0028 LEFT PARENTHESIS | `(` |
| 82 | 4E | 2B | U+002B PLUS SIGN | `+` |
| 83 | 59 | — | U+007C VERTICAL LINE | `\|` | [^2]
| 84 | 50 | 26 | U+0026 AMPERSAND | `&` |
| 85 | AD | 5B | U+005B LEFT SQUARE BRACKET | `[` |
| 86 | BD | 5D | U+005D RIGHT SQUARE BRACKET | `]` |
| 87 | 5A | 21 | U+0021 EXCLAMATION MARK | `!` |
| 88 | 5B | 24 | U+0024 DOLLAR SIGN | `$` |
| 89 | 5C | 2A | U+002A ASTERISK | `*` |
| 8A | 5D | 29 | U+0029 RIGHT PARENTHESIS | `)` |
| 8B | 5E | 3B | U+003B SEMICOLON | `;` |
| 8C | 5F | 5E | U+005E CIRCUMFLEX ACCENT | `^` |
| 8D | 60 | 2D | U+002D HYPHEN-MINUS | `-` |
| 8E | 61 | 2F | U+002F SOLIDUS | `/` |
| 8F | 6A | 76 | U+00A6 BROKEN BAR | `¦` | [^2]
| 90 | 6B | 2C | U+002C COMMA | `,` |
| 91 | 6C | 25 | U+0025 PERCENT SIGN | `%` |
| 92 | 6D | 5F | U+005F LOW LINE | `_` |
| 93 | 6E | 3E | U+003E GREATER-THAN SIGN | `>` |
| 94 | 6F | 3F | U+003F QUESTION MARK | `?` |
| 95 | 79 | 60 | U+0060 GRAVE ACCENT | \` |
| 96 | 7A | 3A | U+003A COLON | `:` |
| 97 | 7B | 23 | U+0023 NUMBER SIGN | `#`
| 98 | 7C | 40 | U+0040 COMMERCIAL AT | `@` |
| 99 | 7D | 27 | U+0027 APOSTROPHE | `'` |
| 9A | 7E | 3D | U+003D EQUALS SIGN | `=` |
| 9B | 7F | 22 | U+0022 QUOTATION MARK | `"` |
| 9C | 8C | — | U+2264 LESS-THAN OR EQUAL TO | `≤` |
| 9D | 9C | — | U+25A1 WHITE SQUARE | `□` | [^3]
| 9E | 9E | — | U+00B1 PLUS-MINUS SIGN | `±` |
| 9F | 9F | — | U+25A0 BLACK SQUARE | `■` | [^4]
| A0 | — | — | U+00B0 DEGREE SIGN | `°` |
| A1 | 8F | — | U+2020 DAGGER | `†` |
| A2 | A1 | 7E | U+007E TILDE | `~` |
| A3 | A0 | — | U+2013 EN DASH | `–` |
| A4 | AB | — | U+2514 BOX DRAWINGS LIGHT UP AND RIGHT | `└` | [^5]
| A5 | AC | — | U+250C BOX DRAWINGS LIGHT DOWN AND RIGHT | `┌` | [^5]
| A6 | AE | — | U+2265 GREATER-THAN OR EQUAL TO | `≥` |
| A7 | B0 | — | U+2070 SUPERSCRIPT ZERO | `⁰` | [^5]
| ... |
| B0 | B9 | — | U+2079 SUPERSCRIPT NINE | `⁹` | [^5]
| B1 | BB | — | U+2518 BOX DRAWINGS LIGHT UP AND LEFT | `┘` | [^5]
| B2 | BC | — | U+2510 BOX DRAWINGS LIGHT DOWN AND LEFT | `┐` | [^5]
| B3 | BE | — | U+2260 NOT EQUAL TO | `≠`
| B4 | BF | — | U+2014 EM DASH | `—`
| B5 | 8D | — | U+2070 SUPERSCRIPT LEFT PARENTHESIS | `⁽`
| B6 | 9D | — | U+207E SUPERSCRIPT RIGHT PARENTHESIS | `⁾`
| B7 | BE | — | U+207A SUPERSCRIPT PLUS SIGN | `⁺` | [^6]
| B8 | C0 | 7B | U+007B LEFT CURLY BRACKET | `{`
| B9 | D0 | 7D | U+007D RIGHT CURLY BRACKET | `}`
| BA | E0 | 5C | U+005C REVERSE SOLIDUS | `\`
| BB | 4A | — | 0+00A2 CENT SIGN | `¢`
| BC | AF | — | U+00B7 MIDDLE DOT | `·` | [^7]
| BD | — | — | — | — | [^8]
| ... |
| FF | — | — | — | — | [^8]

[^1]: From the EBCDIC translation table in `pff.tar.Z`.  The ASCII
  translation table leaves all of them undefined.  Code points are
  only listed for common control characters with some modern relevance.

[^2]: The [document] describes 83 as "a solid vertical pipe" and 8F as
  "a broken vertical pipe".  Even though the ASCII translation table
  in `pff.tar.Z` leaves position 83 undefined and translates 8F to
  U+007C VERTICAL LINE, using U+007C VERTICAL LINE and U+00A6 BROKEN
  BAR, respectively, seem more accurate in a Unicode environment.

[^3]: Unicode inferred from [document] description as "empty box".

[^4]: Unicode inferred from [document] description as "filled box".

[^5]: These characters are as described in the [document].  Some of
    these don't appear in any known EBCDIC code page, but the EBCDIC
    translations given in `pff.tar.Z` match the graphics shown in [RFC
    183] with those hex codes.

[^6]: Described in [document] as "horizontal dagger", which doesn't
    appear in Unicode or any known code page.  This interpretation
    from [RFC 183] seems more likely.

[^7]: Unicode inferred from [document] description as "centered dot,
    or bullet"

[^8]: Reserved

Summary:

|   Range |         Characters |
|-------: |:-------------------|
| 40...4F | `0123456789ABCDEF` |
| 50...5F | `GHIJKLMNOPQRSTUV` |
| 60...6F | `WXYZabcdefghijkl` |
| 70...7F | `mnopqrstuvwxyz .` |
| 80...8F | `<(+\|&[]!$*);^-/¦` |
| 90...9F | ``,%_>?`:#@'="≤□±■`` |
| A0...AF | `°†~–└┌≥⁰ⁱ⁲⁳⁴⁵⁶⁷⁸` |
| B0...BC | `⁹┘┐≠—⁽⁾⁺{}\¢·` |

### Practice: Character Set

The previous section described the translation table in theory.  This
section describes what it contains in the [corpus].

Every file in the corpus is encoded in (extended) ASCII, although 31
of them indicate in their splash strings that they were recoded from
EBCDIC.  This also means that ASCII `0` indicates an unmapped
character, that is, one not in the character set represented by the
table.

The files are encoded in different ASCII extension.  Some appear to be
encoded in [windows-1252], others in [code page 437], others in
unidentified character sets.  The particular code page in use does not
matter to a reader that uses the table for mapping.

[windows-1252]: https://en.wikipedia.org/wiki/Windows-1252
[code page 437]: https://en.wikipedia.org/wiki/Code_page_437

* There are some invariants across the translation tables for every file
  in the corpus:

  - All control codes (in the range 0 to 63) are unmapped.

    One consequence is that strings in the corpus can never contain
    new-lines.  New-lines encoded literally would be problematic
    anyhow because readers [must ignore
    them](#portable-file-characters).

  - Digits `0` to `9` and letters `A` to `Z` and `a` to `z` are
    correctly mapped.

  - Punctuation for space as well as ``(+&$*);-/,%_?`:@'=\`` are
    correctly mapped.

* Characters `<!^>\"~{}` are mapped correctly in almost every file in
  the corpus, with a few outliers.

* Characters `[]` are mostly correct with a few problems.

* Position 97 is correctly `#` in most files, and wrongly `$` in some.

* The characters at positions 83 `|` and 8F `¦` have lots of issues,
  stemming from the history described [on Wikipedia].  In particular,
  EBCDIC and Unicode have separate characters for `|` and `¦`, but
  ASCII does not.

  [on Wikipedia]: https://en.wikipedia.org/wiki/Vertical_bar#Broken_bar

  Most of the corpus leaves 83 `|` unmapped.  Most of the rest map it
  correctly to `|`.  The remainder map it to `!`.

  Most of the corpus maps 8F `¦` to `|`.  Only a few map it correctly
  to `¦` in [windows-1252] or (creatively) to `║` in [code page 437].

* Characters at the following positions are almost always wrong.  The
  table shows:

  - "Character", the character and its position in the portable character set.

  - "Unmapped", the number of files in the corpus that leave the
    character unmapped (that is, set to `0`).

  - "windows-1252", the number of files that map the character
    correctly in [windows-1252].  If there is more than one plausible
    mapping, or if the mapping doesn't exactly match the preferred
    Unicode, the entry shows the mapped character.

  - "cp437", the number of files that map the character correctly in
    [code page 437].

    In a few cases, a plausible mapping in the "windows-1252" column
    is an ASCII character.  Those aren't separately counted in the
    "cp437" column, even though ASCII maps the same way in both
    encodings.

  - "Wrong", the number of files that map the character to nothing
    that makes sense in a known encoding.

  | Character        | Unmapped |           windows-1252 |      cp437 | Wrong |
  |:-----------------|---------:|-----------------------:|-----------:|------:|
  | 9C `≤`           |     1366 |                      0 |         10 |    28 |
  | A6 `≥`           |     1373 |                      0 |         10 |    21 |
  | 9F `■`           |     1373 |                      0 |         10 |    21 |
  | 9E `±`           |     1353 |                     15 |         15 |    23 |
  | A3 `–` (en dash) |     1302 |             as `-`: 65 |  as `─`: 5 |    32 |
  | B4 `—` (em dash) |     1308 |             as `-`: 65 | as `─`: 10 |    21 |
  | A4 `└`           |     1367 |                      0 |         15 |    22 |
  | A5 `┌`           |     1367 |                      0 |         15 |    22 |
  | B1 `┘`           |     1367 |                      0 |         15 |    22 |
  | B2 `┐`           |     1367 |                      0 |         15 |    22 |
  | A8 `¹`           |     1286 | as `¹`: 15; as `1`: 65 |          0 |    38 |
  | A9 `²`           |     1286 | as `²`: 15; as `2`: 65 |         15 |    23 |
  | AA `³`           |     1286 | as `³`: 15; as `3`: 65 |          0 |    38 |
  | AB `⁴`           |     1308 |             as `4`: 65 |          0 |    31 |
  | ...              |      ... |                    ... |        ... |   ... |
  | B0 `⁹`           |     1308 |             as `9`: 65 |          0 |    31 |
  | B3 `≠`           |     1373 |                      0 | as `╪`: 10 |    21 |
  | B6 `⁽`           |     1308 |                      0 |          0 |    96 |
  | B7 `⁾`           |     1373 |                      0 |          0 |    31 |
  | BB `¢`           |     1351 |                     16 |         10 |    27 |
  | BC `·`           |     1357 |  as `·`: 16; as `×`: 1 | as `∙`: 10 |    20 |
  | A0 `°`           |     1382 |  as `°`: 15; as `º`: 1 |          5 |     6 |

* Characters at the following positions are always unmapped or wrong:

  | Character | Unmapped | windows-1252 |      cp437 |                    Wrong |
  |:----------|---------:|-------------:|-----------:|-------------------------:|
  | 9D `□`    |     1373 |            0 | as `╬`: 10 |                       21 |
  | A1 `†`    |     1364 |            0 | as `┼`: 10 |                       30 |
  | A7 `⁰`    |     1373 |    as `Ø`: 1 |          0 |                       30 |
  | B7 `⁺`    |     1373 |            0 |          0 |                       31 |

* Sometimes the reserved characters are mapped (not in any obviously
  useful way).

### Practice: Characters in Use

The previous section reported on the character sets defined in the
translation table in the corpus.  This section reports on the
characters actually found in the corpus.

In practice, characters in the corpus are in [ISO-8859-1], with very
few exceptions.  The exceptions are a handful of files that either use
reserved characters from the portable character set, for unclear
reasons, or declare surprising encodings for bytes in the normal ASCII
range.  These exceptions might be file corruption; they do not appear
to be useful.

As a result, a portable file reader could reasonably ignore the
translation table and simply interpret all portable files as
[ISO-8859-1] or [windows-1252].

There is no visible distinction in practice between portable files in
"communication" versus "tape" format.  Neither kind contains control
characters.

[ISO-8859-1]: https://en.wikipedia.org/wiki/ISO/IEC_8859-1

Files in the corpus have a mix of CRLF and LF-only line ends.

## Tag String

The translation table is followed by an 8-byte tag string that
consists of the exact characters `SPSSPORT` in the portable file's
character set.  This can be used to verify that the file is indeed a
portable file.

> Since every file in the corpus is encoded in (extended) ASCII, this
> string always appears in ASCII too.

## Version and Date Info Record

This record does not have a tag code.  It has the following structure:

- A single character identifying the file format version.  It is
  always `A`.

- An 8-character string field giving the file creation date in the
  format YYYYMMDD.

- A 6-character string field giving the file creation time in the
  format HHMMSS.

> In the [corpus], there is some variation for file creation dates and
> times by product:
>
> - `STAT/TRANSFER` often writes dates that are invalid
>   (e.g. `20040931`) or obviously wrong (e.g. `19040823`, `19000607`).
>
> - `STAT/TRANSFER` often writes the time as all spaces.
>
> - `IBM SPSS Statistics 19.0` (and probably other versions) writes `HH`
>   as ` H` for single-digit hours.
>
> - `SPSS 6.1 for the Power Macintosh` writes invalid dates such as
>   `19:11010`.

## Identification Records

The product identification record has tag code `1`.  It consists of a
single string field giving the name of the product that wrote the
portable file.

The author identification record has tag code `2`.  It is optional and
usually omitted.  If present, it consists of a single string field
giving the name of the person who caused the portable file to be
written.

> The [corpus] contains a few different kinds of authors:
>
> - Organizational names, such as the names of companies or
>   universities or their departments.
>
> - Product names, such as `SPSS for HP-UX`.
>
> - Internet host names, such as `icpsr.umich.edu`.

The subproduct identification record has tag code `3`.  It is optional
and usually omitted.  If present, it consists of a single string field
giving additional information on the product that wrote the portable
file.

> The [corpus] contains a few different kinds of subproduct:
>
> - `x86_64-w64-mingw32` or another target triple (written by PSPP).
>
> - A file name for a `.sav` file.
>
> - `SPSS/PC+ Studentware+` written by `SPSS for MS WINDOWS Release 7.0`
>   in 1996.
>
> - `FILE BUILT VIA IMPORT` written by `SPSS RELEASE 4.1 FOR VAX/VMS` in
>   1998.
>
> - `SPSS/PC+` written by `SPSS for MS WINDOWS Release 7.0` in 1996.
>
> - Multiple instances of `SPSS/PC+` written by `SPSS/PC+ on IBM PC`,
>   but with several spaces padding out both product and subproduct
>   fields.
>
> - `PFF TEST FILE` written by `SPSS-X RELEASE 2.1 FOR IBM VM/CMS` in
>   1986.

## Variable Count Record

The variable count record has tag code `4`.  It consists of a single
integer field giving the number of variables in the file dictionary.

## Precision Record

The precision record has tag code `5`.  It consists of a single integer
field specifying the maximum number of base-30 digits used in data in
the file.

## Case Weight Variable Record

The case weight variable record is optional.  If it is present, it
indicates the variable used for weighting cases; if it is absent, cases
are unweighted.  It has tag code `6`.  It consists of a single string
field that names the weighting variable.

## Variable Records

Each variable record represents a single variable.  Variable records
have tag code `7`.  They have the following structure:

- Width (integer).  This is 0 for a numeric variable.  For portability
  to old versions of SPSS, it should be between 1 and 255 for a string
  variable.

  > Portable files in the [corpus] contain strings as wide as 32000
  bytes.  None of these was written by SPSS itself, but by a variety
  of third-party products: `STAT/TRANSFER`, `inquery export tool (c)
  inworks GmbH`, `QDATA Data Entry System for the IBM PC`.  The
  creation dates in the files range from 2016 to 2024.

- Name (string).  1-8 characters long.  Must be in all capitals.

  > A few portable files that contain duplicate variable names have
  been spotted in the wild.  PSPP handles these by renaming the
  duplicates with numeric extensions: `VAR001`, `VAR002`, and so on.

- Print format.  This is a set of three integer fields:

  - [Format type](system-file.md#format-types) encoded the same as in
    system files.

  - Format width.  1-40.

  - Number of decimal places.  1-40.

  > A few portable files with invalid format types or formats that are
  not of the appropriate width or decimals for their variables have
  been spotted in the wild.  PSPP assigns a default `F` or `A` format
  to a variable with an invalid format.

- Write format.  Same structure as the print format described above.

Each variable record can optionally be followed by a missing value
record, which has tag code `8`.  A missing value record has one field,
the missing value itself (a floating-point or string, as appropriate).
Up to three of these missing value records can be used.

There are also records for missing value ranges:

- Tag code `B` for `X THRU Y` ranges.  It is followed by two
  floating-point values representing `X` and `Y`.

- Tag code `9` for `LO THRU Y` ranges, followed by a floating-point
  number representing `Y`.

- Tag code `A` for `X THRU HI` ranges, followed by a floating-point
  number representing `X`.

If a missing value range is present, it may be followed by a single
missing value record.

In addition, each variable record can optionally be followed by a
variable label record, which has tag code `C`.  A variable label record
has one field, the variable label itself (string).

## Value Label Records

Value label records have tag code `D`.  They have the following format:

- Variable count (integer).

- List of variables (strings).  The variable count specifies the
  number in the list.  Variables are specified by their names.  All
  variables must be of the same type (numeric or string), but string
  variables do not necessarily have the same width.

- Label count (integer).

- List of (value, label) tuples.  The label count specifies the
  number of tuples.  Each tuple consists of a value, which is numeric
  or string as appropriate to the variables, followed by a label
  (string).

> The corpus contains a few portable files that specify duplicate
value labels, that is, two different labels for a single value of a
single variable.  PSPP uses the last value label specified in these
cases.

## Document Record

One document record may optionally follow the value label record.  The
document record consists of tag code `E`, following by the number of
document lines as an integer, followed by that number of strings, each
of which represents one document line.  Document lines must be 80 bytes
long or shorter.

## Portable File Data

The data record has tag code `F`.  There is only one tag for all the
data; thus, all the data must follow the dictionary.  The data is
terminated by the end-of-file marker `Z`, which is not valid as the
beginning of a data element.

Data elements are output in the same order as the variable records
describing them.  String variables are output as string fields, and
numeric variables are output as floating-point fields.

