GET FILE='nhtsa.sav'.
CTABLES
    /TABLE=qnd1 [MEAN, MEDIAN] BY qns3a
    /TABLE=AgeGroup [COLPCT, ROWPCT] BY qns3a.
