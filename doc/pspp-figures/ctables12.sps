GET FILE='nhtsa.sav'.
CTABLES /TABLE=(AgeGroup + qns1)[COLPCT] BY qns3a.
