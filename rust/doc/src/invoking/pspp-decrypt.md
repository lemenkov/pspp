# Decrypting SPSS files with `pspp decrypt`

The `pspp decrypt` command reads an encrypted SPSS file and writes out
an equivalent plaintext file.  The basic syntax is:

```
pspp decrypt <INPUT> <OUTPUT>
```

which reads an encrypted SPSS data, viewer, or syntax file `<INPUT>`,
decrypts it, and writes the decrypted version to `<OUTPUT>`.

Other commands, such as [`pspp convert`](pspp-convert.md), can also
read encrypted files directly.

PSPP does not support writing encrypted files, only reading them.

> ⚠️ Warning: The SPSS encryption format is insecure: when the password
> is unknown, it is much cheaper and faster to decrypt a file
> encrypted this way than if a well designed alternative were used.

## Options

`pspp decrypt` accepts the following options:

* `-p <PASSWORD>`  
  `--password <PASSWORD>`  
  Specifies the password for reading the encrypted input file.
  Without this option, PSPP will interactively prompt for the
  password.

  > ⚠️ The password (and other command-line options) may be visible to
  other users on multiuser systems.
