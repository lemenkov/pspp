# File Handles

A "file handle" is a reference to a data file, system file, or portable
file.  Most often, a file handle is specified as the name of a file as a
string, that is, enclosed within `'` or `"`.

   A file name string that begins or ends with `|` is treated as the
name of a command to pipe data to or from.  You can use this feature to
read data over the network using a program such as `curl` (e.g. `GET
'|curl -s -S http://example.com/mydata.sav'`), to read compressed data
from a file using a program such as `zcat` (e.g. `GET '|zcat
mydata.sav.gz'`), and for many other purposes.

   PSPP also supports declaring named file handles with the [`FILE
HANDLE`](../../commands/data-io/file-handle.md) command.  This command
associates an identifier of your choice (the file handle's name) with
a file.  Later, the file handle name can be substituted for the name
of the file.  When PSPP syntax accesses a file multiple times,
declaring a named file handle simplifies updating the syntax later to
use a different file.  Use of `FILE HANDLE` is also required to read
data files in binary formats.

   In some circumstances, PSPP must distinguish whether a file handle
refers to a system file or a portable file.  When this is necessary to
read a file, e.g. as an input file for `GET` or `MATCH FILES`, PSPP uses
the file's contents to decide.  In the context of writing a file, e.g.
as an output file for `SAVE` or `AGGREGATE`, PSPP decides based on the
file's name: if it ends in `.por` (with any capitalization), then PSPP
writes a portable file; otherwise, PSPP writes a system file.

   `INLINE` is reserved as a file handle name.  It refers to the "data
file" embedded into the syntax file between [`BEGIN DATA` and `END
DATA`](../../commands/data-io/begin-data.md).

   The file to which a file handle refers may be reassigned on a later
`FILE HANDLE` command if it is first closed using [`CLOSE FILE
HANDLE`](../../commands/data-io/close-file-handle.md).


