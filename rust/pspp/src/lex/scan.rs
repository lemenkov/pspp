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

//! Mid-level lexical analysis.
//!
//! This module implements mid-level lexical analysis using the segments
//! output by the lower-level [segmentation phase](super::segment).
//!
//! Scanning accepts as input a stream of segments, which are UTF-8 strings
//! labeled with a [segment type](super::segment::Segment).  It outputs a stream
//! of [Token]s used by the PSPP parser or an error.

use crate::identifier::{Identifier, ReservedWord};

use super::{
    segment::{Segment, Segmenter, Syntax},
    token::{Punct, Token},
};
use std::collections::VecDeque;
use thiserror::Error as ThisError;

/// Error returned by [merge_tokens].
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
    IncompleteUtf8 {
        /// Incomplete sequence.
        substring: String,
        /// Offset of start of sequence.
        offset: usize,
    },

    /// Bad UTF-8 sequence.
    #[error("Invalid UTF-8 sequence `{substring}` starting {offset} digits into hex string.")]
    BadUtf8 {
        /// Invalid sequence.
        substring: String,
        /// Offset of start of sequence.
        offset: usize,
    },

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

/// The action returned by [merge_tokens].
#[derive(Clone, Debug)]
pub enum MergeAction {
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

/// Used by [merge_tokens] to indicate that more input is needed.
#[derive(Copy, Clone, Debug)]
pub struct Incomplete;

impl Segment {
    /// Tries to transform this segment, which was obtained for `s`, into a
    /// token.  Returns one of:
    ///
    /// - `None`: This segment doesn't correspond to any token (because it is a
    ///   comment, white space, etc.) and can be dropped in tokenization.
    ///
    /// - `Some(Ok(token))`: This segment corresponds to the given token.
    ///
    /// - `Some(Err(error))`: The segment contains an error, which the caller
    ///   should report.
    ///
    /// The raw token (or error) that this function returns should ordinarily be
    /// merged with adjacent tokens with [merge_tokens] or some higher-level
    /// construct.
    pub fn to_token(self, s: &str) -> Option<Result<Token, ScanError>> {
        match self {
            Segment::Number => Some(Ok(Token::Number(s.parse().unwrap()))),
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
                Some(Ok(Token::String(s.replace(double_quote, single_quote))))
            }
            Segment::HexString => {
                // Strip `X"` prefix and `"` suffix (or variations).
                let s = &s[2..s.len() - 1];
                for c in s.chars() {
                    if !c.is_ascii_hexdigit() {
                        return Some(Err(ScanError::BadHexDigit(c)));
                    }
                }
                if s.len() % 2 != 0 {
                    return Some(Err(ScanError::OddLengthHexString(s.len())));
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
                    Ok(string) => Some(Ok(Token::String(string))),
                    Err(error) => {
                        let details = error.utf8_error();
                        let offset = details.valid_up_to() * 2;
                        let end = details
                            .error_len()
                            .map(|len| offset + len * 2)
                            .unwrap_or(s.len());
                        let substring = String::from(&s[offset..end]);
                        Some(Err(if details.error_len().is_some() {
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
                    return Some(Err(ScanError::BadLengthUnicodeString(s.len())));
                }
                let Ok(code_point) = u32::from_str_radix(s, 16) else {
                    return Some(Err(ScanError::ExpectedCodePoint));
                };
                let Some(c) = char::from_u32(code_point) else {
                    return Some(Err(ScanError::BadCodePoint(code_point)));
                };
                Some(Ok(Token::String(String::from(c))))
            }

            Segment::UnquotedString
            | Segment::DoRepeatCommand
            | Segment::InlineData
            | Segment::Document
            | Segment::MacroBody
            | Segment::MacroName => Some(Ok(Token::String(String::from(s)))),

            Segment::Identifier => {
                if let Ok(reserved_word) = ReservedWord::try_from(s) {
                    match reserved_word {
                        ReservedWord::And => Some(Ok(Token::Punct(Punct::And))),
                        ReservedWord::Or => Some(Ok(Token::Punct(Punct::Or))),
                        ReservedWord::Not => Some(Ok(Token::Punct(Punct::Not))),
                        ReservedWord::Eq => Some(Ok(Token::Punct(Punct::Eq))),
                        ReservedWord::Ge => Some(Ok(Token::Punct(Punct::Ge))),
                        ReservedWord::Gt => Some(Ok(Token::Punct(Punct::Gt))),
                        ReservedWord::Le => Some(Ok(Token::Punct(Punct::Le))),
                        ReservedWord::Lt => Some(Ok(Token::Punct(Punct::Lt))),
                        ReservedWord::Ne => Some(Ok(Token::Punct(Punct::Ne))),
                        ReservedWord::All => Some(Ok(Token::Punct(Punct::All))),
                        ReservedWord::By => Some(Ok(Token::Punct(Punct::By))),
                        ReservedWord::To => Some(Ok(Token::Punct(Punct::To))),
                        ReservedWord::With => Some(Ok(Token::Punct(Punct::With))),
                    }
                } else {
                    Some(Ok(Token::Id(Identifier::new(s).unwrap())))
                }
            }
            Segment::Punct => match s {
                "(" => Some(Ok(Token::Punct(Punct::LParen))),
                ")" => Some(Ok(Token::Punct(Punct::RParen))),
                "[" => Some(Ok(Token::Punct(Punct::LSquare))),
                "]" => Some(Ok(Token::Punct(Punct::RSquare))),
                "{" => Some(Ok(Token::Punct(Punct::LCurly))),
                "}" => Some(Ok(Token::Punct(Punct::RCurly))),
                "," => Some(Ok(Token::Punct(Punct::Comma))),
                "=" => Some(Ok(Token::Punct(Punct::Equals))),
                "-" => Some(Ok(Token::Punct(Punct::Dash))),
                "&" => Some(Ok(Token::Punct(Punct::And))),
                "|" => Some(Ok(Token::Punct(Punct::Or))),
                "+" => Some(Ok(Token::Punct(Punct::Plus))),
                "/" => Some(Ok(Token::Punct(Punct::Slash))),
                "*" => Some(Ok(Token::Punct(Punct::Asterisk))),
                "<" => Some(Ok(Token::Punct(Punct::Lt))),
                ">" => Some(Ok(Token::Punct(Punct::Gt))),
                "~" => Some(Ok(Token::Punct(Punct::Not))),
                ":" => Some(Ok(Token::Punct(Punct::Colon))),
                ";" => Some(Ok(Token::Punct(Punct::Semicolon))),
                "**" => Some(Ok(Token::Punct(Punct::Exp))),
                "<=" => Some(Ok(Token::Punct(Punct::Le))),
                "<>" => Some(Ok(Token::Punct(Punct::Ne))),
                "~=" => Some(Ok(Token::Punct(Punct::Ne))),
                ">=" => Some(Ok(Token::Punct(Punct::Ge))),
                "!" => Some(Ok(Token::Punct(Punct::Bang))),
                "%" => Some(Ok(Token::Punct(Punct::Percent))),
                "?" => Some(Ok(Token::Punct(Punct::Question))),
                "`" => Some(Ok(Token::Punct(Punct::Backtick))),
                "_" => Some(Ok(Token::Punct(Punct::Underscore))),
                "." => Some(Ok(Token::Punct(Punct::Dot))),
                "!*" => Some(Ok(Token::Punct(Punct::BangAsterisk))),
                _ => unreachable!("bad punctuator {s:?}"),
            },
            Segment::Shbang
            | Segment::Spaces
            | Segment::Comment
            | Segment::Newline
            | Segment::CommentCommand => None,
            Segment::DoRepeatOverflow => Some(Err(ScanError::DoRepeatOverflow)),
            Segment::StartDocument => Some(Ok(Token::Id(Identifier::new("DOCUMENT").unwrap()))),
            Segment::StartCommand | Segment::SeparateCommands | Segment::EndCommand => {
                Some(Ok(Token::End))
            }
            Segment::ExpectedQuote => Some(Err(ScanError::ExpectedQuote)),
            Segment::ExpectedExponent => Some(Err(ScanError::ExpectedExponent(String::from(s)))),
            Segment::UnexpectedChar => {
                Some(Err(ScanError::UnexpectedChar(s.chars().next().unwrap())))
            }
        }
    }
}

/// Attempts to merge a sequence of tokens together into a single token.
///
/// The tokens are taken from the beginning of `input`, which given
/// 0-based token index returns:
///
/// * `Ok(Some(token))`: The token with the given index.
///
/// * `Ok(None)`: End of input.
///
/// * `Err(Incomplete)`: The given token isn't available yet (it may or may not
///   exist).
///
/// This function returns one of:
///
/// * `Ok(Some(MergeAction))`: How to transform one or more input tokens into an
///   output token.
///
/// * `Ok(None)`: End of input.  (Only returned if `input(0)` is `Ok(None)`.)
///
/// * `Err(Incomplete)`: More input tokens are needed.  Call again with longer
///   `input`.  ([Token::End] or [Token::Punct(Punct::EndCmd)] is
///   always sufficient as extra input.)
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
pub fn merge_tokens<'a, F>(input: F) -> Result<Option<MergeAction>, Incomplete>
where
    F: Fn(usize) -> Result<Option<&'a Token>, Incomplete>,
{
    let Some(token) = input(0)? else {
        return Ok(None);
    };
    match token {
        Token::Punct(Punct::Dash) => match input(1)? {
            Some(Token::Number(number)) if number.is_sign_positive() => {
                let number = *number;
                Ok(Some(MergeAction::Expand {
                    n: 2,
                    token: Token::Number(-number),
                }))
            }
            _ => Ok(Some(MergeAction::Copy)),
        },
        Token::String(_) => {
            let mut i = 0;
            while matches!(input(i * 2 + 1)?, Some(Token::Punct(Punct::Plus)))
                && matches!(input(i * 2 + 2)?, Some(Token::String(_)))
            {
                i += 1;
            }
            if i == 0 {
                Ok(Some(MergeAction::Copy))
            } else {
                let mut output = String::new();
                for i in 0..=i {
                    let Token::String(s) = input(i * 2).unwrap().unwrap() else {
                        unreachable!()
                    };
                    output.push_str(s);
                }
                Ok(Some(MergeAction::Expand {
                    n: i * 2 + 1,
                    token: Token::String(output),
                }))
            }
        }
        _ => Ok(Some(MergeAction::Copy)),
    }
}

/// Too-simple lexical analyzer for strings.
///
/// Given a string, [StringSegmenter] provides iteration over raw tokens.
/// Unlike [StringScanner], [StringSegmenter] does not merge tokens using
/// [merge_tokens].  Usually merging is desirable, so [StringScanner] should be
/// preferred.
///
/// This is used as part of macro expansion.
pub struct StringSegmenter<'a> {
    input: &'a str,
    segmenter: Segmenter,
}

impl<'a> StringSegmenter<'a> {
    /// Creates a new [StringSegmenter] for `input` using syntax variant `mode`.
    /// See [Segmenter::new] for an explanation of `is_snippet`.
    pub fn new(input: &'a str, mode: Syntax, is_snippet: bool) -> Self {
        Self {
            input,
            segmenter: Segmenter::new(mode, is_snippet),
        }
    }
}

impl<'a> Iterator for StringSegmenter<'a> {
    type Item = (&'a str, Result<Token, ScanError>);

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            let (seg_len, seg_type) = self.segmenter.push(self.input, true).unwrap()?;
            let (s, rest) = self.input.split_at(seg_len);
            self.input = rest;

            if let Some(token) = seg_type.to_token(s) {
                return Some((s, token));
            }
        }
    }
}

/// Simple lexical analyzer for strings.
///
/// Given a string, [StringScanner] provides iteration over tokens.
pub struct StringScanner<'a> {
    input: &'a str,
    eof: bool,
    segmenter: Segmenter,
    tokens: VecDeque<Token>,
}

impl<'a> StringScanner<'a> {
    /// Creates a new [StringScanner] for `input` using syntax variant `mode`.
    /// See [Segmenter::new] for an explanation of `is_snippet`.
    pub fn new(input: &'a str, mode: Syntax, is_snippet: bool) -> Self {
        Self {
            input,
            eof: false,
            segmenter: Segmenter::new(mode, is_snippet),
            tokens: VecDeque::with_capacity(1),
        }
    }

    fn merge(&mut self, eof: bool) -> Result<Option<Result<Token, ScanError>>, Incomplete> {
        match merge_tokens(|index| {
            if let Some(token) = self.tokens.get(index) {
                Ok(Some(token))
            } else if eof {
                Ok(None)
            } else {
                Err(Incomplete)
            }
        })? {
            Some(MergeAction::Copy) => Ok(Some(Ok(self.tokens.pop_front().unwrap()))),
            Some(MergeAction::Expand { n, token }) => {
                self.tokens.drain(..n);
                Ok(Some(Ok(token)))
            }
            None => Ok(None),
        }
    }

    /// Transforms this [StringScanner] into an iterator that includes only the
    /// [Token]s, omitting [ScanError]s.
    pub fn unwrapped(self) -> impl Iterator<Item = Token> + use<'a> {
        self.map(|scan_token| scan_token.ok().unwrap())
    }
}

impl Iterator for StringScanner<'_> {
    type Item = Result<Token, ScanError>;

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

            match seg_type.to_token(s) {
                Some(Err(error)) => {
                    if let Ok(Some(token)) = self.merge(true) {
                        return Some(token);
                    }
                    self.input = rest;
                    return Some(Err(error));
                }
                Some(Ok(token)) => {
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
