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

use std::fmt::{Display, Formatter, Result as FmtResult};

use crate::identifier::Identifier;

#[derive(Clone, Debug, PartialEq)]
pub enum Token {
    /// Identifier.
    Id(Identifier),

    /// Number.
    Number(f64),

    /// Quoted string.
    String(String),

    /// Command terminator or separator.
    ///
    /// Usually this is `.`, but a blank line also separates commands, and in
    /// batch mode any line that begins with a non-blank starts a new command.
    End,

    /// Operators, punctuators, and reserved words.
    Punct(Punct),
}

impl Token {
    pub fn id(&self) -> Option<&Identifier> {
        match self {
            Self::Id(identifier) => Some(identifier),
            _ => None,
        }
    }

    pub fn matches_keyword(&self, keyword: &str) -> bool {
        self.id().is_some_and(|id| id.matches_keyword(keyword))
    }

    pub fn as_number(&self) -> Option<f64> {
        if let Self::Number(number) = self {
            Some(*number)
        } else {
            None
        }
    }

    pub fn as_integer(&self) -> Option<i64> {
        match self {
            Self::Number(number)
                if *number >= i64::MIN as f64
                    && *number <= i64::MAX as f64
                    && *number == number.floor() =>
            {
                Some(*number as i64)
            }
            _ => None,
        }
    }

    pub fn as_id(&self) -> Option<&Identifier> {
        match self {
            Self::Id(id) => Some(id),
            _ => None,
        }
    }

    pub fn as_string(&self) -> Option<&str> {
        match self {
            Self::String(string) => Some(string.as_str()),
            _ => None,
        }
    }
}

fn is_printable(c: char) -> bool {
    !c.is_control() || ['\t', '\r', '\n'].contains(&c)
}

fn string_representation(s: &str, quote: char, f: &mut Formatter<'_>) -> FmtResult {
    write!(f, "{quote}")?;
    for section in s.split_inclusive(quote) {
        if let Some(rest) = section.strip_suffix(quote) {
            write!(f, "{rest}{quote}{quote}")?;
        } else {
            write!(f, "{section}")?;
        }
    }
    write!(f, "{quote}")
}

impl Display for Token {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        match self {
            Token::Id(s) => write!(f, "{s}"),
            Token::Number(number) => {
                if number.is_sign_negative() {
                    write!(f, "-{}", number.abs())
                } else {
                    write!(f, "{number}")
                }
            }
            Token::String(s) => {
                if s.chars().all(is_printable) {
                    if s.contains('"') {
                        string_representation(s, '\'', f)
                    } else {
                        string_representation(s, '"', f)
                    }
                } else {
                    write!(f, "X\"")?;
                    for byte in s.bytes() {
                        let c1 = char::from_digit((byte >> 4) as u32, 16)
                            .unwrap()
                            .to_ascii_uppercase();
                        let c2 = char::from_digit((byte & 0xf) as u32, 16)
                            .unwrap()
                            .to_ascii_uppercase()
                            .to_ascii_lowercase();
                        write!(f, "{c1}{c2}")?;
                    }
                    write!(f, "\"")
                }
            }
            Token::End => write!(f, "."),
            Token::Punct(punct) => punct.fmt(f),
        }
    }
}

/// Check that all negative numbers, even -0, get formatted with a leading `-`.
#[cfg(test)]
mod test {
    use crate::lex::token::Token;

    #[test]
    fn test_string() {
        assert_eq!(Token::String(String::from("abc")).to_string(), "\"abc\"");
        assert_eq!(
            Token::String(String::from("\u{0080}")).to_string(),
            "X\"C280\""
        );
    }

    #[test]
    fn test_neg0() {
        assert_eq!(Token::Number(-0.0).to_string(), "-0");
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Punct {
    /// `+`.
    Plus,

    /// `-`.
    Dash,

    /// `*`.
    Asterisk,

    /// `/`.
    Slash,

    /// `=`.
    Equals,

    /// `(`.
    LParen,

    /// `)`.
    RParen,

    /// `[`.
    LSquare,

    /// `]`.
    RSquare,

    /// `{`.
    LCurly,

    /// `}`.
    RCurly,

    /// `,`.
    Comma,

    /// `;`.
    Semicolon,

    /// `:`.
    Colon,

    /// `AND` or `&`.
    And,

    /// `OR` or `|`.
    Or,

    /// `NOT` or `~`.
    Not,

    /// `EQ` or `=`.
    Eq,

    /// `GE` or '>=`
    Ge,

    /// `GT` or `>`.
    Gt,

    /// `LE` or `<=`.
    Le,

    /// `LT` or `<`.
    Lt,

    /// `NE` or `~=` or `<>`.
    Ne,

    /// `ALL`.
    All,

    /// `BY`.
    By,

    /// `TO`.
    To,

    /// `WITH`.
    With,

    /// `**`.
    Exp,

    /// `!` (only appears in macros).
    Bang,

    /// `%` (only appears in macros).
    Percent,

    /// `?` (only appears in macros).
    Question,

    /// ```` (only appears in macros).
    Backtick,

    /// `.`.
    ///
    /// This represents a dot in the middle of a line by itself, where it does not end a command.
    Dot,

    /// `_` (only appears in macros).
    ///
    /// Although underscores may appear within identifiers, they can't be the
    /// first character, so this represents an underscore found on its own.
    Underscore,

    /// `!*` (only appears in macros).
    BangAsterisk,
}

impl Punct {
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Plus => "+",
            Self::Dash => "-",
            Self::Asterisk => "*",
            Self::Slash => "/",
            Self::Equals => "=",
            Self::LParen => "(",
            Self::RParen => ")",
            Self::LSquare => "[",
            Self::RSquare => "]",
            Self::LCurly => "{",
            Self::RCurly => "}",
            Self::Comma => ",",
            Self::Semicolon => ";",
            Self::Colon => ":",
            Self::And => "AND",
            Self::Or => "OR",
            Self::Not => "NOT",
            Self::Eq => "EQ",
            Self::Ge => ">=",
            Self::Gt => ">",
            Self::Le => "<=",
            Self::Lt => "<",
            Self::Ne => "~=",
            Self::All => "ALL",
            Self::By => "BY",
            Self::To => "TO",
            Self::With => "WITH",
            Self::Exp => "**",
            Self::Bang => "!",
            Self::Percent => "%",
            Self::Question => "?",
            Self::Backtick => "`",
            Self::Dot => ".",
            Self::Underscore => "_",
            Self::BangAsterisk => "!*",
        }
    }
}
impl Display for Punct {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        write!(f, "{}", self.as_str())
    }
}
