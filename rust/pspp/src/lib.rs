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
