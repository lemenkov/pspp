# String Functions

String functions take various arguments and return various results.

* `CONCAT(STRING, STRING[, ...])`  
  Returns a string consisting of each `STRING` in sequence.
  `CONCAT("abc", "def", "ghi")` has a value of `"abcdefghi"`.  The
  resultant string is truncated to a maximum of 32767 bytes.

* `INDEX(HAYSTACK, NEEDLE)`  
  `RINDEX(HAYSTACK, NEEDLE)`  
  Returns a positive integer indicating the position of the first
  (for `INDEX`) or last (for `RINDEX`) occurrence of `NEEDLE` in
  HAYSTACK.  Returns 0 if HAYSTACK does not contain `NEEDLE`.  Returns
  1 if `NEEDLE` is the empty string.

* `INDEX(HAYSTACK, NEEDLES, NEEDLE_LEN)`  
  `RINDEX(HAYSTACK, NEEDLE, NEEDLE_LEN)`  
  Divides `NEEDLES` into multiple needles, each with length
  `NEEDLE_LEN`, which must be a positive integer that evenly divides
  the length of `NEEDLES`.  Searches `HAYSTACK` for the occurrences of
  each needle and returns a positive integer indicating the byte index
  of the beginning of the first (for `INDEX`) or last (for `RINDEX`)
  needle it finds.  Returns 0 if `HAYSTACK` does not contain any of
  the needles, or if `NEEDLES` is the empty string.

* `LENGTH(STRING)`  
  Returns the number of bytes in `STRING`.

* `LOWER(STRING)`  
  Returns a string identical to `STRING` except that all uppercase
  letters are changed to lowercase letters.  The definitions of
  "uppercase" and "lowercase" are encoding-dependent.

* `LPAD(STRING, LENGTH[, PADDING])`  
  `RPAD(STRING, LENGTH[, PADDING])`  
  If `STRING` is at least `LENGTH` bytes long, these functions return
  `STRING` unchanged.  Otherwise, they return `STRING` padded with
  `PADDING` on the left side (for `LPAD`) or right side (for `RPAD`)
  to `LENGTH` bytes.  These functions report an error and return
  `STRING` unchanged if `LENGTH` is missing or bigger than 32767.

  The `PADDING` argument must not be an empty string and defaults to a
  space if not specified.  If its length does not evenly fit the
  amount of space needed for padding, the returned string will be
  shorter than `LENGTH`.

* `LTRIM(STRING[, PADDING])`  
  `RTRIM(STRING[, PADDING])`  
  These functions return `STRING`, after removing leading (for `LTRIM`)
  or trailing (for `RTRIM`) copies of `PADDING`.  If `PADDING` is
  omitted, these functions remove spaces (but not tabs or other white
  space).  These functions return `STRING` unchanged if `PADDING` is the
  empty string.

* `NUMBER(STRING, FORMAT)`  
  Returns the number produced when `STRING` is interpreted according
  to format specifier `FORMAT`.  If the format width `W` is less than
  the length of `STRING`, then only the first `W` bytes in `STRING`
  are used, e.g. `NUMBER("123", F3.0)` and `NUMBER("1234", F3.0)` both
  have value 123.  If `W` is greater than `STRING`'s length, then it
  is treated as if it were right-padded with spaces.  If `STRING` is
  not in the correct format for `FORMAT`, system-missing is returned.

* `REPLACE(HAYSTACK, NEEDLE, REPLACEMENT[, N])`  
  Returns string `HAYSTACK` with instances of `NEEDLE` replaced by
  `REPLACEMENT`.  If nonnegative integer `N` is specified, it limits
  the maximum number of replacements; otherwise, all instances of
  `NEEDLE` are replaced.

* `STRING(NUMBER, FORMAT)`  
  Returns a string corresponding to `NUMBER` in the format given by
  format specifier `FORMAT`.  For example, `STRING(123.56, F5.1)` has
  the value `"123.6"`.

* `STRUNC(STRING, N)`  
  Returns `STRING`, first trimming it to at most `N` bytes, then
  removing trailing spaces (but not tabs or other white space).
  Returns an empty string if `N` is zero or negative, or `STRING`
  unchanged if `N` is missing.

* `SUBSTR(STRING, START)`  
  Returns a string consisting of the value of `STRING` from position
  `START` onward.  Returns an empty string if `START` is system-missing,
  less than 1, or greater than the length of `STRING`.

* `SUBSTR(STRING, START, COUNT)`  
  Returns a string consisting of the first `COUNT` bytes from `STRING`
  beginning at position `START`.  Returns an empty string if `START`
  or `COUNT` is system-missing, if `START` is less than 1 or greater
  than the number of bytes in `STRING`, or if `COUNT` is less than 1.
  Returns a string shorter than `COUNT` bytes if `START` + `COUNT` - 1
  is greater than the number of bytes in `STRING`.  Examples:
  `SUBSTR("abcdefg", 3, 2)` has value `"cd"`; `SUBSTR("nonsense", 4,
  10)` has the value `"sense"`.

* `UPCASE(STRING)`  
  Returns `STRING`, changing lowercase letters to uppercase letters.

