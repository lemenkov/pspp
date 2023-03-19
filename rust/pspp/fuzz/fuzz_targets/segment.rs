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

#![no_main]

use libfuzzer_sys::fuzz_target;
use pspp::lex::segment::{Segmenter, Mode, Type};

fuzz_target!(|data: &[u8]| {
    if let Ok(mut input) = std::str::from_utf8(data) {
        let mut segmenter = Segmenter::new(Mode::Auto, false);
        loop {
            let (rest, type_) = segmenter.push(input, true).unwrap();
            match type_ {
                Type::End => break,
                _ => (),
            }
            input = rest;
        }
    }
});
