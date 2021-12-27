* This syntax reads 'nhtsa-drinking-2008.sav', which is the exact file
  obtained from data.gov and adjusts the dictionary to better reflect
  the data's real measurement levels, formats, missing values, and
  other metadata, and saves it as 'nhtsa.sav'.

GET 'nhtsa-drinking-2008.sav'.
VARIABLE LEVEL
   ALL (NOMINAL)
   qns1 qn1 qn15 qn49 qn87 qn103 qn105ba TO qn105bd qn122c qn139a TO qn139n qn139ca TO qn139cn qn140aa TO qn140af qnd8 qnd11 (ORDINAL)
   id qn18 qn19a qn20 qn23 qn31 qn35 qn36 qn38 qn41 qn44 qn52 qn65 qn66 qn114 qn121 qn126 qnd1 qnd1b qnd9 (SCALE).
FORMATS
    state qns1 qn1 (F2.0)
    qn100 qn102 qn103 qn116 qn123 qn131a qn133 qn139a qn139e qn139g qn139h (F1.0).
MISSING VALUES
    qns1 (97, 98, 99)
    qn1 (6, 7)
    qn15 (8, 9)
    qn17 (2, 3)
    qn18 (98, 99)
    qn19a (97, 98, 99)
    qn20 qn23 (98, 99)
    qn26 qn27 qn28 qn29 (3, 4)
    qn31 (98, 99)
    qn33 (3, 4)
    qn35 (998, 999)
    qn36 (98, 99)
    qn37 (2, 3)
    qn38 (998, 999)
    qn39h (0)
    qn39m (0)
    qn41 (998, 999)
    qn43a (3, 4)
    qn44 (98, 99)
    qn44a (98, 99)
    qn49 (5, 6)
    qn52 (998, 999)
    qn56 (2, 3)
    qn57 (3, 4)
    qn61 (3, 4)
    qn64b (3, 4)
    qn65 (98, 99)
    qn65a (3, 4)
    qn66 (98, 99)
    qn86 (3, 4)
    qn87 (6, 7)
    qn88_1 qn88_2 qn88_3 (2, 3)
    qn89 qn90 qn90a (3, 4)
    qn91_1 qn91_2 qn91_3 (2, 3)
    qn96a (3, 4)
    qn100 (3, 4)
    qn101 (2, 3)
    qn102 qn102b qn102c (3, 4)
    qn103 (4, 5)
    qn105ba qn105bb qn105bc qn105bd (6, 7)
    qn113 (3, 4)
    qn114 (98, 99)
    qn116 (7, 8)
    qn120 (3, 4)
    qn121 (998, 999)
    qn122c (6, 7)
    qn123 (3, 4)
    qn126 (98, 99)
    qn131a (3, 4)
    qn132a (3, 4)
    qn133 (3, 4)
    qn139a qn139e qn139g qn139h qn139k qn139l qn139m qn139n (6, 7)
    qn139_a (3, 4)
    qn139_b (98, 99)
    qn139ca qn139ce qn139cf qn139cg qn139ch qn139ck qn139cl qn139cn (6, 7)
    qn140aa qn140ab qn140ac qn140ad qn140ae qn140af (6, 7)
    qnd1 (998, 999)
    qnd1b (8, 9)
    qnd2_1 qnd2_2 qnd2_3 (2, 3)
    qnd3 (10, 11)
    qnd5 (3, 4)
    qnd5a (9, 10)
    qnd6_1 qnd6_2 qnd6_3 qnd6_4 qnd6_5 (2, 3)
    qnd7a (3, 4)
    qnd8 (8, 9)
    qnd9 (997, 998, 999)
    qnd11a (3, 4)
    qnd11 (6, 7).
RECODE qnd1 (LO THRU 15=1)
            (16 THRU 25=2)
            (26 THRU 35=3)
            (36 THRU 45=4)
            (46 THRU 55=5)
            (56 THRU 65=6)
            (66 THRU HI=7)
       INTO agegroup.
VAR LEVEL agegroup (ORDINAL).
VARIABLE LABEL agegroup 'Age group'.
VALUE LABELS
    /agegroup
     1 '15 or younger'
     2 '16 to 25'
     3 '26 to 35'
     4 '36 to 45'
     5 '46 to 55'
     6 '56 to 65'
     7 '66 or older'.
SAVE OUTFILE='nhtsa.sav'.
