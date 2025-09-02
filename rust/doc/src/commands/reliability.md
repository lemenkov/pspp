# RELIABILITY

```
RELIABILITY
        /VARIABLES=VAR_LIST
        /SCALE (NAME) = {VAR_LIST, ALL}
        /MODEL={ALPHA, SPLIT[(N)]}
        /SUMMARY={TOTAL,ALL}
        /MISSING={EXCLUDE,INCLUDE}
```

The `RELIABILITY` command performs reliability analysis on the data.

The `VARIABLES` subcommand is required.  It determines the set of
variables upon which analysis is to be performed.

The `SCALE` subcommand determines the variables for which reliability
is to be calculated.  If `SCALE` is omitted, then analysis for all
variables named in the `VARIABLES` subcommand are used.  Optionally, the
NAME parameter may be specified to set a string name for the scale.

The `MODEL` subcommand determines the type of analysis.  If `ALPHA`
is specified, then Cronbach's Alpha is calculated for the scale.  If the
model is `SPLIT`, then the variables are divided into 2 subsets.  An
optional parameter `N` may be given, to specify how many variables to be
in the first subset.  If `N` is omitted, then it defaults to one half of
the variables in the scale, or one half minus one if there are an odd
number of variables.  The default model is `ALPHA`.

By default, any cases with user missing, or system missing values for
any variables given in the `VARIABLES` subcommand are omitted from the
analysis.  The `MISSING` subcommand determines whether user missing
values are included or excluded in the analysis.

The `SUMMARY` subcommand determines the type of summary analysis to
be performed.  Currently there is only one type: `SUMMARY=TOTAL`, which
displays per-item analysis tested against the totals.

## Example

Before analysing the results of a survey—particularly for a multiple
choice survey—it is desirable to know whether the respondents have
considered their answers or simply provided random answers.

In the following example the survey results from the file `hotel.sav`
are used.  All five survey questions are included in the reliability
analysis.  However, before running the analysis, the data must be
preprocessed.  An examination of the survey questions reveals that two
questions, viz: v3 and v5 are negatively worded, whereas the others
are positively worded.  All questions must be based upon the same
scale for the analysis to be meaningful.  One could use the
[`RECODE`](recode.md) command, however a simpler way is to use
[`COMPUTE`](compute.md) and this is what is done in the syntax below.

```
get file="hotel.sav".

* Recode V3 and V5 inverting the sense of the values.
compute v3 = 6 - v3.
compute v5 = 6 - v5.

reliability
   /variables= all
   /model=alpha.
```

In this case, all variables in the data set are used, so we can use
the special keyword `ALL`.

The output, below, shows that Cronbach's Alpha is 0.11 which is a
value normally considered too low to indicate consistency within the
data.  This is possibly due to the small number of survey questions.
The survey should be redesigned before serious use of the results are
applied.

```
Scale: ANY

Case Processing Summary
┌────────┬──┬───────┐
│Cases   │ N│Percent│
├────────┼──┼───────┤
│Valid   │17│ 100.0%│
│Excluded│ 0│    .0%│
│Total   │17│ 100.0%│
└────────┴──┴───────┘

    Reliability Statistics
┌────────────────┬──────────┐
│Cronbach's Alpha│N of Items│
├────────────────┼──────────┤
│             .11│         5│
└────────────────┴──────────┘
```
