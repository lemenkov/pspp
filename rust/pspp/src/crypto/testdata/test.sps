* Encoding: windows-1252.
DATA LIST LIST /name (a25) quantity (f8).
BEGIN DATA.
widgets 10345
oojars 2345
dubreys 98
thingumies 518
END DATA.
 
LIST.
 
DESCRIPTIVES /quantity
 /statistics ALL.