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

//! Lexical analysis for PSPP syntax.
//!
//! PSPP divides traditional "lexical analysis" or "tokenization" into three
//! phases:
//!
//! 1. A low level called "segmentation", implemented in the [segment] module.
//!    This labels syntax strings with [Segment](segment::Segment)s.
//!
//! 2. A middle level called "scanning", implemented in the [scan] module.
//!    This transforms and merges segments to form [Token]s.
//!
//! 3. A high level called "lexing", implemented in the [lexer] module.  Lexing
//!    brings together multiple source files and invokes macro expansion on the
//!    tokens output by the scanner.

// Warn about missing docs, but not for items declared with `#[cfg(test)]`.
#![cfg_attr(not(test), warn(missing_docs))]

pub mod command_name;
pub mod lexer;
pub mod scan;
pub mod segment;
mod token;
pub use token::{Punct, Token};
