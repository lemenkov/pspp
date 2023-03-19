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

//! PSPP syntax scanning.
//!
//! PSPP divides traditional "lexical analysis" or "tokenization" into two
//! phases: a lower-level phase called "segmentation" and a higher-level phase
//! called "scanning".  [segment] implements the segmentation phase and
//! this module the scanning phase.
//!
//! Scanning accepts as input a stream of segments, which are UTF-8 strings each
//! labeled with a segment type.  It outputs a stream of "scan tokens", which
//! are the same as the tokens used by the PSPP parser with a few additional
//! types.

pub mod command_name;
pub mod lexer;
pub mod scan;
pub mod segment;
pub mod token;
