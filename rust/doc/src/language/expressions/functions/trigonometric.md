# Trigonometric Functions

Trigonometric functions take numeric arguments and produce numeric
results.

* `ARCOS(X)`  
  `ACOS(X)`  
  Takes the arccosine, in radians, of `X`.  Results in
  system-missing if `X` is not between -1 and 1 inclusive.  This
  function is a PSPP extension.

* `ARSIN(X)`  
  `ASIN(X)`  
  Takes the arcsine, in radians, of `X`.  Results in
  system-missing if `X` is not between -1 and 1 inclusive.

* `ARTAN(X)`  
  `ATAN(X)`  
  Takes the arctangent, in radians, of `X`.

* `COS(ANGLE)`  
  Takes the cosine of `ANGLE` which should be in radians.

* `SIN(ANGLE)`  
  Takes the sine of `ANGLE` which should be in radians.

* `TAN(ANGLE)`  
  Takes the tangent of `ANGLE` which should be in radians.  Results in
  system-missing at values of ANGLE that are too close to odd
  multiples of π/2.

