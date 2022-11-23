GET FILE='nhtsa.sav'.
CTABLES
    /TABLE freqOfDriving
    /CATEGORIES VARIABLES=freqOfDriving [OTHERNM, SUBTOTAL='Valid Total',
					 MISSING, SUBTOTAL='Missing Total']
                                        TOTAL=YES LABEL='Overall Total'.
