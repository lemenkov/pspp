get file='repairs.sav'.
examine mtbf
 /statistics=descriptives.
compute mtbf_ln = ln (mtbf).
examine mtbf_ln
 /statistics=descriptives.
