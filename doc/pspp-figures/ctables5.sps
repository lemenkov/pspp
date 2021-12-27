GET FILE='nhtsa.sav'.
CTABLES /TABLE qn26 + qn27 > qns3a.
CTABLES /TABLE (qn26 + qn27) > qns3a.
