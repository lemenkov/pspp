GET FILE='nhtsa.sav'.
CTABLES /TABLE=(ageGroup + membersOver16)[COLPCT] BY gender.
