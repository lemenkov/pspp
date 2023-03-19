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

//! PSPP lexical analysis.
//!
//! PSPP divides traditional "lexical analysis" or "tokenization" into two
//! phases: a lower-level phase called "segmentation" and a higher-level phase
//! called "scanning".  [mod segment] implements the segmentation phase and [mod
//! scan] the scanning phase.
//!
//! Scanning accepts as input a stream of segments, which are UTF-8 strings each
//! labeled with a segment type.  It outputs a stream of "scan tokens", which
//! are the same as the tokens used by the PSPP parser with a few additional
//! types.

use crate::identifier::{Identifier, ReservedWord};

use super::{
    segment::{Segment, Segmenter, Syntax},
    token::{Punct, Token},
};
use std::collections::VecDeque;
use thiserror::Error as ThisError;

#[derive(ThisError, Clone, Debug, PartialEq, Eq)]
pub enum ScanError {
    /// Unterminated string constant.
    #[error("Unterminated string constant.")]
    ExpectedQuote,

    /// Missing exponent.
    #[error("Missing exponent following `{0}`")]
    ExpectedExponent(String),

    /// Odd length hex string.
    #[error("String of hex digits has {0} characters, which is not a multiple of 2.")]
    OddLengthHexString(usize),

    /// Invalid hex digit.
    #[error("Invalid hex digit {0:?}.")]
    BadHexDigit(char),

    /// Incomplete UTF-8 sequence.
    #[error("Incomplete UTF-8 sequence `{substring}` starting {offset} digits into hex string.")]
    IncompleteUtf8 { substring: String, offset: usize },

    /// Bad UTF-8 sequence.
    #[error("Invalid UTF-8 sequence `{substring}` starting {offset} digits into hex string.")]
    BadUtf8 { substring: String, offset: usize },

    /// Invalid length Unicode string.
    #[error("Unicode string contains {0} bytes, which is not in the valid range of 1 to 8 bytes.")]
    BadLengthUnicodeString(usize),

    /// Invalid code point.
    #[error("U+{0:04X} is not a valid Unicode code point.")]
    BadCodePoint(u32),

    /// Expected hexadecimal Unicode code point
    #[error("Expected hexadecimal Unicode code point.")]
    ExpectedCodePoint,

    /// `DO REPEAT` nested too deeply.
    #[error("`DO REPEAT` nested too deeply.")]
    DoRepeatOverflow,

    /// Unexpected character.
    #[error("Unexpected character {0:?} in input.")]
    UnexpectedChar(char),
}

/// The input or output to token merging.
#[derive(Clone, Debug, PartialEq)]
pub enum ScanToken {
    Token(Token),
    Error(ScanError),
}

impl ScanToken {
    pub fn token(self) -> Option<Token> {
        match self {
            ScanToken::Token(token) => Some(token),
            ScanToken::Error(_) => None,
        }
    }
}

/// The result of merging tokens.
#[derive(Clone, Debug)]
pub enum MergeResult {
    /// Copy one token literally from input to output.
    Copy,

    /// Expand `n` tokens from the input into `token` in the output.
    Expand {
        /// Number of tokens to expand.
        n: usize,

        /// Replacement token.
        token: Token,
    },
}

#[derive(Copy, Clone, Debug)]
pub struct Incomplete;

impl ScanToken {
    pub fn from_segment(s: &str, segment: Segment) -> Option<Self> {
        match segment {
            Segment::Number => Some(Self::Token(Token::Number(s.parse().unwrap()))),
            Segment::QuotedString => {
                // Trim quote mark from front and back.
                let mut chars = s.chars();
                let quote = chars.next().unwrap();
                let s = chars.as_str().strip_suffix(quote).unwrap();

                // Replace doubled quotes by single ones.
                let (single_quote, double_quote) = match quote {
                    '\'' => ("'", "''"),
                    '"' => ("\"", "\"\""),
                    _ => unreachable!(),
                };
                Some(Self::Token(Token::String(
                    s.replace(double_quote, single_quote),
                )))
            }
            Segment::HexString => {
                // Strip `X"` prefix and `"` suffix (or variations).
                let s = &s[2..s.len() - 1];
                for c in s.chars() {
                    if !c.is_ascii_hexdigit() {
                        return Some(Self::Error(ScanError::BadHexDigit(c)));
                    }
                }
                if s.len() % 2 != 0 {
                    return Some(Self::Error(ScanError::OddLengthHexString(s.len())));
                }
                let bytes = s
                    .as_bytes()
                    .chunks_exact(2)
                    .map(|pair| {
                        let hi = char::from(pair[0]).to_digit(16).unwrap() as u8;
                        let lo = char::from(pair[1]).to_digit(16).unwrap() as u8;
                        hi * 16 + lo
                    })
                    .collect::<Vec<_>>();
                match String::from_utf8(bytes) {
                    Ok(string) => Some(Self::Token(Token::String(string))),
                    Err(error) => {
                        let details = error.utf8_error();
                        let offset = details.valid_up_to() * 2;
                        let end = details
                            .error_len()
                            .map(|len| offset + len * 2)
                            .unwrap_or(s.len());
                        let substring = String::from(&s[offset..end]);
                        Some(Self::Error(if details.error_len().is_some() {
                            ScanError::BadUtf8 { substring, offset }
                        } else {
                            ScanError::IncompleteUtf8 { substring, offset }
                        }))
                    }
                }
            }
            Segment::UnicodeString => {
                // Strip `U"` prefix and `"` suffix (or variations).
                let s = &s[2..s.len() - 1];
                if !(1..=8).contains(&s.len()) {
                    return Some(Self::Error(ScanError::BadLengthUnicodeString(s.len())));
                }
                let Ok(code_point) = u32::from_str_radix(s, 16) else {
                    return Some(Self::Error(ScanError::ExpectedCodePoint));
                };
                let Some(c) = char::from_u32(code_point) else {
                    return Some(Self::Error(ScanError::BadCodePoint(code_point)));
                };
                Some(Self::Token(Token::String(String::from(c))))
            }

            Segment::UnquotedString
            | Segment::DoRepeatCommand
            | Segment::InlineData
            | Segment::Document
            | Segment::MacroBody
            | Segment::MacroName => Some(Self::Token(Token::String(String::from(s)))),

            Segment::Identifier => {
                if let Ok(reserved_word) = ReservedWord::try_from(s) {
                    match reserved_word {
                        ReservedWord::And => Some(Self::Token(Token::Punct(Punct::And))),
                        ReservedWord::Or => Some(Self::Token(Token::Punct(Punct::Or))),
                        ReservedWord::Not => Some(Self::Token(Token::Punct(Punct::Not))),
                        ReservedWord::Eq => Some(Self::Token(Token::Punct(Punct::Eq))),
                        ReservedWord::Ge => Some(Self::Token(Token::Punct(Punct::Ge))),
                        ReservedWord::Gt => Some(Self::Token(Token::Punct(Punct::Gt))),
                        ReservedWord::Le => Some(Self::Token(Token::Punct(Punct::Le))),
                        ReservedWord::Lt => Some(Self::Token(Token::Punct(Punct::Lt))),
                        ReservedWord::Ne => Some(Self::Token(Token::Punct(Punct::Ne))),
                        ReservedWord::All => Some(Self::Token(Token::Punct(Punct::All))),
                        ReservedWord::By => Some(Self::Token(Token::Punct(Punct::By))),
                        ReservedWord::To => Some(Self::Token(Token::Punct(Punct::To))),
                        ReservedWord::With => Some(Self::Token(Token::Punct(Punct::With))),
                    }
                } else {
                    Some(Self::Token(Token::Id(Identifier::new(s).unwrap())))
                }
            }
            Segment::Punct => match s {
                "(" => Some(Self::Token(Token::Punct(Punct::LParen))),
                ")" => Some(Self::Token(Token::Punct(Punct::RParen))),
                "[" => Some(Self::Token(Token::Punct(Punct::LSquare))),
                "]" => Some(Self::Token(Token::Punct(Punct::RSquare))),
                "{" => Some(Self::Token(Token::Punct(Punct::LCurly))),
                "}" => Some(Self::Token(Token::Punct(Punct::RCurly))),
                "," => Some(Self::Token(Token::Punct(Punct::Comma))),
                "=" => Some(Self::Token(Token::Punct(Punct::Equals))),
                "-" => Some(Self::Token(Token::Punct(Punct::Dash))),
                "&" => Some(Self::Token(Token::Punct(Punct::And))),
                "|" => Some(Self::Token(Token::Punct(Punct::Or))),
                "+" => Some(Self::Token(Token::Punct(Punct::Plus))),
                "/" => Some(Self::Token(Token::Punct(Punct::Slash))),
                "*" => Some(Self::Token(Token::Punct(Punct::Asterisk))),
                "<" => Some(Self::Token(Token::Punct(Punct::Lt))),
                ">" => Some(Self::Token(Token::Punct(Punct::Gt))),
                "~" => Some(Self::Token(Token::Punct(Punct::Not))),
                ":" => Some(Self::Token(Token::Punct(Punct::Colon))),
                ";" => Some(Self::Token(Token::Punct(Punct::Semicolon))),
                "**" => Some(Self::Token(Token::Punct(Punct::Exp))),
                "<=" => Some(Self::Token(Token::Punct(Punct::Le))),
                "<>" => Some(Self::Token(Token::Punct(Punct::Ne))),
                "~=" => Some(Self::Token(Token::Punct(Punct::Ne))),
                ">=" => Some(Self::Token(Token::Punct(Punct::Ge))),
                "!" => Some(Self::Token(Token::Punct(Punct::Bang))),
                "%" => Some(Self::Token(Token::Punct(Punct::Percent))),
                "?" => Some(Self::Token(Token::Punct(Punct::Question))),
                "`" => Some(Self::Token(Token::Punct(Punct::Backtick))),
                "_" => Some(Self::Token(Token::Punct(Punct::Underscore))),
                "." => Some(Self::Token(Token::Punct(Punct::Dot))),
                "!*" => Some(Self::Token(Token::Punct(Punct::BangAsterisk))),
                _ => unreachable!("bad punctuator {s:?}"),
            },
            Segment::Shbang
            | Segment::Spaces
            | Segment::Comment
            | Segment::Newline
            | Segment::CommentCommand => None,
            Segment::DoRepeatOverflow => Some(Self::Error(ScanError::DoRepeatOverflow)),
            Segment::StartDocument => {
                Some(Self::Token(Token::Id(Identifier::new("DOCUMENT").unwrap())))
            }
            Segment::StartCommand | Segment::SeparateCommands | Segment::EndCommand => {
                Some(Self::Token(Token::End))
            }
            Segment::ExpectedQuote => Some(Self::Error(ScanError::ExpectedQuote)),
            Segment::ExpectedExponent => {
                Some(Self::Error(ScanError::ExpectedExponent(String::from(s))))
            }
            Segment::UnexpectedChar => Some(Self::Error(ScanError::UnexpectedChar(
                s.chars().next().unwrap(),
            ))),
        }
    }

    /// Attempts to merge a sequence of tokens together into a single token. The
    /// tokens are taken from the beginning of `input`. If successful, removes one
    /// or more token from the beginning of `input` and returnss the merged
    /// token. More input tokens might be needed; if so, leaves `input` alone and
    /// returns `None`. In the latter case, the caller should add more tokens to the
    /// input ([Token::End] or [Token::Punct(Punct::EndCmd)] is always sufficient).
    ///
    /// This performs two different kinds of token merging:
    ///
    /// - String concatenation, where syntax like `"a" + "b"` is converted into a
    ///   single string token.  This is definitely needed because the parser relies
    ///   on it.
    ///
    /// - Negative number merging, where syntax like `-5` is converted from a pair
    ///   of tokens (a dash and a positive number) into a single token (a negative
    ///   number).  This might not be needed anymore because the segmenter
    ///   directly treats a dash followed by a number, with optional intervening
    ///   white space, as a negative number.  It's only needed if we want
    ///   intervening comments to be allowed or for part of the negative number
    ///   token to be produced by macro expansion.
    pub fn merge<'a, F>(get_token: F) -> Result<Option<MergeResult>, Incomplete>
    where
        F: Fn(usize) -> Result<Option<&'a Token>, Incomplete>,
    {
        let Some(token) = get_token(0)? else {
            return Ok(None);
        };
        match token {
            Token::Punct(Punct::Dash) => match get_token(1)? {
                Some(Token::Number(number)) if number.is_sign_positive() => {
                    let number = *number;
                    Ok(Some(MergeResult::Expand {
                        n: 2,
                        token: Token::Number(-number),
                    }))
                }
                _ => Ok(Some(MergeResult::Copy)),
            },
            Token::String(_) => {
                let mut i = 0;
                while matches!(get_token(i * 2 + 1)?, Some(Token::Punct(Punct::Plus)))
                    && matches!(get_token(i * 2 + 2)?, Some(Token::String(_)))
                {
                    i += 1;
                }
                if i == 0 {
                    Ok(Some(MergeResult::Copy))
                } else {
                    let mut output = String::new();
                    for i in 0..=i {
                        let Token::String(s) = get_token(i * 2).unwrap().unwrap() else {
                            unreachable!()
                        };
                        output.push_str(s);
                    }
                    Ok(Some(MergeResult::Expand {
                        n: i * 2 + 1,
                        token: Token::String(output),
                    }))
                }
            }
            _ => Ok(Some(MergeResult::Copy)),
        }
    }
}

pub struct StringSegmenter<'a> {
    input: &'a str,
    segmenter: Segmenter,
}

impl<'a> StringSegmenter<'a> {
    pub fn new(input: &'a str, mode: Syntax, is_snippet: bool) -> Self {
        Self {
            input,
            segmenter: Segmenter::new(mode, is_snippet),
        }
    }
}

impl<'a> Iterator for StringSegmenter<'a> {
    type Item = (&'a str, ScanToken);

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            let (seg_len, seg_type) = self.segmenter.push(self.input, true).unwrap()?;
            let (s, rest) = self.input.split_at(seg_len);
            self.input = rest;

            if let Some(token) = ScanToken::from_segment(s, seg_type) {
                return Some((s, token));
            }
        }
    }
}

pub struct StringScanner<'a> {
    input: &'a str,
    eof: bool,
    segmenter: Segmenter,
    tokens: VecDeque<Token>,
}

impl<'a> StringScanner<'a> {
    pub fn new(input: &'a str, mode: Syntax, is_snippet: bool) -> Self {
        Self {
            input,
            eof: false,
            segmenter: Segmenter::new(mode, is_snippet),
            tokens: VecDeque::with_capacity(1),
        }
    }

    fn merge(&mut self, eof: bool) -> Result<Option<ScanToken>, Incomplete> {
        match ScanToken::merge(|index| {
            if let Some(token) = self.tokens.get(index) {
                Ok(Some(token))
            } else if eof {
                Ok(None)
            } else {
                Err(Incomplete)
            }
        })? {
            Some(MergeResult::Copy) => Ok(Some(ScanToken::Token(self.tokens.pop_front().unwrap()))),
            Some(MergeResult::Expand { n, token }) => {
                self.tokens.drain(..n);
                Ok(Some(ScanToken::Token(token)))
            }
            None => Ok(None),
        }
    }

    pub fn unwrapped(self) -> impl Iterator<Item = Token> + use<'a> {
        self.map(|scan_token| scan_token.token().unwrap())
    }
}

impl Iterator for StringScanner<'_> {
    type Item = ScanToken;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            if let Ok(Some(token)) = self.merge(self.eof) {
                return Some(token);
            }

            let Some((seg_len, seg_type)) = self.segmenter.push(self.input, true).unwrap() else {
                self.eof = true;
                return self.merge(true).unwrap();
            };
            let (s, rest) = self.input.split_at(seg_len);

            match ScanToken::from_segment(s, seg_type) {
                Some(ScanToken::Error(error)) => {
                    if let Ok(Some(token)) = self.merge(true) {
                        return Some(token);
                    }
                    self.input = rest;
                    return Some(ScanToken::Error(error));
                }
                Some(ScanToken::Token(token)) => {
                    self.tokens.push_back(token);
                }
                None => (),
            }
            self.input = rest;
        }
    }
}

#[cfg(test)]
mod test;
