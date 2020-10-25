get file='physiology.sav'.
recode height (179 = SYSMIS).
t-test group=sex(0,1) /variables = height temperature.
