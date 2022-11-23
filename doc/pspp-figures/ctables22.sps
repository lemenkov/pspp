GET FILE='nhtsa.sav'.
VARIABLE LABELS
   hasBeenPassengerOfDesignatedDriver 'desPas'
   hasBeenPassengerOfDrunkDriver 'druPas'
   isLicensedDriver 'licensed'
   hasHostedEventWithAlcohol 'hostAlc'
   hasBeenDesignatedDriver 'desDrv'.
CTABLES
    /TABLE hasBeenPassengerOfDesignatedDriver > hasBeenPassengerOfDrunkDriver
        BY isLicensedDriver > hasHostedEventWithAlcohol + hasBeenDesignatedDriver
	BY gender[TABLEID, LAYERID, SUBTABLEID]
    /SLABELS POSITION=ROW.
