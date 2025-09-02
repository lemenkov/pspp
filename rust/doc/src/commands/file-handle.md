# FILE HANDLE

## Syntax Overview

For text files:

```
FILE HANDLE HANDLE_NAME
        /NAME='FILE_NAME
        [/MODE=CHARACTER]
        [/ENDS={CR,CRLF}]
        /TABWIDTH=TAB_WIDTH
        [ENCODING='ENCODING']
```

For binary files in native encoding with fixed-length records:
```
FILE HANDLE HANDLE_NAME
        /NAME='FILE_NAME'
        /MODE=IMAGE
        [/LRECL=REC_LEN]
        [ENCODING='ENCODING']
```

For binary files in native encoding with variable-length records:
```
FILE HANDLE HANDLE_NAME
        /NAME='FILE_NAME'
        /MODE=BINARY
        [/LRECL=REC_LEN]
        [ENCODING='ENCODING']
```

For binary files encoded in EBCDIC:
```
FILE HANDLE HANDLE_NAME
        /NAME='FILE_NAME'
        /MODE=360
        /RECFORM={FIXED,VARIABLE,SPANNED}
        [/LRECL=REC_LEN]
        [ENCODING='ENCODING']
```

## Details

   Use `FILE HANDLE` to associate a file handle name with a file and its
attributes, so that later commands can refer to the file by its handle
name.  Names of text files can be specified directly on commands that
access files, so that `FILE HANDLE` is only needed when a file is not an
ordinary file containing lines of text.  However, `FILE HANDLE` may be
used even for text files, and it may be easier to specify a file's name
once and later refer to it by an abstract handle.

Specify the file handle name as the identifier immediately following
the `FILE HANDLE` command name.  The identifier `INLINE` is reserved
for representing data embedded in the syntax file (see [BEGIN
DATA](begin-data.md)). The file handle name must not already have been
used in a previous invocation of `FILE HANDLE`, unless it has been
closed with [`CLOSE FILE HANDLE`](close-file-handle.md).

The effect and syntax of `FILE HANDLE` depends on the selected `MODE`:

   - In `CHARACTER` mode, the default, the data file is read as a text
     file.  Each text line is read as one record.

     In `CHARACTER` mode only, tabs are expanded to spaces by input
     programs, except by `DATA LIST FREE` with explicitly specified
     delimiters.  Each tab is 4 characters wide by default, but `TABWIDTH`
     (a PSPP extension) may be used to specify an alternate width.  Use
     a `TABWIDTH` of 0 to suppress tab expansion.

     A file written in `CHARACTER` mode by default uses the line ends of
     the system on which PSPP is running, that is, on Windows, the
     default is CR LF line ends, and on other systems the default is LF
     only.  Specify `ENDS` as `CR` or `CRLF` to override the default.  PSPP
     reads files using either convention on any kind of system,
     regardless of `ENDS`.

   - In `IMAGE` mode, the data file is treated as a series of fixed-length
     binary records.  `LRECL` should be used to specify the record length
     in bytes, with a default of 1024.  On input, it is an error if an
     `IMAGE` file's length is not a integer multiple of the record length.
     On output, each record is padded with spaces or truncated, if
     necessary, to make it exactly the correct length.

   - In `BINARY` mode, the data file is treated as a series of
     variable-length binary records.  `LRECL` may be specified, but
     its value is ignored.  The data for each record is both preceded
     and followed by a 32-bit signed integer in little-endian byte
     order that specifies the length of the record.  (This redundancy
     permits records in these files to be efficiently read in reverse
     order, although PSPP always reads them in forward order.)  The
     length does not include either integer.

   - Mode `360` reads and writes files in formats first used for tapes
     in the 1960s on IBM mainframe operating systems and still
     supported today by the modern successors of those operating
     systems.  For more information, see `OS/400 Tape and Diskette
     Device Programming`, available on IBM's website.

     Alphanumeric data in mode `360` files are encoded in EBCDIC. PSPP
     translates EBCDIC to or from the host's native format as necessary
     on input or output, using an ASCII/EBCDIC translation that is
     one-to-one, so that a "round trip" from ASCII to EBCDIC back to
     ASCII, or vice versa, always yields exactly the original data.

     The `RECFORM` subcommand is required in mode `360`.  The precise
     file format depends on its setting:

     * `F`  
       `FIXED`  
       This record format is equivalent to `IMAGE` mode, except for
       EBCDIC translation.

       IBM documentation calls this `*F` (fixed-length, deblocked)
       format.

     * `V`  
       `VARIABLE`  
       The file comprises a sequence of zero or more variable-length
       blocks.  Each block begins with a 4-byte "block descriptor
       word" (BDW). The first two bytes of the BDW are an unsigned
       integer in big-endian byte order that specifies the length of
       the block, including the BDW itself.  The other two bytes of
       the BDW are ignored on input and written as zeros on output.

       Following the BDW, the remainder of each block is a sequence
       of one or more variable-length records, each of which in turn
       begins with a 4-byte "record descriptor word" (RDW) that has
       the same format as the BDW. Following the RDW, the remainder
       of each record is the record data.

       The maximum length of a record in `VARIABLE` mode is 65,527
       bytes: 65,535 bytes (the maximum value of a 16-bit unsigned
       integer), minus 4 bytes for the BDW, minus 4 bytes for the
       RDW.

       In mode `VARIABLE`, `LRECL` specifies a maximum, not a fixed,
       record length, in bytes.  The default is 8,192.

       IBM documentation calls this `*VB` (variable-length, blocked,
       unspanned) format.

     * `VS`  
        `SPANNED`  
       This format is like `VARIABLE`, except that logical records may
       be split among multiple physical records (called "segments") or
       blocks.  In `SPANNED` mode, the third byte of each RDW is
       called the segment control character (SCC). Odd SCC values
       cause the segment to be appended to a record buffer maintained
       in memory; even values also append the segment and then flush
       its contents to the input procedure.  Canonically, SCC value 0
       designates a record not spanned among multiple segments, and
       values 1 through 3 designate the first segment, the last
       segment, or an intermediate segment, respectively, within a
       multi-segment record.  The record buffer is also flushed at end
       of file regardless of the final record's SCC.

       The maximum length of a logical record in `VARIABLE` mode is
       limited only by memory available to PSPP.  Segments are
       limited to 65,527 bytes, as in `VARIABLE` mode.

       This format is similar to what IBM documentation call `*VS`
       (variable-length, deblocked, spanned) format.

     In mode `360`, fields of type `A` that extend beyond the end of a
     record read from disk are padded with spaces in the host's native
     character set, which are then translated from EBCDIC to the
     native character set.  Thus, when the host's native character set
     is based on ASCII, these fields are effectively padded with
     character `X'80'`.  This wart is implemented for compatibility.

   The `NAME` subcommand specifies the name of the file associated with
the handle.  It is required in all modes but `SCRATCH` mode, in which its
use is forbidden.

   The `ENCODING` subcommand specifies the encoding of text in the
file.  For reading text files in `CHARACTER` mode, all of the forms
described for `ENCODING` on the [`INSERT`](insert.md) command are
supported.  For reading in other file-based modes, encoding
autodetection is not supported; if the specified encoding requests
autodetection then the default encoding is used.  This is also true
when a file handle is used for writing a file in any mode.

