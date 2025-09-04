//! # PSPP in Rust
//!
//! PSPP is a program for statistical analysis of sampled data.  It is
//! a free replacement for the proprietary program SPSS.
//!
//! This is an experimental rewrite of PSPP in Rust.  It includes both programs
//! directly useful to users and a library that can be used by other software.
//! The goal of the rewrite is to ensure that PSPP can be as robust as possible,
//! while remaining fast and portable.
//!
//! This new version is very incomplete--in fact, only a subset of the
//! `pspp-convert` utility is really ready for use.
//!
//! ## Installing PSPP
//!
//! To install PSPP in Rust from source, [install Rust], version 1.88 or later.
//! Then, either:
//!
//! - Use the following command to download PSPP source code from `crates.io`
//!   and build and install it:
//!
//!   ```text
//!   cargo install pspp
//!   ```
//!
//! - Clone a local copy of this repository and build and install it from there,
//!   with: can instead `cd` into `rust/pspp` in the source tree and run:
//!
//!   ```text
//!   git clone git://git.sv.gnu.org/pspp
//!   cd pspp/rust/pspp
//!   cargo install --path .
//!   ```
//!
//! The above commands also work for upgrades.
//!
//! [install Rust]: https://www.rust-lang.org/tools/install
//!
//! ### Uninstalling
//!
//! To uninstall PSPP, run:
//!
//! ```text
//! cargo uninstall pspp
//! ```
//!
//! ## Running PSPP
//!
//! After installing PSPP using one of the methods above, run it with:
//!
//! ```text
//! pspp
//! ```
//!
//! For help, use `pspp --help`.  The only really useful PSPP commands
//! currently are `convert` and `show`.  Help for individual commands is
//! available with `pspp <command> --help`.
//!
//! ## Reading the manual
//!
//! The PSPP manual may be [read online].
//!
//! To build the manual, install [mdBook] using its [instructions] then, from
//! the root of a checked-out copy of this repository, build the manual with:
//!
//! ```text
//! (cd rust/doc && mdbook build)
//! ```
//!
//! This will output the manual as HTML under `rust/doc/book/html`.
//!
//! [read online]: https://pspp.benpfaff.org/manual
//! [mdBook]: https://rust-lang.github.io/mdBook/
//! [instructions]: https://rust-lang.github.io/mdBook/guide/installation.html
//!
//! ## Reporting bugs
//!
//! As this is an experiment, please don't have high expectations (yet).
//! However, questions and comments about using PSPP may be sent to
//! <pspp-users@gnu.org>.  Please email bug reports to <bug-gnu-pspp@gnu.org> or
//! file them online at [Savannah].  Please indicate that you are referring to
//! the Rust rewrite.
//!
//! [Savannah]: https://savannah.gnu.org/bugs/?group=pspp
//!
//! # License
//!
//! Copyright © 2025 Free Software Foundation, Inc.
//!
//! This program is free software: you can redistribute it and/or modify it under
//! the terms of the [GNU General Public License] as published by the Free Software
//! Foundation, either version 3 of the License, or (at your option) any later
//! version.
//!
//! This program is distributed in the hope that it will be useful, but WITHOUT
//! ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
//! FOR A PARTICULAR PURPOSE.  See the [GNU General Public License] for more
//! details.
//!
//! [GNU General Public License]: http://www.gnu.org/licenses/
use std::ops::Range;

pub mod calendar;
pub mod command;
pub mod crypto;
pub mod data;
pub mod dictionary;
pub mod endian;
pub mod engine;
pub mod format;
pub mod hexfloat;
pub mod identifier;
pub mod integer;
pub mod lex;
pub mod locale_charset;
pub mod macros;
pub mod message;
pub mod output;
pub mod prompt;
pub mod settings;
pub mod sys;
pub mod util;
pub mod variable;

/// This is [slice::element_offset] copied out from the standard library so that
/// we can use it while it is still experimental.
#[allow(dead_code)]
pub(crate) fn element_offset<T>(slice: &[T], element: &T) -> Option<usize> {
    if size_of::<T>() == 0 {
        panic!("elements are zero-sized");
    }

    let slice_start = slice.as_ptr().addr();
    let elem_start = std::ptr::from_ref(element).addr();

    let byte_offset = elem_start.wrapping_sub(slice_start);

    if byte_offset % size_of::<T>() != 0 {
        return None;
    }

    let offset = byte_offset / size_of::<T>();

    if offset < slice.len() {
        Some(offset)
    } else {
        None
    }
}

/// This is [slice::subslice_range] copied out from the standard library so that
/// we can use it while it is still experimental.
#[allow(dead_code)]
pub(crate) fn subslice_range<T>(slice: &[T], subslice: &[T]) -> Option<Range<usize>> {
    if size_of::<T>() == 0 {
        panic!("elements are zero-sized");
    }

    let slice_start = slice.as_ptr().addr();
    let subslice_start = subslice.as_ptr().addr();

    let byte_start = subslice_start.wrapping_sub(slice_start);

    if byte_start % size_of::<T>() != 0 {
        return None;
    }

    let start = byte_start / size_of::<T>();
    let end = start.wrapping_add(subslice.len());

    if start <= slice.len() && end <= slice.len() {
        Some(start..end)
    } else {
        None
    }
}
