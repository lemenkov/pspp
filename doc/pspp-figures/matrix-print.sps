SET MDISPLAY=TABLES.
MATRIX.
COMPUTE m={1, 2, 3; 4, 5, 6; 7, 8, 9}.
COMPUTE rlabels={"a", "b", "c"}.
COMPUTE clabels={"x", "y", "z"}.
PRINT m/RNAMES=rlabels/CNAMES=clabels.
END MATRIX.
