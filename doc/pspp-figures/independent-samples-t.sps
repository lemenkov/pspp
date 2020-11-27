get file='physiology.sav'.

select if (height >= 200).

t-test /variables = height
       /groups = sex(0,1).
