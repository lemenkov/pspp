# PSPP in Rust

PSPP is a program for statistical analysis of sampled data.  It is
a free replacement for the proprietary program SPSS.

This is an experimental rewrite of PSPP in Rust.  The goal of the
rewrite is to ensure that PSPP can be as robust as possible, while
remaining fast and portable.

This new version is very incomplete--in fact, only a subset of the
`pspp-convert` utility is really ready for use.

## Installing PSPP

To install this version of PSPP from source, first install Rust using
the instructions at https://www.rust-lang.org/tools/install.  PSPP
requires Rust 1.88 or later.  Once you've done that, you can download
PSPP source code from `crates.io` and build and install it with:

```
cargo install pspp
```

If you have already checked out a copy of this repository, then you
can instead `cd` into `rust/pspp` in the source tree and run:

```
cargo install --path .
```

To uninstall later, run:

```
cargo uninstall pspp
```

## Running PSPP

After installing PSPP using one of the methods above, run it with:

```
pspp
```

For help, use `pspp --help`.  The only really useful PSPP command
currently is `convert`.  For help with it, run:

```
pspp convert --help
```

## Reading the manual

The PSPP manual is maintained in [mdBook] format.  To build the
manual, first install mdBook using the instructions at
https://rust-lang.github.io/mdBook/guide/installation.html.  Then,
from the root of a checked-out copy of this repository, build the
manual with:

```
(cd rust/doc && mdbook build)
```

This will output the manual as HTML under `rust/doc/book/html`.

[mdBook]: https://rust-lang.github.io/mdBook/

## Reporting bugs

As this is an experiment, please don't have high expectations (yet).
However, questions and comments about using PSPP may be sent to
pspp-users@gnu.org.  Please email bug reports to bug-gnu-pspp@gnu.org
or file them online at https://savannah.gnu.org/bugs/?group=pspp.
Please indicate that you are referring to the Rust rewrite.
