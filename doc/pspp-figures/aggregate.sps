GET FILE="personnel.sav".
AGGREGATE OUTFILE=* MODE=REPLACE
        /BREAK=occupation
        /occ_mean_salary=MEAN(salary)
        /occ_median_salary=MEDIAN(salary)
        /occ_std_dev_salary=SD(salary).
LIST.
