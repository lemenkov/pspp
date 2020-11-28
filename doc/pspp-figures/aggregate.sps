get file="personnel.sav".

aggregate outfile=* 
        mode = replace
        /break= occupation
        /occ_mean_salaray = mean (salary)
        /occ_median_salary = median (salary)
        /occ_std_dev_salary = sd (salary).

list.
