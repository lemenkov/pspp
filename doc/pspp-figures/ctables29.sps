GET FILE='nhtsa.sav'.
CTABLES
    /TABLE qn1
    /CATEGORIES VARIABLES=qn1 [OTHERNM, SUBTOTAL='Valid Total',
                               MISSING, SUBTOTAL='Missing Total']
			      TOTAL=YES LABEL='Overall Total'.
