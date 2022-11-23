GET FILE='nhtsa.sav'.
CTABLES /TABLE freqOfDriving.
CTABLES /TABLE freqOfDriving /CATEGORIES VARIABLES=freqOfDriving [1, 2, 3].
