get file='physiology.sav'.

select if (weight > 0).

t-test testval = 76.8
	/variables = weight.
