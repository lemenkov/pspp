# Forming Commands

Most PSPP commands share a common structure.  A command begins with a
command name, such as `FREQUENCIES`, `DATA LIST`, or `N OF CASES`.  The
command name may be abbreviated to its first word, and each word in the
command name may be abbreviated to its first three or more characters,
where these abbreviations are unambiguous.

The command name may be followed by one or more "subcommands".  Each
subcommand begins with a subcommand name, which may be abbreviated to
its first three letters.  Some subcommands accept a series of one or
more specifications, which follow the subcommand name, optionally
separated from it by an equals sign (`=`).  Specifications may be
separated from each other by commas or spaces.  Each subcommand must
be separated from the next (if any) by a forward slash (`/`).

There are multiple ways to mark the end of a command.  The most common
way is to end the last line of the command with a period (`.`) as
described in the previous section.  A blank line, or one that consists
only of white space or comments, also ends a command.

