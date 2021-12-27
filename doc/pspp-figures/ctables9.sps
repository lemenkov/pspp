GET FILE='nhtsa.sav'.
CTABLES
    /TABLE qn20 BY qns3a
    /TABLE qn20 [C] BY qns3a.
