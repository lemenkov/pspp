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

//! Reading and writing system files.
//!
//! This module enables reading and writing "system files", the binary format
//! for SPSS data files.  The system file format dates back 40+ years and has
//! evolved greatly over that time to support new features, but in a way to
//! facilitate interchange between even the oldest and newest versions of
//! software.
//!
//! To read a system file in the simplest way, use [ReaderOptions].

// Warn about missing docs, but not for items declared with `#[cfg(test)]`.
#![cfg_attr(not(test), warn(missing_docs))]

mod cooked;
pub use cooked::*;
pub mod encoding;
pub mod raw;

#[cfg(test)]
pub mod sack;

#[cfg(test)]
mod test;
