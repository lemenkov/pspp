# Files Used by PSPP

PSPP makes use of many files each time it runs.  Some of these it
reads, some it writes, some it creates.  Here is a table listing the
most important of these files:

* command file  
  syntax file  
  These names (synonyms) refer to the file that contains instructions
  that tell PSPP what to do.  The syntax file's name is specified on
  the PSPP command line.  Syntax files can also be read with
  [`INCLUDE`](../../commands/include.md) or
  [`INSERT`](../../commands/insert.md).

* data file  
  Data files contain raw data in text or binary format.  Data can
  also be embedded in a syntax file with `BEGIN DATA` and `END DATA`.

* listing file  
  One or more output files are created by PSPP each time it is run.
  The output files receive the tables and charts produced by
  statistical procedures.  The output files may be in any number of
  formats, depending on how PSPP is configured.

* system file  
  System files are binary files that store a dictionary and a set of
  cases.  `GET` and `SAVE` read and write system files.

* portable file  
  Portable files are files in a text-based format that store a
  dictionary and a set of cases.  `IMPORT` and `EXPORT` read and
  write portable files.

