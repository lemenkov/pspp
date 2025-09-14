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

use binrw::Endian;
use num::{Bounded, Zero};
use ordered_float::OrderedFloat;
use std::{
    collections::{hash_map::Entry, HashMap},
    error::Error as StdError,
    fmt::{Display, Formatter, Result as FmtResult},
    iter::repeat_n,
    path::{Path, PathBuf},
};

use crate::endian::ToBytes;

pub type Result<T, F = Error> = std::result::Result<T, F>;

#[derive(Debug)]
pub struct Error {
    pub file_name: Option<PathBuf>,
    pub line_number: Option<usize>,
    pub token: Option<String>,
    pub message: String,
}

impl Error {
    fn new(
        file_name: Option<&Path>,
        line_number: Option<usize>,
        token: Option<&str>,
        message: String,
    ) -> Error {
        Error {
            file_name: file_name.map(PathBuf::from),
            line_number,
            token: token.map(String::from),
            message,
        }
    }
}

impl StdError for Error {}

impl Display for Error {
    fn fmt(&self, f: &mut Formatter) -> FmtResult {
        match (self.file_name.as_ref(), self.line_number) {
            (Some(ref file_name), Some(line_number)) => {
                write!(f, "{}:{line_number}: ", file_name.display())?
            }
            (Some(ref file_name), None) => write!(f, "{}: ", file_name.display())?,
            (None, Some(line_number)) => write!(f, "line {line_number}: ")?,
            (None, None) => (),
        }
        if let Some(ref token) = self.token {
            write!(f, "at '{token}': ")?;
        }
        write!(f, "{}", self.message)
    }
}

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
pub fn sack(input: &str, input_file_name: Option<&Path>, endian: Endian) -> Result<Vec<u8>> {
    let mut symbol_table = HashMap::new();
    let output = _sack(input, input_file_name, endian, &mut symbol_table)?;
    let output = if !symbol_table.is_empty() {
        for (k, v) in symbol_table.iter() {
            if v.is_none() {
                Err(Error::new(
                    input_file_name,
                    None,
                    None,
                    format!("label {k} used but never defined"),
                ))?
            }
        }
        _sack(input, input_file_name, endian, &mut symbol_table)?
    } else {
        output
    };
    Ok(output)
}

fn _sack(
    input: &str,
    input_file_name: Option<&Path>,
    endian: Endian,
    symbol_table: &mut HashMap<String, Option<u32>>,
) -> Result<Vec<u8>> {
    let mut lexer = Lexer::new(input, input_file_name, endian)?;
    let mut output = Vec::new();
    while parse_data_item(&mut lexer, &mut output, symbol_table)? {}
    Ok(output)
}

fn parse_data_item(
    lexer: &mut Lexer,
    output: &mut Vec<u8>,
    symbol_table: &mut HashMap<String, Option<u32>>,
) -> Result<bool> {
    if lexer.token.is_none() {
        return Ok(false);
    };

    let initial_len = output.len();
    match lexer.take()? {
        Token::Integer(integer) => {
            if let Ok(integer) = TryInto::<i32>::try_into(integer) {
                output.extend_from_slice(&lexer.endian.to_bytes(integer));
            } else if let Ok(integer) = TryInto::<u32>::try_into(integer) {
                output.extend_from_slice(&lexer.endian.to_bytes(integer));
            } else {
                Err(lexer.error(format!(
                    "{integer} is not in the valid range [{},{}]",
                    i32::MIN,
                    u32::MAX
                )))?;
            };
        }
        Token::Float(float) => output.extend_from_slice(&lexer.endian.to_bytes(float.0)),
        Token::PcSysmis => {
            output.extend_from_slice(&[0xf5, 0x1e, 0x26, 0x02, 0x8a, 0x8c, 0xed, 0xff])
        }
        Token::I8 => put_integers::<u8, 1>(lexer, "i8", output)?,
        Token::I16 => put_integers::<u16, 2>(lexer, "i16", output)?,
        Token::I64 => put_integers::<i64, 8>(lexer, "i64", output)?,
        Token::String(string) => output.extend_from_slice(string.as_bytes()),
        Token::S(size) => {
            let Some((Token::String(ref string), _)) = lexer.token else {
                Err(lexer.error(format!("string expected after 's{size}'")))?
            };
            let len = string.len();
            if len > size {
                Err(lexer.error(format!(
                    "{len}-byte string is longer than pad length {size}"
                )))?
            }
            output.extend_from_slice(string.as_bytes());
            output.extend(repeat_n(b' ', size - len));
            lexer.get()?;
        }
        Token::LParen => {
            while !matches!(lexer.token, Some((Token::RParen, _))) {
                parse_data_item(lexer, output, symbol_table)?;
            }
            lexer.get()?;
        }
        Token::Count => put_counted_items::<u32, 4>(lexer, "COUNT", output, symbol_table)?,
        Token::Count8 => put_counted_items::<u8, 1>(lexer, "COUNT8", output, symbol_table)?,
        Token::Hex => {
            let Some((Token::String(ref string), _)) = lexer.token else {
                Err(lexer.error(String::from("string expected after 'hex'")))?
            };
            let mut string = &string[..];
            loop {
                string = string.trim_start();
                if string.is_empty() {
                    break;
                };

                let mut i = string.chars();
                let Some(c0) = i.next() else { return Ok(true) };
                let Some(c1) = i.next() else {
                    Err(lexer.error(String::from("hex string has odd number of characters")))?
                };

                let (Some(digit0), Some(digit1)) = (c0.to_digit(16), c1.to_digit(16)) else {
                    Err(lexer.error(String::from("invalid digit in hex string")))?
                };
                let byte = digit0 * 16 + digit1;
                output.push(byte as u8);

                string = i.as_str();
            }
            lexer.get()?;
        }
        Token::Label(name) => {
            let value = output.len() as u32;
            match symbol_table.entry(name.clone()) {
                Entry::Vacant(v) => {
                    v.insert(Some(value));
                }
                Entry::Occupied(mut o) => {
                    match o.get() {
                        Some(v) => {
                            if *v != value {
                                Err(lexer.error(format!("{name}: can't redefine label for offset {:#x} with offset {:#x}", *v, value)))?
                            }
                        }
                        None => drop(o.insert(Some(value))),
                    }
                }
            };
            return Ok(true);
        }
        Token::At(name) => {
            let mut value = *symbol_table.entry(name.clone()).or_insert(None);
            loop {
                let plus = match lexer.token {
                    Some((Token::Plus, _)) => true,
                    Some((Token::Minus, _)) => false,
                    _ => break,
                };
                lexer.get()?;

                let operand = match lexer.token {
                    Some((Token::At(ref name), _)) => {
                        *symbol_table.entry(name.clone()).or_insert(None)
                    }
                    Some((Token::Integer(integer), _)) => Some(
                        integer
                            .try_into()
                            .map_err(|msg| lexer.error(format!("bad offset literal ({msg})")))?,
                    ),
                    _ => Err(lexer.error(String::from("expecting @label or integer literal")))?,
                };
                lexer.get()?;

                value = match (value, operand) {
                    (Some(a), Some(b)) => Some(
                        if plus {
                            a.checked_add(b)
                        } else {
                            a.checked_sub(b)
                        }
                        .ok_or_else(|| {
                            lexer.error(String::from("overflow in offset arithmetic"))
                        })?,
                    ),
                    _ => None,
                };
            }
            let value = value.unwrap_or(0);
            output.extend_from_slice(&lexer.endian.to_bytes(value));
        }
        _ => (),
    };
    if let Some((Token::Asterisk, _)) = lexer.token {
        lexer.get()?;
        let Token::Integer(count) = lexer.take()? else {
            Err(lexer.error(String::from("positive integer expected after '*'")))?
        };
        if count < 1 {
            Err(lexer.error(String::from("positive integer expected after '*'")))?
        };
        let final_len = output.len();
        for _ in 1..count {
            output.extend_from_within(initial_len..final_len);
        }
    }
    match lexer.token {
        Some((Token::Semicolon, _)) => {
            lexer.get()?;
        }
        Some((Token::RParen, _)) => (),
        _ => Err(lexer.error(String::from("';' expected")))?,
    }
    Ok(true)
}

fn put_counted_items<T, const N: usize>(
    lexer: &mut Lexer,
    name: &str,
    output: &mut Vec<u8>,
    symbol_table: &mut HashMap<String, Option<u32>>,
) -> Result<()>
where
    T: Zero + TryFrom<usize>,
    Endian: ToBytes<T, N>,
{
    let old_size = output.len();
    output.extend_from_slice(&lexer.endian.to_bytes(T::zero()));
    let start = output.len();
    if !matches!(lexer.token, Some((Token::LParen, _))) {
        Err(lexer.error(format!("'(' expected after '{name}'")))?
    }
    lexer.get()?;
    while !matches!(lexer.token, Some((Token::RParen, _))) {
        parse_data_item(lexer, output, symbol_table)?;
    }
    lexer.get()?;
    let delta = output.len() - start;
    let Ok(delta): Result<T, _> = delta.try_into() else {
        Err(lexer.error(format!("{delta} bytes is too much for '{name}'")))?
    };
    let dest = &mut output[old_size..old_size + N];
    dest.copy_from_slice(&lexer.endian.to_bytes(delta));
    Ok(())
}

fn put_integers<T, const N: usize>(
    lexer: &mut Lexer,
    name: &str,
    output: &mut Vec<u8>,
) -> Result<()>
where
    T: Bounded + Display + TryFrom<i64> + Copy,
    Endian: ToBytes<T, N>,
{
    let mut n = 0;
    while let Some(integer) = lexer.take_if(|t| match t {
        Token::Integer(integer) => Some(*integer),
        _ => None,
    })? {
        let Ok(integer) = integer.try_into() else {
            Err(lexer.error(format!(
                "{integer} is not in the valid range [{},{}]",
                T::min_value(),
                T::max_value()
            )))?
        };
        output.extend_from_slice(&lexer.endian.to_bytes(integer));
        n += 1;
    }
    if n == 0 {
        Err(lexer.error(format!("integer expected after '{name}'")))?
    }
    Ok(())
}

#[derive(PartialEq, Eq, Clone, Debug)]
enum Token {
    Integer(i64),
    Float(OrderedFloat<f64>),
    PcSysmis,
    String(String),
    Semicolon,
    Asterisk,
    LParen,
    RParen,
    I8,
    I16,
    I64,
    S(usize),
    Count,
    Count8,
    Hex,
    Label(String),
    At(String),
    Minus,
    Plus,
}

struct Lexer<'a> {
    input: &'a str,
    token: Option<(Token, &'a str)>,
    input_file_name: Option<&'a Path>,
    line_number: usize,
    endian: Endian,
}

fn skip_comments(mut s: &str) -> (&str, usize) {
    let mut n_newlines = 0;
    let s = loop {
        s = s.trim_start_matches([' ', '\t', '\r', '<', '>']);
        if let Some(remainder) = s.strip_prefix('#') {
            let Some((_, remainder)) = remainder.split_once('\n') else {
                break "";
            };
            s = remainder;
            n_newlines += 1;
        } else if let Some(remainder) = s.strip_prefix('\n') {
            s = remainder;
            n_newlines += 1;
        } else {
            break s;
        }
    };
    (s, n_newlines)
}

impl<'a> Lexer<'a> {
    fn new(input: &'a str, input_file_name: Option<&'a Path>, endian: Endian) -> Result<Lexer<'a>> {
        let mut lexer = Lexer {
            input,
            token: None,
            input_file_name,
            line_number: 1,
            endian,
        };
        lexer.token = lexer.next()?;
        Ok(lexer)
    }
    fn error(&self, message: String) -> Error {
        let repr = self.token.as_ref().map(|(_, repr)| *repr);
        Error::new(self.input_file_name, Some(self.line_number), repr, message)
    }
    fn take(&mut self) -> Result<Token> {
        let Some(token) = self.token.take() else {
            Err(self.error(String::from("unexpected end of input")))?
        };
        self.token = self.next()?;
        Ok(token.0)
    }
    fn take_if<F, T>(&mut self, condition: F) -> Result<Option<T>>
    where
        F: FnOnce(&Token) -> Option<T>,
    {
        let Some(ref token) = self.token else {
            return Ok(None);
        };
        match condition(&token.0) {
            Some(value) => {
                self.token = self.next()?;
                Ok(Some(value))
            }
            None => Ok(None),
        }
    }
    fn get(&mut self) -> Result<Option<&Token>> {
        if self.token.is_none() {
            Err(self.error(String::from("unexpected end of input")))?
        } else {
            self.token = self.next()?;
            match self.token {
                Some((ref token, _)) => Ok(Some(token)),
                None => Ok(None),
            }
        }
    }

    fn next(&mut self) -> Result<Option<(Token, &'a str)>> {
        // Get the first character of the token, skipping past white space and
        // comments.
        let (s, n_newlines) = skip_comments(self.input);
        self.line_number += n_newlines;
        self.input = s;

        let start = s;
        let mut iter = s.chars();
        let Some(c) = iter.next() else {
            return Ok(None);
        };
        let (token, rest) = match c {
            c if c.is_ascii_digit() || c == '-' => {
                let len = s
                    .find(|c: char| {
                        !(c.is_ascii_digit() || c.is_alphabetic() || c == '.' || c == '-')
                    })
                    .unwrap_or(s.len());
                let (number, rest) = s.split_at(len);
                let token = if number == "-" {
                    Token::Minus
                } else if let Some(digits) = number.strip_prefix("0x") {
                    Token::Integer(i64::from_str_radix(digits, 16).map_err(|msg| {
                        self.error(format!("bad integer literal '{number}' ({msg})"))
                    })?)
                } else if !number.contains('.') {
                    Token::Integer(number.parse().map_err(|msg| {
                        self.error(format!("bad integer literal '{number}' ({msg})"))
                    })?)
                } else {
                    Token::Float(number.parse().map_err(|msg| {
                        self.error(format!("bad float literal '{number}' ({msg})"))
                    })?)
                };
                (token, rest)
            }
            '"' => {
                let s = iter.as_str();
                let Some(len) = s.find(['\n', '"']) else {
                    Err(self.error(String::from("end-of-file inside string")))?
                };
                let (string, rest) = s.split_at(len);
                let Some(rest) = rest.strip_prefix('"') else {
                    Err(self.error(format!("new-line inside string ({string}...{rest})")))?
                };
                (Token::String(string.into()), rest)
            }
            ';' => (Token::Semicolon, iter.as_str()),
            '*' => (Token::Asterisk, iter.as_str()),
            '+' => (Token::Plus, iter.as_str()),
            '(' => (Token::LParen, iter.as_str()),
            ')' => (Token::RParen, iter.as_str()),
            c if c.is_alphabetic() || c == '@' || c == '_' => {
                let len = s
                    .find(|c: char| {
                        !(c.is_ascii_digit()
                            || c.is_alphabetic()
                            || c == '@'
                            || c == '.'
                            || c == '_')
                    })
                    .unwrap_or(s.len());
                let (s, rest) = s.split_at(len);
                if let Some(rest) = rest.strip_prefix(':') {
                    (Token::Label(s.into()), rest)
                } else if let Some(name) = s.strip_prefix('@') {
                    (Token::At(name.into()), rest)
                } else if let Some(count) = s.strip_prefix('s') {
                    let token =
                        Token::S(count.parse().map_err(|msg| {
                            self.error(format!("bad counted string '{s}' ({msg})"))
                        })?);
                    (token, rest)
                } else {
                    let token = match s {
                        "i8" => Token::I8,
                        "i16" => Token::I16,
                        "i64" => Token::I64,
                        "SYSMIS" => Token::Float(OrderedFloat(-f64::MAX)),
                        "PCSYSMIS" => Token::PcSysmis,
                        "LOWEST" => Token::Float(f64::MIN.next_up().into()),
                        "HIGHEST" => Token::Float(f64::MAX.into()),
                        "ENDIAN" => Token::Integer(if self.endian == Endian::Big { 1 } else { 2 }),
                        "COUNT" => Token::Count,
                        "COUNT8" => Token::Count8,
                        "hex" => Token::Hex,
                        _ => Err(self.error(format!("invalid token '{s}'")))?,
                    };
                    (token, rest)
                }
            }
            _ => Err(self.error(format!("invalid input byte '{c}'")))?,
        };
        self.input = rest;
        let repr = &start[..start.len() - rest.len()];
        Ok(Some((token, repr)))
    }
}

#[cfg(test)]
mod tests {
    use crate::sys::sack::sack;
    use anyhow::Result;
    use binrw::Endian;

    #[test]
    fn basic_sack() -> Result<()> {
        let input = r#"
"$FL2"; s60 "$(#) SPSS DATA FILE PSPP synthetic test file";
2; # Layout code
28; # Nominal case size
0; # Not compressed
0; # Not weighted
1; # 1 case.
100.0; # Bias.
"01 Jan 11"; "20:53:52";
"PSPP synthetic test file: "; i8 244; i8 245; i8 246; i8 248; s34 "";
i8 0 *3;
"#;
        sack(input, None, Endian::Big)?;
        Ok(())
    }

    #[test]
    fn pcp_sack() -> Result<()> {
        let input = r#"
# File header.
2; 0;
@MAIN; @MAIN_END - @MAIN;
@VARS; @VARS_END - @VARS;
@LABELS; @LABELS_END - @LABELS;
@DATA; @DATA_END - @DATA;
(0; 0) * 11;
i8 0 * 128;

MAIN:
    i16 1;         # Fixed.
    s62 "PCSPSS PSPP synthetic test product";
    PCSYSMIS;
    0; 0; i16 1;   # Fixed.
    i16 0;
    i16 15;
    1;
    i16 0;         # Fixed.
    1;
    s8 "11/28/14";
    s8 "15:11:00";
    s64 "PSPP synthetic test file";
MAIN_END:

VARS:
    0; 0; 0; 0x050800; s8 "$CASENUM"; PCSYSMIS;
    0; 0; 0; 0x010800; s8 "$DATE"; PCSYSMIS;
    0; 0; 0; 0x050802; s8 "$WEIGHT"; PCSYSMIS;

    # Numeric variable, no label or missing values.
    0; 0; 0; 0x050800; s8 "NUM1"; PCSYSMIS;

    # Numeric variable, variable label.
    0; 0; @NUM2_LABEL - @LABELS_OFS; 0x050800; s8 "NUM2"; PCSYSMIS;

    # Numeric variable with missing value.
    0; 0; 0; 0x050800; s8 "NUM3"; 1.0;

    # Numeric variable, variable label and missing value.
    0; 0; @NUM4_LABEL - @LABELS_OFS; 0x050800; s8 "NUM4"; 2.0;

    # String variable, no label or missing values.
    0; 0; 0; 0x010800; s8 "STR1"; PCSYSMIS;

    # String variable, variable label.
    0; 0; @STR2_LABEL - @LABELS_OFS; 0x010400; s8 "STR2"; PCSYSMIS;

    # String variable with missing value.
    0; 0; 0; 0x010500; s8 "STR3"; s8 "MISS";

    # String variable, variable label and missing value.
    0; 0; @STR4_LABEL - @LABELS_OFS; 0x010100; s8 "STR4"; s8 "OTHR";

    # Long string variable
    0; 0; 0; 0x010b00; s8 "STR5"; PCSYSMIS;
    0 * 8;

    # Long string variable with variable label
    0; 0; @STR6_LABEL - @LABELS_OFS; 0x010b00; s8 "STR6"; PCSYSMIS;
    0 * 8;
VARS_END:

LABELS:
    3; i8 0 0 0; LABELS_OFS: i8 0;
    NUM2_LABEL: COUNT8("Numeric variable 2's label");
    NUM4_LABEL: COUNT8("Another numeric variable label");
    STR2_LABEL: COUNT8("STR2's variable label");
    STR4_LABEL: COUNT8("STR4's variable label");
    STR6_LABEL: COUNT8("Another string variable's label");
LABELS_END:

DATA:
    0.0; "11/28/14"; 1.0;
    0.0; 1.0; 2.0; PCSYSMIS; s8 "abcdefgh"; s8 "ijkl"; s8 "mnopq"; s8 "r";
    s16 "stuvwxyzAB"; s16 "CDEFGHIJKLM";
DATA_END:
"#;
        sack(input, None, Endian::Big)?;
        Ok(())
    }
}
