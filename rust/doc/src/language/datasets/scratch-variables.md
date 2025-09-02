# Scratch Variables

Most of the time, variables don't retain their values between cases.
Instead, either they're being read from a data file or the active
dataset, in which case they assume the value read, or, if created with
`COMPUTE` or another transformation, they're initialized to the
system-missing value or to blanks, depending on type.

   However, sometimes it's useful to have a variable that keeps its
value between cases.  You can do this with
[`LEAVE`](../../commands/leave.md), or you can use a "scratch
variable".  Scratch variables are variables whose names begin with an
octothorpe (`#`).

   Scratch variables have the same properties as variables left with
`LEAVE`: they retain their values between cases, and for the first
case they are initialized to 0 or blanks.  They have the additional
property that they are deleted before the execution of any procedure.
For this reason, scratch variables can't be used for analysis.  To use
a scratch variable in an analysis, use
[`COMPUTE`](../../commands/compute.md) to copy its value into an
ordinary variable, then use that ordinary variable in the analysis.

