GET FILE='nhtsa.sav'.
CTABLES /TABLE=AgeGroup [COLPCT 'Gender %' PCT5.0,
                         ROWPCT 'Age Group %' PCT5.0]
               BY qns3a.
