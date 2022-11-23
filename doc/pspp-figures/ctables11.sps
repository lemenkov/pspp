GET FILE='nhtsa.sav'.
CTABLES /TABLE=ageGroup [COLPCT 'Gender %' PCT5.0,
                         ROWPCT 'Age Group %' PCT5.0]
               BY gender.
