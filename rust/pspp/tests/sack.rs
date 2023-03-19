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

use std::fs::read_to_string;
use std::path::PathBuf;

use anyhow::{anyhow, Result};
use clap::Parser;
use pspp::endian::Endian;
use pspp::sys::sack::sack;

/// SAv Construction Kit
///
/// The input is a sequence of data items, each followed by a semicolon.  Each
/// data item is converted to the output format and written on stdout.  A data
/// item is one of the following:
///
///   - An integer in decimal, in hexadecimal prefixed by `0x`, or in octal
///     prefixed by `0`.  Output as a 32-bit binary integer.
///
///   - A floating-point number.  Output in 64-bit IEEE 754 format.
///
///   - A string enclosed in double quotes.  Output literally.  There is no
///     syntax for "escapes".  Strings may not contain new-lines.
///
///   - A literal of the form `s<number>` followed by a quoted string as above.
///     Output as the string's contents followed by enough spaces to fill up
///     `<number>` bytes.  For example, `s8 "foo"` is output as `foo` followed
///     by 5 spaces.
///
///   - The literal `i8`, `i16`, or `i64` followed by an integer.  Output
///     as a binary integer with the specified number of bits.
///
///   - One of the literals `SYSMIS`, `LOWEST`, or `HIGHEST`.  Output as a
///     64-bit IEEE 754 float of the appropriate PSPP value.
///
///   - `PCSYSMIS`.  Output as SPSS/PC+ system-missing value.
///
///   - The literal `ENDIAN`.  Output as a 32-bit binary integer, either with
///     value 1 if `--be` is in effect or 2 if `--le` is in effect.
///
///   - A pair of parentheses enclosing a sequence of data items, each followed
///     by a semicolon (the last semicolon is optional).  Output as the enclosed
///     data items in sequence.
///
///   - The literal `COUNT` or `COUNT8` followed by a sequence of parenthesized
///     data items, as above.  Output as a 32-bit or 8-bit binary integer whose
///     value is the number of bytes enclosed within the parentheses, followed
///     by the enclosed data items themselves.
///
/// optionally followed by an asterisk and a positive integer, which specifies a
/// repeat count for the data item.
#[derive(Parser, Debug)]
struct Args {
    /// Big-endian output format (default)
    #[arg(long = "be")]
    be: bool,

    /// Little-endian output format
    #[arg(long = "le")]
    le: bool,

    /// Input file.
    #[arg(required = true, name = "input")]
    input_file_name: PathBuf,

    /// Output file.
    #[arg(required = true, name = "output")]
    output_file_name: PathBuf,
}

fn main() -> Result<()> {
    let Args {
        be,
        le,
        input_file_name,
        output_file_name,
    } = Args::parse();
    let endian = match (be, le) {
        (false, false) | (true, false) => Endian::Big,
        (false, true) => Endian::Little,
        (true, true) => return Err(anyhow!("can't use both `--be` and `--le`")),
    };

    let input_file_str = input_file_name.to_string_lossy();
    let input = read_to_string(&input_file_name)
        .map_err(|err| anyhow!("{input_file_str}: read failed ({err})"))?;

    let output = sack(&input, Some(&input_file_name), endian)?;

    let output_file_str = output_file_name.to_string_lossy();
    std::fs::write(&output_file_name, output)
        .map_err(|err| anyhow!("{output_file_str}: write failed ({err})"))?;

    Ok(())
}
