# Syntax Variants

There are three variants of command syntax, which vary only in how they
detect the end of one command and the start of the next.

   In "interactive mode", which is the default for syntax typed at a
command prompt, a period as the last non-blank character on a line ends
a command.  A blank line also ends a command.

   In "batch mode", an end-of-line period or a blank line also ends a
command.  Additionally, it treats any line that has a non-blank
character in the leftmost column as beginning a new command.  Thus, in
batch mode the second and subsequent lines in a command must be
indented.

   Regardless of the syntax mode, a plus sign, minus sign, or period in
the leftmost column of a line is ignored and causes that line to begin a
new command.  This is most useful in batch mode, in which the first line
of a new command could not otherwise be indented, but it is accepted
regardless of syntax mode.

   The default mode for reading commands from a file is "auto mode".  It
is the same as batch mode, except that a line with a non-blank in the
leftmost column only starts a new command if that line begins with the
name of a PSPP command.  This correctly interprets most valid PSPP
syntax files regardless of the syntax mode for which they are intended.

   The `--interactive` (or `-i`) or `--batch` (or `-b`) options set the
syntax mode for files listed on the PSPP command line.

