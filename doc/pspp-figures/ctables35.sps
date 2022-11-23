GET FILE='nhtsa.sav'.
CTABLES
    /PCOMPUTE &all_drivers=EXPR([1 THRU 2] + [3 THRU 4])
    /PPROPERTIES &all_drivers LABEL='All Drivers'
    /PCOMPUTE &pct_never=EXPR([5] / ([1 THRU 2] + [3 THRU 4] + [5]) * 100)
    /PPROPERTIES &pct_never LABEL='% Not Drivers' FORMAT=COUNT PCT40.1
    /TABLE=freqOfDriving BY gender
    /CATEGORIES VARIABLES=freqOfDriving [1 THRU 2,SUBTOTAL='Frequent Drivers',
					 3 THRU 4, SUBTOTAL='Infrequent Drivers',
					 &all_drivers, 5, &pct_never,
					 MISSING, SUBTOTAL='Not Drivers or Missing'].
