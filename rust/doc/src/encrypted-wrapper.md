# Encrypted File Wrappers

SPSS 21 and later can package multiple kinds of files inside an
encrypted wrapper.  The wrapper has a common format, regardless of the
kind of the file that it contains.

> ⚠️ Warning: The SPSS encryption wrapper is poorly designed.  When the
password is unknown, it is much cheaper and faster to decrypt a file
encrypted this way than if a well designed alternative were used.  If
you must use this format, use a 10-byte randomly generated password.

## Common Wrapper Format

An encrypted file wrapper begins with the following 36-byte header,
where `xxx` identifies the type of file encapsulated: `SAV` for a system
file, `SPS` for a syntax file, `SPV` for a viewer file.  PSPP code for
identifying these files just checks for the `ENCRYPTED` keyword at
offset 8, but the other bytes are also fixed in practice:

```
0000  1c 00 00 00 00 00 00 00  45 4e 43 52 59 50 54 45  |........ENCRYPTE|
0010  44 xx xx xx 15 00 00 00  00 00 00 00 00 00 00 00  |Dxxx............|
0020  00 00 00 00                                       |....|
```

Following the fixed header is essentially the regular contents of the
encapsulated file in its usual format, with each 16-byte block
encrypted with AES-256 in ECB mode.

To make the plaintext an even multiple of 16 bytes in length, the
encryption process appends PKCS #7 padding, as specified in RFC 5652
section 6.3.  Padding appends 1 to 16 bytes to the plaintext, in which
each byte of padding is the number of padding bytes added.  If the
plaintext is, for example, 2 bytes short of a multiple of 16, the
padding is 2 bytes with value 02; if the plaintext is a multiple of 16
bytes in length, the padding is 16 bytes with value 0x10.

The AES-256 key is derived from a password in the following way:

1. Start from the literal password typed by the user.  Truncate it to
   at most 10 bytes, then append as many null bytes as necessary until
   there are exactly 32 bytes.  Call this `password`.

2. Let `constant` be the following 73-byte constant:

   ```
   0000  00 00 00 01 35 27 13 cc  53 a7 78 89 87 53 22 11
   0010  d6 5b 31 58 dc fe 2e 7e  94 da 2f 00 cc 15 71 80
   0020  0a 6c 63 53 00 38 c3 38  ac 22 f3 63 62 0e ce 85
   0030  3f b8 07 4c 4e 2b 77 c7  21 f5 1a 80 1d 67 fb e1
   0040  e1 83 07 d8 0d 00 00 01  00
   ```

3. Compute `CMAC-AES-256(password, constant)`.  Call the 16-byte
   result `cmac`.

4. The 32-byte AES-256 key is `cmac || cmac`, that is, `cmac` repeated
   twice.

### Example

Consider the password `pspp`.  `password` is:

```
0000  70 73 70 70 00 00 00 00  00 00 00 00 00 00 00 00  |pspp............|
0010  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
```

`cmac` is:

```
0000  3e da 09 8e 66 04 d4 fd  f9 63 0c 2c a8 6f b0 45
```

The AES-256 key is:

```
0000  3e da 09 8e 66 04 d4 fd  f9 63 0c 2c a8 6f b0 45
0010  3e da 09 8e 66 04 d4 fd  f9 63 0c 2c a8 6f b0 45
```

### Checking Passwords

A program reading an encrypted file may wish to verify that the
password it was given is the correct one.  One way is to verify that
the PKCS #7 padding at the end of the file is well formed.  However,
any plaintext that ends in byte 01 is well formed PKCS #7, meaning
that about 1 in 256 keys will falsely pass this test.  This might be
acceptable for interactive use, but the false positive rate is too
high for a brute-force search of the password space.

A better test requires some knowledge of the file format being
wrapped, to obtain a "magic number" for the beginning of the file.

* The plaintext of system files begins with `$FL2@(#)` or `$FL3@(#)`.

* Before encryption, a syntax file is prefixed with a line at the
  beginning of the form `* Encoding: ENCODING.`, where ENCODING is the
  encoding used for the rest of the file, e.g. `windows-1252`.  Thus,
  `* Encoding` may be used as a magic number for system files.

* The plaintext of viewer files begins with `50 4b 03 04 14 00 08` (`50
  4b` is `PK`).

## Password Encoding

SPSS also supports what it calls "encrypted passwords."

> ⚠️ Warning: SPSS "encrypted passwords" are not encrypted.  They are
encoded with a simple, fixed scheme and can be decoded to the original
password using the rules described below.

An encoded password is always a multiple of 2 characters long, and
never longer than 20 characters.  The characters in an encoded
password are always in the graphic ASCII range 33 through 126.  Each
successive pair of characters in the password encodes a single byte in
the plaintext password.

Use the following algorithm to decode a pair of characters:

1. Let `a` be the ASCII code of the first character, and `b` be the
   ASCII code of the second character.

2. Let `ah` be the most significant 4 bits of `a`.  Find the line in
   the table below that has `ah` on the left side.  The right side of
   the line is a set of possible values for the most significant 4
   bits of the decoded byte.

   ```
   2  ⇒ 2367
   3  ⇒ 0145
   47 ⇒ 89cd
   56 ⇒ abef
   ```

3. Let `bh` be the most significant 4 bits of `b`.  Find the line in
   the second table below that has `bh` on the left side.  The right
   side of the line is a set of possible values for the most
   significant 4 bits of the decoded byte.  Together with the results
   of the previous step, only a single possibility is left.

   ```
   2  ⇒ 139b
   3  ⇒ 028a
   47 ⇒ 46ce
   56 ⇒ 57df
   ```

4. Let `al` be the least significant 4 bits of `a`.  Find the line in
   the table below that has `al` on the left side.  The right side of
   the line is a set of possible values for the least significant 4
   bits of the decoded byte.

   ```
   03cf ⇒ 0145
   12de ⇒ 2367
   478b ⇒ 89cd
   569a ⇒ abef
   ```

5. Let `bl` be the least significant 4 bits of `b`.  Find the line in
   the table below that has `bl` on the left side.  The right side of
   the line is a set of possible values for the least significant 4
   bits of the decoded byte.  Together with the results of the
   previous step, only a single possibility is left.

   ```
   03cf ⇒ 028a
   12de ⇒ 139b
   478b ⇒ 46ce
   569a ⇒ 57df
   ```

### Example

Consider the encoded character pair `-|`.  `a` is 0x2d and `b` is
0x7c, so `ah` is 2, `bh` is 7, `al` is 0xd, and `bl` is 0xc.  `ah`
means that the most significant four bits of the decoded character is
2, 3, 6, or 7, and `bh` means that they are 4, 6, 0xc, or 0xe.  The
single possibility in common is 6, so the most significant four bits
are 6.  Similarly, `al` means that the least significant four bits are
2, 3, 6, or 7, and `bl` means they are 0, 2, 8, or 0xa, so the least
significant four bits are 2.  The decoded character is therefore 0x62,
the letter `b`.

