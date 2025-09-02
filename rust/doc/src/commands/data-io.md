# Data Input and Output

Data are the focus of the PSPP language.  Each datum belongs to a “case”
(also called an “observation”).  Each case represents an individual or
"experimental unit".  For example, in the results of a survey, the names
of the respondents, their sex, age, etc. and their responses are all
data and the data pertaining to single respondent is a case.  This
chapter examines the PSPP commands for defining variables and reading
and writing data.  There are alternative commands to read data from
predefined sources such as system files or databases.

> These commands tell PSPP how to read data, but the data will
not actually be read until a procedure is executed.
