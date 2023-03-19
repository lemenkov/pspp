# PSPP Language Tutorial

PSPP is a tool for the statistical analysis of sampled data.  You can
use it to discover patterns in the data, to explain differences in one
subset of data in terms of another subset and to find out whether
certain beliefs about the data are justified.  This chapter does not
attempt to introduce the theory behind the statistical analysis, but it
shows how such analysis can be performed using PSPP.

This tutorial assumes that you are using PSPP in its interactive mode
from the command line.  However, the example commands can also be
typed into a file and executed in a post-hoc mode by typing `pspp
FILE-NAME` at a shell prompt, where `FILE-NAME` is the name of the
file containing the commands.  Alternatively, from the graphical
interface, you can select File → New → Syntax to open a new syntax
window and use the Run menu when a syntax fragment is ready to be
executed.  Whichever method you choose, the syntax is identical.

When using the interactive method, PSPP tells you that it's waiting
for your data with a string like `PSPP>` or `data>`.  In the examples
of this chapter, whenever you see text like this, it indicates the
prompt displayed by PSPP, _not_ something that you should type.

Throughout this chapter reference is made to a number of sample data
files.  So that you can try the examples for yourself, you should have
received these files along with your copy of PSPP.[^1]

> Normally these files are installed in the directory
`/usr/local/share/pspp/examples`.  If however your system
administrator or operating system vendor has chosen to install them in
a different location, you will have to adjust the examples
accordingly.

[^1]: These files contain purely fictitious data.  They should not be
used for research purposes.

