get file='hotel.sav'.
* recode negatively worded questions.
compute v3 = 6 - v3.
compute v5 = 6 - v5.
reliability variables=v1, v3, v4.
