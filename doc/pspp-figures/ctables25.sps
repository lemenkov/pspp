GET FILE='nhtsa.sav'.
CTABLES /TABLE (likelihoodOfBeingStoppedByPolice + likelihoodOfHavingAnAccident) [COLPCT].
CTABLES /TABLE (likelihoodOfBeingStoppedByPolice + likelihoodOfHavingAnAccident) [ROWPCT]
  /CLABELS ROW=OPPOSITE.
