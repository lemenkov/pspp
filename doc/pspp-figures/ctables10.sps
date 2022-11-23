GET FILE='nhtsa.sav'.
CTABLES
    /TABLE=age [MEAN, MEDIAN] BY gender
    /TABLE=ageGroup [COLPCT, ROWPCT] BY gender.
