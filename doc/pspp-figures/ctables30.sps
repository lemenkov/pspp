GET FILE='nhtsa.sav'.
CTABLES /TABLE=monthDaysMin1drink [MEAN F8.1, COUNT, VALIDN] > region
    /CATEGORIES VARIABLES=region TOTAL=YES LABEL='All regions'.
