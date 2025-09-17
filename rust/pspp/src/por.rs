// PSPP - a program for statistical analysis.
// Copyright (C) 2025 Free Software Foundation, Inc.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program.  If not, see <http://www.gnu.org/licenses/>.

//! Reading and writing portable files.
//!
//! This module enables reading and writing “portable files”, a text-based
//! format for SPSS data files.  The [portable file format] dates back 40+ years.
//! It was originally designed to facilitate data interchange between systems
//! with unlike character sets, but it did not continue to evolve after the
//! system file format was introduced.  It is obsolete.  PSPP includes readers
//! and writers for portable files only for compatibility; all non-legacy uses
//! of PSPP should use [system files] instead.
//!
//! Use [PortableFile] to read a portable file.  Use [WriteOptions] to write a
//! portable file.
//!
//! [portable file format]: https://pspp.benpfaff.org/manual/portable.html
//! [system files]: crate::sys
#![cfg_attr(not(test), warn(missing_docs))]

mod read;
mod write;

pub use read::{
    Cases, Error, ErrorDetails, Metadata, PortableFile, ReadPad, ReadTranslate, TranslationTable,
    Warning,
};
pub use write::{WriteOptions, Writer};

static PORTABLE_TO_WINDOWS_1252: &[u8] = {
    let s =
    b"                                                                \
      0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz .\
      <(+|&[]!$*);^-/|,%_>?`:#@'=\"  \xb1 \xb0\x86~\x96   0\xb9\xb2\xb3456789   \x97() {}\\\xa2\x95                                                                   ";
    assert!(s.len() == 256);
    s
};

/// Returns the windows-1252 character corresponding to the given `portable`
/// character.
fn portable_to_windows_1252(portable: u8) -> u8 {
    PORTABLE_TO_WINDOWS_1252[portable as usize]
}
