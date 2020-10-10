get file='personnel.sav'.

* Correct a typing error in the original file.
do if occupation = "Scrientist".
 compute occupation = "Scientist".
end if.

autorecode
	variables = occupation into occ
	/blank = missing.

* Delete the old variable.
delete variables occupation.

* Rename the new variable to the old variable's name.
rename variables (occ = occupation).

* Inspect the new variable.
display dictionary /variables=occupation.

